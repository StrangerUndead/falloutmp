import assert from "node:assert/strict";
import { test } from "node:test";
import { WorkshopEditOp, WorkshopPerm } from "../src/services/messages/codes";
import { WorkshopEditMessage, WorkshopObject } from "../src/services/messages/fo4Messages";
import { Fo4MsgType } from "../src/services/messages/msgType";
import { Harness, makeClient } from "./fakePlatform";
import { lastSent, reply } from "./helpers";

const kShop = 0x000250fe;
const kShopLocal = 0x250fe;

async function inBuildMode(perms: number = WorkshopPerm.All): Promise<Harness> {
  const h = makeClient();
  h.platform.refs.map(kShop, kShopLocal);
  const enter = h.client.workshop.enterBuildMode(kShop);
  h.receive({ t: Fo4MsgType.WorkshopMode, workshopRefId: kShop, enter: true, allowed: true, reason: "", perms } as never);
  assert.ok((await enter).ok);
  return h;
}

function obj(refId: number, x = 0, baseId = 0x1001): WorkshopObject {
  return { refId, baseId, pos: [x, 0, 0], rot: [0, 0, 0], scale: 1, flags: 0 };
}

function objects(h: Harness, fields: Record<string, unknown>): void {
  h.receive({
    t: Fo4MsgType.WorkshopObjects,
    workshopRefId: kShop,
    version: 0,
    kind: 1,
    chunk: 0,
    chunkCount: 1,
    added: [],
    removed: [],
    scrappedPrePlaced: [],
    wires: [],
    ...fields,
  } as never);
}

test("build mode is granted by the server with permissions", async () => {
  const h = await inBuildMode(WorkshopPerm.Build);
  assert.deepEqual(h.platform.callsOf("enterWorkshopMode"), [[kShopLocal, WorkshopPerm.Build]]);
  assert.equal(h.client.workshop.activeWorkshopId(), kShop);

  // Scrapping needs the Scrap permission
  const scrap = await h.client.workshop.edit(WorkshopEditOp.Scrap, [{ refId: 1 }]);
  assert.equal(scrap.error, "NoPermission");
  assert.equal(h.transport.ofType(Fo4MsgType.WorkshopEdit).length, 0);

  // The server ends build mode when the builder walks away
  h.receive({ t: Fo4MsgType.WorkshopMode, workshopRefId: 0, enter: false, allowed: false, reason: "LeftBuildArea", perms: 0 } as never);
  assert.equal(h.client.workshop.activeWorkshopId(), 0);
  assert.equal(h.platform.callsOf("exitWorkshopMode").length, 1);
  assert.equal(h.platform.notifications.at(-1), "You left the settlement.");
});

test("denied build mode reports the reason", async () => {
  const h = makeClient();
  const enter = h.client.workshop.enterBuildMode(kShop);
  h.receive({ t: Fo4MsgType.WorkshopMode, workshopRefId: kShop, enter: true, allowed: false, reason: "NoPermission", perms: 0 } as never);
  const r = await enter;
  assert.equal(r.error, "NoPermission");
  assert.equal(h.platform.callsOf("enterWorkshopMode").length, 0);
  const place = await h.client.workshop.place({ recipeId: 1, baseId: 2, pos: [0, 0, 0], rot: [0, 0, 0] });
  assert.equal(place.error, "NotInBuildMode");
});

test("placing resolves with the new ref; objects come from deltas", async () => {
  const h = await inBuildMode();
  const place = h.client.workshop.place({ recipeId: 0x2001, baseId: 0x1001, pos: [10, 10, 0], rot: [0, 0, 1] });
  const sent = lastSent<{ workshopRefId: number; scale: number; fromStored: boolean }>(h, Fo4MsgType.WorkshopPlace);
  assert.equal(sent.workshopRefId, kShop);
  assert.equal(sent.scale, 1);
  assert.equal(sent.fromStored, false);
  reply(h, Fo4MsgType.WorkshopPlace, true, "", { refId: 0xff200000 });
  assert.equal((await place).refId, 0xff200000);

  objects(h, { version: 1, added: [obj(0xff200000, 10)] });
  assert.equal(h.platform.spawned.size, 1);
  const local = h.client.workshop.localIdOf(0xff200000);
  assert.equal(h.client.workshop.serverIdOf(local), 0xff200000);

  // Move: same object, new transform
  objects(h, { version: 2, added: [obj(0xff200000, 50)] });
  assert.equal(h.platform.spawned.size, 1);
  assert.deepEqual(h.platform.callsOf("moveWorkshopObject").at(-1)?.[1], [50, 0, 0]);

  // Scrap: removed
  objects(h, { version: 3, removed: [0xff200000] });
  assert.equal(h.platform.spawned.size, 0);
  assert.equal(h.client.workshop.versionOf(kShop), 3);
});

test("chunked snapshots replace the scene and later stale deltas are dropped", async () => {
  const h = await inBuildMode();
  h.platform.refs.map(0x1a000, 0x1a000); // a pre-placed plugin ref
  objects(h, { version: 1, added: [obj(0xff200009, 1)] }); // will be gone in the snapshot
  const snap = (chunk: number, fields: Record<string, unknown>) =>
    objects(h, { kind: 0, version: 10, chunk, chunkCount: 2, ...fields });
  snap(1, { added: [obj(0xff200002, 2)] });
  assert.equal(h.client.workshop.versionOf(kShop), 1, "incomplete snapshot not applied");
  snap(0, {
    added: [obj(0xff200001, 1)],
    wires: [{ wireRefId: 0xff2000ff, a: 0xff200001, b: 0xff200002, splineBaseId: 0x1d971 }],
    scrappedPrePlaced: [0x1a000],
  });
  assert.equal(h.client.workshop.versionOf(kShop), 10);
  assert.deepEqual(h.client.workshop.objectsOf(kShop).map((o) => o.refId).sort(), [0xff200001, 0xff200002]);
  assert.equal(h.platform.spawned.size, 2);
  assert.equal(h.platform.wires.size, 1);
  assert.deepEqual(h.platform.callsOf("setPrePlacedDisabled"), [[0x1a000, true]]);

  objects(h, { version: 9, added: [obj(0xff200003, 3)] });
  assert.equal(h.client.workshop.objectsOf(kShop).length, 2, "older than the snapshot");

  // Removing an endpoint removes its wire
  objects(h, { version: 11, removed: [0xff200001] });
  assert.equal(h.platform.wires.size, 0);
});

test("wires: the builder adds its own, neighbours get relays", async () => {
  const h = await inBuildMode();
  objects(h, { version: 1, added: [obj(0xff200001), obj(0xff200002, 5)] });
  const connect = h.client.workshop.connectWire(0xff200001, 0xff200002, 0x1d971);
  reply(h, Fo4MsgType.WorkshopWire, true, "", { refId: 0xff2000aa });
  assert.ok((await connect).ok);
  assert.equal(h.platform.wires.size, 1);
  assert.deepEqual(h.client.workshop.wiresOf(kShop).map((w) => w.wireRefId), [0xff2000aa]);

  const off = h.client.workshop.disconnectWire(0xff2000aa);
  reply(h, Fo4MsgType.WorkshopWire, true);
  await off;
  assert.equal(h.platform.wires.size, 0);

  // A neighbour's wire
  h.receive({ t: Fo4MsgType.WorkshopWire, nonce: 9, workshopRefId: kShop, op: 0, a: 0xff200001, b: 0xff200002, splineBaseId: 1, wireRefId: 0xff2000bb } as never);
  assert.equal(h.platform.wires.size, 1);
});

test("edits are split into server-sized batches and refunds merged", async () => {
  const h = await inBuildMode();
  const items = Array.from({ length: 70 }, (_, i) => ({ refId: 0xff210000 + i }));
  const edit = h.client.workshop.edit(WorkshopEditOp.Store, items);
  assert.equal(lastSent<WorkshopEditMessage>(h, Fo4MsgType.WorkshopEdit).items.length, 64);
  reply(h, Fo4MsgType.WorkshopEdit, true, "", { items: [{ item: { baseId: 1, mods: [], condition: 0, stolenFrom: 0, ammoLoaded: 0 }, count: 2 }] });
  await h.flush();
  assert.equal(lastSent<WorkshopEditMessage>(h, Fo4MsgType.WorkshopEdit).items.length, 6);
  reply(h, Fo4MsgType.WorkshopEdit, true, "", { items: [{ item: { baseId: 1, mods: [], condition: 0, stolenFrom: 0, ammoLoaded: 0 }, count: 1 }] });
  const r = await edit;
  assert.ok(r.ok);
  assert.equal(r.items.length, 2);
});

test("settlement state feeds the ratings HUD and permissions", async () => {
  const h = await inBuildMode(WorkshopPerm.Build);
  const ratings: unknown[] = [];
  h.client.events.on("workshopStateChanged", (s) => ratings.push(s.food));
  h.receive({
    t: Fo4MsgType.WorkshopState,
    workshopRefId: kShop,
    version: 4,
    ownerType: 1,
    ownerId: 7,
    yourPerms: WorkshopPerm.All,
    budgetCurrent: 12,
    budgetMax: 100,
    objects: 3,
    maxObjects: 1000,
    food: 6,
    water: 4,
    safety: 10,
    beds: 2,
    power: 5,
    powerLoad: 3,
    population: 2,
    happiness: 60,
  } as never);
  assert.deepEqual(ratings, [6]);
  assert.equal(h.client.workshop.activePermissions(), WorkshopPerm.All);
  const call = h.platform.callsOf("setWorkshopRatings")[0];
  assert.equal(call[0], kShopLocal);
  assert.equal((call[1] as { happiness: number }).happiness, 60);

  const claim = h.client.workshop.setAccess(kShop, 9, WorkshopPerm.Build | WorkshopPerm.Container);
  const m = lastSent<{ op: number; profileId: number; perms: number }>(h, Fo4MsgType.WorkshopManage);
  assert.deepEqual([m.op, m.profileId, m.perms], [2, 9, 5]);
  reply(h, Fo4MsgType.WorkshopManage, true);
  assert.ok((await claim).ok);
});

test("disconnect clears spawned settlement objects", async () => {
  const h = await inBuildMode();
  objects(h, { version: 1, added: [obj(0xff200001)] });
  const pending = h.client.workshop.place({ recipeId: 1, baseId: 2, pos: [0, 0, 0], rot: [0, 0, 0] });
  h.client.onDisconnect();
  assert.equal(h.platform.spawned.size, 0);
  assert.equal((await pending).error, "Disconnected");
  assert.equal(h.client.workshop.activeWorkshopId(), 0);
  assert.equal(h.platform.callsOf("exitWorkshopMode").length, 1);
});

test("a wire removed in the workshop menu is disconnected on the server", async () => {
  const h = await inBuildMode();
  objects(h, {
    kind: 0, version: 10, chunk: 0, chunkCount: 1,
    added: [obj(0xff200001, 1), obj(0xff200002, 2)],
    wires: [{ wireRefId: 0xff2000ff, a: 0xff200001, b: 0xff200002, splineBaseId: 0x1d971 }],
  });
  const [local] = Array.from(h.platform.wires.keys());
  assert.equal(h.client.workshop.wireServerIdOf(local), 0xff2000ff);
  h.platform.fire("workshopWireRemoveRequested", { workbench: kShop, wire: local });
  const [m] = h.transport.ofType<{ op: number; wireRefId: number }>(Fo4MsgType.WorkshopWire);
  assert.equal(m.wireRefId, 0xff2000ff);
  assert.equal(m.op, 1); // disconnect
});
