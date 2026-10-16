import { execFile } from 'child_process';
import * as fs from 'fs';
import * as path from 'path';

export interface CommandCheck {
  id: 'python' | 'bpk' | 'serial';
  label: string;
  found: boolean;
  detail: string;
  hint?: string;
  command?: string;
}

export interface CommandOverrides {
  python?: string;
}

export interface RepoTools {
  root: string;
  bpk: string;
}

function run(command: string, args: string[]): Promise<{ code: number; stdout: string; stderr: string }> {
  return new Promise((resolve) => {
    execFile(command, args, {
      timeout: 20000,
      windowsHide: true,
      env: { ...process.env, PYTHONIOENCODING: 'utf-8', PYTHONUTF8: '1' },
    }, (error, stdout, stderr) => {
      const code = typeof error?.code === 'number' ? error.code : error ? 1 : 0;
      resolve({
        code,
        stdout: stdout.toString(),
        stderr: stderr.toString() || (error && typeof error.code !== 'number' ? error.message : ''),
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

export function findRepo(workspaceRoots: string[]): RepoTools | undefined {
  for (const root of workspaceRoots) {
    const bpk = path.join(root, 'tools', 'bpk.py');
    if (fs.existsSync(bpk)) {
      return { root, bpk };
    }
  }
  return undefined;
}

export async function checkCommands(
  workspaceRoots: string[],
  overrides: CommandOverrides = {},
): Promise<CommandCheck[]> {
  const pythonPath = overrides.python || await where('python');
  let pythonFound = false;
  let pythonDetail = pythonPath || '';
  if (!pythonPath) {
    pythonDetail = '未找到 python';
  } else if (!fs.existsSync(pythonPath)) {
    pythonDetail = `${pythonPath} 文件不存在`;
  } else {
    const version = await run(pythonPath, ['--version']);
    const text = (version.stdout || version.stderr).trim();
    pythonFound = version.code === 0;
    pythonDetail = pythonFound ? `${pythonPath} ${text}`.trim() : (text || pythonPath);
  }

  const repo = findRepo(workspaceRoots);
  let serialFound = false;
  let serialDetail = '先找到可用的 Python';
  if (pythonFound && pythonPath) {
    const probe = await run(pythonPath, ['-c', 'import serial; print(serial.__version__)']);
    serialFound = probe.code === 0;
    serialDetail = serialFound
      ? `pyserial ${probe.stdout.trim()}`
      : '当前 Python 没有 pyserial';
  }

  return [
    {
      id: 'python',
      label: 'Python',
      found: pythonFound,
      detail: pythonDetail,
      command: pythonFound ? pythonPath : undefined,
      hint: pythonFound ? undefined : '安装 Python 3，或点选择指定 python.exe。icon.bin 打包只需标准库；源 PNG 转换另需 Pillow。',
    },
    {
      id: 'bpk',
      label: 'bpk.py',
      found: Boolean(repo),
      detail: repo ? repo.bpk : '工作区根目录没有 tools/bpk.py',
      command: repo?.bpk,
      hint: repo ? undefined : '用 OusePocket 仓库根目录作为工作区。',
    },
    {
      id: 'serial',
      label: 'pyserial',
      found: serialFound,
      detail: serialDetail,
      hint: serialFound ? undefined : '安装：python -m pip install "pyserial==3.5"。没有它只能打包，不能串口安装。',
    },
  ];
}
