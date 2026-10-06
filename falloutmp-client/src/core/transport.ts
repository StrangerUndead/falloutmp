// Outgoing side of the connection. In game this wraps
// mpClientPlugin.send(JSON.stringify(msg), reliable); the native plugin
// turns JSON into the binary form with the shared MessageSerializer.
export interface Fo4Transport {
  send(message: { t: number }, reliable: boolean): void;
}

export class RecordingTransport implements Fo4Transport {
  readonly sent: { message: { t: number }; reliable: boolean }[] = [];
  send(message: { t: number }, reliable: boolean): void {
    // Round-trip through JSON like the real plugin, so tests catch
    // values that don't survive serialization.
    this.sent.push({ message: JSON.parse(JSON.stringify(message)), reliable });
  }
  last<T = { t: number }>(): T {
    if (!this.sent.length) {
      throw new Error("nothing sent");
    }
    return this.sent[this.sent.length - 1].message as unknown as T;
  }
  ofType<T = { t: number }>(t: number): T[] {
    return this.sent
      .filter((s) => s.message.t === t)
      .map((s) => s.message as unknown as T);
  }
  clear(): void {
    this.sent.length = 0;
  }
}
