import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const version = process.argv[2];
const step = Number(process.argv[3]);
if (!/^\d+\.\d+\.\d+$/.test(version ?? '') || !Number.isSafeInteger(step) || step < 1) {
  throw new Error('Usage: node tools/set-version.mjs 1.1.0 10');
}
const manifestFile = path.join(root, 'src/manifest.json');
const manifest = JSON.parse(fs.readFileSync(manifestFile, 'utf8'));
manifest.package.version = version;
fs.writeFileSync(manifestFile, JSON.stringify(manifest, null, 2) + '\n');
const entry = path.join(root, 'src/app/main.js');
const js = fs.readFileSync(entry, 'utf8')
  .replace(/^const STEP = \d+;/m, `const STEP = ${step};`)
  .replace(/^const VERSION = '[^']+';/m, `const VERSION = '${version}';`);
fs.writeFileSync(entry, js);
console.log(`Counter ${version}: +${step} per click; package ID preserved.`);
