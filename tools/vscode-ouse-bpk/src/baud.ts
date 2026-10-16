export const DEFAULT_BAUD = 921600;

export const BAUD_PRESETS = [
  9600,
  19200,
  38400,
  57600,
  115200,
  230400,
  460800,
  921600,
  1000000,
  1500000,
  2000000,
  3000000,
] as const;

export function parseBaud(value: unknown): number | undefined {
  const n = typeof value === 'number' ? value : Number(String(value ?? '').trim());
  if (!Number.isInteger(n) || n < 1200 || n > 12_000_000) return undefined;
  return n;
}
