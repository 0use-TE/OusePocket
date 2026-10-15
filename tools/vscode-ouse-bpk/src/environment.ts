import { execFile } from 'child_process';
import * as fs from 'fs';
import * as path from 'path';

export interface CommandCheck {
  id: 'node' | 'python' | 'brookesia' | 'usb';
  label: string;
  found: boolean;
  detail: string;
  hint?: string;
  command?: string;
}

export interface CommandOverrides {
  node?: string;
  python?: string;
  brookesia?: string;
  usb?: string;
}

function run(command: string, args: string[], cwd?: string): Promise<{ code: number; stdout: string; stderr: string }> {
  return new Promise((resolve) => {
    execFile(command, args, {
      cwd,
      timeout: 20000,
      windowsHide: true,
      env: { ...process.env, PYTHONIOENCODING: 'utf-8', PYTHONUTF8: '1' },
    }, (error, stdout, stderr) => {
      const code = typeof error?.code === 'number' ? error.code : error ? 1 : 0;
      resolve({
        code,
        stdout: stdout.toString(),
        stderr: stderr.toString() || (error && typeof error.code !== 'number' ? String(error.message) : ''),
      });
    });
  });
}

function pickExecutable(lines: string[]): string | undefined {
  const items = lines.map((item) => item.trim()).filter(Boolean);
  if (process.platform === 'win32') {
    const runnable = items.find((item) => /\.(cmd|exe|bat)$/i.test(item));
    if (runnable) {
      return runnable;
    }
  }
  return items[0];
}

async function where(name: string): Promise<string | undefined> {
  const locator = process.platform === 'win32' ? 'where.exe' : 'which';
  const result = await run(locator, [name]);
  if (result.code !== 0) {
    return undefined;
  }
  return pickExecutable(result.stdout.split(/\r?\n/));
}

function localBrookesia(workspaceRoots: string[]): string | undefined {
  const file = process.platform === 'win32' ? 'brookesia.cmd' : 'brookesia';
  for (const root of workspaceRoots) {
    const candidate = path.join(root, 'tools', 'host-tools', 'node_modules', '.bin', file);
    if (fs.existsSync(candidate)) {
      return candidate;
    }
  }
  return undefined;
}

async function inspectExecutable(
  override: string | undefined,
  discovered: string | undefined,
  versionArgs?: string[],
): Promise<{ command?: string; found: boolean; detail: string }> {
  const chosen = override || discovered;
  if (!chosen) {
    return { found: false, detail: '' };
  }
  if (!fs.existsSync(chosen)) {
    return { found: false, detail: `${chosen} 文件不存在` };
  }
  if (!versionArgs) {
    return { command: chosen, found: true, detail: chosen };
  }
  const version = await run(chosen, versionArgs);
  const text = (version.stdout || version.stderr).trim();
  if (version.code !== 0) {
    return { command: chosen, found: false, detail: text || chosen };
  }
  return { command: chosen, found: true, detail: `${chosen} ${text}`.trim() };
}

export async function checkCommands(
  workspaceRoots: string[],
  overrides: CommandOverrides = {},
): Promise<CommandCheck[]> {
  const node = await inspectExecutable(overrides.node, overrides.node ? undefined : await where('node'), ['--version']);
  const python = await inspectExecutable(
    overrides.python,
    overrides.python ? undefined : await where('python'),
    ['--version'],
  );
  const brookesiaOnPath = overrides.brookesia ? undefined : await where('brookesia');
  const brookesiaLocal = overrides.brookesia || brookesiaOnPath ? undefined : localBrookesia(workspaceRoots);
  const brookesia = await inspectExecutable(overrides.brookesia, brookesiaOnPath ?? brookesiaLocal);

  const pythonForUsb = overrides.usb || python.command;
  let usbFound = false;
  let usbDetail = '未找到 python -m brookesia_usb.cli';
  if (pythonForUsb && fs.existsSync(pythonForUsb)) {
    const help = await run(pythonForUsb, ['-m', 'brookesia_usb.cli', '--help']);
    usbFound = help.code === 0;
    usbDetail = usbFound ? `${pythonForUsb} -m brookesia_usb.cli` : (help.stderr || help.stdout || usbDetail).trim();
  } else if (pythonForUsb) {
    usbDetail = `${pythonForUsb} 文件不存在`;
  }

  return [
    {
      id: 'node',
      label: 'node',
      found: node.found,
      detail: node.detail || '未找到 node',
      command: node.command,
      hint: node.found ? undefined : '安装 Node.js 20 或更新版本，或点选择指定 node.exe。',
    },
    {
      id: 'python',
      label: 'python',
      found: python.found,
      detail: python.detail || '未找到 python',
      command: python.command,
      hint: python.found ? undefined : '安装 Python 3.9 或更新版本，或点选择指定 python.exe。',
    },
    {
      id: 'brookesia',
      label: 'brookesia',
      found: brookesia.found,
      detail: brookesia.detail || '未找到 brookesia',
      command: brookesia.command,
      hint: brookesia.found
        ? (overrides.brookesia || brookesiaOnPath ? undefined : '使用仓库里的 tools/host-tools')
        : 'npm install -g esp-brookesia-toolkit@1.0.1，或点选择指定 brookesia.cmd。',
    },
    {
      id: 'usb',
      label: 'brookesia_usb',
      found: usbFound,
      detail: usbDetail,
      command: usbFound ? pythonForUsb : undefined,
      hint: usbFound ? undefined : '点选择指定已安装 brookesia_usb 的 python.exe，或 python -m pip install .reference/esp-brookesia/tools/brookesia_usb',
    },
  ];
}
