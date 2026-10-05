import assert from "node:assert/strict";
import { test } from "node:test";
import { longToNormal, SkympClientBridge } from "../src/integration/skympClientBridge";
import { Av } from "../src/services/messages/codes";
import { HitReportMessage, UpdateMovementFo4Message, WeaponFireMessage } from "../src/services/messages/fo4Messages";
import { Fo4MsgType } from "../src/services/messages/msgType";
import { kPlayer, kPlayerServerId, makeClient } from "./fakePlatform";
import { lastSent } from "./helpers";

const kRaider = 0x0001a2b3;
const kRaiderLocal = 0x1a2b3;

function hostRaider() {
  const h = makeClient();
  h.platform.refs.map(kRaider, kRaiderLocal);
  new SkympClientBridge(h.client).onHostStart({ target: kRaider + 0x100000000 });
  return h;
}

test("HostStart long ids convert like the server's LongToNormal", () => {
  assert.equal(longToNormal(0x10001a2b3), 0x1a2b3);
  assert.equal(longToNormal(0xff000010), 0xff000010);
  const h = hostRaider();
  assert.ok(h.client.ctx.isHosted(kRaider));
  new SkympClientBridge(h.client).onHostStop({ target: kRaider + 0x100000000 });
  assert.ok(!h.client.ctx.isHosted(kRaider));
});

test("a host sends movement for its NPCs with their own sequence", () => {
  const h = hostRaider();
  const mv = (x: number) => ({ worldOrCell: 0x3c, pos: [x, 0, 0] as [number, number, number], yaw: 0, aimPitch: 0, aimHeading: 0, speed: 0, direction: 0, velZ: 0, flags: 0 });
  h.platform.movement = mv(0);
  h.platform.hostedMovement.set(kRaiderLocal, mv(500));
  h.client.tick();
  const sent = h.transport.ofType<UpdateMovementFo4Message>(Fo4MsgType.UpdateMovementFo4);
  assert.deepEqual(sent.map((m) => [m.idx, m.seq, m.pos[0]]), [
    [kPlayerServerId, 1, 0],
    [kRaider, 1, 500],
  ]);
  // Remote samples for our own NPC are ignored
  h.receive({ ...sent[1], ts: 1 } as never);
  h.client.movement.tickRemotes();
  assert.equal(h.platform.callsOf("teleportActor").length, 0);

  // Hosting ends: no more samples for it
  h.client.setHosted(kRaider, false);
  h.platform.now += 200;
  h.client.tick();
  const after = h.transport.ofType<UpdateMovementFo4Message>(Fo4MsgType.UpdateMovementFo4);
  assert.equal(after.filter((m) => m.idx === kRaider).length, 1);
});

test("hosted NPC shots and hits carry the NPC as shooter", () => {
  const h = hostRaider();
  h.platform.fire("weaponFired", { weaponBaseId: 0x4822c, origin: [0, 0, 0], direction: [1, 0, 0], shooter: kRaiderLocal });
  const fire = lastSent<WeaponFireMessage>(h, Fo4MsgType.WeaponFire);
  assert.equal(fire.shooterIdx, kRaider);
  h.receive({ ...fire, seq: 7 } as never); // the server's echo to the host
  h.platform.fire("projectileHit", { localShotId: fire.clientShotId, projectileIndex: 0, target: kPlayer, limb: 0 });
  const hit = lastSent<HitReportMessage>(h, Fo4MsgType.HitReport);
  assert.deepEqual([hit.shooterIdx, hit.shotSeq, hit.targetIdx], [kRaider, 7, kPlayerServerId]);

  // An NPC this client doesn't host can't fire through it
  h.client.setHosted(kRaider, false);
  const before = h.transport.ofType(Fo4MsgType.WeaponFire).length;
  h.platform.fire("weaponFired", { weaponBaseId: 0x4822c, origin: [0, 0, 0], direction: [1, 0, 0], shooter: kRaiderLocal });
  assert.equal(h.transport.ofType(Fo4MsgType.WeaponFire).length, before);
});

test("hosted NPC values: reports go up, the server's truth is applied absolutely", () => {
  const h = hostRaider();
  h.platform.fire("hostedValuesChanged", { actor: kRaiderLocal, values: [{ avId: Av.Health, current: 40, max: 100 }] });
  const report = lastSent<{ idx: number; values: { current: number }[] }>(h, Fo4MsgType.ChangeValuesAv);
  assert.equal(report.idx, kRaider);
  assert.equal(report.values[0].current, 40);
  h.receive({ t: Fo4MsgType.ChangeValuesAv, idx: kRaider, values: [{ avId: Av.Health, current: 60, max: 100 }] } as never);
  assert.deepEqual(h.platform.callsOf("setActorValueCurrent").at(-1), [kRaiderLocal, Av.Health, 60]);
  assert.equal(h.platform.callsOf("setHealthFraction").length, 0, "not treated as a puppet");
  h.client.onDisconnect();
  assert.ok(!h.client.ctx.isHosted(kRaider));
});
