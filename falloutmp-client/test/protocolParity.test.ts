// Cross-language drift guard: parses the C++ protocol headers and checks
// that the TypeScript mirror has the same message ids, the same field names
// (the server rejects JSON with a missing field), the same optional fields
// and the same enum values.
import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as path from "node:path";
import { test } from "node:test";
import * as codes from "../src/services/messages/codes";
import { kMessageDefaults, kNestedDefaults, kOptionalFields } from "../src/services/messages/fo4Messages";
import { Fo4MsgType } from "../src/services/messages/msgType";

const repoRoot = path.resolve(__dirname, "..", "..", "..");
const read = (rel: string) => fs.readFileSync(path.join(repoRoot, rel), "utf8");
const messagesH = read("falloutmp-server/cpp/messages/Fo4Messages.h");
const msgTypeH = read("falloutmp-server/cpp/messages/MsgType.h");
const fo4Dir = "falloutmp-server/cpp/server_guest_lib/fo4/";

interface CppStruct {
  name: string;
  keys: Set<string>;
  optional: Set<string>;
  msgType?: string;
  nested: Map<string, Set<string>>;
}

function keysOf(text: string): Set<string> {
  return new Set(Array.from(text.matchAll(/\.Serialize\(\s*"(\w+)"/g), (m) => m[1]));
}

function parseStructs(text: string): Map<string, CppStruct> {
  const out = new Map<string, CppStruct>();
  const lines = text.split("\n");
  for (let i = 0; i < lines.length; i++) {
    const head = /^struct (\w+)/.exec(lines[i]);
    if (!head) continue;
    let j = i;
    while (j < lines.length && lines[j] !== "};") j++;
    const body = lines.slice(i, j + 1);
    const nested = new Map<string, Set<string>>();
    const outer: string[] = [];
    for (let k = 0; k < body.length; k++) {
      const n = /^ {2}struct (\w+)/.exec(body[k]);
      if (n) {
        let e = k;
        while (e < body.length && body[e] !== "  };") e++;
        nested.set(n[1], keysOf(body.slice(k, e + 1).join("\n")));
        k = e;
        continue;
      }
      outer.push(body[k]);
    }
    const outerText = outer.join("\n");
    const viaMacro = /FO4_MSG_TYPE\((\w+)\)/.exec(outerText);
    const viaTemplate = /ContainerOpFo4<\w+, MsgType::(\w+)>/.exec(outerText);
    out.set(head[1], {
      name: head[1],
      keys: keysOf(outerText),
      optional: new Set(Array.from(outerText.matchAll(/std::optional<[^>]+>\s+(\w+)/g), (m) => m[1])),
      msgType: viaMacro?.[1] ?? viaTemplate?.[1],
      nested,
    });
    i = j;
  }
  // Derived container ops inherit their fields from the template
  const tmpl = out.get("ContainerOpFo4");
  for (const s of out.values()) {
    if (s.keys.size === 0 && tmpl && s.msgType) {
      s.keys = tmpl.keys;
    }
  }
  return out;
}

function parseCppEnum(text: string, header: RegExp): Map<string, number> {
  const m = header.exec(text);
  assert.ok(m, `enum ${header} not found`);
  const start = text.indexOf("{", m.index + m[0].length);
  const end = text.indexOf("}", start);
  const body = text
    .slice(start + 1, end)
    .replace(/\/\/[^\n]*/g, "")
    .split(",")
    .map((s) => s.trim())
    .filter(Boolean);
  const out = new Map<string, number>();
  let next = 0;
  for (const entry of body) {
    const [rawName, rawValue] = entry.split("=").map((s) => s.trim());
    let value = next;
    if (rawValue !== undefined) {
      const shift = /^(\d+)\s*<<\s*(\d+)$/.exec(rawValue);
      value = shift ? Number(shift[1]) << Number(shift[2]) : Number(rawValue);
    }
    out.set(rawName.replace(/^k(?=[A-Z])/, ""), value);
    next = value + 1;
  }
  return out;
}

function tsEnumEntries(e: Record<string, string | number>): Map<string, number> {
  const out = new Map<string, number>();
  for (const [k, v] of Object.entries(e)) {
    if (typeof v === "number") out.set(k, v);
  }
  return out;
}

const structs = parseStructs(messagesH);
const cppTypes = new Map<string, number>(
  Array.from(msgTypeH.matchAll(/^\s*(\w+) = (\d+),/gm), (m) => [m[1], Number(m[2])] as [string, number]),
);

test("every Fallout 4 MsgType value matches the C++ registry", () => {
  for (const [name, value] of tsEnumEntries(Fo4MsgType as unknown as Record<string, string | number>)) {
    assert.equal(cppTypes.get(name), value, `MsgType::${name}`);
  }
  for (const [name, value] of cppTypes) {
    if (value >= 64 && !name.startsWith("k")) {
      assert.equal((Fo4MsgType as unknown as Record<string, number>)[name], value, `TS mirror lacks ${name}`);
    }
  }
});

test("every C++ Fallout 4 message has a TS mirror with identical fields", () => {
  let checked = 0;
  for (const s of structs.values()) {
    if (!s.msgType || s.name === "ContainerOpFo4") continue;
    const id = cppTypes.get(s.msgType);
    assert.ok(id !== undefined, `unknown MsgType ${s.msgType}`);
    const factory = (kMessageDefaults as Record<number, (() => object) | undefined>)[id];
    assert.ok(factory, `no TS defaults for ${s.name} (${id})`);
    const tsOptional = new Set((kOptionalFields as Record<number, string[] | undefined>)[id] ?? []);
    const tsKeys = new Set(["t", ...Object.keys(factory()), ...tsOptional]);
    assert.deepEqual([...tsKeys].sort(), [...s.keys].sort(), `fields of ${s.name}`);
    assert.deepEqual([...tsOptional].sort(), [...s.optional].sort(), `optional fields of ${s.name}`);
    for (const opt of tsOptional) {
      assert.ok(!(opt in factory()), `${s.name}.${opt} is optional and must not be in the defaults`);
    }
    checked++;
  }
  assert.equal(checked, Object.keys(kMessageDefaults).length, "TS mirrors a message the C++ side doesn't have");
});

test("nested protocol structs match", () => {
  const nested = new Map<string, Set<string>>();
  for (const s of structs.values()) {
    if (!s.msgType && s.name !== "ContainerOpFo4") nested.set(s.name, s.keys);
    for (const [n, k] of s.nested) nested.set(n, k);
  }
  for (const [name, factory] of Object.entries(kNestedDefaults)) {
    const cpp = nested.get(name);
    assert.ok(cpp, `C++ has no nested struct ${name}`);
    assert.deepEqual(Object.keys(factory()).sort(), [...cpp].sort(), `fields of ${name}`);
  }
});

test("enum mirrors match the server", () => {
  const pa = read(fo4Dir + "PowerArmor.h");
  const locks = read(fo4Dir + "Locks.h");
  const workshop = read(fo4Dir + "Workshop.h");
  const data = read(fo4Dir + "Fo4Data.h");
  const pairs: [string, Map<string, number>, Record<string, string | number>][] = [
    ["PaPhase", parseCppEnum(pa, /enum class PaPhase\b/), codes.PaPhase],
    ["PowerArmorSlot", parseCppEnum(data, /enum class PowerArmorSlot\b/), codes.PowerArmorSlot],
    ["LockResultCode", parseCppEnum(locks, /enum class LockResultCode\b/), codes.LockResultCode],
    ["HackResultCode", parseCppEnum(locks, /enum class HackResultCode\b/), codes.HackResultCode],
    ["WorkshopEditOp", parseCppEnum(workshop, /enum class WorkshopEditOp\b/), codes.WorkshopEditOp],
    ["EffectKind", parseCppEnum(read(fo4Dir + "Effects.h"), /enum class EffectKind\b/), codes.EffectKind],
    ["PaTransitionKind", parseCppEnum(messagesH, /enum Kind : uint8_t/), codes.PaTransitionKind],
    ["ProgressionOp", parseCppEnum(messagesH.slice(messagesH.indexOf("struct ProgressionRequestMessage")), /enum Op : uint8_t/), codes.ProgressionOp],
    ["WorkshopManageOp", parseCppEnum(messagesH.slice(messagesH.indexOf("struct WorkshopManageMessage")), /enum Op : uint8_t/), codes.WorkshopManageOp],
    ["PartyOp", parseCppEnum(messagesH.slice(messagesH.indexOf("struct PartyActionMessage")), /enum Op : uint8_t/), codes.PartyOp],
    ["EquipOp", parseCppEnum(messagesH.slice(messagesH.indexOf("struct UpdateEquipmentFo4Message")), /enum Op : uint8_t/), codes.EquipOp],
  ];
  for (const [name, cpp, ts] of pairs) {
    assert.deepEqual(
      [...tsEnumEntries(ts)].sort(),
      [...cpp].sort(),
      `enum ${name}`,
    );
  }
  const perms = parseCppEnum(workshop.slice(workshop.indexOf("namespace WorkshopPerm")), /enum : uint16_t/);
  assert.deepEqual(new Map(Object.entries(codes.WorkshopPerm)), perms, "WorkshopPerm");
  const moveFlags = parseCppEnum(read(fo4Dir + "Movement.h").slice(read(fo4Dir + "Movement.h").indexOf("namespace MoveFlag")), /enum : uint16_t/);
  assert.deepEqual(new Map(Object.entries(codes.MoveFlag)), moveFlags, "MoveFlag");
  const owner = parseCppEnum(workshop, /enum class Type : uint8_t/);
  assert.deepEqual([...tsEnumEntries(codes.WorkshopOwnerType)].sort(), [...owner].sort(), "WorkshopOwnerType");
});

test("actor value ids match fo4/ActorValues.h", () => {
  const av = parseCppEnum(read(fo4Dir + "ActorValues.h"), /namespace Av \{\s*enum : FormId/);
  for (const [name, value] of Object.entries(codes.Av)) {
    assert.equal(av.get(name), value, `Av::${name}`);
  }
});
