import assert from "node:assert/strict";
import { test } from "node:test";
import { MoveFlag } from "../src/services/messages/codes";
import { UpdateMovementFo4Message, Vec3 } from "../src/services/messages/fo4Messages";
import { Fo4MsgType } from "../src/services/messages/msgType";
import { lerpAngle } from "../src/services/services/movementService";
import { Harness, kPlayerServerId, makeClient } from "./fakePlatform";

function owner(h: Harness, pos: Vec3, flags = 0): void {
  h.platform.movement = { worldOrCell: 0x3c, pos, yaw: 90, aimPitch: 0, aimHeading: 0, speed: 0, direction: 0, velZ: 0, flags };
}

function sent(h: Harness): UpdateMovementFo4Message[] {
  return h.transport.ofType<UpdateMovementFo4Message>(Fo4MsgType.UpdateMovementFo4);
}

test("owner movement goes out every 100 ms and at once on flag changes", () => {
  const h = makeClient();
  owner(h, [0, 0, 0]);
  h.client.tick();
  assert.equal(sent(h).length, 1);
  assert.equal(h.transport.sent.at(-1)?.reliable, false);
  const first = sent(h)[0];
  assert.equal(first.idx, kPlayerServerId);
  assert.equal(first.seq, 1);
  assert.equal(first.healthPercentage, 100, "complete message");

  h.platform.now += 50;
  h.client.tick();
  assert.equal(sent(h).length, 1, "too soon");
  h.platform.now += 50;
  h.client.tick();
  assert.equal(sent(h).length, 2);

  // Sneaking: sent immediately
  h.platform.now += 10;
  owner(h, [1, 0, 0], MoveFlag.Sneaking);
  h.client.tick();
  assert.equal(sent(h).length, 3);
  assert.equal(sent(h)[2].flags, MoveFlag.Sneaking);

  // Loading screen: nothing, then an immediate sample on resume
  h.platform.movement = undefined;
  h.platform.now += 500;
  h.client.tick();
  assert.equal(sent(h).length, 3);
  owner(h, [5, 0, 0], MoveFlag.Sneaking);
  h.client.tick();
  assert.equal(sent(h).length, 4);
});

test("sequence numbers wrap at 16 bits", () => {
  const h = makeClient();
  owner(h, [0, 0, 0]);
  for (let i = 0; i < 65537; i++) {
    h.platform.now += 100;
    h.client.movement.tickOwner();
  }
  const s = sent(h);
  assert.equal(s[65534].seq, 65535);
  assert.equal(s[65535].seq, 0);
  assert.equal(s[65536].seq, 1);
});

function remote(h: Harness, idx: number, seq: number, ts: number, pos: Vec3, fields: Partial<UpdateMovementFo4Message> = {}): void {
  h.receive({
    t: Fo4MsgType.UpdateMovementFo4,
    idx,
    seq,
    ts,
    worldOrCell: 0x3c,
    pos,
    yaw: 0,
    aimPitch: 0,
    aimHeading: 0,
    speed: 0,
    direction: 0,
    velZ: 0,
    flags: 0,
    healthPercentage: 100,
    ...fields,
  } as never);
}

test("remote actors are interpolated 120 ms behind on the server timeline", () => {
  const h = makeClient();
  const bob = 0xff000002;
  h.platform.refs.map(bob, 0x5000);
  h.platform.now = 10000;
  remote(h, bob, 1, 500000, [0, 0, 0]); // server clock differs from ours
  h.client.movement.tickRemotes();
  assert.deepEqual(h.platform.callsOf("teleportActor")[0], [0x5000, [0, 0, 0], 0, 0x3c], "first sample snaps");

  h.platform.now = 10100;
  remote(h, bob, 2, 500100, [100, 0, 0]);
  // Render time = 500100 - 120 = 499980 -> before the second sample
  h.client.movement.tickRemotes();
  h.platform.now = 10170; // render 500050: halfway
  h.client.movement.tickRemotes();
  const mid = h.platform.callsOf("setActorTransform").at(-1);
  assert.equal(mid?.[0], 0x5000);
  assert.ok(Math.abs((mid?.[1] as number[])[0] - 50) < 1e-6);
  assert.equal(h.platform.callsOf("teleportActor").length, 1, "no snapping in between");
});

test("extrapolation is capped, then the actor holds", () => {
  const h = makeClient({ movement: { interpolationDelayMs: 0, maxExtrapolationMs: 200 } });
  const bob = 0xff000002;
  h.platform.refs.map(bob, 0x5000);
  h.platform.now = 1000;
  remote(h, bob, 1, 1000, [0, 0, 0]);
  h.client.movement.tickRemotes(); // first sample snaps
  h.platform.now = 1100;
  remote(h, bob, 2, 1100, [100, 0, 0]);
  h.platform.now = 1200; // 100 ms past the last sample
  h.client.movement.tickRemotes();
  assert.ok(Math.abs((h.platform.callsOf("setActorTransform").at(-1)?.[1] as number[])[0] - 200) < 1e-6);
  h.platform.now = 5000; // way past: capped at 200 ms ahead
  h.client.movement.tickRemotes();
  assert.ok(Math.abs((h.platform.callsOf("setActorTransform").at(-1)?.[1] as number[])[0] - 300) < 1e-6);
});

test("cell changes and large errors snap; flags and aim are applied", () => {
  const h = makeClient({ movement: { interpolationDelayMs: 0 } });
  const bob = 0xff000002;
  h.platform.refs.map(bob, 0x5000);
  h.platform.now = 1000;
  remote(h, bob, 1, 1000, [0, 0, 0], { flags: MoveFlag.InPowerArmor, aimPitch: 10, aimHeading: 5 });
  h.client.movement.tickRemotes();
  assert.deepEqual(h.platform.callsOf("setMovementFlags"), [[0x5000, MoveFlag.InPowerArmor]]);
  assert.deepEqual(h.platform.callsOf("setAimAngles").at(-1), [0x5000, 10, 5]);

  h.platform.now = 1100;
  remote(h, bob, 2, 1100, [5000, 0, 0], { flags: MoveFlag.InPowerArmor });
  h.client.movement.tickRemotes();
  assert.equal(h.platform.callsOf("teleportActor").length, 2, "large error snaps");
  assert.equal(h.platform.callsOf("setMovementFlags").length, 1, "unchanged flags not reapplied");

  h.platform.now = 1200;
  remote(h, bob, 3, 1200, [5010, 0, 0], { worldOrCell: 0x1234 });
  h.client.movement.tickRemotes();
  assert.deepEqual(h.platform.callsOf("teleportActor").at(-1)?.[3], 0x1234);

  // Reordered/duplicate samples are dropped: the actor stays put
  h.platform.clearCalls();
  remote(h, bob, 2, 1100, [0, 0, 0]);
  remote(h, bob, 3, 1200, [0, 0, 0]);
  h.client.movement.tickRemotes();
  const moved = h.platform.callsOf("setActorTransform").concat(h.platform.callsOf("teleportActor"));
  assert.equal(moved.length, 1);
  assert.deepEqual(moved[0][1], [5010, 0, 0]);

  h.client.onActorDestroyed(bob);
  assert.equal(h.client.movement.remoteCount(), 0);
});

test("the server timestamp unwraps across its 32-bit wrap", () => {
  const h = makeClient({ movement: { interpolationDelayMs: 0 } });
  const bob = 0xff000002;
  h.platform.refs.map(bob, 0x5000);
  h.platform.now = 1000;
  remote(h, bob, 1, 0xffffff9c, [0, 0, 0]); // 100 ms before the wrap
  h.platform.now = 1100;
  remote(h, bob, 2, 0, [100, 0, 0]);
  h.platform.now = 1050;
  h.client.movement.tickRemotes();
  const x = (h.platform.callsOf("teleportActor").at(-1)?.[1] as number[])[0];
  assert.ok(Math.abs(x - 50) < 1e-6, `interpolated across the wrap, got ${x}`);
});

test("yaw interpolates the short way around", () => {
  assert.equal(lerpAngle(350, 10, 0.5), 0);
  assert.equal(lerpAngle(10, 350, 0.5), 0);
  assert.equal(lerpAngle(90, 180, 0.5), 135);
});
