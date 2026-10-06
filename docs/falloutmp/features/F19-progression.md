# F19 — Progression: XP, Levels, SPECIAL, Perks, Bobbleheads & Magazines

| Field | Value |
|---|---|
| Tier | T1 |
| Target level | L4 |
| SkyMP analogue | Learned spells (`learnedSpells`, `ReadBook`, `ReadBookEvent`) — SkyMP level L4 for that narrow case; Skyrim skill XP is client-side (`disableSkillAdvanceService.ts`). No server-side leveling exists in SkyMP |
| Milestone | M7 (XP, levels, SPECIAL, creation SPECIAL), M9 (full perk chart via SRV-021, bobbleheads/magazines) |
| Workstreams | SRV, CLI, PLAT, NET, GM |
| Depends on | F08 (SPECIAL AVs, derived HP/AP/carry), SRV-010, SRV-020, SRV-021 (perk engine), SRV-060 (profile record), F11 (kill attribution), F24/F15/F22/F26 (XP sources), F03 (creation flow), ESPM-009 (PERK, BOOK), ESPM-011 (CTDA) |
| References | reference/fo4-systems-combat-character.md §11, §1.4 (player progression hooks), §1.5 (Papyrus), §1.6 item 3, §18 R5/R6/R12; reference/fo4-data-formats.md §4.18 (PERK); reference/fo4-systems-world-economy.md S19 (creation SPECIAL); 02-architecture.md ADR-010, ADR-020 |

## 1. Summary
Every player has an XP total, a level, seven SPECIAL stats and a set of perk ranks. All of them are decided and stored by the server. The server awards XP for kills (shared by everyone who did enough damage), lockpicks, hacks, crafting, building, discoveries and gamemode quests, and applies Intelligence and perk multipliers. A level-up grants one point. The player spends it in the vanilla perk chart (`LevelUpMenu`); the choice is only a **request**, and the server checks the point, the level, SPECIAL requirements and the rank chain before granting it. New characters distribute 21 SPECIAL points once. Bobbleheads and magazines are per-player collectibles that permanently raise SPECIAL or grant perk ranks. Perks feed the server perk engine, so damage, prices and crafting gates are computed the same for everyone.

## 2. Vanilla Fallout 4 behaviour
- **SPECIAL:** start at 1 each + 21 points (28 total), each 1–10. "You're SPECIAL!" gives +1 point. Each level gives one point, spent on a perk rank or +1 SPECIAL (max 10). Bobbleheads give +1 permanently, even past 10 (combat-character §11.1) [web: https://fallout.fandom.com/wiki/Fallout_4_SPECIAL].
- **XP curve:** to-next = `75·n + 125`; threshold `XP_n = 37.5 n² + 87.5 n − 124` (level 2 at 201 XP). No vanilla cap; the engine crashes at 65 535 [web: https://fallout.fandom.com/wiki/Level].
- **XP sources:**
  - kills: target level + XP offset (ACBS +0x04, OMOD target 514; the formula is unknown → R5);
  - locks Novice 6 / Advanced 12 / Expert 17 / Master 22 (may include INT, verify);
  - terminal hacks (tiered), crafting/building, discovery, speech checks, quests.
- **XP multipliers:**
  - INT +3 %/point;
  - Idiot Savant ×3/×5 random;
  - Well Rested +10 %; Lover's Embrace +15 %;
  - Survival kills ×2;
  - companion kills count only if the player did ≥ ~25 % damage.
  - Entry points `ModExperience` 0x16, `ModKillExperience` 0x52, `ModExperienceLocation` 0x78, `ModExperienceSpeech` 0x79.
- **Perks:** PERK `DATA {trait, level, numRanks, playable, hidden}`; each rank is a separate PERK form chained by `NNAM`; level-up conditions as CTDA (SPECIAL ≥ N, previous rank) (fo4-data-formats §4.18). F4SE `Perk.IsEligible/GetNumRanks/GetNextPerk`. Effects (`PRKE`) are Quest+Stage, Ability (SPEL) or Entry Point.
- **Engine/menus:**
  - `LevelUpMenu` (perk chart), `SPECIALMenu`;
  - `PlayerCharacter::perkCount`, `SelectPerk`, `SetPerkCount`;
  - events `LevelIncrease::Event`, `PerkPointIncreaseEvent`, `XPChangeData`;
  - `Actor::RewardExperience(amount, direct, target, weapon)` (combat-character §1.4).
- **Papyrus:**
  - `Game.RewardPlayerXP(amount, abDirect)` (direct = ignore entry points and INT);
  - `Game.AddPerkPoints`, `Game.GetXPForLevel`, `Game.ShowSPECIALMenu`;
  - `Actor.AddPerk(perk, notify)`, `HasPerk`, `RemovePerk`, `GetLevel`;
  - `Perk.OnEntryRun`.
- **Magazines:** BOOK "Teaching: Add Perk". **Bobbleheads:** the mechanism (item script vs data) is unverified (R12).
- **Single-player assumptions that break:**
  - progression belongs to the one `PlayerCharacter`;
  - `RewardPlayerXP` targets "the player";
  - kill XP assumes one player plus the companion rule;
  - the perk chart commits locally;
  - bobbleheads and magazines are unique world items, so only one player could ever collect them.

## 3. SkyMP baseline
- SkyMP's closest persistent progression is `learnedSpells` (spell tomes via `ReadBook` → `ReadBookEvent`, persisted, validated on equip) [src: MpActor.cpp:1070; gamemode_events/ReadBookEvent.*]. Skyrim skill and level advance is disabled on the client (`disableSkillAdvanceService.ts`); gamemodes do their own perks (`sweetTaffy*PerksService.ts`).
- **Reuse:**
  - the "book teaches something, server-validated, persisted" pattern;
  - `ConditionsEvaluator` for requirements;
  - the property system for owner-only data (S8);
  - `GameModeEvent` for veto.
- **New:** an authoritative progression model, XP awarding, a request/response protocol, and perk-engine integration (ADR-020).

## 4. Design

### 4.1 Authority model
Class A for everything:
- XP, level, SPECIAL base values, pending points, perk ranks, collected bobbleheads/magazines, XP multipliers.
- The client never awards XP or perks. Its menus produce `ProgressionRequest` intents.
- Local engine progression (vanilla XP gain, level-up) is disabled (CLI-021). The local values are only a mirror set from `ProgressionUpdate`.

### 4.2 Server state & persistence
Stored in the per-player **profile record** (SRV-060, ADR-010), section `characters[<actor FormDesc>].progression`. One character per profile by default; the map allows gamemodes with several. Saved on the next tick after each change (§8 rule 4).

| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| XP total | u32 | `progression.xp` | profile record | 0 |
| Level | u16 | `progression.level` | profile record | 1 |
| SPECIAL base | u8[7] | `progression.special` | profile record (F08 reads these as AV bases) | 1 each |
| Creation done | bool | `progression.specialAllocated` | profile record | false |
| Unspent perk points | u16 | `progression.perkPoints` | profile record | 0 |
| Pending SPECIAL points ("You're SPECIAL!") | u8 | `progression.pendingSpecialPoints` | profile record | 0 |
| Perk ranks | map perkChainRoot FormDesc → rank | `progression.perks` | profile record | empty |
| Bobblehead bonuses | map AV → +n | `progression.bonusSpecial` | profile record | empty |
| Collected | set FormDesc (bobblehead/magazine refs or base) | `progression.collected` | profile record | empty |
| Damage attribution for kill XP | read from F11 `MpActor::damageLedger` (single source shared with F12/F13) | F11 | no | — |
| NPC level/perks | from ESM/F13 | `npcLevel`, NPC_ `PRKR` | F13 | ESM |

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `ProgressionRequest` (90) | C→S | `nonce`, `kind` (selectPerk, allocateSpecial, creationSpecial), `perkFormId?`, `av?` (SPECIAL AVIF), `values?[7]` | R | user action; ≤ 5/s | new (registry) |
| `ProgressionUpdate` (89) | S→C | `nonce?`, `error?`, `full` (bool), `level`, `xp`, `xpForNext`, `perkPoints`, `pendingSpecialPoints`, `special[7]` (base incl. bonuses), `perksSet[] {perkFormId, rank}`, `perksRemoved[]`, `collectedAdded[]`, `xpEvents[] {amount, source u8, targetId?}`, `specialAllocated` | R | on change (coalesced per tick); full on login and on every reject | new (registry) |
| `UpdateProperty` (7) | S→C | public `level` | R | on level change | reused |
| `CreateActorFo4` (64) | S→C | owner: full progression; others: `level` only | R | subscribe | reused |
| `ChangeValuesAv` (72) | S→C | derived AVs after level/SPECIAL/perk changes (F08) | R | on change | reused |

`xpEvents.source`: kill, lockpick, hack, craft, build, discover, quest, gamemode, other.

### 4.4 Client capture (owner side)
- Hook `PlayerCharacter::SelectPerk` (platform, F19-T07). Cancel the local effect and send `ProgressionRequest{selectPerk, perkFormId}`. A SPECIAL increase in the chart → `allocateSpecial{av}`. Keep the chart open with an optimistic "pending" state until the reply.
- **Creation:** after F03 accepts the appearance in `create` mode, the client opens `SPECIALMenu` (`Game.ShowSPECIALMenu`). On confirm it reads the 7 values and sends `creationSpecial{values}`. The menu's local commit is cancelled (or overwritten by the reply).
- "You're SPECIAL!" (book) and bobblehead/magazine activations travel through normal F06/F07 activation/pickup. The server recognises them (§4.6).
- Vanilla XP gain, level-up and `RewardPlayerXP` on the client are suppressed (CLI-021; hook `Actor::RewardExperience` on the local player to no-op).

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
- **Owner, on `ProgressionUpdate`:**
  - set the level and XP display through platform natives (`setPlayerLevelAndXp`, F19-T08);
  - set the perk count (`SetPerkCount`);
  - set the SPECIAL bases (F08 AV mirror);
  - `AddPerk`/`RemovePerk` for each changed rank, so engine-visible effects work locally (lockpick tiers, crafting menu gates, carry weight). Server-relevant results stay server-side (SRV-021).
  - Show `xpEvents` as the vanilla XP HUD message (`ShowHUDMessage`, prior-art §5.2).
- **Reject:** `ProgressionUpdate{nonce, error, full:true}` → the client reverts the chart (removes any optimistically added perk, restores the count).
- **Login/reconnect:** the full update in the `isMe` snapshot is applied after F00 world entry and before the HUD becomes visible.
- **Remote players:** only `level` (nameplates, F31). Remote perk effects that matter visually come through other features (e.g. F05 equipment, F20 effects).
- **Respawn:** no change (progression survives death unless a gamemode rule says otherwise).

### 4.6 Validation & anti-cheat
- **XP awards** (`ProgressionService::AwardXp(actor, base, source, ctx)`):
  1. `direct` awards skip multipliers;
  2. otherwise `amount = base × (1 + 0.03·INT) × PerkEngine(ModExperience / ModKillExperience / ModExperienceLocation / ModExperienceSpeech, ctx)` × survival mult (F20) × `xp.rate` setting [inference: INT applied as an additive % on the base; verify with R5];
  3. `onXpGain(actor, amount, source)` may modify or block;
  4. add, then level up while `xp ≥ XP_{level+1}`: one point per level, `level ≤ progression.maxLevel` (default 65534);
  5. on level-up, F08 recomputes derived AVs; fire `onLevelUp` (observe).
- **Kill XP:** when F12 reports a death, every player with damage share ≥ `xp.killShareMinPct` (25 %) in F11's damage ledger (`MpActor::damageLedger`) gets kill XP for the target; F19 never keeps its own ledger. Companion damage counts for its owner (F21). The base value comes from the target level and race/ACBS XP values; the exact formula is calibrated by R5 (G-self logging of `RewardExperience`).
- **Lock/hack/craft/build/discover:** awarded by F24/F15/F22/F26 calling `AwardXp` with their source and base value (GameProfile table). Clients cannot trigger these directly.
- **`selectPerk`:** reject with a full update if any check fails:
  - `perkPoints ≥ 1`;
  - the PERK is playable and not hidden;
  - it is the next rank of an owned chain (rank 1 if none owned; `NNAM` chain resolved from the root);
  - `level ≥ DATA.level`;
  - CTDA conditions pass (SPECIAL ≥ N etc., SRV-021 `ConditionsEvaluator`, ESPM-011);
  - gamemode `onPerkSelect` allows.
  - Apply: `perkPoints −= 1`, rank +1, then notify SRV-021 (entry points) and SRV-020 (ability spells as constant effects).
- **`allocateSpecial`:** a point is available (`perkPoints` or `pendingSpecialPoints`) and base < 10 (bobblehead bonuses do not count toward the 10 cap); `onSpecialAllocate` allows.
- **`creationSpecial`:** only while `specialAllocated == false`; each value 1–10; sum = 28 (GameProfile `creationSpecialTotal`); `onSpecialAllocate` allows. Then set `specialAllocated`.
- **Bobbleheads/magazines** (`progression.collectibles = perPlayer` (default) | `shared`):
  - activating or picking up a known collectible ref (GameProfile table built from the ESM: bobblehead MISC/stand refs, BOOK "Teaching: Add Perk" magazines; R12 confirms the mechanism) applies the effect once per character: bobblehead +1 SPECIAL bonus or its skill effect (SRV-020 constant), magazine perk rank +1 up to `numRanks`;
  - `perPlayer`: the world ref stays for others, and the client hides it for players who have collected it (per-player visibility from `collectedAdded`). `shared`: vanilla, first come.
- **Rate limits and idempotency:** duplicate nonces return the cached reply. More than 5 requests/s are dropped with a full update.
- **Server-only AVs:** `ChangeValuesAv` from a client touching SPECIAL, Experience or derived AVs is rejected (F08 per-AV policy).
- Admin console (`player.setlevel`, `AddPerk`, F30) goes through the same service with permission checks.

### 4.7 Audience / visibility
- `ProgressionUpdate` goes to the owner only (S8).
- `level` is public (neighbours via UpdateProperty and the snapshot).
- Perk effects visible to others reach them through their own features.

### 4.8 NPC parity
- NPCs do not gain XP or select perks.
- Their level (F13 `npcLevel`) and perks (NPC_ `PRKR`) feed SRV-021 the same way. A hosted NPC cannot send `ProgressionRequest` (S6: own actor only).
- Companion perks granted at Infatuation go to the **owner** (F21 calls `mp.addPerk`).

### 4.9 Gamemode API & server Papyrus
- **Properties:** `mp.get(actor,'progression')` (read; private), `mp.get(actor,'level')` (public).
- **Natives:** `mp.rewardXp(actor, amount, source, direct?)`, `mp.addPerk(actor, perk, rank?)`, `mp.removePerk`, `mp.setLevel(actor, level)` (admin), `mp.addPerkPoints(actor, n)`, `mp.resetSpecial(actor)` (re-opens the creation SPECIAL).
- **Settings:** `xp.rate`, `xp.killShareMinPct`, `progression.maxLevel`, `progression.collectibles`, `creationSpecialTotal`.
- **Events:** `onXpGain` (modifiable/blockable), `onLevelUp` (observe), `onPerkSelect` (blockable), `onSpecialAllocate` (blockable), `onCollectibleFound` (blockable).
- **Server Papyrus:**
  - `Actor.AddPerk/HasPerk/RemovePerk/GetLevel`;
  - `Game.AddPerkPoints`, `Game.GetXPForLevel`;
  - `Game.RewardPlayerXP(amount, direct)` targets the player actor of the current event context (`akActionRef`/`akActivator`), or logs and no-ops if there is none (MP ambiguity);
  - `Perk.OnEntryRun` from perk fragments (via SRV-021);
  - `Game.ShowSPECIALMenu` → SpSnippet to the owner (S15).

### 4.10 Edge cases & failure modes
- **Disconnect between request and reply:** the reply is lost; the reconnect snapshot carries the full state. Nonces prevent double spends.
- **Multi-level gain from one award** (quest reward): several points, one `ProgressionUpdate`.
- **Perk definitions change** (load order update): unknown perks are dropped from `perks` with the points refunded, logged.
- **Concurrent awards** (kill shared by 3 players): each award is independent; F11 clears its ledger after F19 has distributed.
- **Server restart:** the profile record is authoritative. F11's damage ledger is transient (kills in flight lose shared XP, acceptable).

### 4.11 Performance budget
- `ProgressionUpdate` delta ≤ 64 B; full ≤ 1.5 KB (~150 perk ranks).
- XP award ≤ 20 µs incl. perk-engine multiplier evaluation.

## 5. Engine / platform work required
- New platform natives (F19-T07/T08, `fallout4-platform`):
  - a `SelectPerk` hook with cancel;
  - `RewardExperience` suppression on the local player;
  - `setPlayerLevelAndXp(level, xp)`, `setPerkCount(n)`;
  - `SPECIALMenu` read-back of the allocated values.
- CLI-021: disable single-player XP gain and the difficulty menu.
- SRV-021 (perk engine) and SRV-060 (profile record) are prerequisites.
- R6: the entry-point argument table dump (`data/fo4/entrypoints.json`).

## 6. Tests
- `L-unit` (`[F19]`, `[Progression]`):
  - XP thresholds (201 → level 2; 1251 → level 5); INT 10 → ×1.3;
  - multi-level award gives n points; level cap;
  - kill shared by two players ≥ 25 % → both awarded; 10 % share → none; companion damage credited to the owner;
  - `selectPerk` accepted (point spent, rank +1, SRV-021 notified);
  - `selectPerk` rejected without points / below level / failing CTDA / skipping rank 1 / non-playable, each with a full update and nonce;
  - duplicate nonce idempotent;
  - `allocateSpecial` cap 10 (bobblehead bonus excluded);
  - `creationSpecial` sum ≠ 28 rejected, second creation rejected;
  - bobblehead raises STR to 11 once; magazine adds a rank up to `numRanks`; a second pickup ignored (`perPlayer`);
  - gamemode veto of `onPerkSelect` and `onXpGain` modification;
  - persistence round trip of the profile section with backward compat;
  - snapshot: the owner gets full progression, neighbours only `level`;
  - client `ChangeValuesAv` on Strength rejected.
- `L-int`: a bot kills NPCs, levels up, selects a perk; a second bot cannot see the first bot's progression.
- `L-fixture`: rank-chain and condition evaluation against a synthetic PERK chain plugin.
- `G-self`: R5 kill-XP measurement (log `RewardExperience` + target level); R12 bobblehead mechanism; `SelectPerk` hook cancel works.
- `G-manual`: create a character (SPECIAL), level up in combat, pick a perk in the chart, read a magazine, pick up a bobblehead seen by a second player who can still collect it.

## 7. Tasks
- [ ] **F19-T01** Progression model in the profile record (SRV-060) + JSON round trip + backward compat — M — Depends: SRV-060, REF-020 — Verify: L-unit — Files: falloutmp-server/cpp/server_guest_lib/fo4/ProgressionState.{h,cpp}; unit/ProgressionTest.cpp
- [ ] **F19-T02** `ProgressionService::AwardXp`: curve, multipliers via SRV-021, level-up, F08 derived-AV recompute, events — M — Depends: F19-T01, SRV-010, SRV-021 (ModExperience entry points) — Verify: L-unit — Files: falloutmp-server/cpp/server_guest_lib/fo4/ProgressionService.{h,cpp}
  - Accept: the threshold and multiplier cases of §6 pass.
- [ ] **F19-T03** Messages `ProgressionUpdate` (89) and `ProgressionRequest` (90) + snapshot fields + public `level` — S — Depends: NET-002 — Verify: L-unit — Files: falloutmp-server/cpp/messages/{ProgressionUpdateMessage,ProgressionRequestMessage}.h, Messages.h; falloutmp-client/src/services/messages/
- [ ] **F19-T04** Request handler: selectPerk / allocateSpecial / creationSpecial with rank chain, CTDA, nonce cache, rate limits, corrections — M — Depends: F19-T02, F19-T03, SRV-021, ESPM-009, ESPM-011 — Verify: L-unit, L-fixture
- [ ] **F19-T05** XP sources: kill XP from F11's damage ledger on F12 death, and the API used by F24/F15/F22/F26 with the GameProfile base-value table — M — Depends: F19-T02, F11, F12 — Verify: L-unit — Files: game_profile/fallout4/xp_values.json
- [ ] **F19-T06** Bobbleheads & magazines: collectible table from the ESM, per-player collection, effects (SRV-020), per-player visibility — M — Depends: F19-T02, F06, F20-T02 — Verify: L-unit, G-manual
- [ ] **F19-T07** Platform: `SelectPerk` hook/cancel, `RewardExperience` suppression, SPECIALMenu read-back — M — Depends: PLAT-031, PLAT-040 — Verify: G-self — Files: fallout4-platform/src/platform_fo4/ProgressionApi.cpp
- [ ] **F19-T08** Client: request service, chart pending state, `ProgressionUpdate` mirror (`setPlayerLevelAndXp`, `SetPerkCount`, AddPerk/RemovePerk), XP HUD messages, creation SPECIAL step — M — Depends: F19-T03, F19-T07, F03-T06 — Verify: L-ts, G-manual — Files: falloutmp-client/src/services/services/progressionService.ts
- [ ] **F19-T09** Gamemode API + server Papyrus (§4.9) + docs — M — Depends: F19-T04 — Verify: L-int — Files: falloutmp-server/ts typings, script_classes/PapyrusGame.cpp, script_classes/PapyrusActor.cpp
- [ ] **F19-T10** Perk ownership feed to SRV-021 and ability perks to SRV-020 (incl. NPC `PRKR`) — S — Depends: SRV-021, F20-T02 — Verify: L-unit
- [ ] **F19-T11** Research: R5 kill-XP formula and R12 bobblehead mechanism; update the reference doc and GameProfile tables — S — Depends: F19-T07 — Verify: G-self — Files: docs/falloutmp/reference/fo4-systems-combat-character.md
- [ ] **F19-T12** `G-manual` script and sign-off — S — Depends: F19-T08 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F19-progression.md

## 8. Open questions & risks
- Profile record vs actor change form: SRV-060 puts progression in the profile record. combat-character §19 proposed `progression{}` on `MpChangeForm`. This spec follows SRV-060/ADR-010; confirm.
- Does INT multiply all XP or only some sources, and are the lock XP values already INT-adjusted (R5)?
- `RewardPlayerXP` in server Papyrus has no single "player". The context rule may surprise script authors; document it in DOCS-003.
- Default `perPlayer` collectibles change vanilla scarcity. This is a gamemode/server-owner decision.
