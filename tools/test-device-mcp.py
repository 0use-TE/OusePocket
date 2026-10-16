"""Check the actual stdio MCP bridge through native Settings, tap and JPEG.

This does not build, flash, reset, install apps, or change preferences. The MCP
child is always stopped before a short native-protocol cleanup owns the UART.
"""
from pathlib import Path
import argparse
import base64
from datetime import datetime
import json
import queue
import subprocess
import sys
import threading
import time

from bpk import Client

ROOT = Path(__file__).resolve().parents[1]


def require(value, message):
    if not value:
        raise RuntimeError(message)


class Bridge:
    def __init__(self, port, baud, log, deadline):
        self.deadline = deadline
        self.sequence = 0
        self.transcript = []
        self.replies = queue.Queue()
        self.failed = False
        self.process = subprocess.Popen(
            [sys.executable, '-u', '-X', 'utf8', str(ROOT / 'tools/device-mcp.py'),
             '--port', port, '--baud', str(baud)], cwd=ROOT,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log,
            text=True, encoding='utf-8')
        def reader():
            try:
                for line in self.process.stdout:
                    self.replies.put(line)
            finally:
                self.replies.put(None)
        self.reader = threading.Thread(target=reader, daemon=True)
        self.reader.start()

    def rpc(self, method, params=None):
        remaining = self.deadline - time.monotonic()
        require(remaining > 0, 'MCP check exceeded its time budget')
        self.sequence += 1
        try:
            self.process.stdin.write(json.dumps(dict(jsonrpc='2.0', id=self.sequence,
                method=method, params=params or {})) + '\n')
            self.process.stdin.flush()
            line = self.replies.get(timeout=min(15, remaining))
            require(line is not None, 'MCP server exited before replying')
            reply = json.loads(line)
            require(reply.get('id') == self.sequence and 'error' not in reply,
                    'Invalid MCP reply: ' + repr(reply))
            result = reply['result']
            require(not result.get('isError'), 'MCP tool failed: ' + repr(result))
        except BaseException:
            self.failed = True
            raise
        image = method == 'tools/call' and params.get('name') == 'device_screenshot'
        self.transcript.append(dict(method=method, params=params,
            result='JPEG image content' if image else result))
        return result

    def call(self, name, **arguments):
        return self.rpc('tools/call', dict(name=name, arguments=arguments))

    def value(self, name, **arguments):
        return json.loads(self.call(name, **arguments)['content'][0]['text'])

    def close(self, deadline):
        process = self.process
        try:
            if process.stdin and not process.stdin.closed:
                process.stdin.close()
        except OSError:
            pass
        try:
            process.wait(timeout=max(.1, min(2, deadline - time.monotonic())))
        except subprocess.TimeoutExpired:
            # Device.close may be waiting for a crash dump. A stalled diagnostic
            # child must not retain COM10 beyond this bounded MCP check.
            process.terminate()
            try:
                process.wait(timeout=max(.1, min(1, deadline - time.monotonic())))
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=1)
        if process.stdout:
            process.stdout.close()
        self.reader.join(timeout=.1)
        require(process.poll() is not None, 'MCP process retained the serial port')


def display_row(tree, state):
    width, height = state['width'], state['height']
    matches = []
    for node in tree['nodes']:
        if node.get('text') not in ('Display', '显示'):
            continue
        x1, y1, x2, y2 = node['bounds']
        x, y = (x1+x2)//2, (y1+y2)//2
        if x2 > x1 and y2 > y1 and 0 <= x < width and 24 < y < height-24:
            matches.append(node)
    require(matches, 'Settings Display row is not visible; start with Settings home or the desktop')
    return max(matches, key=lambda node: node['depth'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM10')
    parser.add_argument('--baud', type=int, default=921600)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--timeout', type=float, default=60,
                        help='Whole-check time budget, including cleanup, 1..60 seconds')
    args = parser.parse_args()
    if not 1 <= args.timeout <= 60:
        parser.error('--timeout must be 1..60 seconds')
    output = args.output or ROOT / '.tools/diagnostics' / ('mcp-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
    output.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    deadline = started + args.timeout
    work_deadline = started + max(.1, args.timeout - 8)
    bridge = None
    initial = None
    touched_settings = False
    display_entered = False
    root_restored = False
    report = dict(status='running', port=args.port, baud=args.baud, timeout=args.timeout)
    exit_code = 0
    with (output / 'server.log').open('w', encoding='utf-8') as log:
        try:
            bridge = Bridge(args.port, args.baud, log, work_deadline)
            bridge.rpc('initialize', dict(protocolVersion='2024-11-05', capabilities={},
                clientInfo=dict(name='ouse-test', version='1')))
            tools = bridge.rpc('tools/list')['tools']
            require(len(tools) == 9, 'Unexpected MCP tool inventory')
            initial = bridge.value('device_state')
            report['initial_state'] = initial
            require(initial['core'] == (ROOT / 'version.txt').read_text(encoding='utf-8').strip(),
                    'Running board core version differs from this workspace')
            require(not any(initial.get(key) for key in ('quick','notifications','recents','tracking')),
                    'Preserve existing panel/gesture state; close it before this check')
            if initial['active'] == 'ouse.settings':
                # Refuse another settings subpage before changing it, because
                # the MCP state API does not export the native selected page.
                display_row(bridge.value('device_tree'), initial)
            bridge.call('device_open_app', id='ouse.settings')
            touched_settings = True
            state = bridge.value('device_state')
            require(state['active'] == 'ouse.settings', 'Native Settings did not open')
            tree = bridge.value('device_tree')
            (output / 'settings-tree.json').write_text(json.dumps(tree, ensure_ascii=False, indent=2), encoding='utf-8')
            row = display_row(tree, state)
            x1,y1,x2,y2 = row['bounds']
            bridge.call('device_tap', x=(x1+x2)//2, y=(y1+y2)//2)
            display_entered = True
            tree = bridge.value('device_tree')
            require(any(node['type'] == 'slider' for node in tree['nodes']),
                    'Tap did not open the native Display page')
            (output / 'display-tree.json').write_text(json.dumps(tree, ensure_ascii=False, indent=2), encoding='utf-8')
            image = bridge.call('device_screenshot')['content'][0]
            require(image.get('type') == 'image' and image.get('mimeType') == 'image/jpeg',
                    'MCP screenshot is not JPEG image content')
            data = base64.b64decode(image['data'], validate=True)
            require(data.startswith(b'\xff\xd8') and data.endswith(b'\xff\xd9'), 'Invalid JPEG bytes')
            (output / 'display.jpg').write_bytes(data)
            dpi = initial['dpi']
            bridge.call('device_swipe', x=max(1, round(3*dpi/160)), y=initial['height']//2,
                        end_x=round(90*dpi/160), end_y=initial['height']//2, duration_ms=180)
            display_row(bridge.value('device_tree'), initial)
            root_restored = True
            bridge.call('device_home')
            require(bridge.value('device_state')['active'] == '', 'MCP Home did not return to desktop')
            report['status'] = 'passed'
        except (Exception, KeyboardInterrupt) as error:
            report.update(status='interrupted' if isinstance(error,KeyboardInterrupt) else 'failed',
                          error=dict(type=type(error).__name__, message=str(error)))
            exit_code = 130 if isinstance(error,KeyboardInterrupt) else 1
        finally:
            # Restore an originally foreground Settings root before shutting
            # down stdio; no app-close tool is part of this nine-tool MCP API.
            if bridge is not None:
                if initial is not None and touched_settings and not bridge.failed:
                    try:
                        if initial['active'] == 'ouse.settings' and display_entered and not root_restored:
                            dpi = initial['dpi']
                            bridge.call('device_swipe', x=max(1,round(3*dpi/160)), y=initial['height']//2,
                                end_x=round(90*dpi/160), end_y=initial['height']//2, duration_ms=180)
                        if initial['active']:
                            bridge.call('device_open_app', id=initial['active'])
                        else:
                            bridge.call('device_home')
                    except Exception as error:
                        report['mcp_restore_error'] = str(error)
                try:
                    bridge.close(deadline)
                except Exception as error:
                    report.update(server_close_error=str(error),status='failed');exit_code=1
                report['server_exit_code'] = bridge.process.poll()
                if report['server_exit_code'] != 0:
                    report['status'],exit_code='failed',1
                (output/'transcript.json').write_text(json.dumps(bridge.transcript,ensure_ascii=False,indent=2),encoding='utf-8')
            # Only after the child has exited may another owner open the UART.
            # Close the Settings instance introduced by the check, then restore
            # the original foreground app. This does not remove any package.
            if initial is not None and touched_settings and bridge is not None and bridge.process.poll() is not None:
                client = None
                try:
                    require(time.monotonic() < deadline, 'No cleanup time remains')
                    client = Client(args.port, baud=args.baud)
                    def command(verb,payload='-'):
                        remaining=deadline-time.monotonic()
                        require(remaining>0,'Serial cleanup exceeded time budget')
                        return client.request(verb,payload,timeout=min(1.5,remaining),attempts=1)
                    if initial['active'] != 'ouse.settings' or (display_entered and not root_restored):
                        command('DBG_CLOSE','ouse.settings')
                    command('DBG_OPEN',initial['active']) if initial['active'] else command('DBG_HOME')
                    final=json.loads(bytes.fromhex(command('DBG_STATE')).decode('utf-8'))
                    report['final_state']=final
                    for key in ('active','theme','locale','font','dpi'):
                        require(final[key]==initial[key],'Cleanup changed initial '+key)
                    report['cleanup_completed']=True
                except Exception as error:
                    report.update(cleanup_error=str(error),status='failed');exit_code=1
                finally:
                    if client is not None:client.close()
    report['elapsed_seconds']=time.monotonic()-started
    (output/'mcp-result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(('PASS' if exit_code==0 else 'FAIL') + ' live MCP: native Settings, Display tap, JPEG, Home; '+str(output.resolve()),flush=True)
    return exit_code


if __name__ == '__main__':
    raise SystemExit(main())
