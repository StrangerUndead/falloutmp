# FalloutMP reference: prior art

> **Purpose.** What every known Fallout 4 multiplayer effort (plus two adjacent projects) actually built, how, and what FalloutMP can reuse. Written for the session that will implement FalloutMP on top of this SkyMP fork.
> **Date.** 2026-10-05. All repositories were cloned and read at the commits pinned in §A.1.
> **Scope.** Everything except animation synchronisation, which has its own reference; animation appears here only in short "brief" notes.
> **Provenance markers.**
> - `[src: ALIAS@commit:path:line]` = read in that repository at that commit (aliases in §A.1).
> - `[web: URL]` = taken from a web page.
> - `[inference]` = my own analysis, not stated by the source.
> - Sentences in a bullet without their own marker inherit the marker of that bullet.
> **Licensing frame.** FalloutMP client/platform = GPLv3, server = AGPLv3, shared libraries = MIT. Reuse verdicts are engineering guidance, not legal advice. Offsets, IDs, struct layouts and algorithms described in prose are treated as *facts* (re-derivable, cite the source); copying *code* is governed by the source licence.

---

## 0. Summary

1. **FO4_Wrld is by far the deepest prior art** (≈100k lines of C++, ≈31k lines of Python, 6 months, AGPL-3.0). It targets exactly one executable (1.11.191) with raw RVAs, no F4SE and no Address Library. It works without Actors for remote players: it injects a NIF body into the scene graph and replicates per-bone rotations. It has solved, with documented dead ends:
   - OMOD weapon assembly on a remote body,
   - power-armor pieces and paint,
   - owner-driven NPC combat with a server HP pool,
   - optimistic container transactions with rollback,
   - a server-commanded save/checkpoint lifecycle.
2. **Fallout Together (TiltedEvolution)** was deleted from the TiltedEvolution tree on **2024-11-03 (PR #729)**, not in Nov 2025. The README mention went in May 2025. The code survives at `TE@d6ce567b` and targeted **OG 1.10.163**. Its most reusable ideas are:
   - remote-actor creation through `IFormFactory` + `Actor::New` + `CreateReferenceAtLocation`,
   - appearance replication through the engine's own **change-form serializer**,
   - projectile relaunch with `bUseOrigin=true`.
3. **Every Address Library ID in Fallout Together is off by one.** Its FO4 `.bin` reader does not skip the 8-byte count header (§3.2.1, §5.2). 0 of 132 IDs match CommonLibF4 exactly; all 50 that can be cross-checked match at `ID−1`.
4. **"F4MP" is two unrelated codebases.**
   - **cokwa/F4MP-Archive** (2020, GPLv3, OG) shipped far more than "movement only": per-bone pose replication, NPC/prop sync, settlement-object sync, weapon-fire relay, client-authoritative PvP damage and a dialogue-line sync hack.
   - **Jous99/F4MP** (2026) revives a *different* F4MP lineage (Unlicense top level, GPLv3 sub-trees). It is early: player-clone bodies via console `placeatme 7`, plus host-authoritative NPC position relay.
5. **Commonwealth Online is closed source.** The public repo is only its website and Supabase backend, with no licence file. Its contributions are product and operations lessons:
   - Iroh NAT traversal,
   - signed directory entries,
   - Argon2id passwords,
   - PrismaUI HTML menus,
   - per-server save profiles.
   It also forked a multi-runtime CommonLibF4 that carries `{OG, NG, AE}` ID triplets (§3.5).
6. **jjnorris/FalloutTogether contains no Fallout code.** It is a pure snapshot of TiltedEvolution `dev` taken after the removal. **vaultmp** is a Fallout 3 project (MIT, last developed in 2015).
7. **Cross-validated layout facts that are stable from OG to AE** (§5.1): `TESObjectREFR` (0xB8…0x100), the `TESNPC` face block (0x248…0x300), actor-value indices (Health 27, AP 1, Rads 58) and `BSAnimationGraphManager+0xD8`.
8. **The biggest unsolved problems** (§5.4):
   - face-morph and body-weight sync on a live actor,
   - PvP hit attribution with FO4's damage pipeline,
   - deterministic leveled actors,
   - power armor on an *actor* puppet,
   - more than 2 simultaneous remote players in practice,
   - workshop sync.

---

## 1. Inventory

| Project | Pinned commit | Last activity | Licence (precise) | Target runtime | Code size | Status |
|---|---|---|---|---|---|---|
| **ThePie88/FO4_Wrld** | `4200f32` (v0.10.0) | 2026-10-02 | **AGPL-3.0** ("Copyright (c) 2026 ThePie88") [src: FO4_Wrld@4200f32:LICENSE:1-4] | **1.11.191 only**, raw RVAs [src: FO4_Wrld@4200f32:README.md:344-350] | C++ 100,243 lines; Python 31,239 lines incl. 32 test files | Active; 2 players tested |
| **tiltedphoques/TiltedEvolution (Fallout Together)** | `d6ce567b` (last tree with FO4 code) | FO4 work 2021-01→2022-09; build fixes until 2024-08; removed 2024-11-03 [src: TE@23e5d407] | **GPL-3.0-or-later**; `code/launcher` LGPL-2.1 (CitizenFX-derived) [src: TE@d6ce567b:LICENSE:1-10]. Depends on Tilted* libraries that are **"All Rights Reserved"** [src: tiltedphoques/TiltedCore, TiltedReverse, TiltedHooks, TiltedUI, TiltedConnect LICENSE:1] | **OG 1.10.163** via `version-1-10-163-0.bin` [src: TE@d6ce567b:Code/client/VersionDb.h:165] | FO4 directory ≈6.3k lines (+13k RTTI) | Abandoned, deleted |
| **cokwa/F4MP-Archive** | `67fd290` (master), `035092f` (develop) | 2020-07-24 | master **GPL-3.0**; develop `server/` (Python) **MIT** (Genetical) [src: F4MP-cokwa@035092f:server/LICENSE:1-3]; librg Apache-2.0, enet MIT, zpl Unlicense/dual | **OG 1.10.163** [src: F4MP-cokwa@67fd290:f4mp/main.cpp:28] | ≈6.7k first-party lines + Papyrus | Archived |
| **Jous99/F4MP** | `9e03c44` | 2026-09-07 | Top level **Unlicense** [src: F4MP-Jous@9e03c44:LICENSE]. `client/` and `server/` each carry a **GPL-3.0** LICENSE inherited from the old F4MPClient/F4MPServer repos. The deprecated upstream README declared "all first party code … public domain" [src: F4MP-Jous@a53006b:README.md:8] | NG/AE claimed; `client-ng` uses libxse CommonLibF4 submodule `a4b283f` (AE-oriented) [inference] | client-ng 1,189 lines; server 1,547 lines | Early, active |
| **G-A-R-D-E-N/Commonwealth-Online** | `7b52878` | 2026-09-29 (site); mod v1.1.0 released 2026-09-22 | **No licence file**; repo holds only Website + Supabase. Mod source is not public (the release repo `CommonwealthOnline-Matrix-Old-Python` returns 404) [web: https://github.com/G-A-R-D-E-N] | AE + OG (claimed) [src: CO@7b52878:Website/views/pages/home.ejs:58] | n/a | Released, closed |
| **jjnorris/FalloutTogether** | `33a4c87` | 2025-09-22 | GPL-3.0 (fork of TE) | none (Skyrim-only tree) | 0 own commits | Snapshot only |
| **Langerz82/vaultmp** | `d33fa8d` (upstream dev ended `f4c84dc`, 2015-02-28) | 2017-06-04 | **MIT**, except listed third-party parts (RakNet BSD, AMX Apache-2.0, CEGUI, …) [src: vaultmp@d33fa8d:LICENSE:1-28] | Fallout 3 1.7 + patched FOSE | — | Dead |

---

## 2. Corrections to `docs/FALLOUT4_PORT_RESEARCH.md` §3.4

| Earlier claim | What the code shows |
|---|---|
| F4MP (cokwa…; Jous99 revival) used librg / GameNetworkingSockets, showed placeholder Protectrons, "got movement only, stalled on animation" | These are two different codebases.<br>• cokwa used **librg/enet**, spawned an ESP actor `"F4MP Player"`, replicated **per-bone local transforms** with interpolation, and synced NPCs, props, workshop objects, weapon fire, hits and dialogue lines [src: F4MP-cokwa@67fd290:f4mp/f4mp.cpp:50-87,1072-1222; f4mp/Character.cpp:22-251].<br>• Jous99 uses **GNS** and a *different* lineage. Its README still says Protectron, but the code now spawns `placeatme 7` (a player clone) [src: F4MP-Jous@9e03c44:client-ng/src/main.cpp:171-173,457]. |
| Fallout Together: "FO4 references removed from the main repo in Nov 2025" | Code removed by PR #729, merged **2024-11-03**: 295 files, −25,475 lines [src: TE@23e5d407]. README sentence removed **2025-05-15** (#780) [src: TE@a2b9994f:README.md] |
| Commonwealth Online: "F4SE + Python relay + Iroh + PrismaUI … PvP" | Consistent with its site. But combat/damage sync is listed as "still in development" in 1.1.0 [src: CO@7b52878:changelogs/1.1.0.md:225-227]. Gameplay transport is **TCP 7777** or Iroh [src: CO@7b52878:Website/views/pages/hosting.ejs:128]. Source is not public. |
| FO4_Wrld: "dxgi proxy … replicates per-bone skeleton transforms" | Correct. In addition, it now has OMOD weapons, power armor, NPC owner election, a server HP pool, containers with rollback and checkpoints (§3.1) |

---

## 3. Project deep dives

### 3.1 FO4_Wrld (ThePie88) — `FO4_Wrld@4200f32`

#### 3.1.1 Identity, runtime, licence

- **Scope and team.** A solo "evening project", 59 commits from 2026-04-25. Target: a "10-player persistent-world survival MMO" [src: FO4_Wrld@4200f32:README.md:6-7].
- **Licence.** AGPL-3.0 [src: FO4_Wrld@4200f32:LICENSE:1-4]. The README also says "Personal mod project. Not distributed." [src: FO4_Wrld@4200f32:README.md:352-356].
  - Many RE dossiers quoted in the changelog are git-ignored, so they are not in the repo [src: FO4_Wrld@4200f32:CHANGELOG.md:1844-1847].
  - The published dossiers are `re/M8P*.txt` and `re/M8_strategic_decision.txt`.
- **Runtime.** `Fallout4.exe` 1.11.191 (Dec 2025 hotfix), image base 0x140000000. Every address is a raw RVA (`sub_14XXXXXXX`) [src: FO4_Wrld@4200f32:fw_native/src/offsets.h:1-11].
  - On a version mismatch the DLL stays inert [src: FO4_Wrld@4200f32:fw_native/src/dll_main.cpp:5-8,110-116].
  - Steam updates are blocked by pinning the app manifest read-only ("Never press Update or Verify") [src: FO4_Wrld@4200f32:CHANGELOG.md:1387-1396].

#### 3.1.2 Architecture

**Loader**
- `dxgi.dll` proxy in the game folder. Since NG, `Fallout4.exe` imports XINPUT1_3 + dxgi + d3d11 and no longer dinput8.
- The proxy forwards `CreateDXGIFactory{,1,2}` to System32 [src: FO4_Wrld@4200f32:fw_native/dxgi.def:1-17; fw_native/src/proxy_exports.cpp:1-60].
- No F4SE. Hooks use MinHook; Frida is used only for RE [src: FO4_Wrld@4200f32:README.md:58-74].

**Threading**
- The game window's WndProc is subclassed. All engine mutation runs on the main thread through posted `WM_APP+N` messages [src: FO4_Wrld@4200f32:fw_native/src/native/scene_inject.h:34-63].
- Colliding `WM_APP` ids caused four silent bugs (T-pose, eaten crouch/pose/door messages) [src: FO4_Wrld@4200f32:CHANGELOG.md:1992-1996,3328-3334,4291-4302].
- A 33 ms `WM_TIMER` keeps the tick alive while the player stands still [src: FO4_Wrld@4200f32:CHANGELOG.md:1036-1040].
- `LoadGame` blocks for about 6 s and pumps messages, so it needs both a load gate and a re-entrancy guard [src: FO4_Wrld@4200f32:CHANGELOG.md:1022-1031].

**Launcher**
- A Python orchestrator plus `FoM.exe`, which writes INI keys [src: FO4_Wrld@4200f32:launcher/fo4_ini.py:52-95]:
  - `bAlwaysActive=1`, `bSkipSplash=1`,
  - `bDisableAutoSave=1`, `bAllowScriptedAutosave=0`, `bAllowScriptedForceSave=0`,
  - `bSaveGameOnQuitToMainMenu=0`, `bWorkshopAutoSaveOnExit=0`.
- Its earlier keys `bAllowAutosaveFromScript` and `bSaveOn*` do not exist in the binary [src: FO4_Wrld@4200f32:CHANGELOG.md:50-54].
- For two clients on one PC: a 1-byte single-instance patch at RVA 0xC2FB62 [src: FO4_Wrld@4200f32:README.md:125-128], and a Frida script that blocks the `steam://` relaunch [src: FO4_Wrld@4200f32:frida/07_bypass_drm.js].

**Transport**
- Own binary UDP protocol: 12-byte header, at most 1400 bytes of payload, and a protocol version in every header, so client, server and launcher must match. Current version is v33 [src: FO4_Wrld@4200f32:fw_native/src/net/protocol.h:189-192,1859-1879].
- Reliable channel: SACK window 32, initial RTO 300 ms doubling on each retransmit. Max retransmits: client 32, server 8 [src: FO4_Wrld@4200f32:net/reliable.py:19-24; fw_native/src/net/reliable.h:101-110].
- The C++ client codec is byte-identical to the Python one (roundtrip tests) [src: FO4_Wrld@4200f32:README.md:80].
- No NAT traversal; "untested over real-world internet routes" [src: FO4_Wrld@4200f32:README.md:282-284].
- Bootstrap is queued, 16 frames in flight and 8 per tick, to stay inside the 32-frame window [src: FO4_Wrld@4200f32:CHANGELOG.md:1017-1020].

**Server model and authority**
- Python asyncio server. It is authoritative for containers, world objects, locks, NPC ownership, NPC HP pools, appearance records and checkpoints [src: FO4_Wrld@4200f32:README.md:38-42].
- Player position is client-reported. A validator caps speed at 2500 u/s and accepts a `cell_id` change as a baseline reset [src: FO4_Wrld@4200f32:net/server/validator.py:32-33,124-158].
- NPC AI runs on one *owner* client per NPC (§3.1.9).

**Identity**
- The launcher holds an Ed25519 key in a ChaCha20-Poly1305 vault wrapped with DPAPI. It signs `"FWAUTH1"‖len‖server_addr‖challenge`; the address binding stops replay against another server. HELLO carries a 144-byte auth tail [src: FO4_Wrld@4200f32:CHANGELOG.md:1714-1738].
- Client id = `fw` + 13 hex characters of the public-key hash. There is also a `net/master` discovery server.
- Resume tokens are single-use, last 24 h and are stored hashed [src: FO4_Wrld@4200f32:CHANGELOG.md:983-986,89-92].

**Persistence** [src: FO4_Wrld@4200f32:CHANGELOG.md:56-101,209-287,376-506; net/server/persistence.py:293-323]
- The server journal is CRC-framed with group fsync. No reliable frame leaves before the journal LSN that covers it is durable. The snapshot is the journal's compaction, written atomically (temp file + fsync + rename + directory fsync).
- One DLL module owns 7 save/load detours. It:
  - refuses every save it did not command and answers with the engine's own "You cannot save right now.",
  - drops quicksave and quickload,
  - refuses uncommanded loads inside the load job.
- The server commands `.fos` "checkpoints" at quiet moments. They are hashed (sha256) and uploaded to the server's TCP "custody" service, so a character follows its player to another PC.
- `net/server/savepos.py` rewrites a `.fos` to move a player:
  - the Player Location global plus the position at the start of the player's change form (inflate, edit, deflate, fix the file-location table),
  - for an interior→exterior move, the sky (mode 1 inside, 3 outside, plus 29 bytes of outdoor data) and the acoustic space, transplanted from a genesis save.
  - It is used for respawn and for the first spawn of a new character [src: FO4_Wrld@4200f32:README.md:142-147].

#### 3.1.3 Remote player representation

**What the ghost is**
- It is **not an Actor**. "Strada B" allocates `NiNode`/`BSFadeNode` with the engine allocator and attaches it under `ShadowSceneNode` [src: FO4_Wrld@4200f32:fw_native/src/native/scene_inject.h:1-22].
- It loads `MaleBody.nif`, `BaseMaleHead.nif` and `MaleHands.nif` through `sub_1417B3E90`, the public NIF loader that bypasses a hanging cache wrapper.
- Every NIF is deep-cloned through `sub_1416BA800` (→ vt[26]). Head and hands must be cloned too, otherwise they share skin instances with the local player and crash on cell entry [src: FO4_Wrld@4200f32:CHANGELOG.md:2677-2722].
- Materials are resolved with the walker `sub_140255BA0`. The body is skinned to a **private clone of `skeleton.nif`** [src: FO4_Wrld@4200f32:CHANGELOG.md:874-882; README.md:129-141].

**Why not an Actor**
- 2026-04-26: the M8 decision chose "PlaceAtMe actor hijack" [src: FO4_Wrld@4200f32:re/M8_strategic_decision.txt:1-24].
- It was later "permanently shelved" [src: FO4_Wrld@4200f32:CHANGELOG.md:4011-4019]. The module remains, dormant: `g_proxy_spawn_disabled{true}` [src: FO4_Wrld@4200f32:fw_native/src/ghost/actor_hijack.cpp:98; fw_native/src/dll_main.cpp:284-287].
- Spawning `PlaceAtMe(PlayerNPC 0x07)` hit "155+ engine special-case branches" that compare `baseForm->formID == 7` and dereference player-only sub-objects (damage, perception, aim, faction, AddTarget). Their workaround was to overwrite `ghost+0xE0` with any vanilla `TESNPC` [src: FO4_Wrld@4200f32:fw_native/src/offsets.h:97-119]. An earlier template was Codsworth `0x1CA7D` [src: FO4_Wrld@4200f32:fw_native/src/offsets.h:1215-1224].

**Consequences**
- Depth, lighting and occlusion come free; shadows do not [src: FO4_Wrld@4200f32:README.md:267-270].
- No collision and no hit target, so no PvP, and NPCs cannot aggro the remote player's ghost [inference]. NPC combat is therefore routed through owner election (§3.1.9).
- Fingers do not articulate: 53 of 80 joints are not in the sender's render tree [src: FO4_Wrld@4200f32:README.md:334-336].
- A second remote ghost is refused until a 3-client test exists [src: FO4_Wrld@4200f32:README.md:277-281].

**Lifecycle**
- A per-peer registry: `PEER_JOIN` only records the need; a main-thread tick injects once stable (120 ticks and ≥2 s, no load, no cell change); teardown runs bottom-up [src: FO4_Wrld@4200f32:CHANGELOG.md:814-845].
- A ghost is built only after the peer's first position arrives [src: FO4_Wrld@4200f32:CHANGELOG.md:507-514].

#### 3.1.4 Appearance

**Recipe**
- Fields: race, sex, hair colour, head parts (sorted), tints (sorted) as `{u16 tint_id, u8 intensity_pct, u32 rgb 0x00BBGGRR}` [src: FO4_Wrld@4200f32:fw_native/src/native/appearance_recipe.h:41-75].
- Text line format v2, `MAX_RECIPE_BYTES` 1280, stored per identity on the server.
- **Morphs and body weights are not carried** [src: FO4_Wrld@4200f32:CHANGELOG.md:1584-1597].

**Layout found at 1.11.191** [src: FO4_Wrld@4200f32:fw_native/src/native/appearance_recipe.h:16-27,36-40]

| Field | Offset |
|---|---|
| Hair colour (`BGSColorForm*`) | `TESNPC+0x248 → +0x00` |
| Head parts (null-terminated) | `+0x2D0` |
| Race | `+0x1B8` |
| Female flag | `+0x70` bit 0 |
| Tint array | `+0x300` |
| Keyed-float map; "undecoded" by FO4_Wrld, but it is `morphSliderValues` (§5.1 B2) | `+0x2F8` |

**Apply only through engine calls**
- Writing the fields directly does nothing until the engine rebuilds from them [src: FO4_Wrld@4200f32:fw_native/src/native/appearance_recipe.cpp:118-171,291]:

| Call | 1.11.191 |
|---|---|
| Add head part | `sub_140655010` |
| Set hair colour | `sub_140654DF0` |
| Set tint `(npc, u16 tintId, float, u32 rgb)` | `sub_14065DAB0` |
| Actor-side tint mirror (the array the compositor reads for the player) | `sub_140D759E0` |
| Rebuild the head: `Actor::Reset3D` with `(actor, 0, 0, 1, 0)` | `0xC73DD0` (asynchronous) |

**Catalogs are walked from live data**
- 420 HDPT records, filtered by the vanilla menu's four tests.
- Hair colours: CLFM flags at `+0x40` (`Playable|RemappingIndex`) separate the 32 real hair colours from 134 tint swatches.
- Tints: `race+0x698+8*sex` → CharGenData, 9 groups / 143 templates / 546 colours. Group order differs per sex, so groups are looked up by name [src: FO4_Wrld@4200f32:CHANGELOG.md:1488-1504; fw_native/src/native/chargen_catalog.cpp:35-55,111-125].

**Tint pitfalls**
- There are two tint arrays.
- Intensity 0 means OFF; the "None" swatch means remove.
- A reproducible apply must first re-apply template defaults, because the delete rule compares against defaults with exact float equality [src: FO4_Wrld@4200f32:CHANGELOG.md:1506-1518].

**Ghost dressing ("borrow")**
- Apply the peer's recipe to the *local* player, let the engine build the head, clone it, then restore [src: FO4_Wrld@4200f32:CHANGELOG.md:1534-1563].
- The composited face textures (diffuse/normal/smoothspec) live in a **global render-target pool (slots 16/17/18)**. They must be duplicated into private textures.
- Body skin colour is computed with `sub_1406555D0`/`sub_1406EED30` [src: FO4_Wrld@4200f32:fw_native/src/native/anatomy_mirror.cpp:670-671].
- The face master held raw bone pointers into the live player rig. Entering or leaving power armor recycled those nodes. Fix: re-point them to a DLL-owned reference `skeleton.nif` [src: FO4_Wrld@4200f32:CHANGELOG.md:889-952].

**Character creation and the server record**
- A forced first-entry ritual: the server sends `chargen_required`; the player is staged 10,000 units up with collision off; `AutoVanityState` camera; ImGui panel [src: FO4_Wrld@4200f32:CHANGELOG.md:1456-1486].
- Pitfall: a joining client published its save's default look and overwrote the server record. Now the client adopts the server recipe first, with a 10 s grace [src: FO4_Wrld@4200f32:CHANGELOG.md:1520-1532].

#### 3.1.5 Movement

- **Sender.** Reads the player singleton (RVA `0x32D2260`) position at `+0xD0` and rotation at `+0xC0` every **50 ms**, ignoring moves under 4 units [src: FO4_Wrld@4200f32:fw_native/src/hooks/player_pos_hook.cpp:5-7; fw_native/src/offsets.h:56,66-67].
  - `POS_STATE` is 36 B (6×f32, u64 timestamp, u32 cell_id). `POS_BROADCAST` is 52 B [src: FO4_Wrld@4200f32:fw_native/src/net/protocol.h:505-516].
  - Snapshots are coalesced to the newest, with a 25 ms send brake [src: FO4_Wrld@4200f32:CHANGELOG.md:996-999].
- **Receiver.** A plain coordinate bind with **no interpolation for players**; "open work" [src: FO4_Wrld@4200f32:README.md:282-284]. A different cell is simply far away, so the ghost leaves the frustum naturally [src: FO4_Wrld@4200f32:CHANGELOG.md:3121-3148].
- **Pose.** Quaternion local rotations over a canonical joint list from `skeleton.nif` (`_skin` anchors filtered), 20 Hz, about 26 KB/s per peer [src: FO4_Wrld@4200f32:CHANGELOG.md:4329-4465].
  - A sentinel `qw=2.0` marks joints missing on the sender.
  - A separate crouch channel carries the COM/Pelvis translation.
- **NPC mirrors** (non-owner) [src: FO4_Wrld@4200f32:CHANGELOG.md:1912-1934,2301-2326]:
  - Pinned through the SetPosition path. During teleport/streaming windows, raw writes go to `Actor+0xD0` and the NIF root, because `sub_140513A80` and vt[202] share a cell-grid spinlock with the streaming thread.
  - The character controller is warped with `sub_141894670` (reached through `sub_140C5C830`; accept both `bhkCharProxyController` and `bhkCharRigidBodyController`).
  - The Havok body is held keyframed with `NiAVObject::SetMotionType` (`0x18763E0`).
  - On ownership handoff the pose is committed with the `Actor::MoveTo` worker `0xC60BE0` (`doProcessUpdate=1`).
  - Locomotion is derived from the position delta → `SpeedSampled` 100/200 plus `Direction`.

#### 3.1.6 Equipment and OMODs

**Sender**
- Detours `ActorEquipManager::EquipObject` `0xCE5900` and `UnequipObject` `0xCE5DA0` (singleton `0x31E3328`), local player only [src: FO4_Wrld@4200f32:fw_native/src/offsets.h:1045-1047; CHANGELOG.md:3951-3960].
- **Argument 4/5/6 order differs between Equip and Unequip.** Getting it wrong produced a 3-day crash class [src: FO4_Wrld@4200f32:CHANGELOG.md:4178-4185].

**EQUIP_OP contents**
- Item form, kind, slot form, count, timestamp, an **effective ARMA priority** (u16) and the **OMOD list inline**.
- The priority is read from `BGSObjectInstance.extra +0x56`. The inventory walk returns a *default* InstanceData instead [src: FO4_Wrld@4200f32:CHANGELOG.md:3562-3599,3336-3348].

**Receiver: armor**
- ARMO addons at `+0x2A8` (count `+0x2B8`, stride 16, priority u16 at `+0`, `ARMA*` at `+8`).
- PrioritySelect re-implemented from `sub_1404626A0`, then gender-aware path scoring, then NIF load and skin re-bind to the ghost skeleton.
- Body cull: ARMO `+0x1E8` bit 3 (slot 33 BODY) → `NIAV_FLAG_APP_CULLED` on the body `BSSubIndexTriShape` [src: FO4_Wrld@4200f32:CHANGELOG.md:3541-3590,3964-3986].

**Receiver: modded weapons**
- `sub_140434DA0(omod, base_fadeNode, placeholder|NULL, flags)` does the per-OMOD attach [src: FO4_Wrld@4200f32:CHANGELOG.md:3269-3307]. It:
  1. reads the OMOD `TESModel` at `+0x50`,
  2. loads, clones and applies materials,
  3. attaches through `sub_14186E960`, which is **BSConnectPoint pairing**: `BSConnectPoint::Children` on the base NIF is matched by string to `::Parents` on the mod NIF.
- The base weapon's TESModel is often `*RecieverDummy.nif`. Fallback: `Weapons\<folder>\<folder>.nif`.
- Verified for every firearm family [src: FO4_Wrld@4200f32:CHANGELOG.md:3192-3225].

**Workarounds and extras**
- **Render lag of one event.** The sender re-equips 50 ms later (EQUIP X / UNEQUIP X / EQUIP X). The receiver ignores UNEQUIP from slot `0x4334D` (`kReadiedWeapon`), which the engine emits transiently [src: FO4_Wrld@4200f32:CHANGELOG.md:3309-3358].
- **Paint/material OMODs.** Parse the property buffer (`omod+0x88`, header `+0x90`, tag 1 = properties, 16-byte records; property 13 = `pwMaterialSwaps`), then apply the `BGSMaterialSwap` per geometry by `.bgsm` name [src: FO4_Wrld@4200f32:CHANGELOG.md:1117-1140; fw_native/src/offsets.h:1266-1316].
- **Outfit at session start.** Items worn when the save loads fire no event. Walk the inventory, take stacks with `flags & 7` that are apparel, and send EQUIP for each. Weapons are not announced (open) [src: FO4_Wrld@4200f32:CHANGELOG.md:746-757; README.md:293-297].

**Dead ends**
- A synthetic REFR + vt[170] (it is only a flag setter).
- The `sub_1404580C0` sync load.
- A `BSModelProcessor` post-hook.
- Name-matched `AttachChild`.
- Shipping mesh blobs: `BSVertexDesc` stride mismatch crashes the GPU path [src: FO4_Wrld@4200f32:CHANGELOG.md:3245-3267,3660-3700].
- A forced boot equip cycle with non-standard arguments (B8) corrupted engine state and froze the client on the Sanctuary bridge [src: FO4_Wrld@4200f32:CHANGELOG.md:2884-2943].

#### 3.1.7 Weapon fire, projectiles, damage, health

- **PvP.** None [inference]; the ghost is not hittable.
- **NPC combat funnels** [src: FO4_Wrld@4200f32:CHANGELOG.md:2445-2505]:

| Funnel | 1.11.191 |
|---|---|
| Per-actor combat brain, `Actor::vt[255]` | `sub_140CCFDF0` |
| `AIProcess::SetCombatTarget` (`Actor+0x380` is only a copy of `AIProcess+0x6C`) | `sub_14087AB30` |
| `Actor::DispatchAttackAction` (every attack type) | `sub_140E6F830` |
| `CombatBehaviorGunFire::DecideAndFire` (single shot only) | `sub_14086FCA0` |
| `ProcessPackages_Movement` | `sub_140CEEC30` |
| Central hit applier (rcx = target Actor) | `sub_140CD2780` |

- `sub_140479680` was mislabelled `FireWeapon` by an agent. It takes a stack buffer.
- **The HP funnel `sub_140CC9650`** is the single chokepoint for every Health delta: weapons, melee, explosions, DoT/poison, radiation/script and falls. Its `delta` is the final post-resist value; its `source` argument decodes the firer [src: FO4_Wrld@4200f32:CHANGELOG.md:2112-2127].
- **Health layout.** The cell at `Actor+0x444` holds 3 modifier floats (0 at full). `max = absolute − sum`. Health is AV index **27**; index 1 is ActionPoints [src: FO4_Wrld@4200f32:CHANGELOG.md:2089-2110].
- **Do not gate `Actor::Kill`.** The `Health<=0 → Kill` decision is duplicated inline in at least 3 per-frame checkers. Instead, clamp absolute Health ≥ 1 inside the funnel detour [src: FO4_Wrld@4200f32:CHANGELOG.md:2093-2098,2137-2146].
- `Actor::Kill` (`0xC612E0`) takes a float as argument 3 and returns a pointer [src: FO4_Wrld@4200f32:CHANGELOG.md:37-40].
- **Server HP pool.** One pool per NPC, keyed by form_id, so it survives handoffs. Claims for an NPC not yet tracked are buffered for 2 s. At 0 the server kills the NPC on every client [src: FO4_Wrld@4200f32:CHANGELOG.md:2129-2157,2054-2057].
- **Player death.** Handled by a server-moved checkpoint reload. The resurrect path was abandoned: the AI process lost its middle-high part, the kill-cam is a VATS mode, and radiation leaked through SetGhost. After respawn, clear Rads (AV 58) [src: FO4_Wrld@4200f32:CHANGELOG.md:289-375,420-426].

#### 3.1.8 Power armor [src: FO4_Wrld@4200f32:CHANGELOG.md:1107-1366]

**The frame**
- A world REFR. Enter = despawn; exit = rebirth with a new `wid`; the server marks it `worn_by` and hands it back when the wearer disconnects [src: FO4_Wrld@4200f32:CHANGELOG.md:1010-1016].
- Only one frame model is recognised (hardcoded form ids) [src: FO4_Wrld@4200f32:CHANGELOG.md:1080].

**Pieces**
- A piece is an ordinary ARMO in the frame inventory (`REFR+0xF8`) with stack flags `0x00`. Mounted means present; there is no separate mounted state.
- Replicas are stocked with the Papyrus `AddItem` worker `sub_1411735A0` and `AttachModToInventoryItem` worker `sub_1411808F0`. The latter refuses stacks with count ≠ 1.
- Condition and core charge are one `ExtraHealth` (type `0x25`), created the engine way: pool alloc, ctor `0x2A3CC0`, write lock, AddExtra `0x272380` [src: FO4_Wrld@4200f32:fw_native/src/offsets.h:925-976].

**Visual**
- The PA ARMA is a placeholder (`AA_Power_Torso` → `ArmorPABody.nif`). The visible mesh is the model OMOD's MODL.
- The ghost skeleton gets 20 PA-only bones grafted in, and its bind pose is retargeted (hand 27 vs 6 units, foot 53 vs 32).

**Station**
- `PowerArmorModMenu` (creator `sub_140AF2400`, destructor `sub_140AF2310`) is *not* ExamineMenu.
- FO4_Wrld polls frames within 1024 units every 500 ms and reports after 3 stable polls.

**Pitfalls**
- A model-DB entry created with the fade-wrap flag gives a `BSFadeNode` root. The engine's biped build then silently produces no geometry. Use the engine's flags `0x2C` (skeleton `0x3D`; bit 23 is the materials-applied latch).
- Headlamps (`AddOnNode158`) live on a global add-on list. Skip them and detach every `BSValueNode` from clones.
- The PA janitor at `+0x109DAA7` deletes frames whose cell unloads.
- `NiNode::AttachChild` growth frees arena-owned arrays. Pre-grow the array instead.
- A TEMPORARY bit saved with a worn frame hung the next load [src: FO4_Wrld@4200f32:CHANGELOG.md:407-419].

#### 3.1.9 NPC sync and AI ownership [src: FO4_Wrld@4200f32:CHANGELOG.md:2206-2423,1851-2028]

- **Pivot.** "Suppress AI everywhere" (B6.5/B6.6) left raiders frozen and immortal [src: FO4_Wrld@4200f32:CHANGELOG.md:2427-2443]. It was replaced by **owner-driven** sync: vanilla AI runs on exactly one client per NPC, and the others mirror it.
- **Election** (server):
  - `threat = 10·engage + 6·proximity + 30·damage`.
  - Hysteresis: MIN_HOLD 2 s, FLIP_MARGIN 1.3, MIN_ABS_DELTA 3, plus a commit band [src: FO4_Wrld@4200f32:net/server/ownership.py:182-246].
- **Observation gate.**
  - S1 InCombat (`Actor+0x2D0 & 0x4000`),
  - S2 `CombatController+0x98`,
  - S3 fired within the last 1.5 s (needed for scripted quest raiders).
- **Per-actor hook.** `Actor::Update_PerFrame` `0xC636A0`.
  - Non-owners still tick but are pinned, and get the mirror-suppress byte `Actor+0x189 = 1`.
  - Opcodes `0x0280`–`0x0291`.
- **Hardening.**
  - The owner-state batch was capped at 17 entries with no rotation, so 12 of 29 NPCs never got positions.
  - Cached bones are pinned through the NiRefObject refcount `+0x08`, with a parent-null probe at `+0x28`.
  - Deaths are recorded server-side and replayed to late observers.
  - At local death the client sends `NPC_UNLOAD` and passes engine writes through during the death window [src: FO4_Wrld@4200f32:CHANGELOG.md:1784-1800,1857-1977].
- **Known gaps** [src: FO4_Wrld@4200f32:CHANGELOG.md:2016-2026; fw_native/src/native/scene_inject.cpp:15034,15361; CHANGELOG.md:558-560]:
  - **Leveled-list divergence.** Placed leveled refs roll a different base and gear per client (`0xFF001AAA` vs `0xFF00105A`).
  - **Mole rat.** Canonical joints are human; `kNpcPoseMinMatch = 1`; the crouch channel writes an absolute translate, which stretched a creature.
  - **Interiors.** The ownership sphere sees NPCs of other cells by their local coordinates.

#### 3.1.10 World state

- **Doors** [src: FO4_Wrld@4200f32:CHANGELOG.md:4209-4289]:
  - Hook the Activate worker `sub_140514180`. `SetOpenState` `sub_140305760` fires 3,527 times at load and 0 times on a keypress.
  - The receiver re-invokes the worker (toggle semantics).
- **Locks and terminals** [src: FO4_Wrld@4200f32:CHANGELOG.md:3020-3071,2738-2755]:
  - Sender: ForceUnlock `0x563320` / ForceLock `0x563360`. These also cover terminal hacks.
  - Receiver: Papyrus `ObjectReference.Lock` binding `sub_141158640`, with `ai_notify=0` to skip the minigame and key use.
  - The server persists state per `(base, cell)`.
- **World-object spawns** [src: FO4_Wrld@4200f32:CHANGELOG.md:1236-1283; fw_native/src/offsets.h:735-856]:
  - Detour the Papyrus `PlaceAtMe` native `0x1159C10` **and** the console worker `0x5E05D0` (the console does not use the native).
  - The server mints a `wid`.
  - Placement: upright, `bhkPickData` ground snap, then cell re-file `0x514C50` (step 4 of the engine's move), gated on the cell being attached.
  - **Removal idiom:** clear no-save `0x4000`, Disable, `RemoveReference`, `GetHandle`, `DestroyByHandle` (`0xC23EC0`).
  - Runtime form ids are recycled (e.g. the Pip-Boy preview REFR). Track objects by engine handle [src: FO4_Wrld@4200f32:CHANGELOG.md:186-199].
- **Globals.** Detour `GlobalVariable.SetValue` native `0x11459E0`. `TESGlobal` value is at `+0x30`; const flag `0x40` [src: FO4_Wrld@4200f32:fw_native/src/offsets.h:139-163; fw_native/src/hooks/worldstate_hook.cpp:5-27].
- **Quests.** Stages are stored and bootstrapped by the server. The client apply through `Quest.SetCurrentStageID` native `0x1185DD0` is pending [src: FO4_Wrld@4200f32:README.md:85; net/server/main.py:576-579].
- **Time and weather.** Parked (B6.11) [src: FO4_Wrld@4200f32:README.md:109].

#### 3.1.11 Containers and inventory [src: FO4_Wrld@4200f32:CHANGELOG.md:581-694,102-208]

- **Key** = the placed REFR form_id, with `(base, cell)` as a drift check. Runtime `0xFF` refs are refused.
  - Exception: a placed actor's corpse is keyed by form_id alone, because its base is a runtime TESNPC that differs per client.
- **Optimistic ops with rollback.** The engine applies the take or deposit at once and the server answers with a verdict.
  - On refusal the client undoes it on the main thread.
  - The server keeps the last 256 verdicts per peer, so a repeated op gets the same answer (idempotency).
- **Convergence.** The server pushes container contents. Clients align with AddItem/RemoveItem at scene settle, cell change and activation.
  - A second pass removes the ammo that AddItem drags along (a 10mm pistol brings 5–9 rounds).
  - An open menu is redrawn with `UpdateList` + the Scaleform `InvalidateLists` invoke.
  - Declare every engine argument as 64-bit unless the decomp proves otherwise.
- **Hooks.**
  - vt[`0x7A`] `AddObjectToContainer`. Actor and PlayerCharacter share this slot, which caused double reports.
  - World pickup through `sub_140500430`.
  - `ContainerMenu` transfer `sub_14103E950` (side 0 = deposit, 1 = withdraw).
  - The container-component signature is `0x544E4343`. A mis-converted constant broke every untouched container for 5 months [src: FO4_Wrld@4200f32:fw_native/src/offsets.h:53,217,283,333].
- **Player inventory capture.**
  - Walk `BGSInventoryList` under its read lock: 24 persisted extra types, vtables validated through RTTI.
  - The OMOD block is behind a pointer at instance-extra `+0x18`.
  - The result was diffed IDENTICAL against a `.fos` parse.
- **Drops.** One world REFR is created per stack, announced when the object is at rest, and dressed through reference-level AttachMod.
- **Open.** Randomness is per client: the first opener decides, and mods are lost through containers.

#### 3.1.12 UI

- **ImGui 1.92.8** draws on the game's own D3D11 device at `Present`. The device is resolved from renderer globals (`0x3A0F410`/`0x38CAAA0`) [src: FO4_Wrld@4200f32:fw_native/src/render/present_hook.cpp:47-48,332-333].
  - Draw on the main thread only: a loading-screen thread also calls Present [src: FO4_Wrld@4200f32:CHANGELOG.md:1436-1445].
- **Input** [src: FO4_Wrld@4200f32:CHANGELOG.md:1447-1454,716-723]:
  - Swallow `WM_INPUT` before the game's raw-input reader; keep key releases.
  - Use the MenuCursor refcount; count `ShowCursor` calls.
  - `Main+0x1D0` is the IsInMenuMode byte.
- **Nametags** use the world root camera. In this engine the **rows** of the world rotation are the camera axes; verified with `NiCamera::WindowPointToRay` `sub_1416D0CA0` [src: FO4_Wrld@4200f32:CHANGELOG.md:727-744].

#### 3.1.13 Animation (brief)

- Bone replication, not event replay.
- First person: `PlayerCharacter` overrides `IAnimationGraphManagerHolder` slot 23 to copy the 1P skeleton onto the 3P skeleton.
- `BSAnimationGraphManager+0xD8` selects the ticked graph. The fix drives the parked 3P graph (`sub_141320430`) [src: FO4_Wrld@4200f32:CHANGELOG.md:1607-1668].

#### 3.1.14 Pitfalls worth remembering

- **Stale engine pointers.** A freed-and-recycled block is still mapped and writable, so SEH does not protect writes. Pin the refcount instead [src: FO4_Wrld@4200f32:CHANGELOG.md:1857-1896].
- **The logger was the stutter.** 0.525 ms per line from flushes [src: FO4_Wrld@4200f32:CHANGELOG.md:1979-1988].
- **`SIO_UDP_CONNRESET`.** Disable it on the Windows server; one `WinError 10054` from a dead peer froze the whole receive path [src: FO4_Wrld@4200f32:CHANGELOG.md:2005-2007].
- **Measure before fixing.** Counters before code: cross-cell ghosts failed because the validator rejected positions, not because of rendering [src: FO4_Wrld@4200f32:CHANGELOG.md:3150-3171].
- **Shutdown and ALT+F4.** An ALT+F4 teardown crash (`0x16632B9`) was twice mistaken for a gameplay crash; mark the teardown in logs [src: FO4_Wrld@4200f32:CHANGELOG.md:1829-1832].

---

### 3.2 TiltedEvolution "Fallout Together" — `TE@d6ce567b`

#### 3.2.1 Identity, runtime and the off-by-one bug

- **Process model.** `FalloutTogether.exe` is a CitizenFX-derived **immersive launcher** that maps `Fallout4.exe` into its own process. The client is linked into it with `/WHOLEARCHIVE` [src: TE@d6ce567b:Code/immersive_launcher/TargetConfig.h:33-38; Code/immersive_launcher/xmake.lua:81-87]. No F4SE.
- **Stack.** Networking is TiltedConnect (a GameNetworkingSockets wrapper); UI is the TiltedUI CEF overlay; the server is entt-based with an `OwnerComponent` per character and a `PartyService` [src: TE@d6ce567b:Code/server/Components/OwnerComponent.h:8-16].
- **Address Library.** Hardcoded to `version-1-10-163-0.bin` ("everyone on fallout 4 uses the same version") [src: TE@d6ce567b:Code/client/VersionDb.h:156-184].
  - The reader assumes the file is (address, id) pairs from byte 0.
  - The real format is `u64 count` followed by `{u64 id, u64 offset}` pairs [src: CLF4-alandtse@ba22620:CommonLibF4/src/REL/IDDB.cpp:106-109; CommonLibF4/include/REL/IDDB.h:12-16].
  - **Every FT ID N therefore resolves to the offset of canonical ID N−1.**
  - Verified: 0 of 132 FT IDs appear in CommonLibF4; every one of the 50 that can be checked appears at N−1 (§5.2). Example: `TESActivateEvent::GetEventSource` is FT 166231, canonical 166230 [src: CLF4-alandtse@ba22620:CommonLibF4/include/RE/Bethesda/Events.h:549].
- **History.** FO4-specific work (anim, equip, projectiles, AI, quests, inventory, weather) ran 2021-01→2022-09; later commits are "fix ft build". Removed in PR #729 [src: TE@23e5d407].

#### 3.2.2 Remote actor spawn and AI suppression

**Base form**
- `TESNPC::Create(buffer, changeFlags)` = `IFormFactory::Create<TESNPC>()`, then `Initialize()`, then `Deserialize` [src: TE@d6ce567b:Code/client/Games/Forms.cpp:55-72,126-139].
- `Initialize()` copies the attack data map, class, combat style, race and outfit from the player base and sets `flags |= 0x200000`.

**Actor creation** [src: TE@d6ce567b:Code/client/Games/References.cpp:568-605; Code/client/Games/Fallout4/Actor.cpp:54-61]
1. `Actor::New()`: game allocator + Actor constructor.
2. `SetSkipSaveFlag` (`unk10 = 0xFFFF`), so the actor is not saved.
3. `SetLevelMod(4)`, `MarkChanged(0x40000000)`, `SetParentCell`, `SetBaseForm`.
4. `ModManager::Spawn` → `TESDataHandler::CreateReferenceAtLocation(handleOut, NEW_REFR_DATA*)` with `refrToPlace = actor` [src: TE@d6ce567b:Code/client/Games/ModManager.cpp:61-81; Code/client/Games/Fallout4/Misc/NEW_REFR_DATA.h:9-28].
5. `ForcePosition`, then `flags &= ~0x200000`.
- Players additionally get `SetIgnoreFriendlyHit(true)` and `SetPlayerRespawnMode()` [src: TE@d6ce567b:Code/client/Services/Generic/CharacterService.cpp:355-452].

**AI suppression for remote actors**
- `HookActorProcess` returns 0, so no AI processing runs.
- Engine `SetPosition`/`RotateX/Y/Z` are blocked; only TE's own scoped writes move the actor [src: TE@d6ce567b:Code/client/Games/References.cpp:940-1007].
- `RunDetection` from a remote player to the local player is cancelled [src: TE@d6ce567b:Code/client/Games/Fallout4/Actor.cpp:395-418].
- AI package sync was tried and disabled (`AI_SYNC 0`) [src: TE@d6ce567b:Code/client/Games/References.cpp:1019-1052].

#### 3.2.3 Appearance via the engine's change-form serializer

- `TESNPC::Serialize` runs the form's own `Save` vfunc into a `BGSSaveFormBuffer`, using the form's change flags; `Deserialize` runs `Load` from a `BGSLoadFormBuffer` [src: TE@d6ce567b:Code/client/Games/Forms.cpp:22-53,94-124; Code/client/Games/SaveLoad.cpp:25-150].
  - Change flags come from an internal lookup on the save-manager singleton.
  - While an override is active, hooks rewrite every form ID written into the buffer as a varint `(modId, baseId)`. The appearance blob is therefore load-order independent.
  - Because the blob is the engine's own change-form data, it carries head parts, tints and morphs (the `CF 0x800`/`0x4000` data in §5.1 B3) [inference].
- **Face rebuild.**
  - `Actor::QueueUpdate` is really `Actor::Reset3D` (§5.2). It is called as `(true, 0, true, 0)` while `bUseFaceGenPreprocessedHeads:General` is temporarily 0 [src: TE@d6ce567b:Code/client/Games/References.cpp:532-550].
  - That setting is also forced to 0 every frame, together with `bAlwaysActive=1` [src: TE@d6ce567b:Code/client/TiltedOnlineApp.cpp:78-92].

#### 3.2.4 Movement interpolation

- Snapshots carry server-clock ticks. The client renders at **current tick − 300 ms**, lerping between the two snapshots around that time [src: TE@d6ce567b:Code/client/Systems/InterpolationSystem.cpp:12-74; Code/client/Services/Generic/CharacterService.cpp:1462].
- It applies the newer snapshot's graph variables (`LoadAnimationVariables`) and writes `middleProcess->direction`.
- Rotation uses a delta-angle lerp; **FO4 forces pitch X = 0**.

#### 3.2.5 Equipment, inventory, projectiles, damage

**Equipment**
- Hooks the internal equip/unequip functions (FT 1474879/1265293). It calls `ActorEquipManager::EquipObject` with `BGSObjectInstance(obj, nullptr)` [src: TE@d6ce567b:Code/client/Games/Fallout4/EquipManager.cpp:14-133].
- Because the instance pointer is null, **OMOD/instance data are never replicated**. The `ObjectEquipParams` layout is in the same file.

**Inventory**
- Walks `BGSInventoryList` (count plus worn = stack flags ≠ 0). Applies with `RemoveAllItems` + `AddObjectToContainer` [src: TE@d6ce567b:Code/client/Games/Fallout4/TESObjectREFR.cpp:25-120].
- No extra data is carried. Struct sizes: `BGSInventoryList` 0x80, `BGSInventoryItem` 0x10 [src: TE@d6ce567b:Code/client/Games/Fallout4/Forms/BGSInventoryList.h; BGSInventoryItem.h].

**Projectiles**
- Sender: hooks `Projectile::Launch(handle*, ProjectileLaunchData&)` (FT 1452335) and relays the launch data [src: TE@d6ce567b:Code/client/Games/Fallout4/Projectiles/Projectile.cpp:81-128].
- A launch whose shooter is remote, and the weapon `Fire` (FT 1056038 = `TESObjectWEAP::Fire`, §5.2), are suppressed for remote shooters [src: TE@d6ce567b:Code/client/Games/Fallout4/Projectiles/Projectile.cpp:133-150].
- Receiver: rebuilds the data with `FromWeapon` from `Actor::GetCurrentWeapon` and `eTargetLimb=-1` [src: TE@d6ce567b:Code/client/Services/Generic/CombatService.cpp:100-192].
  - **`bUseOrigin = true` ("or it'll recalculate it and it desyncs")** [src: TE@d6ce567b:Code/client/Services/Generic/CombatService.cpp:173].
  - `pParentCell` must be valid.
- The FO4 `ProjectileLaunchData` layout is in [src: TE@d6ce567b:Code/client/Games/Fallout4/Projectiles/Projectile.h:20-58].

**Damage**
- The damage hook applies damage only if the target is local or the hitter is the local player. It emits `HealthChangeEvent` and blocks damage to remote players [src: TE@d6ce567b:Code/client/Games/Fallout4/Actor.cpp:326-365].
- Heals go through `ApplyActorEffect`.
- Actor values Rads and RadHealthMax are synced explicitly [src: TE@d6ce567b:Code/client/Games/Fallout4/Actor.cpp:111-134].

#### 3.2.6 Quests, time, weather, menus

- **Quests.** Sinks on `TESQuestStageEvent` and `TESQuestStartStopEvent`; applies with `TESQuest::SetStage`/`SetActive`/`EnsureQuestStarted`; party-leader semantics [src: TE@d6ce567b:Code/client/Games/Fallout4/Forms/TESQuest.cpp:7-50; Code/client/Services/Generic/QuestService.cpp:33-44].
- **Time.** `GameHour`/`TimeScale` globals on the Calendar singleton.
- **Weather.** `Sky` Set/Force/Reset, all marked "TODO(ft): verify". Updates are disabled for non-leaders [src: TE@d6ce567b:Code/client/Games/References.cpp:891-935,1160-1176].
- **Menus.** Clearing `UIMF_PAUSES_GAME`, `UIMF_FREEZE_FRAME_BACKGROUND` and `UIMF_FREEZE_FRAME_PAUSE` on IMenus unpauses them [src: TE@d6ce567b:Code/client/Games/Fallout4/Interface/UI.cpp:56-66].
  - A "fast startup" commit patches raw 1.10.163 addresses to skip the "press any button" engagement prompt [src: TE@2f4a49cf].
- **Tick.** A hook on the Papyrus VM update (FT 759509) runs the app update while the VM is active (`VMContext+0x600`) [src: TE@d6ce567b:Code/client/FalloutVM.cpp:25-61].
- **Animation (brief).** For the player, graph variables are read from graph index 0 (third person) instead of the active index at `+0xD8` [src: TE@fc1cf1ca].

---

### 3.3 F4MP (cokwa) — `F4MP-cokwa@67fd290`

**Architecture**
- An F4SE plugin for OG 1.10.163, a Papyrus quest (`F4MPQuest`) and an ESP that was **never committed** (no `.esp` in any branch) [inference from `git log --all`].
- Transport: librg on enet UDP, port **7779**, 10 ms tick [src: F4MP-cokwa@67fd290:f4mp/f4mp.cpp:50-87; f4mp/scripts/F4MPQuest.psc:140].
- The C++ librg server is a relay that spawns new players at fixed coordinates [src: F4MP-cokwa@67fd290:f4mp_server/Server.h:40-71].
- Messages: Hit, FireWeapon, SpawnEntity, SyncEntity, SpawnBuilding, RemoveBuilding, Speak [src: F4MP-cokwa@67fd290:f4mp/common.h:11-23].
- The tick runs from an F4SE delay functor that reschedules every 1 ms [src: F4MP-cokwa@67fd290:f4mp/f4mp.cpp:301-471].

**Remote players**
- `PlaceActorAtMe(f4mpPlayerBase)` from Papyrus [src: F4MP-cokwa@67fd290:f4mp/scripts/F4MPQuest.psc:26-47].
- Appearance is written onto the **single shared base NPC named "F4MP Player"** [src: F4MP-cokwa@67fd290:f4mp/Player.cpp:36-44], so every remote player shares one base: last writer wins [inference].
- AI: `EnableAI(false)` is commented out. `StopCombat()` is polled instead, presumably relying on the ESP's NPC record [src: F4MP-cokwa@67fd290:f4mp/scripts/F4MPPlayer.psc:57,336-338].

**Appearance**
- Captured from `TESNPC`: sex, the 3 weights, hair colour *by name*, head parts *by `partName` string*, `morphSetValue` (0x2D8), `morphRegionData` (index + 8 floats, 0x2E0) and `morphSetData` (key/value, 0x2F8) [src: F4MP-cokwa@67fd290:f4mp/client.h:31-64].
- Applied by `Heap_Free`-ing and re-allocating those fields. There is no explicit face rebuild, and tints and face textures are TODO [src: F4MP-cokwa@67fd290:f4mp/Player.cpp:242-382].

**Movement and pose**
- Position goes through Papyrus `TranslateTo` with `speed = distance×10` (calling it natively crashed) [src: F4MP-cokwa@67fd290:f4mp/f4mp.cpp:538-566].
- Per-bone **local transforms** (position, quaternion, scale) are double-buffered and interpolated on the receiver [src: F4MP-cokwa@67fd290:f4mp/Character.cpp:22-251].
- Animation (brief): in first person (`GetObjectRootNode() != root`) the sender substitutes pre-recorded third-person clips baked into `Animations.cpp` [src: F4MP-cokwa@67fd290:f4mp/Character.cpp:143-165].

**Equipment**
- `(formType, full NAME string)` pairs, resolved by scanning `arrARMO`/`arrWEAP` for the name. This is fragile [src: F4MP-cokwa@67fd290:f4mp/client.h:69-102; f4mp/f4mp.cpp:492-536].

**Fire and damage**
- The local `weaponFire` animation event becomes a FireWeapon message.
- The receiver places an invisible `F4MPFirePoint` about 100 units in front of the shooter and calls `Weapon.Fire` with damage zeroed through `InstanceData.SetAttackDamage(owner, 0)` [src: F4MP-cokwa@67fd290:f4mp/scripts/F4MPFirePoint.psc:1-33; f4mp/scripts/F4MPPlayer.psc:64-86,342-350].
- PvP damage is **client-authoritative**: the attacker reads `InstanceData.GetAttackDamage`; the server routes the hit to the victim's controlling peer, which calls `DamageValue` [src: F4MP-cokwa@67fd290:f4mp_server/Server.h:145-151; f4mp/scripts/F4MPQuest.psc:60-62].

**NPCs and props**
- The client scans its parent cell.
  - Every non-`0xFF` actor is reported once (SpawnEntity); the first reporter controls it and streams its bones.
  - Props within 500 units send SyncEntity. The server drops a SyncEntity unless the sender is the **closest player**, and drops older timestamps [src: F4MP-cokwa@67fd290:f4mp/f4mp.cpp:1072-1142; f4mp_server/Server.h:161-226].

**Workshop**
- New `0xFF` refs whose base `formType == 36` ("static objects, right?") are broadcast.
- Receivers use `PlaceAtMe_Native` + `SetPosition`/`SetAngle`, keyed by `(owner entity, formID)`, and delete on removal [src: F4MP-cokwa@67fd290:f4mp/f4mp.cpp:760-823,1095-1220].

**Dialogue (experimental, later disabled)**
- `TopicInfo.OnBegin` is registered for about 2,000 hardcoded INFO ids.
- The receiver clones a DIAL from template `0x0022FA98`, patches its info array (`+sizeof(TESForm)+48`, counts at `+64/+68`) and calls `Say` [src: F4MP-cokwa@67fd290:f4mp/f4mp.cpp:825-1002].

**Testing trick**
- Several F4MP client instances run inside one game process, switched with `SetClient` [src: F4MP-cokwa@67fd290:f4mp/f4mp.cpp:1046-1069].

**Branches**
- `develop` holds an unfinished client rewrite (abstracted librg layer) and an MIT Python server wrapper [src: F4MP-cokwa@035092f].

---

### 3.4 F4MP (Jous99 revival) — `F4MP-Jous@9e03c44`

**Lineage**
- The repo started from the *deprecated F4MP umbrella repo*: same Discord as cokwa, credits Alin Octavian and Benjamin Kyd [src: F4MP-Jous@9e03c44:README.md:84-90; F4MP-Jous@a53006b:README.md:1-8].
- The old ASI client (`client/`, pattern scanning + its own D3D11/ImGui hook) is deprecated because pattern scanning could not find the player reliably [src: F4MP-Jous@9e03c44:client/README.md].

**client-ng**
- A CommonLibF4 (libxse) F4SE plugin built with xmake [src: F4MP-Jous@9e03c44:client-ng/xmake.lua].
- UI: F4SE Menu Framework (an ImGui host, DCCStudios) plus HUD markers drawn with `HUDMenuUtils::WorldPtToScreenPt3` [src: F4MP-Jous@9e03c44:client-ng/src/main.cpp:580-698].

**Transport and server**
- GameNetworkingSockets, port 7779, protocol v1. Header: type(2) + size(4) + timestamp(8) [src: F4MP-Jous@9e03c44:client-ng/src/network/NetworkClient.h:17-109].
- The server is a pure relay with a configurable tick (default 60), a Rust-style admin console, a master-server heartbeat and a Docker image [src: F4MP-Jous@9e03c44:docs/ROADMAP.md].

**Bodies**
- Spawned with `Console::ExecuteCommand("player.placeatme 7 1")`, then found about 400 ms later by searching for the nearest unclaimed actor of base `0x7` [src: F4MP-Jous@9e03c44:client-ng/src/main.cpp:171-196,436-465].
- AI is calmed with `InitiateDoNothingPackage()` + `StopCombat()`.
- Despawn: `Disable()` + `SetWantsDelete(true)` + `MarkAsDeleted()`, so ghosts do not pile up in the save [src: F4MP-Jous@9e03c44:client-ng/src/main.cpp:253-265].
- Every remote player looks like the *local* character [src: F4MP-Jous@9e03c44:docs/ROADMAP.md:59-62]. The README's "Protectron" text is stale.

**Movement**
- Sent 60 times a second from a network thread: position, eye heading, speed, moveDir and booleans for sprint/sneak/jump/weapon-drawn [src: F4MP-Jous@9e03c44:client-ng/src/main.cpp:96-137].
- Receiver: exponential smoothing `alpha 0.35` at 60 Hz with `SetPosition`, a teleport above 1500 units, and `SetHeading` [src: F4MP-Jous@9e03c44:client-ng/src/main.cpp:320-352].
- Graph variables: `bIsSynced=true` every tick so the graph honours the `Speed` value that is set, plus `Speed`, `Direction`, `IsSprinting`, `IsSneaking`, `bInJumpState` and hysteresis-filtered `moveStart`/`moveStop`/`SprintStart`/`SneakStart` events [src: F4MP-Jous@9e03c44:client-ng/src/main.cpp:350-412; docs/ROADMAP.md:34].

**Appearance**
- Captured only: sex, race, `bodyTintColorRGBA`, hair colour, up to 16 head parts [src: F4MP-Jous@9e03c44:client-ng/src/main.cpp:39-65].

**NPCs (host-authoritative)**
- A client toggled to "host" broadcasts up to 30 actors within 4000 units, form id < `0xFF000000`, at 10 Hz.
- Other clients find the same NPC by form id, calm its AI and drive it [src: F4MP-Jous@9e03c44:client-ng/src/main.cpp:467-554].
- A "hide self" mode turns a host into an invisible world simulator [src: F4MP-Jous@9e03c44:client-ng/src/main.cpp:29-32].

---

### 3.5 Commonwealth Online — `CO@7b52878` (closed source)

**What is public**
- The website (EJS, static build) and Supabase (accounts, factions, forum, server directory).
- **No mod code and no licence** [src: CO@7b52878:changelogs/1.1.0.md; Website/src/config.js:22-28].
- Nexus mod 107542 [web: https://www.nexusmods.com/fallout4/mods/107542].
- The org also publishes:
  - a **commonlibf4 fork** of the Dear-Modding-FO4 lineage (GPL-3.0 + modding exception) whose `include/RE/IDs.h` carries `REL::VariantID{OG, NG[, AE]}` for 1,166 symbols [src: GARDEN-CLF4@e6ffbe1:include/RE/IDs.h:76];
  - "BGS", a behaviour-graph editor (MIT) [web: https://github.com/G-A-R-D-E-N].
- The CO team's own commits in that fork touch `CombatFormulas::CalcTargetedLimbDamage` (it was wired to the CalcWeaponDamage ID), inventory stack matching and `TESObjectWEAP` [src: GARDEN-CLF4@e6ffbe1 commits 5261456, d753cc8, fd85f49]. This hints at combat and inventory work in progress [inference].

**Architecture (claimed)**
- An F4SE plugin per client [src: CO@7b52878:Website/views/pages/home.ejs:58-61].
- A "Python relay" server; a C++ rewrite is announced [src: CO@6d0cd22:changelogs/1.0.6.md "What comes next"].
- Transports: TCP 7777 for gameplay, UDP 7778 for LAN discovery [src: CO@7b52878:Website/views/pages/hosting.ejs:112-128]. Or **Iroh** (QUIC with direct connection and relay fallback) through a Rust sidecar `co-tunnel.exe`. The invite code is the host's 64-hex EndpointId [src: CO@7b52878:changelogs/1.1.0.md:98-117; Website/views/pages/hosting.ejs:29].
- "Reliable control traffic, snapshot baselines and deltas, per-peer publication and client interpolation"; interest filtered by cell and worldspace [src: CO@7b52878:Website/src/routes/page-config.js:238-248].

**Security**
- Bounded admission framing; Argon2id password verifiers stored as PHC strings; per-EndpointId throttling and persistent bans.
- Directory listings must prove ownership of the EndpointId by challenge and signature.
- Packet size, packet rate and bandwidth caps.
- "Removed unsafe network-triggered weather console execution" [src: CO@7b52878:changelogs/1.1.0.md:143-185].

**Synced (claimed)** [src: CO@7b52878:changelogs/1.1.0.md:58-79]
- "Remote player proxy actors"; position/rotation; locomotion state.
- Body morph, hair colour, head parts, face morphs, tints, complexion.
- Apparel; right-hand weapons with local form resolution and drawn state.
- Shared weather, time and days passed, with a reassignable "world-state owner".

**Onboarding**
- A custom cell `COVault109` with LooksMenu and SPECIAL tracking; per-server save profiles keyed by EndpointId [src: CO@7b52878:changelogs/1.1.0.md:45-56,81-96].

**UI**
- **PrismaUI_F4** (Ultralight HTML), not redistributed. Custom main menu, server browser and controller ownership [src: CO@7b52878:changelogs/1.1.0.md:29-43,206-208].

**Not done**
- Combat/damage, inventory, quests, settlements, NPCs, companions, dialogue, VATS, power armor [src: CO@7b52878:changelogs/1.1.0.md:225-239].

---

### 3.6 jjnorris/FalloutTogether — `FT-jj@33a4c87`

- HEAD is an ancestor of TiltedEvolution `dev`. There are no commits by the fork owner.
- `Code/client/Games` contains only `Skyrim/`.
- Use `TE@d6ce567b` (or the TE branch `falloutTogether` at `b850014`) instead.

### 3.7 vaultmp (Langerz82 fork of foxtacles/vaultmp) — `vaultmp@d33fa8d`

**What it is**
- Fallout 3 (1.7, with a patched FOSE `fose_1_7_vmp.dll`), with leftover New Vegas code [src: vaultmp@d33fa8d:LICENSE:12-28; source/Game.cpp:2145].

**Architecture**
- The launcher `vaultmp.exe` runs the client logic (RakNet). An injected DLL talks to it over named pipes [src: vaultmp@d33fa8d:source/vaultmpdll/vaultmp.cpp:1-5,45-46].
- The DLL executes game script functions by opcode (`FuncLookup`/`CallCommand`, `ExecuteCommand`) [src: vaultmp@d33fa8d:source/vaultmpdll/vaultmp.cpp:791].
- The server derives SQLite databases from the ESMs [src: vaultmp@d33fa8d:source/GameFactory.cpp:33-39]. Scripting is PAWN (AMX) and C++ [src: vaultmp@d33fa8d:source/vaultserver/Script.hpp:151-220].
- Damage formulas were researched [src: vaultmp@d33fa8d:research/formulas/Basic damage.md]. The UI is CEGUI.

**Relevance**
- Architecture parallels SkyMP (server-side ESM data + scripting). Nothing FO4-specific.

---

## 4. Cross-project comparison

| Topic | FO4_Wrld | TE Fallout Together | F4MP cokwa | F4MP Jous99 | Commonwealth Online |
|---|---|---|---|---|---|
| Loader | dxgi proxy, MinHook | own exe loader | F4SE (OG) | F4SE + CommonLibF4 | F4SE |
| Remote player | NIF in scene graph, not an Actor | runtime TESNPC + Actor via CreateReferenceAtLocation | ESP NPC via PlaceActorAtMe (shared base) | console `placeatme 7` | "proxy actors" |
| AI off | n/a (owner model for NPCs) | ActorProcess/SetPosition/Rotate hooks, detection hook | StopCombat poll | DoNothing package + StopCombat | ? |
| Appearance | recipe: parts, hair, tints; borrow + Reset3D | engine change-form serializer + Reset3D | raw TESNPC fields incl. morphs (no rebuild) | captured only | claims incl. morphs |
| Movement | 20 Hz, no interpolation for players | 300 ms interpolation buffer | TranslateTo + bone interpolation | 60 Hz exponential smoothing | interpolation (claimed) |
| Equip / OMOD | full OMOD + PA + paint | no OMOD | by name, no OMOD | none | apparel + right-hand weapon |
| Fire / hit | NPC owner model; no PvP | Projectile::Launch relay | FirePoint + client damage | none | WIP |
| HP | server pool, clamp at funnel | owner-local apply | client value | none | WIP |
| NPCs | owner election by threat | owner = spawner (ownership component) | first reporter + closest-player props | host by form id | none |
| Containers | server, optimistic + rollback | inventory set only | none | none | none |
| Persistence | journal + server-commanded `.fos` checkpoints | — | — | — | per-server save profiles |
| Transport / NAT | own UDP, no NAT | GNS | enet | GNS | TCP + Iroh (NAT traversal) |
| UI | ImGui | CEF | console | ImGui framework | PrismaUI (Ultralight) |

---

## 5. Reusable knowledge for FalloutMP

**Reuse codes**
- **F** — the fact is free to use; re-implement it and cite the source.
- **C-any** — code is usable in the client, the server and MIT libraries (MIT/Unlicense).
- **C-gpl** — code is usable in the GPLv3 client and the AGPLv3 server, not in MIT libraries.
- **C-agpl** — code is usable in the AGPLv3 server. Avoid it in the GPLv3 client: the GPLv3 §13 combination is allowed, but the AGPL obligations stay attached.
- **✗** — no code reuse.

### 5.1 Engine facts

**A. Address Library**

| # | Fact | Value / runtime | Source | Reuse |
|---|---|---|---|---|
| A1 | OG/NG `.bin` format: `u64 count` followed by `{u64 id, u64 offset}` | OG, NG | [src: CLF4-alandtse@ba22620:CommonLibF4/src/REL/IDDB.cpp:106-109] | F; C-any (MIT) |
| A2 | Fallout Together IDs = canonical + 1 | OG | §3.2.1, §5.2 | F |
| A3 | AE reuses the NG ID of unchanged functions; singletons/data get new IDs (≈4.79M range), e.g. ActorEquipManager singleton is 1174340 / 2690994 / 4798287 (OG/NG/AE) | OG/NG/AE | [src: GARDEN-CLF4@e6ffbe1:include/RE/IDs.h:76-79; CLF4-libxse@7c8c6f8:include/RE/IDs.h:75-82] | F; C-gpl |
| A4 | AE plugins must set the `AddressLibrary_1_11_137` independence bit | AE | [src: CLF4-libxse@7c8c6f8:include/F4SE/Interfaces.h:489,513] | F |

**B. Layouts** (stable OG → 1.11.191 → AE wherever three sources agree)

| # | Fact | Value | Source | Reuse |
|---|---|---|---|---|
| B1 | `TESObjectREFR` members | `IAnimationGraphManagerHolder` 0x48, parentCell 0xB8, angle 0xC0, location 0xD0, base 0xE0, loadedData 0xF0 (3D at +0x08), inventoryList 0xF8, extraList 0x100; `TESForm` flags 0x10, formID 0x14, formType 0x1A | [src: TE@d6ce567b:Code/client/Games/Fallout4/TESObjectREFR.h:233-240] (OG); [src: FO4_Wrld@4200f32:fw_native/src/offsets.h:62-93,1490-1491] (1.11.191); [src: CLF4-libxse@7c8c6f8:include/RE/T/TESObjectREFR.h:604-609] (AE) | F |
| B2 | `TESNPC` face block (size 0x308) | headRelatedData 0x248 (hair colour at +0); morphWeight 0x278 (thin/muscular/large); headParts 0x2D0; morphRegionSliderValues 0x2D8 (MRSV, 5 floats); facialBoneRegionSliderValues 0x2E0 (FMRI → 8 floats); numHeadParts 0x2E8; bodyTintColor RGBA 0x2EA; morphSliderValues 0x2F8 (MSDK → MSDV); tintingData 0x300 | [src: F4SE@6f6a7ca:f4se/GameObjects.h:116-246] (1.11.240); [src: CLF4-libxse@7c8c6f8:include/RE/T/TESNPC.h:174-209]; [src: TE@d6ce567b:Code/client/Games/Fallout4/Forms/TESNPC.h:43-121] (OG); [src: FO4_Wrld@4200f32:fw_native/src/native/appearance_recipe.h:16-27] | F |
| B3 | `TESNPC` change-flag bits | 0x800 face (head data, parts, skin colour, tints); 0x4000 weights + morphs; 0x2000000 race; 0x400 class; 0x40000/0x80000 outfits | [src: F4SE@6f6a7ca:f4se/GameObjects.h:124-209 comments] | F |
| B4 | `BSAnimationGraphManager+0xD8` = active graph index (player: 3P graph [0] plus a 1P graph) | OG, 1.11.191 | [src: TE@fc1cf1ca]; [src: FO4_Wrld@4200f32:fw_native/src/hooks/first_person_graph.cpp:123] | F |
| B5 | Actor-value indices into the `ActorValue::Singleton` array | ActionPoints 1, Health 27, RadHealthMax 57, Rads 58 (array OG 405390, NG/AE 2189587) | [src: TE@d6ce567b:Code/client/Games/Fallout4/Forms/ActorValueInfo.h:11,36,65-66]; [src: FO4_Wrld@4200f32:CHANGELOG.md:2089-2092,420-426] | F |
| B6 | HP modifier cell `Actor+0x444` (3 floats); `max = current − sum` | 1.11.191 | [src: FO4_Wrld@4200f32:fw_native/src/offsets.h:2165-2176] | F |
| B7 | Actor state words | `Actor+0x380` = combat target as ObjectRefHandle (copy of AIProcess+0x6C); `+0x2D0` bit 0x4000 = InCombat; `+0x130` bits 17–20 = life state (7 = essential down, not dead) | [src: FO4_Wrld@4200f32:fw_native/src/offsets.h:206-208,1690-1691; CHANGELOG.md:41-42] | F |
| B8 | `BGSInventoryList` and stacks | entries +0x58, count +0x68, lock +0x78, item stride 0x10; stack: next +0x10, extra +0x18, count +0x20, flags +0x24 (`flags & 7` = equipped) | [src: FO4_Wrld@4200f32:fw_native/src/offsets.h:85-90; CHANGELOG.md:1383-1385,750-752] | F |
| B9 | OMOD and instance extra | OMOD formType 0x90, size 0xC8, model at +0x50, properties at +0x88/+0x90, attach parents +0x98, keywords +0xB0; `BGSObjectInstanceExtra` = extra type 0x35, mod block pointer at +0x18, 8-byte records `{formID, attachIdx, rank, flag}` | [src: FO4_Wrld@4200f32:fw_native/src/offsets.h:1266-1381] | F |
| B10 | `TESObjectARMO` / ARMA | ARMO biped slots +0x1E8; addons +0x2A8 / count +0x2B8; InstanceData priority +0x56; ARMA models M3rd +0x50, F3rd +0x90, M1st +0x150, F1st +0x190 | [src: FO4_Wrld@4200f32:fw_native/src/offsets.h:1096-1183] | F |
| B11 | `BSSkin::Instance` | bones_fb +0x10 (count +0x20); bones_pri +0x28 = pointers to `bone+0x70` world matrices (read by the GPU); skel_root +0x48; `BSGeometry+0x140` → instance | [src: FO4_Wrld@4200f32:README.md:136-141; CHANGELOG.md:2662-2667] | F |
| B12 | `NiAVObject` | refcount +0x08 (free ⇔ refcount reaches 0); parent +0x28; local translate +0x60; world rotate +0x70 (rows = axes), translate +0xA0, scale +0xAC; flag bit 23 = materials-applied latch | [src: FO4_Wrld@4200f32:fw_native/src/offsets.h:660-662; CHANGELOG.md:1857-1893,1187-1208] | F |
| B13 | `NEW_REFR_DATA` (0x70) | vtbl, pos 0x8, rot 0x14, base 0x20, cell 0x28, worldspace 0x30, refrToPlace 0x38, +0x68 = 0x1000000, +0x6C = 1 | [src: TE@d6ce567b:Code/client/Games/Fallout4/Misc/NEW_REFR_DATA.h:9-28] (OG) | F; C-gpl |
| B14 | `ProjectileLaunchData` | layout | [src: TE@d6ce567b:Code/client/Games/Fallout4/Projectiles/Projectile.h:20-58] (OG) | F; C-gpl |
| B15 | Character-creation data | CLFM flags +0x40 (Playable 1, RemappingIndex 2), colour +0x30; race tint slot +0x698 + 8×sex | [src: FO4_Wrld@4200f32:fw_native/src/native/chargen_catalog.cpp:35-55,111-125] | F |
| B16 | Misc | `TESGlobal` value +0x30 (const flag 0x40); PlayerCamera state +0x28 (compare with the FirstPersonState vtable); Havok→game units 69.991249 | [src: FO4_Wrld@4200f32:fw_native/src/offsets.h:162-163,631-645,854] | F |

**C. Functions** (OG/NG/AE IDs from §5.2 where known; RVAs are 1.11.191 only)

| # | Function | IDs / RVA | Notes | Source |
|---|---|---|---|---|
| C1 | `TESDataHandler::CreateReferenceAtLocation(handle&, NEW_REFR_DATA&)` | OG 500304, NG/AE 2192301 | Places a pre-allocated Actor | [src: TE@d6ce567b:Code/client/Games/ModManager.cpp:61-81] |
| C2 | `IFormFactory` factories | OG 228366, NG 2689177, AE 4796464 | Create a runtime TESNPC | [src: TE@d6ce567b:Code/client/Games/Forms.cpp:126-139] |
| C3 | `Actor::Reset3D(reloadAll, addFlags, queueReset, exclude)` | OG 302888, NG/AE 2229913; RVA 0xC73DD0 | Face rebuild; TE `(1,0,1,0)` + `bUseFaceGenPreprocessedHeads=0`; FO4_Wrld `(0,0,1,0)`; asynchronous | TE §3.2.3; FO4_Wrld §3.1.4 |
| C4 | `ActorEquipManager::EquipObject` / `UnequipObject` | OG 988029/1292493, NG/AE 2231392/2231395; RVA 0xCE5900/0xCE5DA0 | Argument order differs between the two | §3.1.6 |
| C5 | `BGSObjectInstance` ctor | OG 1095748, NG/AE 2197563 | Pass real instance data to keep OMODs | §5.2 |
| C6 | `TESObjectREFR::ActivateRef`, `AddInventoryItem` | OG 753531/78185, NG/AE 2201147/2200949 | — | §5.2 |
| C7 | `TESObjectWEAP::Fire`; `Projectile::Launch` | OG 1056037, NG/AE 2198960; Launch OG 1452334 (unverified) | — | §3.2.5 |
| C8 | HP funnel | RVA 0xCC9650 | Clamp Health ≥ 1 here rather than gating Kill | §3.1.7 |
| C9 | Per-actor and movement functions | `Actor::Update_PerFrame` 0xC636A0; `MoveTo` worker 0xC60BE0; char-controller warp 0x1894670; `SetMotionType` 0x18763E0; `Kill` 0xC612E0 (float arg 3) | — | §3.1.5, §3.1.7 |
| C10 | Weapon assembly | OMOD attach 0x434DA0; BSConnectPoint attach 0x186E960; NIF load 0x17B3E90; deep clone 0x16BA800; material walker 0x255BA0 | — | §3.1.6 |
| C11 | Papyrus workers | `AddItem` 0x11735A0; `AttachModToInventoryItem` 0x11808F0 (count must be 1); `ExtraHealth` creation idiom | — | §3.1.8 |
| C12 | Locks | ForceUnlock/ForceLock 0x563320/0x563360; Papyrus Lock 0x1158640 (`ai_notify=0`); `REFR_LOCK::SetLocked` OG 157617, NG/AE 2191020 | — | §3.1.10 |
| C13 | Placement and removal | `PlaceAtMe` native 0x1159C10; console worker 0x5E05D0; `DestroyByHandle` 0xC23EC0; re-file 0x514C50 | — | §3.1.10 |
| C14 | Quests and globals | `TESQuest::SetStage` OG 952799, NG/AE 2207743; `GlobalVariable.SetValue` 0x11459E0; `Quest.SetCurrentStageID` 0x1185DD0 | — | §3.1.10, §3.2.6 |
| C15 | Weather and time | `Sky::Singleton` OG 484694, NG/AE 2192448; `ForceWeather` 698558/2208861; `ResetWeather` 6511/2208860; `Calendar::Singleton` 1444952/2689092/4796378 | TE flags these "unverified" | §5.2 |
| C16 | `TESActivateEvent::GetEventSource` | OG 166230 (verified) | Quest stage/start-stop and load-game sources: FT−1 = 540905/1404315/823570 (unverified) | [src: TE@d6ce567b:Code/client/Games/Fallout4/Events/EventDispatcher.h:82-94] |
| C17 | Engine-level graph variable setter | `SetGraphVariableFloat` OG 27400, NG/AE 2214545; RVAs bool/int/float 0x818D60/80/A0 | Animation (brief) | §5.2 |
| C18 | Dev only | Single-instance patch RVA 0xC2FB62 | — | §3.1.2 |

### 5.2 Fallout Together ID correction table

Rows where CommonLibF4 confirms the −1 rule: OG is TE−1; NG/AE come from `GARDEN-CLF4@e6ffbe1:include/RE/IDs.h` (`VariantID`) or `CLF4-alandtse@ba22620` (`RelocationID`).

| FT symbol (TE file) | TE ID | Canonical OG | NG | AE |
|---|---|---|---|---|
| main loop → `Main::OnIdle` (FalloutVM.cpp:50) | 633525 | 633524 | 2228917 | 2228917 |
| `IFormFactory` factories (FormFactory.cpp:10) | 228367 | 228366 | 2689177 | 4796464 |
| `BGSSaveLoadManager` singleton (SaveLoad.cpp:19) | 1247321 | 1247320 | 2697802 | 2697802 |
| `TESForm::GetFormByNumericID` (Forms.cpp:16) | 796115 | 796114 | 2193092 | 2193092 |
| `BGSSaveLoadGame` singleton (Forms.cpp:113) | 177948 | 177947 | 2697789 | 2697789 |
| `TES` singleton (TES.cpp:8) | 1194836 | 1194835 | 2698044 | 2698044 |
| `ProcessLists` singleton (TES.cpp:16) | 1569707 | 1569706 | 2688869 | 4796160 |
| `TESObjectREFR::GetHandle` (References.cpp:162) | 1573131 | 1573130 | 2201196 | 2201196 |
| `Actor::GetLevel` (References.cpp:520) | 661618 | 661617 | 2229734 | 2229734 |
| "QueueUpdate" = `Actor::Reset3D` (References.cpp:545) | 302889 | 302888 | 2229913 | 2229913 |
| `PlayerCharacter` singleton (References.cpp:659) | 303411 | 303410 | 2690919 | (libxse 4798212) |
| `TESObjectREFR::GetLock` (References.cpp:679) | 930786 | 930785 | 2202648 | 2202648 |
| `Sky` singleton / `ResetWeather` / `ForceWeather` | 484695 / 6512 / 698559 | 484694 / 6511 / 698558 | 2192448 / 2208860 / 2208861 | same as NG |
| SetPosition = `TESObjectREFR::SetLocationOnReference` (References.cpp:1139) | 1101833 | 1101832 | 2201138 | 2201138 |
| `TESObjectREFR::AddLockChange` (References.cpp:1154) | 1578707 | 1578706 | 2200731 | 2200731 |
| `TESObjectWEAP::Fire` (Projectile.cpp:93) | 1056038 | 1056037 | 2198960 | 2198960 |
| `TESDataHandler` singleton (FormManager.cpp:7) | 711559 | 711558 | 2688883 | 4796135 |
| `BSStringPool::Entry::Release` | 1204431 | 1204430 | 2268720 | 2268720 |
| `BGSObjectInstance` ctor | 1095749 | 1095748 | 2197563 | 2197563 |
| `TESQuest::SetStage` | 952800 | 952799 | 2207743 | 2207743 |
| `ActorValue` array | 405391 | 405390 | 2189587 | 2189587 |
| `ActorEquipManager` singleton / `EquipObject` / `UnequipObject` | 1174341 / 988030 / 1292494 | 1174340 / 988029 / 1292493 | 2690994 / 2231392 / 2231395 | 4798287 / 2231392 / 2231395 |
| `TESObjectREFR::ActivateRef` / `AddInventoryItem` | 753532 / 78186 | 753531 / 78185 | 2201147 / 2200949 | same as NG |
| `UI` singleton | 548588 | 548587 | 2689028 | 4796314 |
| `GameVM` singleton | 996228 | 996227 | 2689134 | 4796420 |
| `TaskQueueInterface` singleton (TE called it WeatherManager) | 7492 | 7491 | 2698331 | 2698331 |
| `BSGraphics::GetRendererData` / `Renderer::End` (Present) | 1235450 / 700870 | 1235449 / 700869 | 2704429 / 2276834 | same as NG |
| `MemoryManager::Allocate` / `Deallocate` | 652768 / 1582182 | 652767 / 1582181 | 2267872 / 2267874 | same as NG |
| `Calendar` singleton | 1444953 | 1444952 | 2689092 | 4796378 |
| `BGSCreatedObjectManager` singleton | 1000679 | 1000678 | 2689006 | 4796296 |
| `INISettingCollection` singleton | 791184 | 791183 | 2704108 | 2704108 |
| `NiCamera::WorldPtToScreenPt3` | 109442 | 109441 | 2270344 | 2270344 |
| `TESDataHandler::CreateReferenceAtLocation` | 500305 | 500304 | 2192301 | 2192301 |
| `REFR_LOCK::SetLocked` | 157618 | 157617 | 2191020 | 2191020 |
| `SubtitleManager` singleton / `ShowSubtitle` | 740512 / 875509 | 740511 / 875508 | 2689088 / 2249542 | 4796374 / 2249542 |
| `SetGraphVariableFloat` | 27401 | 27400 | 2214545 | 2214545 |
| `SendHUDMessage::ShowHUDMessage` | 1163006 | 1163005 | 2222440 | 2222440 |

The other 82 FT IDs (Actor ctor/dtor, the damage function, the projectile Launch function, the equip hooks 1474879/1265293, `RemoveAllItems`, `DropObject`, the save buffers, …) are unverified. Apply −1 and confirm against the Address Library before using them [inference]. The full list is in `TE@d6ce567b`: grep `POINTER_FALLOUT4`.

### 5.3 Adopt / Avoid

**Adopt**
1. **Actors for remote players, built TE-style.** Per-player runtime `TESNPC` (C2, with player defaults), `Actor::New` + `CreateReferenceAtLocation` (C1), skip-save flag.
   - **Never use the player base 0x7** as a template: FO4_Wrld documents 155+ player special-cases, and Jous99 inherits them.
   - This also keeps PvP, collisions, ragdolls and power armor possible, which FO4_Wrld's ghost rules out.
2. **Suppress AI and engine motion on remote actors in the engine, not in Papyrus.** TE's ActorProcess/SetPosition/Rotate/RunDetection hooks. Also FO4_Wrld's character-controller warp and keyframed motion type for pinned NPCs.
3. **Appearance.** Ship the TESNPC change-form blob with remapped form IDs (TE). The data is the B2/B3 fields; morphs included.
   - Rebuild through `Actor::Reset3D` with `bUseFaceGenPreprocessedHeads=0`.
   - Validate against FO4_Wrld's tint lessons (two arrays, defaults, intensity 0). The server should keep a parsed, validated recipe as well, as FO4_Wrld does.
4. **Item identity = base + OMOD list (+ instance extras: health, priority).**
   - Equip with a real `BGSObjectInstance`.
   - Announce worn items at session start.
   - Filter the transient `kReadiedWeapon` unequip.
   - Strip the ammo that AddItem drags along.
5. **Projectiles.** TE relay with `bUseOrigin=true`, plus server-side validation in SkyMP's hit model. Suppress `Fire` on puppets.
6. **NPCs.** SkyMP host model, plus FO4_Wrld's threat-based owner election with hysteresis, death release/replay, and a server HP pool with an HP≥1 clamp at the engine HP funnel.
7. **Containers.** Keep SkyMP's server-first model. Borrow FO4_Wrld's keys (placed form_id + (base, cell) check; refuse runtime refs; corpse by form_id), idempotent op ids and verdict cache.
8. **Saves.** Own the save/load lifecycle: refuse uncommanded saves and loads, force the INI keys. Use FO4_Wrld's `.fos` position-rewrite knowledge for spawning and respawning into a template save, which matches SkyMP's `template.ess` approach.
9. **Robustness patterns.** Engine-handle tracking instead of runtime form ids; refcount pinning of nodes; removal idiom C13; main-thread marshalling with one central message-id table.
10. **Menus.** Unpause by clearing IMenu flags (TE).
11. **Runtime IDs.** Use CommonLibF4 `VariantID{OG, NG, AE}` tables (Dear-Modding/libxse lineage) instead of raw RVAs. Port FO4_Wrld RVAs by name-matching against `IDs.h` and confirming with the 1.11.191 Address Library.
12. **Hosting and security.**
    - Optional NAT traversal (Iroh/QUIC relay, CO).
    - Signed directory entries.
    - Argon2id server passwords.
    - Ed25519 launcher identity bound to the server address (FO4_Wrld) for offline or community servers.

**Avoid**
- Raw single-runtime RVAs; dxgi-proxy-only loading without a runtime check.
- A scene-graph ghost as the *player* body: no hits, no PvP, no PA actor path, and a huge RE surface. Keep it only as an idea for first-person animation.
- Console `placeatme` plus a nearest-actor search (race-prone); a single shared base NPC for everyone (cokwa).
- Looking up items, hair colours or head parts by display-name strings (cokwa). Use the SkyMP `FormDesc` / mod-index mapping.
- Client-authoritative damage values (cokwa); gating `Actor::Kill` (FO4_Wrld lesson); "suppress AI everywhere".
- Leaving leveled lists to each client's RNG; SkyMP's server evaluation already avoids this.
- Mesh-blob shipping; forced equip cycles with non-standard arguments (B8).
- TCP for gameplay [inference]; executing console commands built from network data (CO removed this).
- The Tilted* support libraries (All Rights Reserved). Treat TiltedEvolution code as GPL "design reference".
- Copying FO4_Wrld code into the GPLv3 client. Use its facts.
- Using Fallout Together IDs without the −1 correction.

### 5.4 Open questions no prior art solved

1. **Live morph and weight sync.** Does Reset3D after writing `morphSliderValues`/`facialBoneRegionSliderValues`/`morphRegionSliderValues`/`morphWeight` (B2) rebuild a *live* actor correctly? FO4_Wrld skipped morphs, cokwa never rebuilt, CO is closed, and TE's serializer path was never validated on FO4.
2. **PvP on FO4.** Hit attribution per limb, projectile versus hitscan, VATS, server-side parity with `CombatFormulas` (CalcWeaponDamage/CalcTargetedLimbDamage), sneak, crits.
3. **Deterministic leveled actors and placed leveled refs.** TPTA template semantics; same visuals on every client.
4. **Power armor for an actor puppet.** Enter the furniture, race/skeleton switch, piece transfer, fusion-core drain. FO4_Wrld solved it only for the ghost.
5. **More than 2 remote players.** Nobody has run it: FO4_Wrld refuses a second ghost, and CO's "16" is a target.
6. **Workshop/settlement sync** with budget, power and ownership (cokwa naive, FO4_Wrld open, CO planned).
7. **Companions, dialogue/scenes, quest progression** with vanilla scripts running.
8. **VATS and Pip-Boy** in a shared clock; time scale with sleep/wait/menus (TE/CO only partial).
9. **Interest management in interiors**, where local coordinates collide (FO4_Wrld §3.1.9).
10. **Cross-runtime sessions** (OG + NG + AE clients together, claimed by CO) and a policy for Steam auto-updates.
11. **Server-side ammo, reload and fire-rate validation.**
12. **Creature pose and skeleton schemas** if any bone replication is used (the mole-rat problem).
13. **A full `.fos` writer.** FO4_Wrld only rewrites positions and sky.

---

## Appendix A

### A.1 Aliases and pinned commits

| Alias | Repository | Commit |
|---|---|---|
| FO4_Wrld | https://github.com/ThePie88/FO4_Wrld | 4200f32ee3fa09cc77086955dbe4a0da19f9b8b1 |
| CO | https://github.com/G-A-R-D-E-N/Commonwealth-Online | 7b52878bbf19b7a985fe8de789c72418e6267796 (1.0.6 changelog at 6d0cd22) |
| GARDEN-CLF4 | https://github.com/G-A-R-D-E-N/commonlibf4 | e6ffbe1b1660aee420193112e694da2b0e227b86 |
| F4MP-cokwa | https://github.com/cokwa/F4MP-Archive | 67fd2900b4af1f9b871a6f062e4116bc666b12b9 (develop 035092f423fc91e980c18b21de287cf9a57cdb5d) |
| F4MP-Jous | https://github.com/Jous99/F4MP | 9e03c443398df26b4407b6c5a00b69e9f741e709 (first upload a53006b) |
| TE | https://github.com/tiltedphoques/TiltedEvolution | d6ce567b7225d2b30a65daebde3dfe83479eb0d9 (pre-removal); removal 23e5d407d2751336189bfc6b5d6b74701d6d8a6c; README a2b9994fcac8637e80d0875e2a247056dc20b145; branch falloutTogether b850014949ab6356aef2e1bbaa116fb7f862bd9f; HEAD 4917189f6ac382e874f9ee735c4fef6a350e449e |
| FT-jj | https://github.com/jjnorris/FalloutTogether | 33a4c87a092b07f8f4d303cad9777664364a195b |
| vaultmp | https://github.com/Langerz82/vaultmp | d33fa8df6c19285c255341e6f205af0725109b25 |
| CLF4-alandtse | https://github.com/alandtse/CommonLibF4 (MIT) | ba22620e455dffaf1d7021c16b0728dc28f1ddae |
| CLF4-libxse | https://github.com/shad0wshayd3/CommonLibF4 (libxse line; GPL-3.0 + modding exception) | 7c8c6f8a349ac85abf6fc8a7e5a71732d9339954 |
| F4SE | https://github.com/ianpatt/f4se (no licence grant; facts only) | 6f6a7caa9aeaebc957a70d934b6ab6b80eb768b2 (`CURRENT_RELEASE_RUNTIME` 1.11.240) |

To re-fetch: `git clone --filter=blob:none <url> && git checkout <commit>`. TiltedEvolution's FO4 code is under `Code/client/Games/Fallout4` at `d6ce567b`.

### A.2 How the ID cross-check was done

1. Extracted every `POINTER_FALLOUT4(type, name, id)` in `TE@d6ce567b` (132 entries).
2. Looked up `id` and `id−1`:
   - in `REL::ID(...)` and `RelocationID(og, ng)` in CLF4-alandtse,
   - in `REL::VariantID{og, ng[, ae]}` in GARDEN-CLF4.
3. Result: 0 exact hits; 50 distinct symbols hit at `id−1`, with matching semantics in every case (e.g. EquipObject ↔ `ActorEquipManager::EquipObject`).
