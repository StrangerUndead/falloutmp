# OPS — Server Packaging, Master Server, Releases

- [ ] **OPS-001** Docker image for the FalloutMP server (Node 22 + `scam_native.node` + dist), with docs on mounting the user-provided `data/` (Fallout4.esm etc.) — S — Depends: BUILD-001 — Verify: L-int
- [ ] **OPS-002** Master server / server list (ADR-017): reuse `MasterClient` protocol; sessions for online login; signed listings — L — Depends: — — Verify: L-int — **Needs user decision** on hosting (Q-05)
- [ ] **OPS-003** Server admin deployment guide (ports 7777/UDP + UI port, settings, DB drivers, backups) — S — Depends: OPS-001 — Verify: —
- [ ] **OPS-010** Release process: semantic versions per component (platform, client, server), changelog, Nexus packaging, runtime compatibility matrix — S — Depends: BUILD-008 — Verify: —
- [ ] **OPS-011** Optional NAT traversal / relay for community hosting (Iroh/QUIC idea from Commonwealth Online) — L — post-1.0
