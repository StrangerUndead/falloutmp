#!/usr/bin/env bash
# Joins this server with two headless test clients (no game needed): one
# walks in a circle, the other stands still and must see it move.
# Run it on the server machine while the server is running:
#   /opt/falloutmp-server/tools/test-join.sh
# The bots log in with profile ids 900001 and 900002 and get characters
# at the start point like real players.
set -u
DIR="$(cd "$(dirname "$0")" && pwd)"
PORT=$(node -e 'try{console.log(require(process.argv[1]).port||7777)}catch(e){console.log(7777)}' "$DIR/../server-settings.json")
HOST="${1:-127.0.0.1}"
BOT="$DIR/fmp_bot"
JS="$DIR/falloutmp-client.js"
OUT="$(mktemp -d)"

echo "Joining $HOST:$PORT with two test clients (15 seconds)..."
"$BOT" --script "$JS" --host "$HOST" --port "$PORT" --profile 900001 --walk --seconds 15 > "$OUT/a.json" 2> "$OUT/a.log" &
A=$!
sleep 3
"$BOT" --script "$JS" --host "$HOST" --port "$PORT" --profile 900002 --seconds 10 > "$OUT/b.json" 2> "$OUT/b.log"
wait $A

node - "$OUT/a.json" "$OUT/b.json" <<'JS'
const fs = require("fs");
const [a, b] = process.argv.slice(2).map((f) => { try { return JSON.parse(fs.readFileSync(f, "utf8")); } catch { return null; } });
if (!a || !b) { console.log("FAIL: a test client crashed"); process.exit(1); }
const ok = (c, t) => console.log((c ? "  ok   " : "  FAIL ") + t);
ok(a.connected && b.connected, "both clients connected");
ok(a.placed && b.placed, `the server spawned them (at ${b.spawnPos.map(Math.round).join(", ")} in ${b.worldOrCell.toString(16)})`);
ok(a.puppetsSpawned > 0 && b.puppetsSpawned > 0, "each client saw the other player");
ok(b.puppets.some((p) => p.moves > 10), "the walking client's movement reached the other");
ok(a.puppetsDeleted > 0, "the other player disappeared when it left");
const pass = a.connected && b.connected && a.placed && b.placed && a.puppetsSpawned > 0 && b.puppetsSpawned > 0 && b.puppets.some((p) => p.moves > 10);
console.log(pass ? "PASS: the server is joinable" : "FAIL: see the logs in " + require("path").dirname(process.argv[2]));
process.exit(pass ? 0 : 1);
JS
