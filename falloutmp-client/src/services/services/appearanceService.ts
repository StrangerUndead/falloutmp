import { ClientContext } from "../context";
import { AppearanceFo4, UpdateAppearanceFo4Message } from "../messages/fo4Messages";
import { Fo4MsgType } from "../messages/msgType";
import { LooksMenuMode } from "../../platform/falloutPlatform";

// F03: appearance (docs/falloutmp/features/F03-appearance-chargen.md).
// The server opens the editor (SetRaceMenuOpen) for new characters; when
// the LooksMenu closes, the player's face goes to the server, which
// validates, stores and relays it. Remote players' faces are applied to
// their puppets on arrival and on every stream-in.
export class AppearanceService {
  private ownRev = 0;
  private own: AppearanceFo4 | undefined;
  private editorRequested = false;
  private remotes = new Map<number, UpdateAppearanceFo4Message>();
  private applying = new Set<number>();

  constructor(private readonly ctx: ClientContext) {
    ctx.router.on(Fo4MsgType.UpdateAppearanceFo4, (m) => this.onAppearance(m));
  }

  get ownAppearance(): AppearanceFo4 | undefined {
    return this.own;
  }

  // SetRaceMenuOpen (29), or props.isRaceMenuOpen of the own CreateActor.
  onRaceMenu(open: boolean): void {
    const p = this.ctx.platform;
    if (open) {
      this.editorRequested = true;
      p.openLooksMenu(this.own ? LooksMenuMode.Remake : LooksMenuMode.Create);
    } else if (this.editorRequested) {
      this.editorRequested = false;
      p.closeLooksMenu();
    }
  }

  // Platform "looksMenuClosed".
  onLooksMenuClosed(): void {
    const p = this.ctx.platform;
    const data = p.getAppearanceFo4(p.getPlayer());
    if (!this.editorRequested) {
      // Opened by something else (console, a stray script): put the
      // server's face back.
      if (this.own && data && !sameAppearance(data, this.own)) {
        void p.applyAppearanceFo4(p.getPlayer(), this.own);
      }
      return;
    }
    this.editorRequested = false;
    if (!data) {
      p.log("warn", "LooksMenu closed but the player's appearance couldn't be read");
      return;
    }
    this.ctx.send(Fo4MsgType.UpdateAppearanceFo4, { idx: 0, rev: this.ownRev + 1, data }, true);
  }

  applyTo(serverId: number): void {
    const m = this.remotes.get(serverId);
    if (m) {
      this.apply(serverId, m.data);
    }
  }

  forgetActor(serverId: number): void {
    this.remotes.delete(serverId);
    this.applying.delete(serverId);
  }

  reset(): void {
    this.remotes.clear();
    this.applying.clear();
    this.editorRequested = false;
  }

  private onAppearance(m: UpdateAppearanceFo4Message): void {
    const p = this.ctx.platform;
    if (this.ctx.isLocalActor(m.idx) || m.idx === 0) {
      this.ownRev = m.rev;
      this.own = m.data;
      const current = p.getAppearanceFo4(p.getPlayer());
      if (!current || !sameAppearance(current, m.data)) {
        void p.applyAppearanceFo4(p.getPlayer(), m.data);
      }
      return;
    }
    const prev = this.remotes.get(m.idx);
    if (prev && prev.rev >= m.rev) {
      return;
    }
    this.remotes.set(m.idx, m);
    this.apply(m.idx, m.data);
  }

  private apply(serverId: number, data: AppearanceFo4): void {
    const p = this.ctx.platform;
    const local = p.refs.toLocal(serverId);
    if (!local) {
      return; // applied on stream-in
    }
    this.applying.add(serverId);
    void p.applyAppearanceFo4(local, data).then((ok) => {
      this.applying.delete(serverId);
      if (!ok) {
        p.log("warn", `Appearance of ${serverId.toString(16)} couldn't be applied`);
      }
      this.ctx.events.emit("appearanceApplied", { actor: serverId, ok });
    });
  }
}

// Field order differs between sources (the plugin's JSON sorts keys, the
// server keeps declaration order), so compare canonical forms.
export function sameAppearance(a: AppearanceFo4, b: AppearanceFo4): boolean {
  return canonical(a) === canonical(b);
}

function canonical(v: unknown): string {
  if (Array.isArray(v)) {
    return `[${v.map(canonical).join(",")}]`;
  }
  if (v && typeof v === "object") {
    const o = v as Record<string, unknown>;
    return `{${Object.keys(o).sort().map((k) => `${JSON.stringify(k)}:${canonical(o[k])}`).join(",")}}`;
  }
  return JSON.stringify(v);
}
