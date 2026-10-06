#!/usr/bin/env bash
# Starts the FalloutMP server from this folder.
cd "$(dirname "$0")"

if [ ! -f server-settings.json ]; then
  cp server-settings.example.json server-settings.json
  echo "Created server-settings.json from the example. Edit it (name, port, rules), then start again." >&2
  exit 1
fi

for f in $(node -e 'console.log((require("./server-settings.json").loadOrder || ["Fallout4.esm"]).join(" "))'); do
  if [ ! -f "data/$f" ] && [ ! -f "$f" ]; then
    echo "Missing data/$f. Copy it from your Fallout 4 Data folder (Steam/steamapps/common/Fallout 4/Data)." >&2
    exit 1
  fi
done

exec node dist_back/falloutmp-server.js
