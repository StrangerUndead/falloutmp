import { ItemCount } from "../services/messages/fo4Messages";

export interface RequestOutcome<Reply = undefined> {
  ok: boolean;
  error: string; // server error code, "Timeout" or "Disconnected"
  nonce: number;
  requestType: number;
  refId: number;
  items: ItemCount[];
  reply?: Reply; // the typed reply for requests answered by their own type
}

interface Pending {
  requestType: number;
  deadlineMs: number;
  resolve: (o: RequestOutcome<unknown>) => void;
}

export interface RequestTrackerOptions {
  timeoutMs?: number;
  // Nonces are unique per connection on the server (workshop requests
  // reject duplicates), so a reconnect must not restart from 1.
  firstNonce?: number;
}

// Pairs nonce'd requests with their replies. Timeouts are driven by tick(),
// not timers, because the game runtime only guarantees a per-frame tick.
export class RequestTracker {
  private pending = new Map<number, Pending>();
  private next: number;
  readonly timeoutMs: number;

  constructor(private readonly nowMs: () => number, opts: RequestTrackerOptions = {}) {
    this.timeoutMs = opts.timeoutMs ?? 10000;
    this.next = (opts.firstNonce ?? 1) >>> 0 || 1;
  }

  nextNonce(): number {
    let n = this.next;
    for (;;) {
      this.next = (this.next + 1) >>> 0 || 1;
      if (n !== 0 && !this.pending.has(n)) {
        return n;
      }
      n = this.next;
    }
  }

  track<Reply = undefined>(nonce: number, requestType: number): Promise<RequestOutcome<Reply>> {
    const existing = this.pending.get(nonce);
    if (existing) {
      existing.resolve(this.failure(nonce, existing.requestType, "Superseded"));
    }
    return new Promise<RequestOutcome<Reply>>((resolve) => {
      this.pending.set(nonce, {
        requestType,
        deadlineMs: this.nowMs() + this.timeoutMs,
        resolve: resolve as (o: RequestOutcome<unknown>) => void,
      });
    });
  }

  isPending(nonce: number): boolean {
    return this.pending.has(nonce);
  }

  pendingCount(): number {
    return this.pending.size;
  }

  // Returns false for unknown nonces (late replies after a timeout).
  complete<Reply>(nonce: number, outcome: Omit<RequestOutcome<Reply>, "nonce" | "requestType"> & { requestType?: number }): boolean {
    const p = this.pending.get(nonce);
    if (!p) {
      return false;
    }
    this.pending.delete(nonce);
    p.resolve({ ...outcome, nonce, requestType: outcome.requestType ?? p.requestType });
    return true;
  }

  tick(): void {
    const now = this.nowMs();
    for (const [nonce, p] of Array.from(this.pending.entries())) {
      if (now >= p.deadlineMs) {
        this.pending.delete(nonce);
        p.resolve(this.failure(nonce, p.requestType, "Timeout"));
      }
    }
  }

  failAll(error: string): void {
    const all = Array.from(this.pending.entries());
    this.pending.clear();
    for (const [nonce, p] of all) {
      p.resolve(this.failure(nonce, p.requestType, error));
    }
  }

  private failure(nonce: number, requestType: number, error: string): RequestOutcome<never> {
    return { ok: false, error, nonce, requestType, refId: 0, items: [] };
  }
}
