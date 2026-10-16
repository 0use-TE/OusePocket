import { execFile } from 'child_process';

export interface SerialPortInfo {
  id: string;
  label: string;
}

function powershell(script: string): Promise<string> {
  return new Promise((resolve) => {
    execFile('powershell.exe', ['-NoProfile', '-Command', script], {
      timeout: 15000,
      windowsHide: true,
    }, (_error, stdout) => {
      resolve(stdout.toString());
    });
  });
}

function addPort(ports: Map<string, string>, id: string, label: string): void {
  const name = id.toUpperCase();
  if (!/^COM\d+$/.test(name)) {
    return;
  }
  const current = ports.get(name);
  if (!current || current === name) {
    ports.set(name, label.trim() || name);
  }
}

export async function listSerialPorts(): Promise<SerialPortInfo[]> {
  if (process.platform !== 'win32') {
    return [];
  }
  const ports = new Map<string, string>();
  const pnp = await powershell(
    "[Console]::OutputEncoding = [System.Text.Encoding]::UTF8; " +
    "Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match '\\(COM\\d+\\)' } | ForEach-Object { $_.Name }"
  );
  for (const line of pnp.split(/\r?\n/)) {
    const match = line.match(/\((COM\d+)\)/i);
    if (match) {
      addPort(ports, match[1], line);
    }
  }
  const plain = await powershell(
    "[Console]::OutputEncoding = [System.Text.Encoding]::UTF8; " +
    "try { [System.IO.Ports.SerialPort]::GetPortNames() } catch { @() }"
  );
  for (const line of plain.split(/\r?\n/)) {
    const match = line.trim().match(/^(COM\d+)$/i);
    if (match) {
      addPort(ports, match[1], match[1]);
    }
  }
  return [...ports.entries()]
    .map(([id, label]) => ({ id, label }))
    .sort((a, b) => Number(a.id.slice(3)) - Number(b.id.slice(3)));
}
