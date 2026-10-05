// Entry point of the in-game bundle (build/falloutmp-client.js), run by the
// fallout4-platform plugin's QuickJS host. The host calls
// globalThis.__fmpOnEvent(kind, payloadJson):
//   "gameReady"                       a save is loaded: connect
//   "tick" {nowMs}                    every frame
//   "connected" / "disconnected"      transport state
//   "connectionFailed" / "connectionDenied" {error}
//   "message"                         a server message as JSON
//   "platformEvent" {name, data}      PlatformEvents (captured game actions)
import { PlatformEvents } from "../platform/falloutPlatform";
import { createNativePlatform, NativeBridge } from "./nativePlatform";
import { RefMap, WorldSession } from "./worldSession";

interface HostGlobals {
  __fmp: NativeBridge;
  __fmpOnEvent?: (kind: string, payloadJson: string) => void;
  falloutmp?: unknown;
}

const g = globalThis as unknown as HostGlobals;
const bridge = g.__fmp;
const refs = new RefMap();
const native = createNativePlatform(bridge, refs);
const session = new WorldSession(native.platform, refs, {
  connect: (host, port) => bridge.connect(host, port),
  disconnect: () => bridge.disconnect(),
  send: (message, reliable) => bridge.send(JSON.stringify(message), reliable),
});
let started = false;

g.__fmpOnEvent = (kind, payloadJson) => {
  switch (kind) {
    case "tick": {
      const { nowMs } = JSON.parse(payloadJson) as { nowMs: number };
      native.setNow(nowMs);
      session.tick();
      if (session.inWorld) {
        native.emit("tick", {});
      }
      return;
    }
    case "message":
      return session.onMessage(payloadJson);
    case "gameReady":
      if (!started) {
        started = true;
        session.start();
      }
      return;
    case "connected":
      return session.onConnected();
    case "disconnected":
      return session.onDisconnected();
    case "connectionFailed":
      return session.onConnectionFailed(errorOf(payloadJson));
    case "connectionDenied":
      return session.onConnectionDenied(errorOf(payloadJson));
    case "platformEvent": {
      const { name, data } = JSON.parse(payloadJson) as { name: keyof PlatformEvents; data: never };
      native.emit(name, data);
      return;
    }
  }
};

// For the in-game console and debugging
g.falloutmp = { session, refs, missingNatives: () => native.missingNatives() };
bridge.log("info", "FalloutMP client script loaded");

function errorOf(payloadJson: string): string {
  try {
    return (JSON.parse(payloadJson) as { error?: string }).error || "unknown error";
  } catch {
    return "unknown error";
  }
}
