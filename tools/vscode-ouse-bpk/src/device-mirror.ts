import * as vscode from 'vscode';
import { DeviceMcp, parseState, toolImage, type DeviceState } from './device-mcp';

interface Session {
  port: string;
  baud: number;
  connected: boolean;
  dispose: () => void;
  reveal: () => void;
  refresh: () => void;
}

let current: Session | undefined;

export function deviceMirrorPort(): string | undefined {
  return current?.connected ? current.port : undefined;
}

export function closeDeviceMirror(): void {
  current?.dispose();
}

function isJpegBase64(data: string): boolean {
  if (!data || data.length < 8) return false;
  try {
    const buf = Buffer.from(data, 'base64');
    return buf.length >= 4
      && buf[0] === 0xff && buf[1] === 0xd8
      && buf[buf.length - 2] === 0xff && buf[buf.length - 1] === 0xd9;
  } catch {
    return false;
  }
}

export async function openDeviceMirror(
  context: vscode.ExtensionContext,
  python: string,
  script: string,
  port: string,
  baud: number,
  output: vscode.OutputChannel,
  onClose?: () => void,
): Promise<void> {
  if (current?.port === port && current.baud === baud) {
    current.reveal();
    current.refresh();
    return;
  }
  current?.dispose();

  const panel = vscode.window.createWebviewPanel(
    'ouseBpk.device',
    '设备画面',
    vscode.ViewColumn.Beside,
    { enableScripts: true, retainContextWhenHidden: true, localResourceRoots: [context.extensionUri] },
  );

  let closed = false;
  let busy = false;
  let mcp: DeviceMcp | undefined;
  let size: DeviceState = { width: 320, height: 480 };
  let poll: NodeJS.Timeout | undefined;
  let settleMs = 0;

  const post = (message: unknown): void => {
    void panel.webview.postMessage(message);
  };

  const stopPoll = (): void => {
    if (poll) {
      clearInterval(poll);
      poll = undefined;
    }
  };

  const finish = (): void => {
    if (closed) return;
    closed = true;
    stopPoll();
    if (current?.dispose === disposeAll) current = undefined;
    mcp?.stop();
    mcp = undefined;
    onClose?.();
  };

  const disposeAll = (): void => {
    finish();
    panel.dispose();
  };

  const setConnected = (value: boolean): void => {
    if (current) current.connected = value;
  };

  const startPoll = (): void => {
    stopPoll();
    poll = setInterval(() => {
      if (!mcp || busy || closed || !current?.connected) return;
      void run(async () => undefined, 0, true);
    }, 1000);
  };

  const connect = async (): Promise<void> => {
    if (closed) return;
    stopPoll();
    mcp?.stop();
    mcp = await DeviceMcp.start(python, script, port, baud, (line) => output.appendLine(line));
    setConnected(true);
    startPoll();
    onClose?.();
  };

  const disconnect = (): void => {
    stopPoll();
    mcp?.stop();
    mcp = undefined;
    setConnected(false);
    post({ type: 'disconnected' });
    post({ type: 'status', text: '已断开' });
    onClose?.();
  };

  const refresh = async (): Promise<void> => {
    if (!mcp) throw new Error('设备未连接');
    if (settleMs > 0) await new Promise((resolve) => setTimeout(resolve, settleMs));
    settleMs = 0;
    size = parseState(await mcp.call('device_state'));
    let image = '';
    for (let attempt = 0; attempt < 2; attempt++) {
      image = toolImage(await mcp.call('device_screenshot'));
      if (isJpegBase64(image)) break;
      if (attempt === 0) await new Promise((resolve) => setTimeout(resolve, 80));
    }
    if (!isJpegBase64(image)) throw new Error('截图数据无效，请再点刷新');
    post({ type: 'screen', image, width: size.width, height: size.height, seq: Date.now() });
    post({ type: 'status', text: size.active ? `前台 ${size.active}` : '桌面' });
  };

  const run = async (action: () => Promise<void>, waitMs = 0, quiet = false): Promise<void> => {
    if (busy || closed) return;
    busy = true;
    if (!quiet) post({ type: 'busy', value: true });
    try {
      await action();
      settleMs = Math.max(settleMs, waitMs);
      if (mcp) await refresh();
    } catch (error) {
      post({ type: 'status', text: error instanceof Error ? error.message : String(error), error: true });
    } finally {
      busy = false;
      if (!quiet) post({ type: 'busy', value: false });
    }
  };

  const requestRefresh = (): void => {
    void run(async () => undefined, 0, false);
  };

  current = {
    port,
    baud,
    connected: false,
    dispose: disposeAll,
    reveal: () => panel.reveal(vscode.ViewColumn.Beside),
    refresh: requestRefresh,
  };
  panel.onDidDispose(finish);
  panel.webview.html = html(panel.webview, context.extensionUri);

  panel.webview.onDidReceiveMessage((message: {
    type?: string;
    x?: number;
    y?: number;
    endX?: number;
    endY?: number;
    gesture?: string;
    deltaY?: number;
  }) => {
    if (message?.type === 'disconnect') {
      disconnect();
      return;
    }
    if (message?.type === 'connect') {
      void run(async () => {
        await connect();
      });
      return;
    }
    if (message?.type === 'ready') {
      void run(async () => {
        await connect();
      });
      return;
    }
    if (message?.type === 'refresh') {
      void run(async () => undefined);
      return;
    }
    if (!mcp || !current?.connected) return;
    if (message?.type === 'tap') {
      const x = Math.round(Number(message.x));
      const y = Math.round(Number(message.y));
      if (!Number.isFinite(x) || !Number.isFinite(y)) return;
      const point = {
        x: Math.max(0, Math.min(size.width - 1, x)),
        y: Math.max(0, Math.min(size.height - 1, y)),
      };
      void run(() => mcp!.call('device_tap', point).then(() => undefined), 120);
      return;
    }
    if (message?.type === 'swipe') {
      const x = Math.round(Number(message.x));
      const y = Math.round(Number(message.y));
      const endX = Math.round(Number(message.endX));
      const endY = Math.round(Number(message.endY));
      if (![x, y, endX, endY].every(Number.isFinite)) return;
      const swipe = {
        x: Math.max(0, Math.min(size.width - 1, x)),
        y: Math.max(0, Math.min(size.height - 1, y)),
        end_x: Math.max(0, Math.min(size.width - 1, endX)),
        end_y: Math.max(0, Math.min(size.height - 1, endY)),
        duration_ms: 240,
      };
      void run(() => mcp!.call('device_swipe', swipe).then(() => undefined), 160);
      return;
    }
    if (message?.type === 'wheel') {
      const swipe = wheelSwipe(Number(message.deltaY), size.width, size.height);
      if (!swipe) return;
      void run(() => mcp!.call('device_swipe', swipe).then(() => undefined), 120);
      return;
    }
    if (message?.type === 'gesture') {
      const swipe = gesture(message.gesture, size.width, size.height);
      if (!swipe) return;
      const settle = message.gesture === 'recents' ? 280 : 160;
      void run(() => mcp!.call('device_swipe', swipe).then(() => undefined), settle);
    }
  });
}

function gesture(name: string | undefined, width: number, height: number): Record<string, number> | undefined {
  const midX = Math.round(width / 2);
  const midY = Math.round(height / 2);
  const top = Math.round(height * 0.28);
  const bottom = Math.round(height * 0.72);
  if (name === 'back') return { x: 8, y: midY, end_x: 80, end_y: midY, duration_ms: 240 };
  if (name === 'pull') {
    const x = Math.min(width - 8, Math.round(width * 3 / 4));
    return { x, y: 8, end_x: x, end_y: 170, duration_ms: 280 };
  }
  if (name === 'up' || name === 'scroll-up') {
    return { x: midX, y: bottom, end_x: midX, end_y: top, duration_ms: 280 };
  }
  if (name === 'down' || name === 'scroll-down') {
    return { x: midX, y: top, end_x: midX, end_y: bottom, duration_ms: 280 };
  }
  if (name === 'recents') {
    const startY = Math.max(1, height - 6);
    const endY = Math.max(0, height - 100);
    return { x: midX, y: startY, end_x: midX, end_y: endY, duration_ms: 180, hold_ms: 220 };
  }
  return undefined;
}

function wheelSwipe(deltaY: number, width: number, height: number): Record<string, number> | undefined {
  if (!Number.isFinite(deltaY) || Math.abs(deltaY) < 1) return undefined;
  const midX = Math.round(width / 2);
  const midY = Math.round(height / 2);
  const travel = Math.round(Math.min(160, Math.max(90, height * 0.22)));
  const half = Math.round(travel / 2);
  const clampY = (y: number) => Math.max(0, Math.min(height - 1, y));
  if (deltaY > 0) {
    return { x: midX, y: clampY(midY + half), end_x: midX, end_y: clampY(midY - half), duration_ms: 180 };
  }
  return { x: midX, y: clampY(midY - half), end_x: midX, end_y: clampY(midY + half), duration_ms: 180 };
}

function html(webview: vscode.Webview, extensionUri: vscode.Uri): string {
  const style = webview.asWebviewUri(vscode.Uri.joinPath(extensionUri, 'media', 'device.css'));
  const script = webview.asWebviewUri(vscode.Uri.joinPath(extensionUri, 'media', 'device.js'));
  const nonce = String(Date.now());
  return `<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src data:; style-src ${webview.cspSource}; script-src 'nonce-${nonce}';">
<link rel="stylesheet" href="${style}">
</head>
<body>
<div class="frame">
<div class="toolbar">
  <div class="toolbar-row">
    <button id="refresh" type="button">刷新</button>
    <button id="back" type="button" data-gesture="back">返回</button>
    <button id="pull" type="button" data-gesture="pull">下拉</button>
    <button id="up" type="button" data-gesture="up">上滑</button>
    <button id="down" type="button" data-gesture="down">下滑</button>
    <button id="recents" type="button" data-gesture="recents">后台</button>
    <button id="link" type="button">断开</button>
  </div>
  <div class="toolbar-row">
    <label class="field">显示
      <select id="scaleMode">
        <option value="fit">适应窗口</option>
        <option value="native">原始分辨率</option>
      </select>
    </label>
  </div>
</div>
<p id="status">正在连接…</p>
<div id="view">
  <div class="stage" id="stage">
    <img id="screen" alt="设备画面" draggable="false">
  </div>
</div>
</div>
<script nonce="${nonce}" src="${script}"></script>
</body>
</html>`;
}
