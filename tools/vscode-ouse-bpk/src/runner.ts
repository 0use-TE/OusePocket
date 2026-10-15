import { spawn, type ChildProcess } from 'child_process';
import * as vscode from 'vscode';

export interface RunResult {
  code: number;
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
  const batch = process.platform === 'win32' && /\.(cmd|bat)$/i.test(command);
  const quote = (value: string) => `"${value.replace(/"/g, '""')}"`;
  const child = batch
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
  const done = new Promise<RunResult>((resolve) => {
    child.stdout?.on('data', (chunk: Buffer) => output.append(chunk.toString()));
    child.stderr?.on('data', (chunk: Buffer) => output.append(chunk.toString()));
    child.on('error', (error) => {
      output.appendLine(error.message);
      resolve({ code: 1 });
    });
    child.on('close', (code) => {
      output.appendLine(`exit ${code ?? 1}`);
      resolve({ code: code ?? 1 });
    });
  });
  return { process: child, done };
}
