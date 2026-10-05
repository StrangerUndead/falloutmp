import assert from "node:assert/strict";
import { test } from "node:test";
import { PartyOp } from "../src/services/messages/codes";
import { PartyActionMessage } from "../src/services/messages/fo4Messages";
import { Fo4MsgType } from "../src/services/messages/msgType";
import { SkympClientBridge, EmitterTransport } from "../src/integration/skympClientBridge";
import { FalloutMpClient } from "../src/falloutMpClient";
import { FakePlatform, key, kPlayerProfile, kPlayerServerId, makeClient } from "./fakePlatform";
import { lastSent, reply } from "./helpers";

function partyState(fields: Partial<PartyActionMessage>): never {
  return { t: Fo4MsgType.PartyAction, nonce: 0, op: PartyOp.State, targetProfileId: -1, partyId: 0, value: false, leader: -1, members: [], error: "", ...fields } as never;
}

test("party: invite pushes, accept, state pushes and leader checks", async () => {
  const h = makeClient();
  const invites: number[] = [];
  h.client.events.on("partyInvite", (i) => invites.push(i.fromProfileId));
  h.receive(partyState({ op: PartyOp.Invite, partyId: 3, targetProfileId: 1 }));
  assert.deepEqual(invites, [1]);
  assert.equal(h.client.party.pendingInvites().length, 1);

  const accept = h.client.party.accept(3);
  const req = lastSent<PartyActionMessage>(h, Fo4MsgType.PartyAction);
  assert.deepEqual([req.op, req.partyId], [PartyOp.Accept, 3]);
  h.receive(partyState({ nonce: req.nonce, partyId: 3, leader: 1, members: [1, kPlayerProfile] }));
  assert.ok((await accept).ok);
  assert.ok(h.client.party.inParty());
  assert.ok(!h.client.party.isLeader());
  assert.equal(h.client.party.pendingInvites().length, 0);

  assert.equal((await h.client.party.kick(1)).error, "NotLeader");
  assert.equal((await h.client.party.invite(kPlayerProfile)).error, "CannotTargetSelf");

  // Pushed state: promoted to leader
  const changes: number[] = [];
  h.client.events.on("partyChanged", (s) => changes.push(s.leader));
  h.receive(partyState({ partyId: 3, leader: kPlayerProfile, members: [1, kPlayerProfile] }));
  assert.ok(h.client.party.isLeader());
  h.receive(partyState({ partyId: 3, leader: kPlayerProfile, members: [1, kPlayerProfile] }));
  assert.deepEqual(changes, [kPlayerProfile], "unchanged pushes are not re-announced");

  // Kicked
  h.receive(partyState({ partyId: 0 }));
  assert.ok(!h.client.party.inParty());

  // Invite expiry
  h.receive(partyState({ op: PartyOp.Invite, partyId: 4, targetProfileId: 2 }));
  h.platform.now += 61000;
  assert.equal(h.client.party.pendingInvites().length, 0);

  // A failed request carries the error in the state reply
  const flag = h.client.party.setPvpFlag(true);
  const f = lastSent<PartyActionMessage>(h, Fo4MsgType.PartyAction);
  h.receive(partyState({ nonce: f.nonce, error: "FlagCooldown" }));
  assert.equal((await flag).error, "FlagCooldown");
});

test("map discovery and fast travel", async () => {
  const h = makeClient();
  const sanctuary = 0x0001f388;
  h.platform.refs.map(sanctuary, 0x1f388);
  h.receive({ t: Fo4MsgType.MapDiscovery, full: true, markers: [{ refId: 0xaa, name: "Vault 111", type: 1, pos: [0, 0, 0] }] } as never);
  assert.equal(h.platform.notifications.length, 0, "the join list is not announced");
  h.receive({ t: Fo4MsgType.MapDiscovery, full: false, markers: [{ refId: sanctuary, name: "Sanctuary", type: 2, pos: [1, 2, 3] }] } as never);
  assert.equal(h.platform.notifications.at(-1), "Discovered: Sanctuary");
  assert.deepEqual(h.platform.callsOf("setMapMarker"), [[0x1f388, true, true]]);
  assert.equal(h.client.map.known().length, 2);

  assert.equal((await h.client.map.fastTravel(0xbb)).error, "NotDiscovered");
  const ft = h.client.map.fastTravel(sanctuary);
  assert.equal(lastSent<{ markerRefId: number }>(h, Fo4MsgType.FastTravelRequest).markerRefId, sanctuary);
  reply(h, Fo4MsgType.FastTravelRequest, false, "OverEncumbered");
  assert.equal((await ft).error, "OverEncumbered");
  assert.equal(h.platform.notifications.at(-1), "You are carrying too much to fast travel.");

  // A later full list drops markers the server no longer reports
  h.receive({ t: Fo4MsgType.MapDiscovery, full: true, markers: [{ refId: 0xaa, name: "Vault 111", type: 1, pos: [0, 0, 0] }] } as never);
  assert.ok(!h.client.map.isKnown(sanctuary));
  assert.deepEqual(h.platform.callsOf("setMapMarker").at(-1), [0x1f388, false, false]);
});

test("server time is applied once, then only to correct drift", () => {
  const h = makeClient();
  const tw = (gameHour: number, weatherId = 0x1000, timeScale = 20) =>
    h.receive({ t: Fo4MsgType.WorldTimeWeather, gameDays: 10, gameHour, timeScale, weatherId, transitionSec: 10, radstorm: false, serverNowMs: 0 } as never);
  tw(8);
  assert.deepEqual(h.platform.callsOf("setGameTime"), [[10, 8]]);
  assert.deepEqual(h.platform.callsOf("forceWeather"), [[0x1000, 0]], "first weather is instant");
  assert.deepEqual(h.platform.callsOf("setTimeScale"), [[20]]);

  // 10 s later at scale 20 the game advanced 200 s = 0.0556 h on its own
  h.platform.now += 10000;
  tw(8 + 200 / 3600);
  assert.equal(h.platform.callsOf("setGameTime").length, 1, "in sync, no jump");
  assert.equal(h.client.timeWeather.corrections, 0);

  h.platform.now += 10000;
  tw(12, 0x2000);
  assert.deepEqual(h.platform.callsOf("setGameTime").at(-1), [10, 12]);
  assert.equal(h.client.timeWeather.corrections, 1);
  assert.deepEqual(h.platform.callsOf("forceWeather").at(-1), [0x2000, 10], "later changes blend");

  const est = h.client.timeWeather.estimatedGameHour() ?? -1;
  assert.ok(Math.abs(est - 12) < 1e-9);
});

test("platform capture events drive the services", async () => {
  const h = makeClient();
  h.platform.refs.map(0xff00f001, 0x9001);
  h.platform.refs.map(0x000250fe, 0x250fe);
  h.platform.fire("powerArmorEnterRequested", { frame: 0x9001 });
  assert.equal(lastSent<{ frameRefId: number }>(h, Fo4MsgType.PowerArmorTransition).frameRefId, 0xff00f001);
  h.platform.fire("craftRequested", { workbench: 0x250fe, recipeId: 0x2001, count: 1 });
  assert.equal(lastSent<{ workbenchRefId: number }>(h, Fo4MsgType.CraftItemFo4).workbenchRefId, 0x000250fe);
  h.platform.fire("equipRequested", { item: key(0x500), equip: false });
  assert.equal(lastSent<{ op: number }>(h, Fo4MsgType.UpdateEquipmentFo4).op, 1);
  h.platform.fire("weaponFired", { weaponBaseId: 1, origin: [0, 0, 0], direction: [1, 0, 0] });
  h.platform.fire("projectileHit", { localShotId: 0, projectileIndex: 0, target: 0x9001, limb: 0 });
  assert.equal(h.client.combat.pendingHitCount(), 1, "uses the last local shot");

  // Requests time out on the platform tick
  h.platform.now += 60000;
  h.platform.fire("tick", {});
  await h.flush();
  assert.equal(h.client.ctx.requests.pendingCount(), 0);
});

test("non-Fallout messages pass through; disconnect fails pending requests", async () => {
  const h = makeClient();
  assert.equal(h.client.onMessage({ t: 2 }), false);
  assert.equal(h.client.onMessage({ t: Fo4MsgType.VatsAction }), true, "reserved range is ours");
  assert.ok(h.platform.logs.some((l) => l.includes("no handler")));
  const pending = h.client.map.fastTravel(0);
  void pending;
  const craft = h.client.crafting.craft(1, 2);
  h.client.onDisconnect();
  assert.equal((await craft).error, "Disconnected");
});

test("a throwing handler doesn't break the message loop", () => {
  const h = makeClient();
  h.client.events.on("inventoryChanged", () => {
    throw new Error("ui bug");
  });
  h.receive({ t: Fo4MsgType.SetInventoryFo4, refId: 0, version: 1, entries: [] } as never);
  assert.ok(h.platform.logs.some((l) => l.includes("listener of inventoryChanged threw")));
  h.receive({ t: Fo4MsgType.SetInventoryFo4, refId: 0, version: 2, entries: [{ item: key(1), count: 1 }] } as never);
  assert.equal(h.client.inventory.count(1), 1);
});

test("the skymp bridge routes messages and sendMessage events", () => {
  const emitted: { message: { t: number }; reliability: string }[] = [];
  const platform = new FakePlatform();
  const client = new FalloutMpClient(platform, new EmitterTransport({ emit: (_e, p) => emitted.push(p) }));
  const bridge = new SkympClientBridge(client);
  bridge.onCreateActor({ idx: kPlayerServerId, isMe: true, profileId: 3 });
  assert.equal(client.ctx.session.localActorId, kPlayerServerId);
  assert.ok(bridge.handleIncoming({ t: Fo4MsgType.MapDiscovery, full: true, markers: [] } as never));
  assert.ok(!bridge.handleIncoming({ t: 1 }));
  client.equipment.requestState();
  assert.deepEqual(emitted.map((e) => [e.message.t, e.reliability]), [[Fo4MsgType.UpdateEquipmentFo4, "reliable"]]);
  void client.crafting.craft(1, 2);
  bridge.onConnectionLost();
  assert.equal(client.ctx.requests.pendingCount(), 0);
});
