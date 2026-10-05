# OPS — Server Packaging, Master Server, Releases

- [ ] **OPS-001** Docker image for the FalloutMP server (Node 22 + `scam_native.node` + dist), with docs on mounting the user-provided `data/` (Fallout4.esm etc.) — S — Depends: BUILD-001 — Verify: L-int
- [ ] **OPS-002** Master server / server list (ADR-017): reuse `MasterClient` protocol; sessions for online login; signed listings; directory rules: literal IPv4 equal to the observed source, no hostnames, private ranges only in dev mode, TTL registrations (prior-art §3.5.1) — L — Depends: — — Verify: L-int — **Needs user decision** on hosting (Q-05)
- [ ] **OPS-003** Server admin deployment guide (ports 7777/UDP + UI port, settings, DB drivers, backups); ship a non-root systemd unit with `Restart=on-failure`, graceful SIGTERM that flushes persistence (with an integration test), and port-troubleshooting docs (pattern from prior-art §3.5.2) — S — Depends: OPS-001 — Verify: —
- [ ] **OPS-010** Release process: semantic versions per component (platform, client, server), changelog, Nexus packaging, runtime compatibility matrix — S — Depends: BUILD-008 — Verify: —
- [ ] **OPS-011** Optional NAT traversal / relay for community hosting (Iroh/QUIC idea from Commonwealth Online) — L — post-1.0
- [ ] **OPS-012** Optional desktop host GUI that wraps the server process (start/stop, settings, player list, logs, kick/ban), like Commonwealth Online's Qt host (prior-art §3.5.2) — M — post-1.0
