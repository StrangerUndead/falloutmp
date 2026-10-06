import assert from "node:assert/strict";
import { test } from "node:test";
import { HackResultCode, LockResultCode } from "../src/services/messages/codes";
import { BarterMessage, HitReportMessage, WeaponFireMessage } from "../src/services/messages/fo4Messages";
import { Fo4MsgType } from "../src/services/messages/msgType";
import { key, kPlayer, kPlayerServerId, makeClient } from "./fakePlatform";
import { lastSent, reply } from "./helpers";

test("hits are claimed with the server shot sequence", () => {
  const h = makeClient();
  const shotA = h.client.combat.fire(0x4822c, [0, 0, 100], [1, 0, 0]);
  const shotB = h.client.combat.fire(0x4822c, [0, 0, 100], [1, 0, 0]);
  const fire = lastSent<WeaponFireMessage>(h, Fo4MsgType.WeaponFire);
  assert.equal(fire.clientShotId, shotB);
  assert.equal(h.transport.sent.at(-1)?.reliable, false, "shots are unreliable");

  // A hit reported before the server relay arrives waits for it
  h.client.combat.reportHit(shotA, 0, 0xff0000aa, 1);
  assert.equal(h.transport.ofType(Fo4MsgType.HitReport).length, 0);
  assert.equal(h.client.combat.pendingHitCount(), 1);
  h.receive({ ...fire, clientShotId: shotA, shooterIdx: kPlayerServerId, seq: 41 } as never);
  const hit = lastSent<HitReportMessage>(h, Fo4MsgType.HitReport);
  assert.deepEqual([hit.shotSeq, hit.targetIdx, hit.limb], [41, 0xff0000aa, 1]);

  // Later hits of a known shot go out at once
  h.receive({ ...fire, shooterIdx: kPlayerServerId, seq: 42 } as never);
  h.client.combat.reportHit(shotB, 1, 0xff0000ab, 0);
  assert.equal(lastSent<HitReportMessage>(h, Fo4MsgType.HitReport).shotSeq, 42);

  // Hits for a shot the server never relayed expire
  const shotC = h.client.combat.fire(0x4822c, [0, 0, 0], [1, 0, 0]);
  h.client.combat.reportHit(shotC, 0, 0xff0000aa, 0);
  h.platform.now += 5000;
  h.client.tick();
  assert.equal(h.client.combat.pendingHitCount(), 0);
});

test("remote shots replay cosmetically; damage and kills apply to puppets", () => {
  const h = makeClient();
  const raider = 0xff0000aa;
  h.platform.refs.map(raider, 0x7700);
  h.receive({ t: Fo4MsgType.WeaponFire, shooterIdx: raider, seq: 3, weaponBaseId: 0x99, origin: [1, 2, 3], direction: [0, 1, 0], clientShotId: 0 } as never);
  assert.deepEqual(h.platform.callsOf("playRemoteShot"), [[0x7700, 0x99, [1, 2, 3], [0, 1, 0]]]);

  const damage: number[] = [];
  h.client.events.on("damageApplied", (e) => damage.push(e.total));
  h.receive({ t: Fo4MsgType.DamageApplied, targetIdx: raider, aggressorIdx: kPlayerServerId, total: 30, amounts: [30], limb: 1, critical: true, killed: true } as never);
  assert.deepEqual(h.platform.callsOf("playHitReaction"), [[0x7700, 1, 30, true]]);
  assert.deepEqual(h.platform.callsOf("killActor"), [[0x7700, kPlayer]]);
  assert.deepEqual(damage, [30]);

  let killedBy = 0;
  h.client.events.on("localPlayerKilled", (e) => (killedBy = e.aggressorIdx));
  h.receive({ t: Fo4MsgType.DamageApplied, targetIdx: kPlayerServerId, aggressorIdx: raider, total: 99, amounts: [], limb: 0, critical: false, killed: true } as never);
  assert.equal(killedBy, raider);
  assert.equal(h.platform.callsOf("killActor").length, 1, "the player's death is the upstream death sync's job");
});

test("reload waits for the server's round count", async () => {
  const h = makeClient();
  const r = h.client.combat.reload();
  const nonce = lastSent<{ nonce: number }>(h, Fo4MsgType.WeaponReload).nonce;
  h.receive({ t: Fo4MsgType.WeaponReload, nonce, loaded: 12, ok: true } as never);
  assert.equal((await r).reply?.loaded, 12);
  assert.deepEqual(h.platform.callsOf("setAmmoLoaded"), [[kPlayer, 12]]);
});

test("crafting, modding and scrapping report the server's results", async () => {
  const h = makeClient();
  const crafted: number[] = [];
  h.client.events.on("crafted", (e) => crafted.push(e.items[0].count));
  const craft = h.client.crafting.craft(0xb001, 0x2001, 2);
  reply(h, Fo4MsgType.CraftItemFo4, true, "", { items: [{ item: key(0x10), count: 2 }] });
  assert.ok((await craft).ok);
  assert.deepEqual(crafted, [2]);

  const mod = h.client.crafting.attachMod(0xb001, key(0x4822c), 0x77);
  assert.equal(lastSent<{ op: number }>(h, Fo4MsgType.ModItem).op, 0);
  reply(h, Fo4MsgType.ModItem, false, "MissingComponents");
  assert.equal((await mod).error, "MissingComponents");
  assert.equal(h.platform.notifications.at(-1), "You don't have the components.");

  assert.equal((await h.client.crafting.detachMod(0xb001, key(0x4822c), 0x77)).error, "IncompatibleMod");
  assert.equal((await h.client.crafting.craft(0xb001, 0x2001, 0)).error, "InvalidCount");

  const scrap = h.client.crafting.scrap(0xb001, key(0x4822c));
  reply(h, Fo4MsgType.ScrapItem, true, "", { items: [{ item: key(0x731a4), count: 3 }] });
  assert.equal((await scrap).items[0].count, 3);
});

test("barter quotes then trades at the quoted price", async () => {
  const h = makeClient();
  const buy = [{ item: key(0x2000), count: 1 }];
  const quote = h.client.barter.quote(0xbeef, buy, []);
  let m = lastSent<BarterMessage>(h, Fo4MsgType.Barter);
  assert.equal(m.op, 0);
  h.receive({ ...m, capsDelta: -35, error: "" } as never);
  const q = await quote;
  assert.equal(q.reply?.capsDelta, -35);

  const trade = h.client.barter.trade(0xbeef, buy, [], -35);
  m = lastSent<BarterMessage>(h, Fo4MsgType.Barter);
  assert.deepEqual([m.op, m.capsDelta], [1, -35]);
  h.receive({ ...m, error: "PriceChanged" } as never);
  const t = await trade;
  assert.equal(t.ok, false);
  assert.equal(h.platform.notifications.at(-1), "Prices changed; check the offer again.");

  assert.equal((await h.client.barter.trade(0xbeef, [], [], 0)).error, "EmptyTrade");
});

test("lockpicking: the server opens the menu, rolls attempts and unlocks", () => {
  const h = makeClient();
  const door = 0x0002a000;
  h.platform.refs.map(door, 0x2a000);
  const lock = (fields: Record<string, number>) => h.receive({ t: Fo4MsgType.LockpickAttempt, op: 0, refId: door, sessionId: 0, outcome: 0, xp: 0, ...fields } as never);

  h.client.locks.activateLock(door);
  lock({ outcome: LockResultCode.BeginLockpick, sessionId: 5 });
  assert.deepEqual(h.platform.callsOf("openLockpickMenu"), [[0x2a000, 5]]);
  h.client.locks.attemptLockpick();
  assert.equal(lastSent<{ op: number; sessionId: number }>(h, Fo4MsgType.LockpickAttempt).sessionId, 5);
  lock({ op: 1, outcome: LockResultCode.Failed, sessionId: 5 });
  assert.equal(h.platform.callsOf("closeLockpickMenu").length, 0, "menu stays open after a broken pin");
  lock({ op: 1, outcome: LockResultCode.Unlocked, sessionId: 5, xp: 10 });
  assert.deepEqual(h.platform.callsOf("closeLockpickMenu"), [[true]]);
  assert.deepEqual(h.platform.callsOf("setLocked"), [[0x2a000, false]]);
  assert.equal(h.client.locks.activeLockpickSession(), undefined);

  lock({ outcome: LockResultCode.NeedsKey });
  assert.equal(h.platform.notifications.at(-1), "You need the key.");

  // Cancelling tells the server and closes the menu
  lock({ outcome: LockResultCode.BeginLockpick, sessionId: 6 });
  h.client.locks.cancelLockpick();
  assert.equal(lastSent<{ op: number }>(h, Fo4MsgType.LockpickAttempt).op, 2);
  assert.deepEqual(h.platform.callsOf("closeLockpickMenu").at(-1), [false]);
});

test("terminal hacking follows the server's attempts", () => {
  const h = makeClient();
  const term = 0x0003b000;
  h.platform.refs.map(term, 0x3b000);
  const hack = (fields: Record<string, number>) => h.receive({ t: Fo4MsgType.TerminalAction, op: 0, refId: term, sessionId: 0, outcome: 0, attemptsLeft: 0, xp: 0, ...fields } as never);
  h.client.locks.activateTerminal(term);
  hack({ outcome: HackResultCode.BeginHack, sessionId: 2, attemptsLeft: 4 });
  assert.deepEqual(h.platform.callsOf("openHackingMenu"), [[0x3b000, 2, 4]]);
  h.client.locks.guessPassword();
  hack({ op: 1, outcome: HackResultCode.WrongGuess, sessionId: 2, attemptsLeft: 3 });
  assert.deepEqual(h.platform.callsOf("setHackingAttemptsLeft"), [[3]]);
  hack({ op: 1, outcome: HackResultCode.Hacked, sessionId: 2, xp: 15 });
  assert.deepEqual(h.platform.callsOf("closeHackingMenu"), [[true]]);
  assert.deepEqual(h.platform.callsOf("openTerminal"), [[0x3b000]]);

  hack({ outcome: HackResultCode.BeginHack, sessionId: 3, attemptsLeft: 1 });
  hack({ op: 1, outcome: HackResultCode.LockoutStarted, sessionId: 3 });
  assert.deepEqual(h.platform.callsOf("closeHackingMenu").at(-1), [false]);
  assert.equal(h.platform.notifications.at(-1), "The terminal is locked.");

  hack({ outcome: HackResultCode.Open });
  assert.equal(h.platform.callsOf("openTerminal").length, 2);
});
