import { spawn, type ChildProcess } from 'child_process';
import * as vscode from 'vscode';

export interface RunResult {
  code: number;
  stdout: string;
  stderr: string;
}

function spawnLogged(
  command: string,
  args: string[],
  cwd?: string,
): ChildProcess {
  const batch = process.platform === 'win32' && /\.(cmd|bat)$/i.test(command);
  const quote = (value: string) => `"${value.replace(/"/g, '""')}"`;
  return batch
    ? spawn(process.env.ComSpec || 'cmd.exe', ['/d', '/s', '/c', `"${[quote(command), ...args.map(quote)].join(' ')}"`], {
      cwd,
      windowsHide: true,
      windowsVerbatimArguments: true,
      env: { ...process.env, PYTHONIOENCODING: 'utf-8', PYTHONUTF8: '1' },
    })
    : spawn(command, args, {
      cwd,
      windowsHide: true,
      env: { ...process.env, PYTHONIOENCODING: 'utf-8', PYTHONUTF8: '1' },
    });
}

export function runLogged(
  output: vscode.OutputChannel,
  command: string,
  args: string[],
  cwd?: string,
): { process: ChildProcess; done: Promise<RunResult> } {
  output.show(true);
  output.appendLine('');
  output.appendLine(`> ${command} ${args.join(' ')}`);
  if (cwd) {
    output.appendLine(`  ${cwd}`);
  }
  const child = spawnLogged(command, args, cwd);
  let stdout = '';
  let stderr = '';
  const done = new Promise<RunResult>((resolve) => {
    child.stdout?.on('data', (chunk: Buffer) => {
      const text = chunk.toString();
      stdout += text;
      output.append(text);
    });
    child.stderr?.on('data', (chunk: Buffer) => {
      const text = chunk.toString();
      stderr += text;
      output.append(text);
    });
    child.on('error', (error) => {
      output.appendLine(error.message);
      resolve({ code: 1, stdout, stderr: stderr || error.message });
    });
    child.on('close', (code) => {
      output.appendLine(`exit ${code ?? 1}`);
      resolve({ code: code ?? 1, stdout, stderr });
    });
  });
  return { process: child, done };
}
