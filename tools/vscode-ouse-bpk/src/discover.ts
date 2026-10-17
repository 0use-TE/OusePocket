import * as fs from 'fs';
import * as path from 'path';

const SKIP = new Set([
  'node_modules', '.reference', 'build', 'dist', '.git', 'out', 'managed_components',
  'legacy', '.tools', 'tmp',
]);

export interface BpkApp {
  root: string;
  manifestPath: string;
  relativeRoot: string;
  id: string;
  name: string;
  icon: string;
  names?: { zh_CN?: string; en?: string };
  background?: boolean;
  notifications?: boolean;
  network?: boolean;
  media?: boolean;
  alarms?: boolean;
  issues: string[];
  packagePath?: string;
}

interface ManifestFile {
  runtime?: unknown;
  format?: unknown;
  api?: unknown;
  id?: unknown;
  name?: unknown;
  names?: unknown;
  capabilities?: unknown;
  icon?: unknown;
  assets?: unknown;
}

export interface AppDraft {
  id: string;
  name: string;
  icon: string;
  nameZh?: string;
  nameEn?: string;
  background?: boolean;
  notifications?: boolean;
  network?: boolean;
  media?: boolean;
  alarms?: boolean;
}

function walkApps(dir: string, found: string[]): void {
  let entries: fs.Dirent[];
  try {
    entries = fs.readdirSync(dir, { withFileTypes: true });
  } catch {
    return;
  }
  const names = new Set(entries.filter((entry) => entry.isFile()).map((entry) => entry.name));
  if (names.has('manifest.json') && names.has('page.json') && names.has('app.js')) {
    found.push(dir);
  }
  for (const entry of entries) {
    if (!entry.isDirectory() || SKIP.has(entry.name)) {
      continue;
    }
    walkApps(path.join(dir, entry.name), found);
  }
}

function byteLength(value: string): number {
  return Buffer.byteLength(value, 'utf8');
}

function validName(value: unknown): value is string {
  return typeof value === 'string' && !value.includes('\0') && byteLength(value) > 0 &&
    byteLength(value) < 48 && Buffer.from(value, 'utf8').toString('utf8') === value;
}

interface AssetFile {
  id: string;
  type: 'image' | 'text' | 'binary';
  path: string;
}

function validAssetPath(value: unknown): value is string {
  return typeof value === 'string' && value.length <= 95 &&
    /^assets\/(?:[A-Za-z0-9._-]+\/)*[A-Za-z0-9._-]+$/.test(value) &&
    value.split('/').every((part) => part !== '.' && part !== '..');
}

function assetsOf(manifest: ManifestFile): AssetFile[] | undefined {
  if (manifest.assets === undefined) return [];
  if (!Array.isArray(manifest.assets) || manifest.assets.length > 16) return undefined;
  const ids = new Set<string>(), paths = new Set<string>();
  for (const item of manifest.assets) {
    if (!item || typeof item !== 'object' || Array.isArray(item) ||
        Object.keys(item).sort().join(',') !== 'id,path,type' ||
        typeof item.id !== 'string' || !/^[A-Za-z][A-Za-z0-9_-]{0,30}$/.test(item.id) || ids.has(item.id) ||
        !['image', 'text', 'binary'].includes(item.type) || !validAssetPath(item.path) || paths.has(item.path)) {
      return undefined;
    }
    ids.add(item.id); paths.add(item.path);
  }
  return manifest.assets as AssetFile[];
}

export function manifestIssues(manifest: ManifestFile): string[] {
  const issues: string[] = [];
  if (manifest.runtime !== 'ouse-core' || manifest.api !== 1) {
    issues.push('需要 runtime=ouse-core、api=1');
  }
  if (Object.hasOwn(manifest, 'format')) issues.push('BPK 清单不使用 format 字段，请删除');
  const id = manifest.id;
  if (typeof id !== 'string' || !/^user\.[a-z0-9._-]+$/.test(id) || id.length >= 48) {
    issues.push('ID 必须是 user.*，最多 47 个 ASCII 字符');
  }
  if (!validName(manifest.name)) {
    issues.push('名称须为 1～47 个 UTF-8 字节');
  }
  if (manifest.icon !== 'icon.png' && manifest.icon !== 'icon.bin') {
    issues.push('图标须为源目录内的 icon.png 或 icon.bin');
  }
  if (manifest.names !== undefined) {
    if (!manifest.names || typeof manifest.names !== 'object' || Array.isArray(manifest.names)) {
      issues.push('names 须为含 zh_CN/en 的对象，未配置语言回退默认名称');
    } else {
      for (const [locale, value] of Object.entries(manifest.names)) {
        if (!['zh_CN', 'en'].includes(locale) || !validName(value)) {
          issues.push('names 仅支持 zh_CN/en，每个名称须为 1～47 个 UTF-8 字节');
        }
      }
    }
  }
  if (manifest.capabilities !== undefined) {
    const caps = manifest.capabilities;
    if (!caps || typeof caps !== 'object' || Array.isArray(caps) ||
        Object.entries(caps).some(([key, value]) => !['background', 'notifications', 'network', 'media', 'alarms'].includes(key) || typeof value !== 'boolean')) {
      issues.push('capabilities 仅支持 background/notifications/network/media/alarms: true/false');
    }
  }
  if (!assetsOf(manifest)) {
    issues.push('assets 最多 16 项，每项仅含 id/type/path；ID 为 1～31 位 ASCII 标识符，type 为 image/text/binary，path 为 assets/ 下最多 95 字节的唯一规范路径');
  }
  return issues;
}

function imageSize(bytes: Buffer, png: boolean, side: number, maximum: number): number {
  let width = 0, height = 0;
  if (!png) {
    if (bytes.length < 8 || bytes.subarray(0, 4).toString('ascii') !== 'OIC1') throw new Error('需要 OIC1 头');
    width = bytes.readUInt16LE(4); height = bytes.readUInt16LE(6);
    if (bytes.length !== 8 + width * height * 4) throw new Error('像素数据长度不匹配');
  } else {
    const signature = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
    if (bytes.length < 33 || !bytes.subarray(0, 8).equals(signature) ||
        bytes.readUInt32BE(8) !== 13 || bytes.subarray(12, 16).toString('ascii') !== 'IHDR') throw new Error('不是 PNG 图片');
    width = bytes.readUInt32BE(16); height = bytes.readUInt32BE(20);
  }
  const pixels = width * height * 4;
  if (width < 1 || width > side || height < 1 || height > side || pixels > maximum) {
    throw new Error(`宽、高须为 1～${side} 像素，像素数据最多 ${maximum} 字节`);
  }
  return pixels;
}

function validPageDocument(value: unknown): boolean {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return false;
  const page = value as Record<string, unknown>;
  const hasRoot = Object.hasOwn(page, 'root'), hasPages = Object.hasOwn(page, 'pages');
  if (page.api !== 1 || hasRoot === hasPages) return false;
  const object = (node: unknown): boolean => !!node && typeof node === 'object' && !Array.isArray(node);
  if (hasRoot) return object(page.root);
  if (!Array.isArray(page.pages) || page.pages.length < 1 || page.pages.length > 16) return false;
  const ids = new Set<string>();
  for (const item of page.pages) {
    if (!object(item)) return false;
    const entry = item as Record<string, unknown>;
    if (typeof entry.id !== 'string' || !/^[A-Za-z][A-Za-z0-9_-]{0,30}$/.test(entry.id) ||
        ids.has(entry.id) || !object(entry.root)) return false;
    ids.add(entry.id);
  }
  return typeof page.initialPage === 'string' && ids.has(page.initialPage);
}

function fileIssues(root: string, manifest: ManifestFile): string[] {
  const issues: string[] = [];
  const checks: { name: string; limit: number }[] = [
    { name: 'manifest.json', limit: 8192 },
    { name: 'page.json', limit: 32768 },
    { name: 'app.js', limit: 32768 },
  ];
  for (const check of checks) {
    const full = path.join(root, check.name);
    const size = fs.statSync(full).size;
    if (size < 1 || size > check.limit) {
      issues.push(`${check.name} 须为 1～${check.limit} 字节`);
    }
  }
  const packed = new Map<string, number>();
  packed.set('page.json', fs.statSync(path.join(root, 'page.json')).size);
  packed.set('app.js', fs.statSync(path.join(root, 'app.js')).size);
  if (manifest.icon === 'icon.png' || manifest.icon === 'icon.bin') {
    const name = manifest.icon;
    try {
      const pixels = imageSize(fs.readFileSync(path.join(root, name)), name === 'icon.png', 96, 36864);
      packed.set('icon.bin', 8 + pixels);
    } catch (error) {
      issues.push(error instanceof Error ? `图标：${error.message}` : `无法读取 ${name}`);
    }
  }
  const assets = assetsOf(manifest);
  if (assets) {
    let imageBytes = 0, dataBytes = 0;
    const packedAssets: AssetFile[] = [];
    for (const asset of assets) {
      const name = asset.path;
      try {
        const full = fs.realpathSync(path.join(root, name));
        const relative = path.relative(fs.realpathSync(root), full);
        if (relative.startsWith('..') || path.isAbsolute(relative) || !fs.statSync(full).isFile()) {
          throw new Error('资源须为应用源目录内的文件');
        }
        const bytes = fs.readFileSync(full);
        let packedName = name, size = bytes.length;
        if (asset.type === 'image') {
          const png = path.extname(name).toLowerCase() === '.png';
          const pixels = imageSize(bytes, png, 512, 131072);
          imageBytes += pixels; size = pixels + 8;
          if (png) packedName = name.slice(0, -4) + '.bin';
        } else {
          const limit = asset.type === 'text' ? 16384 : 65536;
          if (size > limit) throw new Error(`最多 ${limit} 字节`);
          if (asset.type === 'text' && (bytes.includes(0) || !Buffer.from(bytes.toString('utf8'), 'utf8').equals(bytes))) {
            throw new Error('文本须为无 NUL 的 UTF-8');
          }
          dataBytes += size;
        }
        if (packed.has(packedName)) throw new Error('转换后的资源路径重复');
        packed.set(packedName, size); packedAssets.push({ ...asset, path: packedName });
      } catch (error) {
        issues.push(`${name}：${error instanceof Error ? error.message : '无法读取资源'}`);
      }
    }
    if (imageBytes > 131072 || dataBytes > 131072) issues.push('图片像素和文本/二进制资源分别累计最多 131072 字节');
    const converted = { ...manifest, icon: 'icon.bin', ...(manifest.assets === undefined ? {} : { assets: packedAssets }) };
    const manifestBytes = byteLength(JSON.stringify(converted));
    if (manifestBytes > 8192) issues.push('转换后的清单最多 8192 字节');
    packed.set('manifest.json', manifestBytes);
    let packageBytes = 22;
    for (const [name, size] of packed) packageBytes += 76 + 2 * byteLength(name) + size;
    if (packageBytes > 524288) issues.push('最终 BPK 最多 524288 字节');
  }

  try {
    const page: unknown = JSON.parse(fs.readFileSync(path.join(root, 'page.json'), 'utf8'));
    if (!validPageDocument(page)) {
      issues.push('page.json 需要 api=1；root 与 pages 二选一。多页须有 1～16 个唯一 ASCII ID、root 对象和有效 initialPage');
    }
  } catch {
    issues.push('page.json 不是合法 JSON');
  }
  return issues;
}

function shouldList(manifest: ManifestFile | undefined, broken: boolean): boolean {
  if (broken || !manifest) {
    return true;
  }
  if (manifest.runtime === 'ouse-core') {
    return true;
  }
  if (typeof manifest.id === 'string' && manifest.id.startsWith('user.')) {
    return true;
  }
  if (typeof manifest.id === 'string' && manifest.id.startsWith('ouse.')) {
    return false;
  }
  return true;
}

function textField(value: unknown): string {
  return typeof value === 'string' ? value : '';
}

export function discoverApps(workspaceRoots: string[], packageDir?: string): BpkApp[] {
  const folders: string[] = [];
  for (const root of workspaceRoots) {
    walkApps(root, folders);
  }
  const apps: BpkApp[] = [];
  for (const root of folders) {
    const manifestPath = path.join(root, 'manifest.json');
    let parsed: ManifestFile | undefined;
    let broken = false;
    try {
      parsed = JSON.parse(fs.readFileSync(manifestPath, 'utf8')) as ManifestFile;
      if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
        broken = true;
        parsed = undefined;
      }
    } catch {
      broken = true;
    }
    if (!shouldList(parsed, broken)) {
      continue;
    }
    const issues = broken ? ['manifest.json 不是合法 JSON'] : manifestIssues(parsed ?? {});
    if (!broken) {
      issues.push(...fileIssues(root, parsed ?? {}));
    }
    const id = textField(parsed?.id);
    const workspace = workspaceRoots.find((item) => {
      const relative = path.relative(item, root);
      return relative === '' || (!relative.startsWith('..') && !path.isAbsolute(relative));
    }) ?? workspaceRoots[0];
    const packagePath = id && packageDir ? path.join(packageDir, `${id}.bpk`) : undefined;
    const caps = parsed?.capabilities && typeof parsed.capabilities === 'object' && !Array.isArray(parsed.capabilities)
      ? parsed.capabilities as { background?: unknown; notifications?: unknown; network?: unknown; media?: unknown; alarms?: unknown } : undefined;
    apps.push({
      root,
      manifestPath,
      relativeRoot: workspace ? path.relative(workspace, root) || '.' : root,
      id,
      name: textField(parsed?.name),
      icon: textField(parsed?.icon),
      names: parsed?.names && typeof parsed.names === 'object' && !Array.isArray(parsed.names)
        ? parsed.names as BpkApp['names'] : undefined,
      background: caps?.background === true,
      notifications: caps?.notifications === true,
      network: caps?.network === true,
      media: caps?.media === true,
      alarms: caps?.alarms === true,
      issues,
      packagePath: packagePath && fs.existsSync(packagePath) ? packagePath : undefined,
    });
  }
  apps.sort((a, b) => a.relativeRoot.localeCompare(b.relativeRoot));
  return apps;
}

export function appByRoot(apps: BpkApp[], root: string): BpkApp | undefined {
  const resolved = path.resolve(root);
  return apps.find((app) => path.resolve(app.root) === resolved);
}
