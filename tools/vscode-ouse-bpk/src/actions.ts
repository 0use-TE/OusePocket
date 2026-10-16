import * as fs from 'fs';
import * as path from 'path';
import type * as vscode from 'vscode';
import { parseBaud } from './baud';
import { manifestIssues, type AppDraft, type BpkApp } from './discover';
import { runLogged } from './runner';

export function saveApp(app: BpkApp, draft: AppDraft): void {
  const id = draft.id.trim();
  const name = draft.name;
  const icon = draft.icon;
  let manifest: Record<string, unknown>;
  try {
    const parsed: unknown = JSON.parse(fs.readFileSync(app.manifestPath, 'utf8'));
    if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
      throw new Error('manifest 不是对象');
    }
    manifest = parsed as Record<string, unknown>;
  } catch (error) {
    if (error instanceof Error && error.message === 'manifest 不是对象') {
      throw error;
    }
    throw new Error('manifest.json 不是合法 JSON，请先在编辑器里修好再保存');
  }
  manifest.runtime = 'ouse-core';
  manifest.api = 1;
  manifest.id = id;
  manifest.name = name;
  manifest.icon = icon;
  if (draft.nameZh !== undefined || draft.nameEn !== undefined) {
    const names = manifest.names && typeof manifest.names === 'object' && !Array.isArray(manifest.names)
      ? { ...manifest.names as Record<string, unknown> } : {};
    for (const [locale, value] of [['zh_CN', draft.nameZh], ['en', draft.nameEn]] as const) {
      if (value === undefined) continue;
      if (value === '') delete names[locale];
      else names[locale] = value;
    }
    if (Object.keys(names).length) manifest.names = names;
    else delete manifest.names;
  }
  if (draft.background !== undefined || draft.notifications !== undefined) {
    const caps = manifest.capabilities && typeof manifest.capabilities === 'object' && !Array.isArray(manifest.capabilities)
      ? { ...manifest.capabilities as Record<string, unknown> } : {};
    if (draft.background !== undefined) {
      if (draft.background) caps.background = true;
      else delete caps.background;
    }
    if (draft.notifications !== undefined) {
      if (draft.notifications) caps.notifications = true;
      else delete caps.notifications;
    }
    if (Object.keys(caps).length) manifest.capabilities = caps;
    else delete manifest.capabilities;
  }
  const issues = manifestIssues(manifest);
  if (issues.length) throw new Error(issues[0]);
  fs.writeFileSync(app.manifestPath, `${JSON.stringify(manifest, null, 2)}\n`, 'utf8');
}

export async function packApp(
  app: BpkApp,
  python: string,
  bpkScript: string,
  packageDir: string,
  output: vscode.OutputChannel,
): Promise<string> {
  if (!app.id) {
    throw new Error('应用没有 ID');
  }
  const target = path.join(packageDir, `${app.id}.bpk`);
  const result = await runLogged(output, python, [bpkScript, 'pack', app.root, target]).done;
  if (result.code !== 0) {
    throw new Error(app.icon === 'icon.png'
      ? '打包失败，详见 Ouse BPK 输出。PNG 转换需要当前 Python 安装 Pillow；也可明确使用预转换的 icon.bin。'
      : '打包失败，详见 Ouse BPK 输出');
  }
  return target;
}

export async function installApp(
  bpk: string,
  port: string,
  baud: number,
  python: string,
  bpkScript: string,
  output: vscode.OutputChannel,
  openAfter: boolean,
): Promise<void> {
  const selected = port.trim().toUpperCase();
  if (!/^COM\d+$/.test(selected)) {
    throw new Error('先选择串口');
  }
  if (parseBaud(baud) === undefined) {
    throw new Error('波特率无效，范围 1200～12000000');
  }
  if (!fs.existsSync(bpk)) {
    throw new Error('没有可安装的 bpk');
  }
  const args = [bpkScript, '--port', selected, '--baud', String(baud), 'install', bpk];
  if (openAfter) {
    args.push('--open');
  }
  const result = await runLogged(output, python, args).done;
  if (result.code !== 0) {
    throw new Error('安装失败。串口可能被监视器占用，详见 Ouse BPK 输出。');
  }
}

export async function removeApp(
  id: string,
  port: string,
  baud: number,
  python: string,
  bpkScript: string,
  output: vscode.OutputChannel,
): Promise<void> {
  const selected = port.trim().toUpperCase();
  const appId = id.trim();
  if (!/^COM\d+$/.test(selected)) {
    throw new Error('先选择串口');
  }
  if (parseBaud(baud) === undefined) {
    throw new Error('波特率无效，范围 1200～12000000');
  }
  if (!/^user\.[a-z0-9._-]{1,42}$/.test(appId)) {
    throw new Error('只能卸载 user.* 应用');
  }
  const result = await runLogged(output, python, [
    bpkScript, '--port', selected, '--baud', String(baud), 'remove', appId,
  ]).done;
  if (result.code !== 0) {
    throw new Error('卸载失败。串口可能被监视器占用，或设备上没有这个应用。详见 Ouse BPK 输出。');
  }
}

export interface DeviceSync {
  installed: string[];
  active: string;
  alive: string[];
}

export async function syncDeviceApps(
  port: string,
  baud: number,
  python: string,
  bpkScript: string,
  output: vscode.OutputChannel,
): Promise<DeviceSync> {
  const selected = port.trim().toUpperCase();
  if (!/^COM\d+$/.test(selected)) {
    throw new Error('先选择串口');
  }
  if (parseBaud(baud) === undefined) {
    throw new Error('波特率无效，范围 1200～12000000');
  }
  const result = await runLogged(output, python, [
    bpkScript, '--port', selected, '--baud', String(baud), 'sync',
  ]).done;
  if (result.code !== 0) {
    throw new Error('同步失败。串口可能被监视器或设备画面占用，详见 Ouse BPK 输出。');
  }
  const lines = result.stdout.trim().split(/\r?\n/).filter((line) => line.trim().startsWith('{'));
  const raw = lines[lines.length - 1];
  if (!raw) {
    throw new Error('同步失败：设备没有返回状态');
  }
  let parsed: { installed?: unknown; active?: unknown; alive?: unknown };
  try {
    parsed = JSON.parse(raw) as { installed?: unknown; active?: unknown; alive?: unknown };
  } catch {
    throw new Error('同步失败：设备状态无法解析');
  }
  const installed = Array.isArray(parsed.installed)
    ? parsed.installed.filter((id): id is string => typeof id === 'string' && id.length > 0)
    : [];
  const alive = Array.isArray(parsed.alive)
    ? parsed.alive.filter((id): id is string => typeof id === 'string' && id.length > 0)
    : [];
  const active = typeof parsed.active === 'string' ? parsed.active : '';
  return { installed, active, alive };
}
