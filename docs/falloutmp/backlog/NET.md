# NET — Protocol, Message Registry, Transport

Context: [01-sync-standard.md](../01-sync-standard.md) §5–7; [reference/skymp-sync-inventory.md](../reference/skymp-sync-inventory.md) §1.2, §1.7, §4.5(a); [reference/skyrim-coupling-index.md](../reference/skyrim-coupling-index.md) §5.3.

- [ ] **NET-001** Protocol prefix per game: `GameProfile::ProtocolPrefix()` (`"7_"` / `"fo4-1_"`); client plugin `CreateClientEx(host, port, prefix)` alongside `CreateClient` — S — Depends: REF-003 — Verify: L-unit, L-int
  - Accept: a Skyrim-prefix bot cannot connect to an FO4 server, and vice versa.
- [ ] **NET-002** Per-game message registration in `MessageSerializerFactory` (`CreateMessageSerializer(GameId)`); raise `MsgType::Max`; enforce ranges (34–63 reserved, < 123) with a static_assert — M — Depends: NET-001 — Verify: L-unit
  - Accept: Skyrim packets are byte-identical before and after; FO4 types are rejected by a Skyrim server.
- [ ] **NET-003** Templated payload structs (`CreateActorT<Appearance, Equipment, AvMap>`, `InventoryT<ExtraData>`) shared by Skyrim and FO4 twins — M — Depends: NET-002 — Verify: L-unit
- [ ] **NET-004** Client reliable = RELIABLE_ORDERED (I2), or a sequence counter on each reliable stream — S — Depends: — — Verify: L-int
- [ ] **NET-005** Inventory/ammo-changing messages sent reliable (I3) — S — Depends: NET-002 — Verify: L-unit
- [ ] **NET-006** `SendMessageToActorListeners` honours `reliable` (I4) — S — Depends: — — Verify: L-unit
- [ ] **NET-007** Rejection-correction framework: helper to send the authoritative state on every reject (SetInventoryFo4, UpdateProperty, Teleport2, state keyframe) (I12) — M — Depends: NET-002 — Verify: L-unit
- [ ] **NET-008** Bounds-checked `BitStreamInputArchive` (size limits on strings/vectors; integral-constant checks) (I20) — S — Depends: — — Verify: L-unit (fuzz-style tests)
- [ ] **NET-009** Unknown message types: server logs (rate-limited), client ignores and logs (I21) — S — Depends: — — Verify: L-unit, L-ts
- [ ] **NET-010** Session string table (graph-variable names, event names, keyword EDIDs) distributed on connect; ids in hot messages — S — Depends: NET-002 — Verify: L-unit
- [ ] **NET-011** Per-message-type bandwidth/rate metrics (Prometheus) and a bot-driven bandwidth test — S — Depends: — — Verify: L-int
- [ ] **NET-012** Protocol documentation generator: dump the registry (id, name, fields, reliability) into `docs/falloutmp/reference/protocol.md` from code — S — Depends: NET-002 — Verify: L-unit
- [ ] **NET-013** Per-client packet and byte rate limits with violation windows (e.g. 120 pkt/s, 256 KiB/s, disconnect after 3 violating windows; tune by load test), connect-attempt limiter, pending-connection cap — S — Depends: — — Verify: L-unit, L-int — (pattern from Commonwealth Online, reference/prior-art.md §3.5.1)
