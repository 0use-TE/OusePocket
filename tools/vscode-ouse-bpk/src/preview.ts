import * as fs from 'fs';
import * as path from 'path';
import * as vscode from 'vscode';
import { findRepo } from './environment';
import { BUNDLED_THEME_COLORS } from './theme-colors.generated';

export interface PreviewDocument {
  title: string;
  page: string;
  script: string;
  capabilities?: {
    background?: boolean;
    notifications?: boolean;
    network?: boolean;
    media?: boolean;
    alarms?: boolean;
  };
}

export interface ThemeColors {
  api: number;
  tokens: string[];
  light: Record<string, string>;
  dark: Record<string, string>;
}

interface Session {
  panel: vscode.WebviewPanel;
  document: PreviewDocument;
  themeColors: ThemeColors;
  extensionUri: vscode.Uri;
  workspaceRoots: string[];
}

const sessions = new Map<string, Session>();

function html(webview: vscode.Webview, extensionUri: vscode.Uri): string {
  const style = webview.asWebviewUri(vscode.Uri.joinPath(extensionUri, 'media', 'preview.css'));
  const script = webview.asWebviewUri(vscode.Uri.joinPath(extensionUri, 'media', 'preview.js'));
  const nonce = String(Date.now());
  return `<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src ${webview.cspSource}; script-src 'nonce-${nonce}' 'unsafe-eval';">
<link rel="stylesheet" href="${style}">
</head>
<body>
<div class="frame">
<div class="toolbar">
  <strong id="title">预览</strong>
  <div class="toolbar-row">
    <label class="field">外观
      <select id="theme">
        <option value="light">浅色</option>
        <option value="dark">深色</option>
      </select>
    </label>
    <label class="field">语言
      <select id="locale"></select>
    </label>
    <button id="restart" type="button">重启</button>
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
<div class="stage">
  <div class="phone" id="phone">
    <div class="screen" id="screen"></div>
    <div class="cover" id="home" hidden>
      <p>已回桌面</p>
      <button id="resume" type="button">恢复</button>
    </div>
    <div class="fault" id="fault" hidden></div>
  </div>
</div>
<pre class="log" id="log"></pre>
</div>
<script nonce="${nonce}" src="${script}"></script>
</body>
</html>`;
}

function validateThemeColors(value: unknown, source: string): ThemeColors {
  const record = (item: unknown): item is Record<string, unknown> =>
    !!item && typeof item === 'object' && !Array.isArray(item);
  const expected = BUNDLED_THEME_COLORS.tokens;
  const invalid = (): never => { throw new Error(`${source}: 主题须含 api=1、完整 tokens 和 light/dark 的 12 个 #RRGGBB 颜色`); };
  if (!record(value) || Object.keys(value).sort().join(',') !== 'api,dark,light,tokens' || value.api !== 1 ||
      !Array.isArray(value.tokens) || value.tokens.length !== expected.length ||
      value.tokens.some((token, index) => token !== expected[index])) return invalid();
  const palettes: Record<string, Record<string, string>> = {};
  for (const mode of ['light', 'dark']) {
    const palette = value[mode];
    if (!record(palette) || Object.keys(palette).length !== expected.length ||
        expected.some((token) => !Object.hasOwn(palette, token) || typeof palette[token] !== 'string' ||
          !/^#[0-9a-fA-F]{6}$/.test(palette[token] as string))) return invalid();
    palettes[mode] = Object.fromEntries(expected.map((token) => [token, (palette[token] as string).toLowerCase()]));
  }
  return { api: 1, tokens: [...expected], light: palettes.light, dark: palettes.dark };
}

function readThemeFile(file: string): ThemeColors | undefined {
  if (!fs.existsSync(file)) return undefined;
  let data: unknown;
  try {
    data = JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch (error) {
    throw new Error(`${file}: 无法读取主题 JSON (${error instanceof Error ? error.message : String(error)})`);
  }
  return validateThemeColors(data, file);
}

export function loadThemeColors(extensionUri: vscode.Uri, workspaceRoots: string[] = []): ThemeColors {
  // Marketplace / cloud installs are self-contained: colors are compiled into the extension.
  // Local monorepo only: prefer live colors.json so preview tracks firmware without reinstall.
  const repo = findRepo(workspaceRoots);
  if (repo) {
    const live = readThemeFile(path.join(repo.root, 'components', 'ouse_core', 'theme', 'colors.json'));
    if (live) return live;
  }
  const media = readThemeFile(path.join(extensionUri.fsPath, 'media', 'theme-colors.json'));
  return media ?? validateThemeColors(BUNDLED_THEME_COLORS, 'bundled theme');
}

function postApp(session: Session): void {
  void session.panel.webview.postMessage({
    type: 'app',
    ...session.document,
    themeColors: session.themeColors,
  });
}

export function openAppPreview(
  context: vscode.ExtensionContext,
  key: string,
  document: PreviewDocument,
  workspaceRoots: string[] = [],
): void {
  const roots = workspaceRoots.length
    ? workspaceRoots
    : (vscode.workspace.workspaceFolders?.map((folder) => folder.uri.fsPath) ?? []);
  const themeColors = loadThemeColors(context.extensionUri, roots);
  let session = sessions.get(key);
  if (!session) {
    const panel = vscode.window.createWebviewPanel(
      'ouseBpk.preview',
      document.title || '预览',
      vscode.ViewColumn.Beside,
      { enableScripts: true, retainContextWhenHidden: true, localResourceRoots: [context.extensionUri] },
    );
    session = {
      panel,
      document,
      themeColors,
      extensionUri: context.extensionUri,
      workspaceRoots: roots,
    };
    sessions.set(key, session);
    panel.onDidDispose(() => {
      if (sessions.get(key)?.panel === panel) sessions.delete(key);
    });
    panel.webview.onDidReceiveMessage((message: { type?: string }) => {
      const current = sessions.get(key);
      if (!current) return;
      if (message?.type === 'ready') {
        postApp(current);
        return;
      }
      if (message?.type === 'restart') {
        void reloadPreview(key);
      }
    });
  }
  session.document = document;
  session.themeColors = themeColors;
  session.workspaceRoots = roots;
  session.extensionUri = context.extensionUri;
  session.panel.title = document.title || '预览';
  session.panel.reveal(vscode.ViewColumn.Beside);
  session.panel.webview.html = html(session.panel.webview, context.extensionUri);
}

async function reloadPreview(key: string): Promise<void> {
  const session = sessions.get(key);
  if (!session) return;
  try {
    const page = await readEditorText(path.join(key, 'page.json'));
    const script = await readEditorText(path.join(key, 'app.js'));
    let capabilities = session.document.capabilities;
    try {
      const manifest = JSON.parse(await readEditorText(path.join(key, 'manifest.json'))) as {
        capabilities?: { background?: unknown; notifications?: unknown; network?: unknown; media?: unknown; alarms?: unknown };
      };
      const caps = manifest.capabilities;
      if (caps && typeof caps === 'object') {
        capabilities = {
          background: Boolean(caps.background),
          notifications: Boolean(caps.notifications),
          network: Boolean(caps.network),
          media: Boolean(caps.media),
          alarms: Boolean(caps.alarms),
        };
      }
    } catch {
      /* keep existing capabilities */
    }
    session.document = { ...session.document, page, script, capabilities };
    session.themeColors = loadThemeColors(session.extensionUri, session.workspaceRoots);
    postApp(session);
  } catch (error) {
    const text = error instanceof Error ? error.message : String(error);
    await session.panel.webview.postMessage({ type: 'fault', message: text });
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
