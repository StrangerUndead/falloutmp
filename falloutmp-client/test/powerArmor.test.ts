import assert from "node:assert/strict";
import { test } from "node:test";
import { PaPhase, PaTransitionKind } from "../src/services/messages/codes";
import { PowerArmorTransitionMessage } from "../src/services/messages/fo4Messages";
import { Fo4MsgType } from "../src/services/messages/msgType";
import { Harness, key, kPlayer, kPlayerServerId, makeClient } from "./fakePlatform";

const kFrameRef = 0xff00f001;
const kFrameLocal = 0x9001;

function serverReply(h: Harness, ok: boolean, phase: PaPhase, error = ""): void {
  const sent = h.transport.ofType<PowerArmorTransitionMessage>(Fo4MsgType.PowerArmorTransition);
  const req = sent[sent.length - 1];
  h.receive({ ...req, t: Fo4MsgType.PowerArmorTransition, actorIdx: kPlayerServerId, ok, error, phase } as never);
}

function sentKinds(h: Harness): number[] {
  return h.transport.ofType<PowerArmorTransitionMessage>(Fo4MsgType.PowerArmorTransition).map((m) => m.kind);
}

test("entering power armor: request, animation, ack, in", async () => {
  const h = makeClient();
  h.platform.refs.map(kFrameRef, kFrameLocal);
  h.platform.holdAnimations = true;
  const done = h.client.powerArmor.enter(kFrameRef);
  assert.deepEqual(sentKinds(h), [PaTransitionKind.Enter]);
  assert.ok(h.client.powerArmor.isBusy());

  serverReply(h, true, PaPhase.Entering);
  await h.flush();
  assert.equal(h.client.powerArmor.getPhase(), PaPhase.Entering);
  assert.deepEqual(h.platform.callsOf("playPowerArmorEnter"), [[kFrameLocal]]);
  assert.deepEqual(sentKinds(h), [PaTransitionKind.Enter], "no ack before the animation ends");

  // A second request during the transition is refused locally
  const again = await h.client.powerArmor.enter(kFrameRef);
  assert.equal(again.error, "InTransition");

  h.platform.releaseAnimation();
  await h.flush();
  assert.deepEqual(sentKinds(h), [PaTransitionKind.Enter, PaTransitionKind.Ack]);
  const nonces = h.transport.ofType<PowerArmorTransitionMessage>(Fo4MsgType.PowerArmorTransition).map((m) => m.nonce);
  assert.equal(nonces[0], nonces[1], "the ack carries the request nonce");

  serverReply(h, true, PaPhase.In);
  const r = await done;
  assert.ok(r.ok);
  assert.equal(h.client.powerArmor.getPhase(), PaPhase.In);
  assert.equal(h.client.powerArmor.getFrame(), kFrameRef);
  assert.ok(!h.client.powerArmor.isBusy());

  // The owner's state brings the pieces, core charge and jetpack
  h.receive({
    t: Fo4MsgType.PowerArmorState,
    actorIdx: kPlayerServerId,
    frameRefId: kFrameRef,
    phase: PaPhase.In,
    frameBaseId: 0x1000,
    pieces: [{ slot: 2, item: key(0x2000), healthPct: 80 }],
    coreBaseId: 0x75,
    coreCharge: 0.75,
    unpowered: false,
    jetpackCapable: true,
  } as never);
  assert.deepEqual(h.platform.callsOf("setFusionCoreCharge"), [[0.75]]);
  assert.deepEqual(h.platform.callsOf("setJetpackEnabled").at(-1), [true]);
  assert.equal(h.platform.callsOf("applyPowerArmorVisual").length, 1);
  assert.equal(h.platform.callsOf("setInPowerArmor").length, 0, "no snapping in steady state");
});

test("exiting power armor mirrors the handshake", async () => {
  const h = makeClient();
  h.platform.refs.map(kFrameRef, kFrameLocal);
  const enter = h.client.powerArmor.enter(kFrameRef);
  serverReply(h, true, PaPhase.Entering);
  await h.flush();
  serverReply(h, true, PaPhase.In);
  await enter;

  const exit = h.client.powerArmor.exit();
  assert.equal(h.transport.ofType<PowerArmorTransitionMessage>(Fo4MsgType.PowerArmorTransition).at(-1)?.frameRefId, kFrameRef);
  serverReply(h, true, PaPhase.Exiting);
  await h.flush();
  assert.equal(h.platform.callsOf("playPowerArmorExit").length, 1);
  serverReply(h, true, PaPhase.Out);
  assert.ok((await exit).ok);
  assert.equal(h.client.powerArmor.getPhase(), PaPhase.Out);
  assert.deepEqual(sentKinds(h), [0, 2, 1, 2]);
  assert.equal((await h.client.powerArmor.exit()).error, "NotInPowerArmor");
});

test("a refused request shows the reason and changes nothing", async () => {
  const h = makeClient();
  h.platform.refs.map(kFrameRef, kFrameLocal);
  const enter = h.client.powerArmor.enter(kFrameRef);
  serverReply(h, false, PaPhase.Out, "Occupied");
  const r = await enter;
  assert.equal(r.error, "Occupied");
  assert.equal(h.client.powerArmor.getPhase(), PaPhase.Out);
  assert.equal(h.platform.callsOf("playPowerArmorEnter").length, 0);
  assert.equal(h.platform.notifications.at(-1), "Someone else is using that power armor.");
});

test("an interrupted animation is rolled back by the server timeout", async () => {
  const h = makeClient();
  h.platform.refs.map(kFrameRef, kFrameLocal);
  h.platform.enterAnimationResult = false;
  const enter = h.client.powerArmor.enter(kFrameRef);
  serverReply(h, true, PaPhase.Entering);
  const r = await enter;
  assert.equal(r.error, "Interrupted");
  assert.deepEqual(sentKinds(h), [0], "no ack");
  // The server's Tick times the transition out
  h.receive({ t: Fo4MsgType.PowerArmorTransition, nonce: 0, actorIdx: kPlayerServerId, frameRefId: 0, kind: 0, phase: PaPhase.Out, ok: false, error: "Timeout", exitPos: [0, 0, 0] } as never);
  assert.equal(h.client.powerArmor.getPhase(), PaPhase.Out);
  assert.deepEqual(h.platform.callsOf("setInPowerArmor").at(-1), [kPlayer, kFrameLocal, false]);
  // and a new attempt is possible
  void h.client.powerArmor.enter(kFrameRef);
  assert.equal(sentKinds(h).length, 2);
});

test("server-forced exits snap the player out", async () => {
  const h = makeClient();
  h.platform.refs.map(kFrameRef, kFrameLocal);
  const enter = h.client.powerArmor.enter(kFrameRef);
  serverReply(h, true, PaPhase.Entering);
  await h.flush();
  serverReply(h, true, PaPhase.In);
  await enter;
  h.receive({ t: Fo4MsgType.PowerArmorState, actorIdx: kPlayerServerId, frameRefId: kFrameRef, phase: PaPhase.Out, frameBaseId: 0, pieces: [], coreBaseId: 0, unpowered: false, jetpackCapable: false } as never);
  assert.equal(h.client.powerArmor.getPhase(), PaPhase.Out);
  assert.deepEqual(h.platform.callsOf("setInPowerArmor").at(-1), [kPlayer, kFrameLocal, false]);
  assert.deepEqual(h.platform.callsOf("setJetpackEnabled").at(-1), [false]);
});

test("remote actors are put in and out of frames and dressed", () => {
  const h = makeClient();
  const remote = 0xff000077;
  h.platform.refs.map(remote, 0x6100);
  h.platform.refs.map(kFrameRef, kFrameLocal);
  const state = (phase: PaPhase) =>
    h.receive({ t: Fo4MsgType.PowerArmorState, actorIdx: remote, frameRefId: kFrameRef, phase, frameBaseId: 0x1000, pieces: [], coreBaseId: 0, unpowered: true, jetpackCapable: false } as never);
  state(PaPhase.Entering);
  state(PaPhase.In);
  state(PaPhase.Exiting);
  assert.deepEqual(h.platform.callsOf("setInPowerArmor"), [
    [0x6100, kFrameLocal, true],
    [0x6100, kFrameLocal, false],
  ]);
  assert.equal(h.platform.callsOf("applyPowerArmorVisual").length, 2);
  assert.equal(h.platform.callsOf("setFusionCoreCharge").length, 0, "only the owner sees the charge");
  assert.equal(h.client.powerArmor.stateOf(remote)?.unpowered, true);
});
