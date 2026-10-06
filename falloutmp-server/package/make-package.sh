#!/usr/bin/env bash
# Builds the server package: falloutmp-server-linux-x64.tar.gz, unpacking to
# falloutmp-server/. It holds no server-settings.json, data or world files,
# so unpacking it over an existing install updates the program only.
#
#   falloutmp-server/package/make-package.sh <build dir> [output .tar.gz]
set -euo pipefail
BUILD="$(cd "$1" && pwd)"
OUT="$(realpath -m "${2:-falloutmp-server-linux-x64.tar.gz}")"
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
SRV="$BUILD/dist/server"
CLIENT_JS="$REPO/falloutmp-client/build/falloutmp-client.js"
BOT="$BUILD/fallout4-platform/fmp_bot"

for f in "$SRV/dist_back/falloutmp-server.js" "$SRV/scam_native.node" "$CLIENT_JS" "$BOT"; do
  [ -f "$f" ] || { echo "missing $f (build the server, fmp_bot and the client bundle first)" >&2; exit 1; }
done

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
DST="$TMP/falloutmp-server"
mkdir -p "$DST/tools" "$DST/dist_back" "$DST/data"

cp "$SRV/dist_back/falloutmp-server.js" "$SRV/dist_back/falloutmp-server.js.map" "$DST/dist_back/"
cp "$SRV/scam_native.node" "$SRV/package.json" "$DST/"
cp "$HERE/start.sh" "$HERE/falloutmp.service" "$HERE/server-settings.example.json" "$DST/"
cp "$HERE/tools/test-join.sh" "$BOT" "$CLIENT_JS" "$DST/tools/"
cp "$REPO/docs/falloutmp/guides/install-guide.md" "$DST/INSTALL.md"
chmod +x "$DST/start.sh" "$DST/tools/test-join.sh" "$DST/tools/fmp_bot"
git -C "$REPO" rev-parse --short HEAD > "$DST/VERSION" 2>/dev/null || echo unknown > "$DST/VERSION"

tar czf "$OUT" -C "$TMP" falloutmp-server
echo "Wrote $OUT"
