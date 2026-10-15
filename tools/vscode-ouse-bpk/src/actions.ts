import * as fs from 'fs';
import * as path from 'path';
import type { ChildProcess } from 'child_process';
import * as vscode from 'vscode';
import type { BpkApp } from './discover';
import type { CommandCheck } from './environment';
import { runLogged } from './runner';
import { zipFiles } from './zip';

export interface AppDraft {
  nameEn: string;
  nameZh: string;
  version: string;
  visible: boolean;
}

const REQUIRED = ['manifest.json', 'app/main.js', 'res/profile.json'];

function commandOf(checks: CommandCheck[], id: CommandCheck['id']): string {
  const found = checks.find((item) => item.id === id);
  if (!found?.found || !found.command) {
    throw new Error(found?.hint || `未找到 ${found?.label ?? id}`);
  }
  return found.command;
}

export function saveApp(app: BpkApp, draft: AppDraft): void {
  const version = draft.version.trim();
  if (!version) {
    throw new Error('版本不能为空');
  }
  const manifest = JSON.parse(fs.readFileSync(app.manifestPath, 'utf8')) as {
    package?: { name?: unknown; version?: string; visible?: boolean };
  };
  if (!manifest.package || typeof manifest.package !== 'object') {
    throw new Error('manifest 缺少 package');
  }
  const currentName = manifest.package.name;
  const name = currentName && typeof currentName === 'object' && !Array.isArray(currentName)
    ? { ...(currentName as Record<string, string>) }
    : {};
  name.en = draft.nameEn;
  name.zh_CN = draft.nameZh;
  manifest.package.name = name;
  manifest.package.version = version;
  manifest.package.visible = draft.visible;
  fs.writeFileSync(app.manifestPath, `${JSON.stringify(manifest, null, 2)}\n`, 'utf8');

  const packagePath = path.join(app.root, 'package.json');
  if (!fs.existsSync(packagePath)) {
    return;
  }
  const npmPackage = JSON.parse(fs.readFileSync(packagePath, 'utf8')) as { version?: unknown };
  if (typeof npmPackage.version === 'string') {
    npmPackage.version = version;
    fs.writeFileSync(packagePath, `${JSON.stringify(npmPackage, null, 2)}\n`, 'utf8');
  }
}

function collectFiles(dir: string, prefix: string, out: { name: string; full: string }[]): void {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    const name = prefix ? `${prefix}/${entry.name}` : entry.name;
    if (entry.isDirectory()) {
      collectFiles(full, name, out);
    } else if (entry.isFile()) {
      out.push({ name, full });
    }
  }
}

function packLite(app: BpkApp, version: string): string {
  const source = path.join(app.root, 'src');
  const files: { name: string; full: string }[] = [];
  collectFiles(source, '', files);
  files.sort((a, b) => a.name.localeCompare(b.name));
  const names = new Set(files.map((file) => file.name));
  for (const required of REQUIRED) {
    if (!names.has(required)) {
      throw new Error(`缺少 ${required}`);
    }
  }
  const targetDir = path.join(app.root, 'dist');
  fs.mkdirSync(targetDir, { recursive: true });
  const target = path.join(targetDir, `${app.id}-${version}.bpk`);
  fs.writeFileSync(target, zipFiles(files.map((file) => ({
    name: file.name,
    data: fs.readFileSync(file.full),
    modified: fs.statSync(file.full).mtime,
  }))));
  return target;
}

async function packToolkit(
  app: BpkApp,
  checks: CommandCheck[],
  output: vscode.OutputChannel,
): Promise<string> {
  const brookesia = commandOf(checks, 'brookesia');
  const result = await runLogged(output, brookesia, ['build'], app.root).done;
  if (result.code !== 0) {
    throw new Error('brookesia build 失败');
  }
  const files: string[] = [];
  for (const folder of ['dist', 'build']) {
    const dir = path.join(app.root, folder);
    if (!fs.existsSync(dir)) {
      continue;
    }
    for (const name of fs.readdirSync(dir)) {
      if (name.endsWith('.bpk')) {
        files.push(path.join(dir, name));
      }
    }
  }
  let best: { file: string; mtime: number } | undefined;
  for (const full of files) {
    const mtime = fs.statSync(full).mtimeMs;
    if (!best || mtime > best.mtime) {
      best = { file: full, mtime };
    }
  }
  if (!best) {
    throw new Error('dist 里没有 bpk');
  }
  return best.file;
}

export async function packApp(
  app: BpkApp,
  version: string,
  checks: CommandCheck[],
  output: vscode.OutputChannel,
): Promise<string> {
  if (app.kind === 'lite') {
    const target = packLite(app, version);
    output.appendLine(`已打包 ${target}`);
    return target;
  }
  return packToolkit(app, checks, output);
}

export async function installApp(
  bpk: string,
  port: string,
  checks: CommandCheck[],
  output: vscode.OutputChannel,
  installer: string,
): Promise<void> {
  const selected = port.trim().toUpperCase();
  if (!/^COM\d+$/.test(selected)) {
    throw new Error('先选择串口');
  }
  if (!fs.existsSync(installer)) {
    throw new Error('插件里缺少 install-bpk.py');
  }
  const python = commandOf(checks, 'usb');
  const result = await runLogged(output, python, [
    installer, '--port', selected, '--baudrate', '115200', bpk,
  ]).done;
  if (result.code !== 0) {
    throw new Error(`安装失败，串口可能正被监视器占用。详见 Output。`);
  }
}

export function startSimulate(
  app: BpkApp,
  bpk: string | undefined,
  checks: CommandCheck[],
  output: vscode.OutputChannel,
  current: ChildProcess | undefined,
): ChildProcess {
  if (current && current.exitCode === null) {
    current.kill();
    output.appendLine('已停止上一个预览。');
  }
  const brookesia = commandOf(checks, 'brookesia');
  const args = ['simulate', '--resolution', '320x480'];
  if (app.kind === 'lite') {
    if (!bpk) {
      throw new Error('没有可预览的 bpk');
    }
    args.push('--package', bpk);
  }
  return runLogged(output, brookesia, args, app.root).process;
}
