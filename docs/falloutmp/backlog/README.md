# Backlog — Infrastructure Workstreams

Feature implementation tasks live in each feature spec (`features/Fxx-*.md`, IDs `Fxx-Tnn`). This folder holds the **infrastructure** work that features depend on.

| Prefix | File | Scope |
|---|---|---|
| ENV | [ENV.md](ENV.md) | Dev environment, bootstrap, CI (Linux/Windows), toolchains |
| BUILD | [BUILD.md](BUILD.md) | CMake/vcpkg game switch, CommonLibF4 port, packaging/dist |
| REF | [REF.md](REF.md) | Game-pluggability refactors of shared code (Skyrim stays green) |
| ESPM | [ESPM.md](ESPM.md) | libespm Fallout 4 support (game detection, ESL, FO4 records, strings, BA2) |
| PVM | [PVM.md](PVM.md) | papyrus-vm Fallout 4 support + server-side FO4 natives |
| DATA | [DATA.md](DATA.md) | Save files (.fos), archives utilities outside libespm |
| PLAT | [PLAT.md](PLAT.md) | `fallout4-platform` (F4SE plugin: Node, reflection, hooks, natives, UI) |
| NET | [NET.md](NET.md) | Protocol, message registry, transport fixes |
| SRV | [SRV.md](SRV.md) | Server core systems for FO4 (GameProfile impl, AV store, effects, perks, OMOD stats, lag compensation) |
| CLI | [CLI.md](CLI.md) | `falloutmp-client` fork core (non-feature services, test harness) |
| FRONT | [FRONT.md](FRONT.md) | `falloutmp-front` UI |
| GM | [GM.md](GM.md) | Public default gamemode `falloutmp-gamemode` |
| QA | [QA.md](QA.md) | Self-test plugin, test scripts, load/soak tests |
| OPS | [OPS.md](OPS.md) | Server packaging, master server, releases |
| DOCS | [DOCS.md](DOCS.md) | User/admin/gamemode docs, licensing audit |

**Task line format:**

```
- [ ] **ID** Title — Size — Depends: … — Verify: … — Files: …
  - Accept: …
```

Sizes, verification codes and status markers are defined in [../README.md](../README.md) §3–4.
