import assert from "node:assert/strict";
import test from "node:test";
import { Fo4MsgType } from "../src/services/messages/msgType";
import { createNativePlatform, NativeBridge } from "../src/runtime/nativePlatform";
import { UpstreamMsgType } from "../src/runtime/upstreamMessages";
import { Connection, RefMap, WorldSession } from "../src/runtime/worldSession";
import { FakePlatform, kPlayer, kPlayerProfile } from "./fakePlatform";

const kMe = 0xff000010;
const kOther = 0xff000011;

class FakeConnection implements Connection {
  connects: [string, number][] = [];
  sent: { message: { t: number }; reliable: boolean }[] = [];
  disconnects = 0;
  connect(host: string, port: number): void {
    this.connects.push([host, port]);
  }
  disconnect(): void {
    this.disconnects++;
  }
  send(message: object, reliable: boolean): void {
    this.sent.push({ message: message as { t: number }, reliable });
  }
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  ofType(t: number): any[] {
    return this.sent.filter((s) => s.message.t === t).map((s) => s.message);
  }
}

function createActor(idx: number, refrId: number, isMe: boolean, pos = [100, 200, 300]) {
  return JSON.stringify({
    t: UpstreamMsgType.CreateActor,
    idx,
    isMe,
    refrId,
    transform: { worldOrCell: 0x3c, pos, rot: [0, 0, 90] },
    appearance: { name: "Nora", isFemale: true },
  });
}

function setup() {
  const platform = new FakePlatform();
  platform.movement = {
    worldOrCell: 0x3c, pos: [1, 2, 3], yaw: 0, aimPitch: 0, aimHeading: 0, speed: 0, direction: 0, velZ: 0, flags: 0,
  };
  const conn = new FakeConnection();
  const session = new WorldSession(platform, platform.refs, conn, { retryDelayMs: 1000 });
  return { platform, conn, session };
}

function joined() {
  const s = setup();
  s.session.start();
  s.session.onConnected();
  s.session.onMessage(createActor(3, kMe, true));
  return s;
}

test("connects to the configured server and logs in with the profile id", () => {
  const { conn, session } = setup();
  session.start();
  assert.deepEqual(conn.connects, [["127.0.0.1", 7777]]);
  assert.equal(session.state, "connecting");
  session.onConnected();
  const [login] = conn.ofType(UpstreamMsgType.CustomPacket);
  assert.deepEqual(JSON.parse(login.contentJsonDump), {
    customPacketType: "loginWithSkympIo",
    gameData: { profileId: kPlayerProfile },
  });
  assert.equal(conn.sent[0].reliable, true);
});

test("no server configured: nothing happens", () => {
  const { platform, conn, session } = setup();
  platform.clientConfig = undefined;
  session.start();
  assert.equal(conn.connects.length, 0);
  assert.ok(platform.logs.some((l) => l.includes("No server configured")));
});

test("the own actor moves the player and movement goes out under its form id", () => {
  const { platform, conn, session } = joined();
  assert.equal(session.state, "inWorld");
  assert.equal(session.client!.ctx.session.localActorId, kMe);
  const tp = platform.calls.find((c) => c.fn === "teleportActor");
  assert.deepEqual(tp?.args, [kPlayer, [100, 200, 300], 90, 0x3c]);
  platform.fire("tick", {});
  const [mov] = conn.ofType(Fo4MsgType.UpdateMovementFo4);
  assert.equal(mov.idx, kMe); // the form id, never the slot idx 3
});

test("other players get a puppet that follows the relayed movement", () => {
  const { platform, session } = joined();
  session.onMessage(createActor(4, kOther, false, [500, 500, 0]));
  assert.equal(platform.puppets.size, 1);
  const [puppet, spawn] = Array.from(platform.puppets.entries())[0];
  assert.equal(spawn.name, "Nora");
  assert.equal(spawn.baseId, 0);
  assert.equal(platform.refs.toLocal(kOther), puppet);

  for (let i = 0; i < 5; i++) {
    session.onMessage(JSON.stringify({
      t: Fo4MsgType.UpdateMovementFo4, idx: kOther, seq: i + 1, ts: 1000 + i * 100, worldOrCell: 0x3c,
      pos: [500 + i * 10, 500, 0], yaw: 0, aimPitch: 0, aimHeading: 0, speed: 0, direction: 0, velZ: 0, flags: 0,
      healthPercentage: 100,
    }));
  }
  platform.now += 600;
  platform.fire("tick", {});
  const moved = platform.calls.filter((c) => (c.fn === "setActorTransform" || c.fn === "teleportActor") && c.args[0] === puppet);
  assert.ok(moved.length > 0);

  session.onMessage(JSON.stringify({ t: UpstreamMsgType.DestroyActor, idx: 4 }));
  assert.equal(platform.puppets.size, 0);
  assert.equal(platform.refs.toLocal(kOther), 0);
});

test("load-order references map to themselves without a puppet", () => {
  const { platform, session } = joined();
  session.onMessage(createActor(9, 0x100001234, false));
  assert.equal(platform.puppets.size, 0);
  assert.equal(platform.refs.toLocal(0x1234), 0x1234);
});

test("a teleport from the server moves the named actor", () => {
  const { platform, session } = joined();
  session.onMessage(JSON.stringify({ t: UpstreamMsgType.Teleport, idx: 3, pos: [7, 8, 9], rot: [0, 0, 45], worldOrCell: 0x3c }));
  const last = platform.calls.filter((c) => c.fn === "teleportActor").pop();
  assert.deepEqual(last?.args, [kPlayer, [7, 8, 9], 45, 0x3c]);
});

test("a dropped connection removes puppets and reconnects with a fresh client", () => {
  const { platform, conn, session } = joined();
  session.onMessage(createActor(4, kOther, false));
  const oldClient = session.client!;
  session.onDisconnected();
  assert.equal(platform.puppets.size, 0);
  assert.equal(session.state, "waitingToRetry");
  assert.ok(platform.notifications.includes("Lost connection to the server"));

  session.tick();
  assert.equal(conn.connects.length, 1); // not yet
  platform.now += 1000;
  session.tick();
  assert.equal(conn.connects.length, 2);
  session.onConnected();
  assert.notEqual(session.client, oldClient);

  // The old client no longer reacts to frames
  const before = conn.ofType(Fo4MsgType.UpdateMovementFo4).length;
  oldClient.ctx.session.localActorId = 0xdead;
  platform.fire("tick", {});
  assert.ok(conn.ofType(Fo4MsgType.UpdateMovementFo4).every((m) => m.idx !== 0xdead));
  assert.ok(conn.ofType(Fo4MsgType.UpdateMovementFo4).length >= before);
});

test("denied connections retry slower and report the reason", () => {
  const { platform, conn, session } = setup();
  session.start();
  session.onConnectionDenied("Invalid password");
  assert.ok(platform.notifications.some((n) => n.includes("Invalid password")));
  platform.now += 5000;
  session.tick();
  assert.equal(conn.connects.length, 1);
  platform.now += 30000;
  session.tick();
  assert.equal(conn.connects.length, 2);
});

test("login failures are shown to the player", () => {
  const { platform, session } = setup();
  session.start();
  session.onConnected();
  session.onMessage(JSON.stringify({ t: 1, contentJsonDump: JSON.stringify({ customPacketType: "loginFailedBanned" }) }));
  assert.ok(platform.notifications.some((n) => n.includes("loginFailedBanned")));
});

test("the native platform forwards calls and tolerates missing natives", () => {
  const calls: [string, unknown[]][] = [];
  const logs: string[] = [];
  const bridge: NativeBridge = {
    native: (name, argsJson) => {
      calls.push([name, JSON.parse(argsJson)]);
      if (name === "spawnPuppet") return JSON.stringify({ r: 0xff800001 });
      if (name === "getMovementFo4") return JSON.stringify({ r: null });
      return JSON.stringify({ missing: true });
    },
    send: () => {},
    connect: () => {},
    disconnect: () => {},
    log: (_l, t) => logs.push(t),
  };
  const n = createNativePlatform(bridge, new RefMap());
  const p = n.platform;
  assert.equal(p.spawnPuppet({ pos: [0, 0, 0], yaw: 0, worldOrCell: 0x3c, baseId: 0, name: "", isFemale: false }), 0xff800001);
  assert.equal(p.getMovementFo4(0x14), undefined);
  assert.deepEqual(p.getInventoryEx(0x14), []);
  assert.deepEqual(p.getInventoryEx(0x14), []);
  assert.equal(calls.filter(([name]) => name === "getInventoryEx").length, 1); // asked once
  assert.equal(logs.filter((l) => l.includes("getInventoryEx")).length, 1);
  assert.deepEqual(n.missingNatives(), ["getInventoryEx"]);
  assert.deepEqual(calls[0], ["spawnPuppet", [{ pos: [0, 0, 0], yaw: 0, worldOrCell: 0x3c, baseId: 0, name: "", isFemale: false }]]);
  n.setNow(1234);
  assert.equal(p.nowMs(), 1234);
  assert.equal(p.getPlayer(), 0x14);
  let ticks = 0;
  p.on("tick", () => ticks++);
  n.emit("tick", {});
  assert.equal(ticks, 1);
});

test("puppets removed for a save come back where the actor last was", () => {
  const { platform, session } = joined();
  session.onMessage(createActor(4, kOther, false, [500, 500, 0]));
  session.onMessage(JSON.stringify({
    t: Fo4MsgType.UpdateMovementFo4, idx: kOther, seq: 1, ts: 1000, worldOrCell: 0x3c,
    pos: [900, 900, 10], yaw: 45, aimPitch: 0, aimHeading: 0, speed: 0, direction: 0, velZ: 0, flags: 0,
    healthPercentage: 100,
  }));
  const [oldRef] = Array.from(platform.puppets.keys());
  platform.puppets.clear(); // the plugin deleted them before saving
  session.recreatePuppets();
  const [[newRef, spawn]] = Array.from(platform.puppets.entries());
  assert.notEqual(newRef, oldRef);
  assert.deepEqual(spawn.pos, [900, 900, 10]);
  assert.equal(platform.refs.toLocal(kOther), newRef);
  platform.fire("tick", {});
  const tp = platform.calls.filter((c) => c.fn === "teleportActor" && c.args[0] === newRef);
  assert.equal(tp.length, 1); // snapped into place, not interpolated from nowhere
});
