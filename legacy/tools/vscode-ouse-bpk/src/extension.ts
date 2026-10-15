import type { ChildProcess } from 'child_process';
import * as path from 'path';
import * as vscode from 'vscode';
import { installApp, packApp, saveApp, startSimulate, type AppDraft } from './actions';
import { appByRoot, discoverApps, type BpkApp } from './discover';
import { checkCommands, type CommandCheck, type CommandOverrides } from './environment';
import { listSerialPorts, type SerialPortInfo } from './ports';

const PORT_KEY = 'ouseBpk.port';
const COMMANDS_KEY = 'ouseBpk.commandPaths';

interface Notice {
  tone: 'info' | 'error';
  text: string;
}

interface Incoming {
  type: string;
  root?: string;
  port?: string;
  id?: CommandCheck['id'];
  edit?: boolean;
  nameEn?: string;
  nameZh?: string;
  version?: string;
  visible?: boolean;
}

class BpkViewProvider implements vscode.WebviewViewProvider {
  public static readonly viewType = 'ouseBpk.apps';
  private view: vscode.WebviewView | undefined;
  private checks: CommandCheck[] = [];
  private apps: BpkApp[] = [];
  private ports: SerialPortInfo[] = [];
  private notice: Notice | undefined;
  private busy = false;
  private simulate: ChildProcess | undefined;

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
    return this.load();
  }

  private workspaceRoots(): string[] {
    return (vscode.workspace.workspaceFolders ?? []).map((folder) => folder.uri.fsPath);
  }

  private commandOverrides(): CommandOverrides {
    return this.context.workspaceState.get<CommandOverrides>(COMMANDS_KEY) ?? {};
  }

  private async load(): Promise<void> {
    const roots = this.workspaceRoots();
    const [checks, ports] = await Promise.all([
      checkCommands(roots, this.commandOverrides()),
      listSerialPorts(),
    ]);
    this.checks = checks;
    this.ports = ports;
    this.apps = discoverApps(roots);
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
      busy: this.busy,
      notice: this.notice,
    });
  }

  private appFrom(message: Incoming): BpkApp {
    const apps = discoverApps(this.workspaceRoots());
    const app = message.root ? appByRoot(apps, message.root) : undefined;
    if (!app) {
      throw new Error('这个应用已不在工作区里');
    }
    return app;
  }

  private async onMessage(message: Incoming): Promise<void> {
    try {
      if (message.type === 'ready' || message.type === 'refresh') {
        await this.load();
        return;
      }
      if (message.type === 'setPort') {
        await this.context.workspaceState.update(PORT_KEY, message.port ?? '');
        return;
      }
      if (message.type === 'pickCommand' && message.id) {
        await this.pickCommand(message.id);
        return;
      }
      if (this.busy) {
        return;
      }
      if (message.type === 'save') {
        const app = this.appFrom(message);
        saveApp(app, this.draftOf(app, message));
        this.notice = { tone: 'info', text: '已保存' };
        await this.load();
        return;
      }
      if (message.type === 'pack' || message.type === 'install' || message.type === 'simulate') {
        await this.runAppAction(message);
      }
    } catch (error) {
      this.notice = { tone: 'error', text: error instanceof Error ? error.message : String(error) };
      this.post();
    }
  }

  private async pickCommand(id: CommandCheck['id']): Promise<void> {
    const filters: { [name: string]: string[] } = {};
    if (id === 'brookesia') {
      filters.Brookesia = ['cmd', 'exe', 'bat'];
    } else {
      filters['可执行文件'] = ['exe'];
    }
    filters['所有文件'] = ['*'];
    const picked = await vscode.window.showOpenDialog({
      canSelectFiles: true,
      canSelectFolders: false,
      canSelectMany: false,
      openLabel: '选择',
      filters,
    });
    const file = picked?.[0]?.fsPath;
    if (!file) {
      return;
    }
    const overrides = { ...this.commandOverrides(), [id]: file };
    await this.context.workspaceState.update(COMMANDS_KEY, overrides);
    await this.load();
  }

  private draftOf(app: BpkApp, message: Incoming): AppDraft {
    const version = (message.version ?? app.version).trim();
    if (!version) {
      throw new Error('版本不能为空');
    }
    return {
      nameEn: message.nameEn ?? app.nameEn,
      nameZh: message.nameZh ?? app.nameZh,
      version,
      visible: message.visible ?? app.visible,
    };
  }

  private async runAppAction(message: Incoming): Promise<void> {
    const app = this.appFrom(message);
    const draft = this.draftOf(app, message);
    if (message.edit) {
      saveApp(app, draft);
    }
    this.busy = true;
    this.notice = undefined;
    this.post();
    try {
      const checks = await checkCommands(this.workspaceRoots(), this.commandOverrides());
      this.checks = checks;
      const bpk = await packApp(app, draft.version, checks, this.output);
      if (message.type === 'install') {
        const installer = path.join(this.context.extensionPath, 'bundled', 'install-bpk.py');
        await installApp(bpk, message.port ?? '', checks, this.output, installer);
        this.notice = { tone: 'info', text: `已安装到 ${message.port}` };
      } else if (message.type === 'simulate') {
        this.simulate = startSimulate(app, bpk, checks, this.output, this.simulate);
        this.notice = { tone: 'info', text: '预览已启动，输出在 Ouse BPK 通道' };
      } else {
        this.notice = { tone: 'info', text: `已打包 ${path.basename(bpk)}` };
      }
    } finally {
      this.busy = false;
      await this.load();
    }
  }

  stopSimulate(): void {
    if (this.simulate && this.simulate.exitCode === null) {
      this.simulate.kill();
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
<div class="toolbar">
  <select id="port"></select>
  <input id="portCustom" type="text" placeholder="COM10" spellcheck="false" hidden>
</div>
<div id="root"></div>
<script nonce="${nonce}" src="${script}"></script>
</body>
</html>`;
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
  provider?.stopSimulate();
  provider = undefined;
}
