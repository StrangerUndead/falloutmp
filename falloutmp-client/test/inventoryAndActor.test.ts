import assert from "node:assert/strict";
import { test } from "node:test";
import { Av } from "../src/services/messages/codes";
import { Fo4MsgType } from "../src/services/messages/msgType";
import { key, kPlayer, kPlayerServerId, makeClient } from "./fakePlatform";
import { lastSent, reply } from "./helpers";

const kSteel = 0x731a4;
const k10mm = 0x4822c;

test("inventory snapshots are diffed into the game silently", () => {
  const h = makeClient();
  h.platform.inventories.set(kPlayer, [{ item: key(kSteel), count: 5 }]);
  const want = [
    { item: key(kSteel), count: 2 },
    { item: key(k10mm, { mods: [0x11], ammoLoaded: 12 }), count: 1 },
  ];
  h.receive({ t: Fo4MsgType.SetInventoryFo4, refId: 0, version: 3, entries: want } as never);
  assert.ok(h.platform.inventoryMatches(kPlayer, want));
  assert.ok(h.platform.callsOf("addItemEx").every((a) => a[3] === true), "silent");
  assert.equal(h.client.inventory.count(kSteel), 2);
  assert.equal(h.client.inventory.find(key(k10mm, { mods: [0x11] }))?.count, 1);

  // An older snapshot arriving late is ignored
  h.receive({ t: Fo4MsgType.SetInventoryFo4, refId: 0, version: 2, entries: [] } as never);
  assert.equal(h.client.inventory.count(kSteel), 2);
});

test("the periodic reconcile repairs a diverged game inventory", () => {
  const h = makeClient({ inventoryReconcileIntervalMs: 5000 });
  const want = [{ item: key(kSteel), count: 2 }];
  h.receive({ t: Fo4MsgType.SetInventoryFo4, refId: 0, version: 1, entries: want } as never);
  h.platform.inventories.set(kPlayer, [{ item: key(kSteel), count: 9 }]); // a script added some
  h.platform.now += 4000;
  h.client.tick();
  assert.equal(h.client.inventory.divergenceCount, 0, "not yet");
  h.platform.now += 1000;
  h.client.tick();
  assert.equal(h.client.inventory.divergenceCount, 1);
  assert.ok(h.platform.inventoryMatches(kPlayer, want));
});

test("the reconcile never removes the Pip-Boy (a local-only item)", () => {
  const h = makeClient({ inventoryReconcileIntervalMs: 5000 });
  const pipboy = { item: key(0x00021b3b), count: 1 };
  const want = [{ item: key(kSteel), count: 2 }];
  h.receive({ t: Fo4MsgType.SetInventoryFo4, refId: 0, version: 1, entries: want } as never);
  h.platform.inventories.set(kPlayer, [{ item: key(kSteel), count: 2 }, pipboy]);
  h.platform.now += 5000;
  h.client.tick();
  assert.equal(h.client.inventory.divergenceCount, 0);
  assert.ok(h.platform.inventoryMatches(kPlayer, [...want, pipboy]));
});

test("container contents are cached and applied when the container streams in", () => {
  const h = makeClient();
  const box = 0x0001f00d;
  const entries = [{ item: key(kSteel), count: 3 }];
  h.receive({ t: Fo4MsgType.SetInventoryFo4, refId: box, version: 1, entries } as never);
  assert.deepEqual(h.client.inventory.containerContents(box), entries);
  assert.equal(h.platform.callsOf("addItemEx").length, 0, "not streamed in");
  h.platform.refs.map(box, 0x5000);
  h.client.inventory.applyContainerToRef(box);
  assert.ok(h.platform.inventoryMatches(0x5000, entries));
});

test("take/put/drop/use are server requests with local pre-checks", async () => {
  const h = makeClient();
  h.receive({ t: Fo4MsgType.SetInventoryFo4, refId: 0, version: 1, entries: [{ item: key(kSteel), count: 1 }] } as never);

  const take = h.client.inventory.take(0x1234, key(k10mm), 1);
  const m = lastSent<{ refId: number; count: number; pos: number[] }>(h, Fo4MsgType.TakeItemFo4);
  assert.equal(m.refId, 0x1234);
  assert.deepEqual(m.pos, [0, 0, 0], "complete message");
  reply(h, Fo4MsgType.TakeItemFo4, true);
  assert.equal((await take).ok, true);

  const put = await h.client.inventory.put(0x1234, key(kSteel), 5);
  assert.equal(put.error, "ItemNotFound", "can't put more than you have");
  assert.equal(h.transport.ofType(Fo4MsgType.PutItemFo4).length, 0);
  assert.equal(h.platform.notifications.at(-1), "You don't have that item.");

  const drop = h.client.inventory.drop(key(kSteel), 1);
  reply(h, Fo4MsgType.DropItemFo4, false, "Busy");
  assert.equal((await drop).error, "Busy");

  const use = await h.client.inventory.useItem(0xdead);
  assert.equal(use.error, "NotInInventory");
});

test("own actor values set max before current; neighbours get health fractions", () => {
  const h = makeClient();
  h.receive({
    t: Fo4MsgType.ChangeValuesAv,
    idx: kPlayerServerId,
    values: [{ avId: Av.Health, current: 90, max: 125 }],
  } as never);
  const calls = h.platform.calls.map((c) => c.fn);
  assert.deepEqual(calls, ["setActorValueMax", "setActorValueCurrent"]);
  assert.equal(h.client.actorValues.get(Av.Health), 90);
  assert.equal(h.client.actorValues.healthFraction(kPlayerServerId), 90 / 125);

  // Unchanged values are not re-applied
  h.platform.clearCalls();
  h.receive({ t: Fo4MsgType.ChangeValuesAv, idx: kPlayerServerId, values: [{ avId: Av.Health, current: 90, max: 125 }] } as never);
  assert.equal(h.platform.calls.length, 0);

  const remote = 0xff000099;
  h.receive({
    t: Fo4MsgType.ChangeValuesAv,
    idx: remote,
    values: [
      { avId: Av.Health, current: 0.5, max: 1 },
      { avId: Av.LeftMobilityCondition, current: 0, max: 100 },
    ],
  } as never);
  assert.equal(h.platform.callsOf("setHealthFraction").length, 0, "not streamed in yet");
  h.platform.refs.map(remote, 0x7001);
  h.client.onActorStreamedIn(remote);
  assert.deepEqual(h.platform.callsOf("setHealthFraction"), [[0x7001, 0.5]]);
  assert.deepEqual(h.platform.callsOf("setActorValueCurrent"), [[0x7001, Av.LeftMobilityCondition, 0]]);
});

test("equipment follows the server state for the owner and puppets", () => {
  const h = makeClient();
  const gun = key(k10mm, { mods: [0x11], ammoLoaded: 12 });
  const hat = key(0x500);
  h.platform.equipped.set(kPlayer, [key(0x999)]);
  h.client.equipment.equip(gun);
  assert.equal(lastSent<{ op: number }>(h, Fo4MsgType.UpdateEquipmentFo4).op, 0);
  assert.equal(h.platform.callsOf("equipItemEx").length, 0, "nothing until the server agrees");

  h.receive({ t: Fo4MsgType.UpdateEquipmentFo4, actorIdx: kPlayerServerId, op: 2, item: key(0), weapon: gun, armor: [hat] } as never);
  assert.deepEqual(h.platform.callsOf("unequipItemEx").map((a) => (a[1] as { baseId: number }).baseId), [0x999]);
  assert.deepEqual(h.platform.callsOf("equipItemEx").map((a) => [(a[1] as { baseId: number }).baseId, a[2]]), [
    [0x500, false],
    [k10mm, false],
  ]);
  assert.deepEqual(h.platform.callsOf("setAmmoLoaded"), [[kPlayer, 12]]);
  assert.equal(h.client.equipment.localWeapon()?.baseId, k10mm);

  // Puppets can't drop their gear
  const remote = 0xff000050;
  h.platform.refs.map(remote, 0x6000);
  h.receive({ t: Fo4MsgType.UpdateEquipmentFo4, actorIdx: remote, op: 2, item: key(0), armor: [hat] } as never);
  assert.deepEqual(h.platform.callsOf("equipItemEx").at(-1)?.slice(0, 1), [0x6000]);
  assert.equal(h.platform.callsOf("equipItemEx").at(-1)?.[2], true);
});

test("progression mirrors perks, announces level ups and prompts creation", async () => {
  const h = makeClient();
  const levelUps: number[] = [];
  h.client.events.on("levelUp", (e) => levelUps.push(e.level));
  let prompted = 0;
  h.client.events.on("characterCreationRequired", () => prompted++);
  const update = (level: number, perks: number[], created = true, perkPoints = 1) =>
    h.receive({
      t: Fo4MsgType.ProgressionUpdate,
      level,
      xp: 300,
      xpForNextLevel: 450,
      perkPoints,
      special: [1, 1, 1, 1, 1, 1, 1],
      perks,
      created,
    } as never);
  update(1, [], false, 0);
  assert.equal(prompted, 1);
  assert.equal(h.platform.callsOf("openSpecialMenu").length, 1);

  const bad = await h.client.progression.createCharacter([10, 10, 1, 1, 1, 1, 1]);
  assert.equal(bad.error, "BadSpecialAllocation");
  const create = h.client.progression.createCharacter([4, 4, 4, 4, 4, 4, 4]);
  assert.equal(lastSent<{ op: number; special: number[] }>(h, Fo4MsgType.ProgressionRequest).op, 0);
  reply(h, Fo4MsgType.ProgressionRequest, true);
  assert.ok((await create).ok);

  update(2, [0xa1, 0xa2]);
  update(3, [0xa2, 0xa3]);
  assert.deepEqual(h.platform.callsOf("addPerk").map((a) => a[0]), [0xa1, 0xa2, 0xa3]);
  assert.deepEqual(h.platform.callsOf("removePerk").map((a) => a[0]), [0xa1]);
  assert.deepEqual(levelUps, [2, 3]);
  assert.equal(prompted, 1, "only once");
  assert.ok(h.client.progression.hasPerk(0xa3));

  update(3, [0xa2, 0xa3], true, 0);
  const noPoints = await h.client.progression.buyPerk("GunNut");
  assert.equal(noPoints.error, "NoPerkPoints");
});

test("effects: owner list with countdown, addiction notice, puppet visuals", () => {
  const h = makeClient();
  const update = (idx: number, effects: unknown[], addictions: number[] = []) =>
    h.receive({ t: Fo4MsgType.EffectsUpdate, idx, full: true, effects, addictions } as never);
  update(kPlayerServerId, [{ effectId: 0xe1, sourceItem: 0x23736, kind: 0, avId: Av.Health, magnitude: 10, remainingMs: 4000 }]);
  h.platform.now += 1500;
  assert.equal(h.client.effects.active()[0].remainingMs, 2500);
  assert.ok(!("receivedAtMs" in h.client.effects.active()[0]));

  update(kPlayerServerId, [], [0x1234]);
  assert.ok(h.client.effects.isAddicted(0x1234));
  assert.equal(h.platform.notifications.at(-1), "You have become addicted.");
  update(kPlayerServerId, [], [0x1234]);
  assert.equal(h.platform.notifications.filter((n) => n.includes("addicted")).length, 1);

  const bob = 0xff000002;
  h.platform.refs.map(bob, 0x5000);
  const fx = { effectId: 0xe2, sourceItem: 0x33, kind: 0, avId: 0, magnitude: 0, remainingMs: 0 };
  update(bob, [fx]);
  update(bob, [fx]);
  assert.deepEqual(h.platform.callsOf("applyEffectVisuals"), [[0x5000, [0x33]]]);
  update(bob, []);
  assert.deepEqual(h.platform.callsOf("applyEffectVisuals").at(-1), [0x5000, []]);
});
