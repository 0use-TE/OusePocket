import * as fs from 'fs';
import * as path from 'path';

const SKIP = new Set([
  'node_modules', '.reference', 'build', 'dist', '.git', 'out', 'managed_components',
  'ESP32-P4-WIFI6-Touch-LCD-3.5',
]);

export interface BpkService {
  name: string;
  version: string;
}

export interface BpkApp {
  root: string;
  manifestPath: string;
  relativeRoot: string;
  kind: 'toolkit' | 'lite';
  id: string;
  nameEn: string;
  nameZh: string;
  version: string;
  visible: boolean;
  services: BpkService[];
  distPath?: string;
}

interface ManifestPackage {
  id?: unknown;
  version?: unknown;
  visible?: unknown;
  name?: unknown;
}

interface ManifestFile {
  package?: ManifestPackage;
  runtime?: { type?: unknown };
  services?: unknown;
}

function walkManifests(dir: string, found: string[]): void {
  let entries: fs.Dirent[];
  try {
    entries = fs.readdirSync(dir, { withFileTypes: true });
  } catch {
    return;
  }
  for (const entry of entries) {
    if (SKIP.has(entry.name)) {
      continue;
    }
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      if (entry.name === 'src') {
        const manifest = path.join(full, 'manifest.json');
        if (fs.existsSync(manifest)) {
          found.push(manifest);
        }
      }
      walkManifests(full, found);
    }
  }
}

function asServices(value: unknown): BpkService[] {
  if (!Array.isArray(value)) {
    return [];
  }
  const services: BpkService[] = [];
  for (const item of value) {
    if (!item || typeof item !== 'object') {
      continue;
    }
    const record = item as { name?: unknown; version?: unknown };
    if (typeof record.name === 'string') {
      services.push({
        name: record.name,
        version: typeof record.version === 'string' ? record.version : '',
      });
    }
  }
  return services;
}

function namesOf(value: unknown): { en: string; zh: string } {
  if (!value || typeof value !== 'object') {
    return { en: typeof value === 'string' ? value : '', zh: '' };
  }
  const record = value as { en?: unknown; zh_CN?: unknown };
  return {
    en: typeof record.en === 'string' ? record.en : '',
    zh: typeof record.zh_CN === 'string' ? record.zh_CN : '',
  };
}

function newestBpk(distDir: string): string | undefined {
  if (!fs.existsSync(distDir)) {
    return undefined;
  }
  const files = fs.readdirSync(distDir).filter((name) => name.endsWith('.bpk'));
  let best: { file: string; mtime: number } | undefined;
  for (const file of files) {
    const full = path.join(distDir, file);
    const mtime = fs.statSync(full).mtimeMs;
    if (!best || mtime > best.mtime) {
      best = { file: full, mtime };
    }
  }
  return best?.file;
}

export function discoverApps(workspaceRoots: string[]): BpkApp[] {
  const manifests: string[] = [];
  for (const root of workspaceRoots) {
    walkManifests(root, manifests);
  }
  const apps: BpkApp[] = [];
  for (const manifestPath of manifests) {
    let parsed: ManifestFile;
    try {
      parsed = JSON.parse(fs.readFileSync(manifestPath, 'utf8')) as ManifestFile;
    } catch {
      continue;
    }
    const id = parsed.package?.id;
    const version = parsed.package?.version;
    if (parsed.runtime?.type !== 'JavaScript' || typeof id !== 'string' || typeof version !== 'string') {
      continue;
    }
    const projectRoot = path.dirname(path.dirname(manifestPath));
    const kind = fs.existsSync(path.join(projectRoot, 'brookesia.config.js')) ? 'toolkit' : 'lite';
    const names = namesOf(parsed.package?.name);
    const expectedLite = path.join(projectRoot, 'dist', `${id}-${version}.bpk`);
    const distPath = kind === 'lite'
      ? (fs.existsSync(expectedLite) ? expectedLite : undefined)
      : newestBpk(path.join(projectRoot, 'dist'));
    const workspace = workspaceRoots.find((item) => {
      const relative = path.relative(item, projectRoot);
      return relative === '' || (!relative.startsWith('..') && !path.isAbsolute(relative));
    }) ?? workspaceRoots[0];
    apps.push({
      root: projectRoot,
      manifestPath,
      relativeRoot: workspace ? path.relative(workspace, projectRoot) || '.' : projectRoot,
      kind,
      id,
      nameEn: names.en,
      nameZh: names.zh,
      version,
      visible: parsed.package?.visible !== false,
      services: asServices(parsed.services),
      distPath,
    });
  }
  apps.sort((a, b) => a.relativeRoot.localeCompare(b.relativeRoot));
  return apps;
}

export function appByRoot(apps: BpkApp[], root: string): BpkApp | undefined {
  const resolved = path.resolve(root);
  return apps.find((app) => path.resolve(app.root) === resolved);
}
