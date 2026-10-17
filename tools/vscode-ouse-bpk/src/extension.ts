import * as path from 'path';
import * as vscode from 'vscode';
import { installApp, packApp, removeApp, saveApp, syncDeviceApps } from './actions';
import { DEFAULT_BAUD, parseBaud } from './baud';
import { appByRoot, discoverApps, type AppDraft, type BpkApp } from './discover';
import { checkCommands, findRepo, type CommandCheck, type CommandOverrides } from './environment';
import { listSerialPorts, type SerialPortInfo } from './ports';
import { closeDeviceMirror, deviceMirrorPort, openDeviceMirror } from './device-mirror';
import { openAppPreview } from './preview';

const PORT_KEY = 'ouseBpk.port';
const BAUD_KEY = 'ouseBpk.baud';
const COMMANDS_KEY = 'ouseBpk.commandPaths';

interface Notice {
  tone: 'info' | 'error';
  text: string;
}

interface Incoming {
  type: string;
  root?: string;
  port?: string;
  baud?: number;
  id?: string;
  name?: string;
  nameZh?: string;
  nameEn?: string;
  icon?: string;
  background?: boolean;
  notifications?: boolean;
  edit?: boolean;
  openAfter?: boolean;
}

class BpkViewProvider implements vscode.WebviewViewProvider {
  public static readonly viewType = 'ouseBpk.apps';
  private view: vscode.WebviewView | undefined;
  private checks: CommandCheck[] = [];
  private apps: BpkApp[] = [];
  private ports: SerialPortInfo[] = [];
  private notice: Notice | undefined;
  private busy = false;
  private installed = new Set<string>();
  private alive = new Set<string>();
  private active = '';
  private synced = false;

  constructor(
    private readonly context: vscode.ExtensionContext,
    private readonly output: vscode.OutputChannel,
  ) {}

  resolveWebviewView(webviewView: vscode.WebviewView): void {
    this.view = webviewView;
    const webview = webviewView.webview;
    webview.options = { enableScripts: true, localResourceRoots: [this.context.extensionUri] };
    webview.html = this.html(webview);
    webview.onDidReceiveMessage((message: Incoming) => {
      void this.onMessage(message);
    });
    webviewView.onDidDispose(() => {
      this.view = undefined;
    });
  }

  refresh(): Promise<void> {
    return this.syncDevice();
  }

  private requirePortFree(): void {
    if (deviceMirrorPort()) {
      throw new Error('先关闭设备画面，串口正在使用');
    }
  }

  private savedBaud(): number {
    return parseBaud(this.context.workspaceState.get<number>(BAUD_KEY)) ?? DEFAULT_BAUD;
  }

  private baudFrom(message: Incoming): number {
    return parseBaud(message.baud) ?? this.savedBaud();
  }

  private workspaceRoots(): string[] {
    return (vscode.workspace.workspaceFolders ?? []).map((folder) => folder.uri.fsPath);
  }

  private commandOverrides(): CommandOverrides {
    return this.context.workspaceState.get<CommandOverrides>(COMMANDS_KEY) ?? {};
  }

  private packageDir(): string | undefined {
    const repo = findRepo(this.workspaceRoots());
    return repo ? path.join(repo.root, '.tools') : undefined;
  }

  private async load(): Promise<void> {
    const roots = this.workspaceRoots();
    const [checks, ports] = await Promise.all([
      checkCommands(roots, this.commandOverrides()),
      listSerialPorts(),
    ]);
    this.checks = checks;
    this.ports = ports;
    this.apps = discoverApps(roots, this.packageDir());
    this.post();
  }

  private post(): void {
    const port = this.context.workspaceState.get<string>(PORT_KEY) ?? '';
    void this.view?.webview.postMessage({
      type: 'state',
      commands: this.checks,
      apps: this.apps,
      ports: this.ports,
      port,
      baud: this.savedBaud(),
      busy: this.busy,
      notice: this.notice,
      synced: this.synced,
      installed: [...this.installed],
      alive: [...this.alive],
      active: this.active,
      mirror: Boolean(deviceMirrorPort()),
    });
  }

  private appFrom(message: Incoming): BpkApp {
    const apps = discoverApps(this.workspaceRoots(), this.packageDir());
    const app = message.root ? appByRoot(apps, message.root) : undefined;
    if (!app) {
      throw new Error('这个应用已不在工作区里');
    }
    return app;
  }

  private async onMessage(message: Incoming): Promise<void> {
    try {
      if (message.type === 'ready') {
        await this.load();
        return;
      }
      if (message.type === 'refresh' || message.type === 'sync') {
        await this.syncDevice(message);
        return;
      }
      if (message.type === 'setPort') {
        await this.context.workspaceState.update(PORT_KEY, message.port ?? '');
        return;
      }
      if (message.type === 'setBaud') {
        const baud = parseBaud(message.baud);
        if (baud !== undefined) {
          await this.context.workspaceState.update(BAUD_KEY, baud);
        }
        return;
      }
      if (message.type === 'pickCommand' && message.id === 'python') {
        await this.pickPython();
        return;
      }
      if (message.type === 'preview') {
        await this.preview(message);
        return;
      }
      if (message.type === 'device') {
        await this.openDevice(message);
        return;
      }
      if (this.busy) {
        return;
      }
      if (message.type === 'save') {
        const app = this.appFrom(message);
        saveApp(app, this.draftOf(message, app));
        this.notice = { tone: 'info', text: '已保存' };
        await this.load();
        return;
      }
      if (message.type === 'remove') {
        await this.runRemove(message);
        return;
      }
      if (message.type === 'pack' || message.type === 'install') {
        await this.runAppAction(message);
      }
    } catch (error) {
      this.notice = { tone: 'error', text: error instanceof Error ? error.message : String(error) };
      this.post();
    }
  }

  private async pickPython(): Promise<void> {
    const picked = await vscode.window.showOpenDialog({
      canSelectFiles: true,
      canSelectFolders: false,
      canSelectMany: false,
      openLabel: '选择',
      filters: { Python: ['exe'], '所有文件': ['*'] },
    });
    const file = picked?.[0]?.fsPath;
    if (!file) {
      return;
    }
    const overrides = { ...this.commandOverrides(), python: file };
    await this.context.workspaceState.update(COMMANDS_KEY, overrides);
    await this.load();
  }

  private async openDevice(message: Incoming): Promise<void> {
    if (this.busy) {
      throw new Error('正在打包或安装，请稍候');
    }
    const selected = (message.port ?? this.context.workspaceState.get<string>(PORT_KEY) ?? '').trim();
    if (!/^COM\d+$/i.test(selected)) {
      throw new Error('先选择串口');
    }
    await this.context.workspaceState.update(PORT_KEY, selected.toUpperCase());
    const baud = this.baudFrom(message);
    await this.context.workspaceState.update(BAUD_KEY, baud);
    this.checks = await checkCommands(this.workspaceRoots(), this.commandOverrides());
    const python = this.requireCommand('python');
    if (!this.checks.some((item) => item.id === 'serial' && item.found)) {
      throw new Error('当前 Python 没有 pyserial。安装：python -m pip install "pyserial==3.5"');
    }
    const repo = findRepo(this.workspaceRoots());
    if (!repo) {
      throw new Error('工作区根目录没有 tools/bpk.py');
    }
    this.output.appendLine(`设备画面 ${selected.toUpperCase()} ${baud}`);
    await openDeviceMirror(
      this.context,
      python,
      path.join(repo.root, 'tools', 'device-mcp.py'),
      selected.toUpperCase(),
      baud,
      this.output,
      () => this.post(),
    );
    this.post();
  }

  private async syncDevice(message?: Incoming): Promise<void> {
    if (this.busy) return;
    this.requirePortFree();
    const selected = (message?.port ?? this.context.workspaceState.get<string>(PORT_KEY) ?? '').trim();
    if (!/^COM\d+$/i.test(selected)) {
      throw new Error('先选择串口');
    }
    const baud = message ? this.baudFrom(message) : this.savedBaud();
    this.busy = true;
    this.notice = undefined;
    this.post();
    try {
      this.checks = await checkCommands(this.workspaceRoots(), this.commandOverrides());
      const python = this.requireCommand('python');
      const bpkScript = this.requireCommand('bpk');
      if (!this.checks.some((item) => item.id === 'serial' && item.found)) {
        throw new Error('当前 Python 没有 pyserial。安装：python -m pip install "pyserial==3.5"');
      }
      await this.context.workspaceState.update(PORT_KEY, selected.toUpperCase());
      await this.context.workspaceState.update(BAUD_KEY, baud);
      const status = await syncDeviceApps(selected, baud, python, bpkScript, this.output);
      this.installed = new Set(status.installed);
      this.alive = new Set(status.alive);
      this.active = status.active;
      this.synced = true;
      const live = status.alive.length;
      this.notice = {
        tone: 'info',
        text: status.installed.length
          ? `已同步 ${status.installed.length} 个应用` + (live ? `，${live} 个在运行` : '')
          : '设备上没有已安装应用',
      };
    } finally {
      this.busy = false;
      await this.load();
    }
  }

  private async preview(message: Incoming): Promise<void> {
    const current = this.appFrom(message);
    if (message.edit && !this.busy) {
      saveApp(current, this.draftOf(message, current));
      await this.load();
    }
    const app = message.edit && !this.busy ? this.appFrom(message) : current;
    const page = await readEditorText(path.join(app.root, 'page.json'));
    const script = await readEditorText(path.join(app.root, 'app.js'));
    openAppPreview(
      this.context,
      app.root,
      {
        title: app.name || app.id || '预览',
        page,
        script,
        capabilities: {
          background: Boolean(app.background),
          notifications: Boolean(app.notifications),
          network: Boolean(app.network),
          media: Boolean(app.media),
          alarms: Boolean(app.alarms),
        },
      },
      this.workspaceRoots(),
    );
  }

  private draftOf(message: Incoming, app: BpkApp): AppDraft {
    return {
      id: (message.id ?? app.id).trim(),
      name: message.name ?? app.name,
      nameZh: message.nameZh,
      nameEn: message.nameEn,
      icon: message.icon ?? app.icon,
      background: message.background ?? app.background,
      notifications: message.notifications ?? app.notifications,
    };
  }

  private requireCommand(id: 'python' | 'bpk'): string {
    const found = this.checks.find((item) => item.id === id);
    if (!found?.found || !found.command) {
      throw new Error(found?.hint || `未找到 ${found?.label ?? id}`);
    }
    return found.command;
  }

  private async runRemove(message: Incoming): Promise<void> {
    this.requirePortFree();
    const selected = (message.port ?? '').trim();
    if (!/^COM\d+$/i.test(selected)) {
      throw new Error('先选择串口');
    }
    const app = this.appFrom(message);
    const id = (message.id ?? app.id).trim();
    if (!/^user\.[a-z0-9._-]{1,42}$/.test(id)) {
      throw new Error('只能卸载 user.* 应用');
    }
    const choice = await vscode.window.showWarningMessage(
      `从设备卸载 ${id}？会删除应用和私有数据。`,
      { modal: true },
      '卸载',
    );
    if (choice !== '卸载') {
      return;
    }
    this.busy = true;
    this.notice = undefined;
    this.post();
    try {
      this.checks = await checkCommands(this.workspaceRoots(), this.commandOverrides());
      const python = this.requireCommand('python');
      const bpkScript = this.requireCommand('bpk');
      if (!this.checks.some((item) => item.id === 'serial' && item.found)) {
        throw new Error('当前 Python 没有 pyserial。安装：python -m pip install "pyserial==3.5"');
      }
      const baud = this.baudFrom(message);
      await this.context.workspaceState.update(PORT_KEY, selected.toUpperCase());
      await this.context.workspaceState.update(BAUD_KEY, baud);
      await removeApp(id, selected, baud, python, bpkScript, this.output);
      this.installed.delete(id);
      this.alive.delete(id);
      if (this.active === id) this.active = '';
      this.synced = true;
      this.notice = { tone: 'info', text: `已从 ${selected.toUpperCase()} 卸载 ${id}` };
    } finally {
      this.busy = false;
      await this.load();
    }
  }

  private async runAppAction(message: Incoming): Promise<void> {
    const current = this.appFrom(message);
    if (message.edit) {
      saveApp(current, this.draftOf(message, current));
      await this.load();
    }
    const app = this.appFrom(message);
    if (app.issues.length) {
      this.notice = { tone: 'error', text: app.issues[0] };
      await this.load();
      return;
    }
    if (message.type === 'install') {
      this.requirePortFree();
      const selected = (message.port ?? '').trim();
      if (!/^COM\d+$/i.test(selected)) {
        throw new Error('先选择串口');
      }
    }
    this.busy = true;
    this.notice = undefined;
    this.post();
    try {
      this.checks = await checkCommands(this.workspaceRoots(), this.commandOverrides());
      const python = this.requireCommand('python');
      const bpkScript = this.requireCommand('bpk');
      if (message.type === 'install' && !this.checks.some((item) => item.id === 'serial' && item.found)) {
        throw new Error('当前 Python 没有 pyserial。安装：python -m pip install "pyserial==3.5"');
      }
      const packageDir = this.packageDir();
      if (!packageDir) {
        throw new Error('工作区根目录没有 tools/bpk.py');
      }
      const bpk = await packApp(app, python, bpkScript, packageDir, this.output);
      if (message.type === 'install') {
        const baud = this.baudFrom(message);
        await this.context.workspaceState.update(BAUD_KEY, baud);
        await installApp(bpk, message.port ?? '', baud, python, bpkScript, this.output, message.openAfter !== false);
        if (app.id) {
          this.installed.add(app.id);
          if (message.openAfter !== false) {
            this.alive.add(app.id);
            this.active = app.id;
          }
          this.synced = true;
        }
        this.notice = { tone: 'info', text: `已安装到 ${(message.port ?? '').trim().toUpperCase()}` };
      } else {
        this.notice = { tone: 'info', text: `已打包 ${path.basename(bpk)}` };
      }
    } finally {
      this.busy = false;
      await this.load();
    }
  }

  private html(webview: vscode.Webview): string {
    const style = webview.asWebviewUri(vscode.Uri.joinPath(this.context.extensionUri, 'media', 'panel.css'));
    const script = webview.asWebviewUri(vscode.Uri.joinPath(this.context.extensionUri, 'media', 'panel.js'));
    const nonce = String(Date.now());
    return `<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src ${webview.cspSource}; script-src 'nonce-${nonce}';">
<link rel="stylesheet" href="${style}">
</head>
<body>
<section id="deviceBlock" class="block">
  <button type="button" id="deviceToggle" class="fold">▸  设备</button>
  <div id="deviceBody" class="toolbar" hidden>
    <div class="toolbar-row">
      <select id="port"></select>
      <input id="portCustom" type="text" placeholder="COM10" spellcheck="false" hidden>
    </div>
    <div class="toolbar-row device-actions">
      <select id="baud"></select>
      <input id="baudCustom" type="text" placeholder="921600" spellcheck="false" hidden>
      <button id="sync" type="button" title="同步设备应用">同步</button>
      <button id="device" type="button">设备画面</button>
    </div>
  </div>
</section>
<div id="root"></div>
<script nonce="${nonce}" src="${script}"></script>
</body>
</html>`;
  }
}

async function readEditorText(file: string): Promise<string> {
  try {
    const document = await vscode.workspace.openTextDocument(vscode.Uri.file(file));
    return document.getText();
  } catch {
    throw new Error(`无法读取 ${path.basename(file)}`);
  }
}

let provider: BpkViewProvider | undefined;

export function activate(context: vscode.ExtensionContext): void {
  const output = vscode.window.createOutputChannel('Ouse BPK');
  provider = new BpkViewProvider(context, output);
  context.subscriptions.push(
    output,
    vscode.window.registerWebviewViewProvider(BpkViewProvider.viewType, provider, {
      webviewOptions: { retainContextWhenHidden: true },
    }),
    vscode.commands.registerCommand('ouseBpk.refresh', () => provider?.refresh()),
  );
}

export function deactivate(): void {
  closeDeviceMirror();
  provider = undefined;
}
