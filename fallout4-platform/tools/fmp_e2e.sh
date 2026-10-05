#!/usr/bin/env bash
# End-to-end join test without Fallout 4: starts a fallout4 server on a
# synthetic Fallout4.esm, joins it with two headless bots (one walking)
# and checks that each sees the other and that the walker's movement
# reaches the other bot.
#
#   fallout4-platform/tools/fmp_e2e.sh <build dir> [work dir]
set -euo pipefail
BUILD="$(cd "$1" && pwd)"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="${2:-$(mktemp -d)}"
PORT=$(( 20000 + RANDOM % 20000 ))
BOT="${FMP_BOT:-$BUILD/fallout4-platform/fmp_bot}"
SCRIPT="$REPO/falloutmp-client/build/falloutmp-client.js"

[ -f "$SCRIPT" ] || { echo "build the client bundle first: (cd falloutmp-client && npm run bundle)"; exit 2; }

rm -rf "$WORK/srv" && mkdir -p "$WORK/srv/data"
cd "$WORK/srv"
cp -r "$BUILD/dist/server/dist_back" "$BUILD/dist/server/scam_native.node" "$BUILD/dist/server/package.json" .
cp -r "$BUILD/dist/server/data/scripts" data/
"$BUILD/fallout4-platform/fmp_make_test_esm" data/Fallout4.esm
touch gamemode.js
cat > server-settings.json <<JSON
{ "game": "fallout4", "dataDir": "data", "loadOrder": ["Fallout4.esm"], "name": "e2e",
  "port": $PORT, "maxPlayers": 10, "offlineMode": true, "npcEnabled": false }
JSON

node dist_back/skymp5-server.js > server.log 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null || true' EXIT
for _ in $(seq 1 60); do
  grep -aq "listening on" server.log && break
  kill -0 $SERVER 2>/dev/null || { cat server.log; echo "server exited"; exit 1; }
  sleep 0.5
done

"$BOT" --script "$SCRIPT" --port $PORT --profile 1 --walk --seconds 10 > a.json 2> a.log &
A=$!
sleep 2
"$BOT" --script "$SCRIPT" --port $PORT --profile 2 --seconds 6 > b.json 2> b.log
wait $A

node - a.json b.json <<'JS'
const fs = require("fs");
const [a, b] = process.argv.slice(2).map((f) => JSON.parse(fs.readFileSync(f, "utf8")));
const fail = [];
for (const [name, bot] of [["A", a], ["B", b]]) {
  if (!bot.connected) fail.push(`${name} not connected`);
  if (!bot.placed) fail.push(`${name} was never placed by the server`);
  if (bot.jsErrors) fail.push(`${name} had ${bot.jsErrors} script errors`);
  if (bot.puppetsSpawned < 1) fail.push(`${name} saw no other player`);
}
// B left first: A must have removed its puppet
if (a.puppetsDeleted < 1) fail.push("A kept the puppet of B after B left");
// F03: new characters get the editor; B sees A's face (profile 1: male)
if (!a.looksMenuOpened || !b.looksMenuOpened) fail.push("the editor did not open for new characters");
if (!b.puppets.some((p) => p.appearance && p.appearance.hairColorId === 0x1001)) fail.push("B did not get A's appearance");
// F02: A's walk variables and jump events reach B's puppet of A
if (!b.puppets.some((p) => p.graphWrites > 0)) fail.push("B got no graph variables from A");
if (!b.puppets.some((p) => (p.animEvents || []).includes("jumpStart"))) fail.push("B did not replay A's jump");
// B stood still and must have seen A walk
if (!b.puppets.some((p) => p.moves > 10)) fail.push("B did not see A move");
console.log(JSON.stringify({ a, b }, null, 1));
if (fail.length) { console.error("FAIL: " + fail.join("; ")); process.exit(1); }
console.log("PASS: two clients joined, saw each other, synced movement, appearance and animation");
JS
