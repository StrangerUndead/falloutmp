import assert from "node:assert/strict";
import { test } from "node:test";
import { EventBus } from "../src/core/eventBus";
import { diffInventories, sameStack, stackKey } from "../src/core/itemKeys";
import { RequestTracker } from "../src/core/requestTracker";
import { buildMessage, kMessageDefaults } from "../src/services/messages/fo4Messages";
import { describeError } from "../src/services/messages/codes";
import { Fo4MsgType, isFallout4MsgType } from "../src/services/messages/msgType";
import { key } from "./fakePlatform";

test("RequestTracker: unique nonces, completion, timeout and failAll", async () => {
  let now = 0;
  const t = new RequestTracker(() => now, { timeoutMs: 100, firstNonce: 0xffffffff });
  const a = t.nextNonce();
  const b = t.nextNonce();
  assert.equal(a, 0xffffffff);
  assert.equal(b, 1, "wraps around and skips 0");
  const pa = t.track(a, Fo4MsgType.CraftItemFo4);
  const pb = t.track(b, Fo4MsgType.UseItem);
  assert.ok(t.complete(a, { ok: true, error: "", refId: 3, items: [] }));
  assert.equal((await pa).refId, 3);
  assert.equal((await pa).requestType, Fo4MsgType.CraftItemFo4);
  assert.equal(t.complete(a, { ok: true, error: "", refId: 0, items: [] }), false, "late reply");
  now = 100;
  t.tick();
  const rb = await pb;
  assert.equal(rb.ok, false);
  assert.equal(rb.error, "Timeout");
  const pc = t.track(t.nextNonce(), Fo4MsgType.Barter);
  t.failAll("Disconnected");
  assert.equal((await pc).error, "Disconnected");
  assert.equal(t.pendingCount(), 0);
});

test("RequestTracker never hands out a nonce that is still pending", () => {
  const t = new RequestTracker(() => 0, { firstNonce: 5 });
  void t.track(6, Fo4MsgType.UseItem);
  assert.equal(t.nextNonce(), 5);
  assert.equal(t.nextNonce(), 7);
});

test("stack identity ignores loaded ammo and mod order like the server", () => {
  const a = key(0x1, { mods: [3, 1], ammoLoaded: 5 });
  const b = key(0x1, { mods: [1, 3], ammoLoaded: 12 });
  assert.ok(sameStack(a, b));
  assert.notEqual(stackKey(a), stackKey(key(0x1, { mods: [1, 3], stolenFrom: 9 })));
  assert.notEqual(stackKey(a), stackKey(key(0x1, { mods: [1, 3], condition: 500 })));
});

test("diffInventories removes before adding and merges duplicate stacks", () => {
  const from = [
    { item: key(0x1), count: 2 },
    { item: key(0x1), count: 1 },
    { item: key(0x2, { mods: [7] }), count: 1 },
  ];
  const to = [
    { item: key(0x1), count: 1 },
    { item: key(0x2, { mods: [8] }), count: 1 },
  ];
  const ops = diffInventories(from, to);
  assert.deepEqual(
    ops.map((o) => [o.item.baseId, o.item.mods, o.count]),
    [
      [0x1, [], -2],
      [0x2, [7], -1],
      [0x2, [8], 1],
    ],
  );
  assert.deepEqual(diffInventories(to, to), []);
});

test("EventBus isolates throwing listeners", () => {
  const bus = new EventBus<{ x: number }>();
  const errors: string[] = [];
  bus.onListenerError = (e) => errors.push(e);
  let got = 0;
  bus.on("x", () => {
    throw new Error("boom");
  });
  const off = bus.on("x", (v) => (got += v));
  bus.once("x", (v) => (got += v * 10));
  bus.emit("x", 1);
  bus.emit("x", 1);
  off();
  bus.emit("x", 1);
  assert.equal(got, 12);
  assert.deepEqual(errors, ["x", "x", "x"]);
});

test("buildMessage always produces complete requests", () => {
  for (const t of Object.keys(kMessageDefaults).map(Number)) {
    const m = buildMessage(t as keyof typeof kMessageDefaults) as unknown as Record<string, unknown>;
    assert.equal(m.t, t);
    for (const [k, v] of Object.entries(m)) {
      assert.notEqual(v, undefined, `message ${t} field ${k}`);
    }
  }
  const craft = buildMessage(Fo4MsgType.CraftItemFo4, { recipeId: 9, count: undefined });
  assert.equal(craft.count, 1, "undefined keeps the default");
  const a = buildMessage(Fo4MsgType.WorkshopPlace);
  a.pos[0] = 5;
  assert.equal(buildMessage(Fo4MsgType.WorkshopPlace).pos[0], 0, "defaults are not shared");
});

test("message type range and error text", () => {
  assert.ok(isFallout4MsgType(64));
  assert.ok(isFallout4MsgType(120));
  assert.ok(!isFallout4MsgType(63));
  assert.ok(!isFallout4MsgType(121));
  assert.equal(describeError("MissingComponents"), "You don't have the components.");
  assert.equal(describeError("SomeNewCode"), "Some new code.");
  assert.equal(describeError(""), "");
});
