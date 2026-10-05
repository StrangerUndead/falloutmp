import assert from "node:assert/strict";
import { test } from "node:test";
import { LooksMenuMode } from "../src/platform/falloutPlatform";
import {
  AppearanceFo4,
  emptyAppearance,
  UpdateActionsMessage,
  UpdateAppearanceFo4Message,
  UpdateGraphVariablesMessage,
} from "../src/services/messages/fo4Messages";
import { Fo4MsgType } from "../src/services/messages/msgType";
import { kPlayer, makeClient } from "./fakePlatform";

const kRemote = 0xff000050;
const kPuppet = 0xff900050;

function face(raceId = 0x13746, isFemale = true): AppearanceFo4 {
  return { ...emptyAppearance(), raceId, isFemale, headPartIds: [1, 2] };
}

test("owner graph variables go out at 15 Hz, only when they change, full every second", () => {
  const h = makeClient();
  const vars = new Map([
    ["Speed", { name: "Speed", type: 0, value: 0 }],
    ["IsSneaking", { name: "IsSneaking", type: 2, value: 0 }],
  ]);
  h.platform.graphVariables.set(kPlayer, vars);
  h.client.tick();
  const sent = () => h.transport.ofType<UpdateGraphVariablesMessage>(Fo4MsgType.UpdateGraphVariables);
  assert.equal(sent().length, 1);
  assert.equal(sent()[0].idx, 0); // own actor
  assert.equal(sent()[0].values.length, 2);
  assert.equal(h.transport.sent.at(-1)?.reliable, false);

  h.platform.now += 70;
  h.client.tick();
  assert.equal(sent().length, 1, "nothing changed");

  vars.set("Speed", { name: "Speed", type: 0, value: 300 });
  h.client.tick();
  assert.equal(sent().length, 2);
  assert.deepEqual(sent()[1].values.map((v) => v.name), ["Speed"]);
  vars.set("Speed", { name: "Speed", type: 0, value: 320 });
  h.platform.now += 20;
  h.client.tick();
  assert.equal(sent().length, 2, "too soon");
  h.platform.now += 50;
  h.client.tick();
  assert.equal(sent().length, 3);

  h.platform.now += 1000;
  h.client.tick();
  assert.equal(sent()[3].values.length, 2, "full snapshot");
});

test("owner animation events are batched per frame and sent reliable", () => {
  const h = makeClient();
  h.platform.fire("animationEvent", { actor: kPlayer, name: "jumpStart" });
  h.platform.fire("animationEvent", { actor: kPlayer, name: "reloadStart" });
  h.platform.fire("animationEvent", { actor: 0x12345, name: "ignored" }); // not ours
  h.client.tick();
  const [m] = h.transport.ofType<UpdateActionsMessage>(Fo4MsgType.UpdateActions);
  assert.deepEqual(m.events, ["jumpStart", "reloadStart"]);
  assert.equal(m.idx, 0);
  assert.equal(h.transport.sent.find((s) => s.message.t === Fo4MsgType.UpdateActions)?.reliable, true);
});

test("remote events replay except gameplay ones; variables are applied and kept for stream-in", () => {
  const h = makeClient();
  h.platform.refs.map(kRemote, kPuppet);
  h.receive({ t: Fo4MsgType.UpdateActions, idx: kRemote, seq: 1, ts: 0, events: ["jumpStart", "weaponFire"] } as never);
  assert.deepEqual(h.platform.animEvents, [{ actor: kPuppet, name: "jumpStart" }]);

  h.receive({ t: Fo4MsgType.UpdateGraphVariables, idx: kRemote, seq: 5, ts: 0, values: [{ name: "Speed", type: 0, value: 250 }] } as never);
  assert.equal(h.platform.graphVariables.get(kPuppet)?.get("Speed")?.value, 250);
  // Reordered (older) packet is dropped
  h.receive({ t: Fo4MsgType.UpdateGraphVariables, idx: kRemote, seq: 4, ts: 0, values: [{ name: "Speed", type: 0, value: 1 }] } as never);
  assert.equal(h.platform.graphVariables.get(kPuppet)?.get("Speed")?.value, 250);

  // A new puppet gets the cached state
  h.platform.refs.map(kRemote, kPuppet + 1);
  h.client.onActorStreamedIn(kRemote);
  assert.equal(h.platform.graphVariables.get(kPuppet + 1)?.get("Speed")?.value, 250);
});

test("the server opens the editor; the edited face goes up with the next revision", () => {
  const h = makeClient();
  h.client.appearance.onRaceMenu(true);
  assert.equal(h.platform.looksMenuOpen, LooksMenuMode.Create);
  h.platform.appearances.set(kPlayer, face());
  h.platform.fire("looksMenuClosed", {});
  const [m] = h.transport.ofType<UpdateAppearanceFo4Message>(Fo4MsgType.UpdateAppearanceFo4);
  assert.equal(m.idx, 0);
  assert.equal(m.rev, 1);
  assert.equal(m.data.isFemale, true);

  // The server echoes the stored appearance to the owner: no re-apply when equal
  h.receive({ t: Fo4MsgType.UpdateAppearanceFo4, idx: h.client.ctx.session.localActorId, rev: 1, data: face() } as never);
  assert.ok(!h.platform.calls.some((c) => c.fn === "applyAppearanceFo4"));
  // Next time the editor is a remake
  h.client.appearance.onRaceMenu(true);
  assert.equal(h.platform.looksMenuOpen, LooksMenuMode.Remake);
});

test("a LooksMenu the server didn't open is reverted to the stored face", () => {
  const h = makeClient();
  h.receive({ t: Fo4MsgType.UpdateAppearanceFo4, idx: h.client.ctx.session.localActorId, rev: 2, data: face() } as never);
  h.platform.appearances.set(kPlayer, face(0x13746, false)); // edited via console
  h.platform.fire("looksMenuClosed", {});
  assert.equal(h.transport.ofType(Fo4MsgType.UpdateAppearanceFo4).length, 0);
  assert.equal(h.platform.appearances.get(kPlayer)?.isFemale, true);
});

test("remote faces apply to puppets now or on stream-in, newest revision wins", async () => {
  const h = makeClient();
  h.receive({ t: Fo4MsgType.UpdateAppearanceFo4, idx: kRemote, rev: 3, data: face(0x13746, false) } as never);
  assert.ok(!h.platform.calls.some((c) => c.fn === "applyAppearanceFo4"), "not streamed in yet");
  h.platform.refs.map(kRemote, kPuppet);
  h.client.onActorStreamedIn(kRemote);
  assert.equal(h.platform.appearances.get(kPuppet)?.isFemale, false);
  h.receive({ t: Fo4MsgType.UpdateAppearanceFo4, idx: kRemote, rev: 2, data: face(0x13746, true) } as never);
  assert.equal(h.platform.appearances.get(kPuppet)?.isFemale, false, "older revision ignored");
  await h.flush();
  let applied = 0;
  h.client.events.on("appearanceApplied", () => applied++);
  h.receive({ t: Fo4MsgType.UpdateAppearanceFo4, idx: kRemote, rev: 4, data: face(0x13746, true) } as never);
  await h.flush();
  assert.equal(h.platform.appearances.get(kPuppet)?.isFemale, true);
  assert.equal(applied, 1);
});

test("appearance comparison ignores field order", async () => {
  const { sameAppearance } = await import("../src/services/services/appearanceService");
  const a = face();
  const reordered = JSON.parse(JSON.stringify(Object.fromEntries(Object.entries(a).reverse()))) as AppearanceFo4;
  assert.ok(sameAppearance(a, reordered));
  assert.ok(!sameAppearance(a, { ...a, isFemale: !a.isFemale }));
});
