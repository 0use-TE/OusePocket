import { type ChildProcessWithoutNullStreams, spawn } from 'child_process';

interface RpcError {
  code?: number;
  message?: string;
}

interface RpcResponse {
  id?: number;
  result?: ToolResult;
  error?: RpcError;
}

export interface ToolContent {
  type?: string;
  text?: string;
  data?: string;
  mimeType?: string;
}

export interface ToolResult {
  content?: ToolContent[];
  isError?: boolean;
}

export interface DeviceState {
  width: number;
  height: number;
  active?: string;
}

interface Pending {
  resolve: (result: ToolResult) => void;
  reject: (error: Error) => void;
}

export class DeviceMcp {
  private readonly pending = new Map<number, Pending>();
  private buffer = '';
  private nextId = 1;
  private chain: Promise<void> = Promise.resolve();
  private closed = false;

  private constructor(private readonly child: ChildProcessWithoutNullStreams) {
    child.stdout.setEncoding('utf8');
    child.stdout.on('data', (chunk: string) => this.onStdout(chunk));
    child.on('exit', (code) => {
      this.failAll(new Error(code ? `设备连接已结束 (${code})` : '设备连接已结束'));
    });
  }

  static async start(python: string, script: string, port: string, baud: number, onLog: (line: string) => void): Promise<DeviceMcp> {
    const child = spawn(python, ['-u', script, '--port', port, '--baud', String(baud)], {
      windowsHide: true,
      env: { ...process.env, PYTHONIOENCODING: 'utf-8', PYTHONUTF8: '1' },
    });
    const client = new DeviceMcp(child);
    child.stderr.setEncoding('utf8');
    child.stderr.on('data', (chunk: string) => {
      for (const line of chunk.split(/\r?\n/)) {
        if (line.trim()) onLog(line);
      }
    });
    try {
      await client.request('initialize', {});
    } catch (error) {
      client.stop();
      throw error;
    }
    return client;
  }

  call(name: string, args: Record<string, unknown> = {}): Promise<ToolResult> {
    const run = this.chain.then(() => this.request('tools/call', { name, arguments: args }));
    this.chain = run.then(() => undefined, () => undefined);
    return run;
  }

  stop(): void {
    this.closed = true;
    this.failAll(new Error('设备画面已关闭'));
    if (!this.child.killed) this.child.kill();
  }

  private request(method: string, params: Record<string, unknown>): Promise<ToolResult> {
    if (this.closed) return Promise.reject(new Error('设备画面已关闭'));
    const id = this.nextId++;
    const body = JSON.stringify({ jsonrpc: '2.0', id, method, params });
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.child.stdin.write(body + '\n', (error) => {
        if (error) {
          this.pending.delete(id);
          reject(error);
        }
      });
    });
  }

  private onStdout(chunk: string): void {
    this.buffer += chunk;
    let newline = this.buffer.indexOf('\n');
    while (newline >= 0) {
      const line = this.buffer.slice(0, newline).trim();
      this.buffer = this.buffer.slice(newline + 1);
      if (line) this.onLine(line);
      newline = this.buffer.indexOf('\n');
    }
  }

  private onLine(line: string): void {
    let message: RpcResponse;
    try {
      message = JSON.parse(line) as RpcResponse;
    } catch {
      return;
    }
    if (typeof message.id !== 'number') return;
    const pending = this.pending.get(message.id);
    if (!pending) return;
    this.pending.delete(message.id);
    if (message.error) {
      pending.reject(new Error(message.error.message || '设备命令失败'));
      return;
    }
    const result = message.result ?? {};
    if (result.isError) {
      const text = result.content?.find((item) => item.type === 'text')?.text;
      pending.reject(new Error(text || '设备命令失败'));
      return;
    }
    pending.resolve(result);
  }

  private failAll(error: Error): void {
    this.closed = true;
    for (const pending of this.pending.values()) pending.reject(error);
    this.pending.clear();
  }
}

export function toolText(result: ToolResult): string {
  return result.content?.find((item) => item.type === 'text')?.text ?? '';
}

export function toolImage(result: ToolResult): string {
  const data = result.content?.find((item) => item.type === 'image')?.data;
  if (!data) throw new Error('没有画面');
  return data;
}

export function parseState(result: ToolResult): DeviceState {
  const state = JSON.parse(toolText(result)) as DeviceState;
  if (!state.width || !state.height) throw new Error('设备没有返回屏幕尺寸');
  return state;
}
