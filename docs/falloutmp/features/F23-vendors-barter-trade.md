# F23 — Vendors, Barter, Caps & Trade

| Field | Value |
|---|---|
| Tier | T1 (vendors, BarterMenu, prices, caps, restock, shared stock, Cap Collector investment). T2: per-player vendor stock, player-to-player trade (gamemode) |
| Target level | L4 |
| SkyMP analogue | None in SkyMP (no barter sync). Building blocks: container occupancy and `inventory` property (`MpObjectReference.cpp:1512-1568`), atomic `Inventory::RemoveItems`, reloot (`RelootContainer`, `MpObjectReference.cpp:900`, `DoReloot` `:1142`), `DropItem` blocks `0xF` (B12). SkyMP level L0 |
| Milestone | M9 (vendors, barter); per-player stock and player trade in M12 |
| Workstreams | SRV, CLI, PLAT, NET, GM, FRONT, PVM, QA |
| Depends on | F04 (`ItemKey`, caps), F06 (container contents, caps-in-container rule), F07 (activation), F08 (Charisma AV), F13 (vendor NPC alive/hosted), F14 (leveled restock), F16 + SRV-022 (instance value), F19 + SRV-021 (Cap Collector, price entry points), F25 + SRV-070 (clock: vendor hours, restock), F22 (settlement stores), F27 (dialogue barter topic, later), ESPM-010 (FACT vendor data), NET-007, QA-040 |
| References | reference/fo4-systems-world-economy.md **S6**, S7, S15, S18, §1 (B2, B6, B8, B12), §3.2–3.7; reference/fo4-data-formats.md §4.15 (FACT `VEND`/`VENC`/`VENV`), §4.14 (CONT/LVLI); reference/skymp-sync-inventory.md §1.9 (validation patterns), §2 (Containers row); reference/fo4-systems-combat-character.md §1.7 (`PerkEngine`); 02-architecture.md ADR-010, ADR-019, ADR-020 |

## 1. Summary
Players trade with vendors (Diamond City traders, caravans, settlement stores) through the vanilla Barter menu. Every player near a vendor sees the same stock; each sale or purchase is executed atomically on the server, so caps and items cannot be duplicated or lost, even with two players buying the last item at once. Prices come from the server's FO4 formula (Charisma, Cap Collector and other perk entry points, item instance value including mods and legendary effects), and the client's displayed price must match. Vendor stock and caps restock on the server clock (default every 2 game days). Servers can switch to per-player vendor stock. Player-to-player trading is a T2 gamemode feature built on an atomic server exchange API.

## 2. Vanilla Fallout 4 behaviour
- **Vendor data** lives on FACT:
  - `DATA` flag `Vendor` 0x4000;
  - `VEND` buy/sell FLST;
  - `VENC` merchant container REFR, which holds vendor stock **and caps**, usually in a holding cell;
  - `VENV {u16 startHour, endHour, radius; u8 buysStolen; u8 buySellEverythingNotInList; u8 buysNonStolen}`;
  - `PLVD` location; conditions.
  Runtime `FACTION_VENDOR_DATA{…, merchantContainer, lastDayReset}` [src: xEdit wbDefinitionsFO4.pas:6785-6846; CLF4 F/FACTION_VENDOR_DATA.h] (data-formats §4.15; world-economy S6(b)).
- **Prices:**
  - buy modifier = 3.50 − 0.15 × Charisma; sell modifier = 1 / buy modifier;
  - multiplied by perk/bobblehead modifiers;
  - final buy ≥ 1.2 × value, sell ≤ 0.8 × value [web: wiki:Charisma_(Fallout_4) §Effect on Barter].
  - Cap Collector 1 = 10 %, 2 = 20 % (multiplicative); ranks `01D2456, 0D75E2, 01D2457`. Rank 3 lets the player invest 500 caps per shop type [web: wiki:Cap_Collector_(Fallout_4)].
  - The rounding rule and GMST names are `[inference]`; verify with G-self/D-real.
- **Restock:** vendor inventory and caps re-roll on refresh; `iDaysToRespawnVendor` ≈ 2 days (low confidence; read the real GMST, D-real). Speech bobblehead +100 caps (world-economy S6(a)).
- **Caps:** `Caps001` 0x0000000F. They cannot be dropped or put in containers [web: wiki:Bottlecap_(Fallout_4)].
- **Menu:** `BarterMenu`:
  - fields `ItemBarterData{stackQuantityMap, capsOwedByPlayer}`, `barteredItems`, `vendorChestRef`, `vendorActor`, `confirmingTrade`;
  - functions `CompleteTrade()`, `ClearTradingData()`, `GetCapsOwedByPlayer()`, virtual `ConfirmInvestment` [src: CLF4 B/BarterMenu.h].
  - Papyrus `Actor.ShowBarterMenu()`; `ObjectReference.OnSell(Actor akSeller)` [src: F4SE vanilla/Actor.psc:760; ObjectReference.psc:801-843, 1091].
- **Settlement stores** (Local Leader 2; tier 3 needs Cap Collector 2) use `WorkshopVendorContainers` per type/level [src: F4SE vanilla/WorkshopParentScript.psc:191-210].
- **SP assumptions that break:** prices and the trade execute locally; the vendor chest is a save-local ref; restock uses the local clock; one shared stock competes between players.

## 3. SkyMP baseline
- SkyMP has no barter path. What exists:
  - container occupancy (single occupant, 512 u reach measured from the previous occupant, B3/B4);
  - the `inventory` property sent to the occupant;
  - atomic `Inventory::RemoveItems`; reloot timers (B6, CONT `Respawns` flag ignored);
  - `DropItem` refusing `0xF` (Gold001 = FO4 Caps001, B12).
- Holding-cell `VENC` containers are not loaded by the server unless referenced (B8 rule: only certain base types are instantiated; holding cells are never streamed).
- **Reuse:** container inventory model, `RelootContainer` (after the B2 fix), atomic inventory ops, gamemode event infrastructure.
- **New:** vendor resolution, the price engine, the trade transaction, restock service, and BarterMenu capture/apply.

## 4. Design

### 4.1 Authority model
- **Class A:** vendor stock, vendor caps, player caps and items, prices, restock timing, investment state, stolen-item rules.
- The client's BarterMenu is a view. Its confirm becomes a request; a price mismatch is rejected, not "fixed".
- **Class D:** menu presentation and sounds.

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Vendor stock + vendor caps | `Inventory` | merchant container (`VENC`) `MpChangeFormREFR.inv` | yes (`inv`) | CONT `CNTO`/LVLI (F14) |
| `VendorState{lastRestockGameDay, investedCaps, inventoryVersion}` | struct | `MpChangeFormREFR.vendor` (new optional, on the merchant container) | yes (`vendor`) | restock day = first load |
| Per-player stock (T2) `profileId → Inventory + lastRestockGameDay` | map | reuses F06's `MpChangeFormREFR.privateInventories` (new optional) | yes | rolled on first open |
| Vendor index (vendor faction → container, VENV hours/radius/flags, buy/sell FLST keywords) | cache | `VendorService` (new) | no | ESM FACT |
| Open barter sessions `actorId → {vendorId, invVersion, opened ms}` | map | `VendorService` | no | — |
| Player caps + items | `Inventory` | actor `inv` | yes | — |
| Stolen marker on entries | `ExtraData.stolenFrom` (F06; excluded from stacking) | actor `inv` | yes | none |

### 4.3 Protocol
`Barter` (98, both, R) uses an `op` discriminator. Every reply echoes `nonce`.

| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `Activate` (6) | C→S | activate a vendor NPC | R | user action | reused (F07) |
| `Barter` op `open` | C→S | `nonce`, `vendorActorId` | R | from activation/dialogue | new (registry 98) |
| `Barter` op `opened` | S→C | `nonce`, `vendorActorId`, `containerRefId`, `invVersion` u32, `buyMult`, `sellMult` (f32, final after perks), `vendorCaps` | R | reply | new |
| `SetInventoryFo4` (68) with `refId` = merchant container, `version` = `invVersion` | S→C | vendor stock (entries with `ItemKey`, count) to every open session | R | with `opened` and on change | reused (F06 container model) |
| `Barter` op `trade` | C→S | `nonce`, `vendorActorId`, `invVersion`, `buy[] {ItemKey, count, unitPrice}`, `sell[] {ItemKey, count, unitPrice}`, `capsDelta` (s32, + = player receives) | R | on `CompleteTrade` | new |
| `Barter` op `invest` | C→S | `nonce`, `vendorActorId` | R | on `ConfirmInvestment` | new |
| `Barter` op `close` | C→S / S→C | `vendorActorId`, `reason` | R | menu closed / server forces (vendor died, hours over, too far) | new |
| `Barter` op `result` | S→C | `nonce`, `ok`, `error` u16 (`NotOpen, StaleInventory, PriceMismatch, NotEnoughCaps, VendorNoCaps, ItemNotFound, NotBuyable, NotSellable, StolenRefused, Hostile, Closed, TooFar, Dead, RateLimited, GamemodeBlocked, Duplicate`), `capsDelta`, `invVersion` | R | per trade/invest | new |
| `SetInventoryFo4` (68) | S→C | player inventory | R | after trade and on every reject | reused (F04) |
| `ProgressionUpdate` (89) | S→C | (none in vanilla barter; gamemode XP hooks may use it) | R | — | reused (F19) |

### 4.4 Client capture (owner side)
- **Open:** activating a vendor NPC (vanilla dialogue is blocked, ADR-011) sends `Activate`. The server decides whether it is a barter. Later, F27 can route the vanilla "Trade" dialogue topic to `Barter open`.
- On `opened`:
  - the client fills its local `VENC` ref from the server inventory (`ResetContainer` + `AddItemEx`, as SkyMP does for containers);
  - it sets the local caps;
  - it calls `ShowBarterMenu()` on its copy of the vendor actor (native wrapper, PLAT-042).
- **Trade:** a hook on `BarterMenu::CompleteTrade` reads `barteredItems` / `ItemBarterData` and `GetCapsOwedByPlayer()`. It sends `Barter trade` with the prices shown, then **suppresses** the local transfer (pre-commit cancel; RE). Fallback: allow the local transfer and let `SetInventoryFo4` + container contents overwrite.
- **Investment:** `ConfirmInvestment` hook → `Barter invest`.
- **Close:** the `MenuOpenCloseEvent` for `BarterMenu` → `Barter close`.

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
- **Barterer:** after `result ok`, the authoritative `SetInventoryFo4` (player) and container contents (vendor) replace the local state; the menu refreshes (`refreshBarterMenu` native, or re-open). After a reject, the same messages restore the pre-trade state and a HUD message shows the reason.
- **Other players** bartering with the same vendor (shared stock) receive the updated container contents with a new `invVersion`; their menus refresh.
- **Remotes** see nothing else: no world-visible state. Items moved are private (S8).
- **Reconnect:** the session is dropped on disconnect; there is no partial trade.
- **Restart:** vendor stock and `VendorState` load from change forms.

### 4.6 Validation & anti-cheat
Every rejection sends `Barter result{ok=false}` + `SetInventoryFo4` + current vendor contents (S12).
1. **Identity:** own actor; rate ≤ 5 trades/s; nonce dedupe (cached result) (S6).
2. **Open checks:**
   - the target actor is in a vendor faction with a resolvable `VENC`;
   - alive (F12) and not hostile to this player (F13 factions / crime ledger, world-economy S15);
   - within `VENV` hours on the server clock (F25), unless `vendors.ignoreHours`;
   - distance ≤ the F07 activation reach for NPC_ (per-type override `vendors.reach`, default 512 u, server positions);
   - `onBarterOpen` not vetoed.
   Shared stock allows **several concurrent barterers** (optimistic concurrency by `invVersion`). It does not use SkyMP's single-occupant rule.
3. **Session:** a trade requires an open session for this vendor; the session ends on close, distance > reach + 128 u, vendor death, or hours ending (→ `close` S→C).
4. **Stale stock:** `invVersion` ≠ current → `StaleInventory`, with fresh contents.
5. **Lines:**
   - counts ∈ [1, 65535]; no caps (`0xF`) in `buy`/`sell`;
   - each `buy` `ItemKey` exists in the vendor stock with ≥ count; each `sell` `ItemKey` exists in the player's inventory with ≥ count and is not equipped (vanilla), not quest/`CantDrop`;
   - the vendor accepts the item: the `VEND` keyword list matches, or `buySellEverythingNotInList` inverts it;
   - stolen items are accepted only if `buysStolen`.
6. **Prices:**
   - `unit = round(value(ItemKey) × mult)`;
   - `value` from SRV-022 (base + OMOD Value props, legendary included);
   - `buyMult = max(1.2, (3.5 − 0.15·CHA) × Π perkBuyMods)`, `sellMult = min(0.8, (1 / (3.5 − 0.15·CHA)) × Π perkSellMods)`;
   - CHA is the actor's current Charisma AV (F08, so chems and apparel count); perk modifiers from SRV-021 price entry points;
   - an optional gamemode multiplier `mp.set(actorId, "barterPriceMult", x)`.
   Each client `unitPrice` must equal the server price within ± `vendors.priceTolerance` (default 1 cap), else `PriceMismatch` (also resend `opened` with server multipliers). The sum of line prices must equal `capsDelta`.
7. **Caps:** player caps ≥ the net cost; vendor caps ≥ the net payout (vanilla vendors cannot pay more than they hold).
8. **Commit (S13):**
   - copy both inventories and apply all lines plus caps;
   - assert no negative counts;
   - `onBarter` veto check;
   - commit both, bump `invVersion`, flush both change forms in one save batch (F23-T08).
   No partial trade can exist.
9. **Investment:**
   - Cap Collector 3 (SRV-021);
   - player caps ≥ 500;
   - not already invested in this shop type (`VendorState.investedCaps`);
   - then caps move to the vendor and its caps-on-restock baseline rises (`vendors.investBonus`).

### 4.7 Audience / visibility
- Barterer: `Barter` replies, `SetInventoryFo4`.
- Every open session of the same vendor: container contents and `invVersion` (S8: vendor stock is visible only to those bartering).
- Nothing is broadcast to grid neighbours.

### 4.8 NPC parity
- Vendor NPCs are ESM actors, hosted when players are near (F13). The vendor *state* (stock, caps) lives on the merchant container and never depends on the host. A barter session requires the vendor actor to be alive on the server; host migration does not affect it.
- **Settlement store vendors** are settlers assigned to a store object (F22-T25). They join the vendor faction of that store type/level and use the same container model.
- NPCs never initiate barter.

### 4.9 Gamemode API & server Papyrus
- **Events:**
  - `onBarterOpen(actorId, vendorId)` **blockable**;
  - `onBarter(actorId, vendorId, buy[], sell[], capsDelta)` **blockable**;
  - `onVendorInvest(actorId, vendorId)` **blockable**;
  - `onVendorRestock(vendorId, containerId)` **not blockable**.
- **Properties:** `mp.get(containerId, "vendor")` → `VendorState`; `mp.set(containerId, "vendor", {lastRestockGameDay?, investedCaps?})`; `mp.set(actorId, "barterPriceMult", x)` (private, default 1).
- **API:** `mp.restockVendor(containerId)`.
- **Player-to-player trade (T2):** `mp.exchangeItems(actorA, itemsA[], capsA, actorB, itemsB[], capsB)`. It performs an atomic two-sided swap with the same S13 rules, and returns `false` without mutation on any shortfall. The default gamemode implements offer/accept UX with `CustomPacket` + a front widget; no new message.
- **Settings** (S22): `vendors.stockMode` (`shared` | `perPlayer`), `vendors.respawnDays` (default 2, overridden by the GMST if found), `vendors.reach`, `vendors.ignoreHours`, `vendors.priceTolerance`, `vendors.investBonus`.
- **Papyrus:**
  - `Actor.ShowBarterMenu()` on the server → server-side `Barter open` for the targeted player (S15);
  - fire `ObjectReference.OnSell(akSeller)` on the merchant container after a sale `[inference: vanilla target of OnSell unverified]`;
  - `OnItemAdded/OnItemRemoved` (F04-T09).

### 4.10 Edge cases & failure modes
- **Two players buy the last item:** the first commit bumps `invVersion`; the second gets `StaleInventory` + fresh contents.
- **Disconnect between confirm and result:** the trade is either committed or not. On reconnect the inventory snapshot shows the truth; a resent nonce returns the cached result.
- **Server crash between the two change-form writes:** risk of loss or duplication. F23-T08 makes the flush of both forms atomic per driver (single batch / journal), as QA-040 requires.
- **Restock while a session is open:** deferred until no session is open, or `respawnDays + 1` passes, then forced with `close` to the open sessions.
- **Vendor killed mid-session** → `close`. The container keeps its stock (vanilla); looting the vendor's body is F06.
- **Caps in containers/dropped:** refused by F06 (`PutItemFo4`) and B12 (`DropItem`), so caps leave a player only through barter, `mp.exchangeItems` or gamemode APIs.
- **Per-player stock (T2):** each profile's private inventory restocks on its own clock; caps paid by one player are not visible to another.
- **Holding-cell containers:** loaded explicitly by `VendorService` from the FACT `VENC` refs (B8 extension), even though no player ever streams that cell.

### 4.11 Performance budget
- `Barter trade` ≤ 32 B + 16 B per line. Pricing is O(lines) with cached instance values (SRV-022).
- A trade costs ≤ 100 µs on the server plus one coalesced save batch.
- Restock is lazy: evaluated on open and in a low-frequency sweep (every 10 game minutes) rather than per-vendor timers (avoiding SkyMP's per-object timer hotspot).

## 5. Engine / platform work required
- `ShowBarterMenu` wrapper on a local actor copy, with the vendor chest bound to the local `VENC` ref (PLAT-042 menus).
- `BarterMenu` hooks: `CompleteTrade` (read lines, caps owed; pre-commit cancel), `ConfirmInvestment`, refresh native.
- Fill the local vendor chest from server contents (`ResetContainer`/`AddItemEx` with `ItemKey`, PLAT-080).
- G-self check: the displayed unit prices equal the server formula for CHA 1/5/10 with and without Cap Collector.

## 6. Tests
- `L-unit` (`[F23]`, `[Vendor]`):
  - `Barter` round trips per op;
  - price table golden values at CHA 1/5/10/15 × Cap Collector 0/1/2, including the 1.2/0.8 clamps and a modded/legendary instance value;
  - accept trade: both inventories and caps updated, `invVersion` bumped, audience correct (barterer + other sessions only);
  - every reject (`StaleInventory`, `PriceMismatch`, `NotEnoughCaps`, `VendorNoCaps`, `NotBuyable` by keyword list, `StolenRefused`, `Hostile`, `Closed` by hours with a fake clock, `TooFar`, `Dead`, `GamemodeBlocked`, duplicate nonce), each asserting the correction set;
  - atomicity: a failing line in a multi-line trade leaves both sides untouched;
  - restock after `respawnDays` with a fake clock, deferred while a session is open;
  - investment rules;
  - per-player stock isolation (T2);
  - `mp.exchangeItems` atomic swap and shortfall;
  - persistence round trip of the vendor container + `VendorState`; backward-compatible load.
- `L-fixture`: synthetic FACT with `VEND`/`VENC`/`VENV`, merchant CONT with LVLI.
- `L-int`: two bots barter with one vendor concurrently and race for the last item; a kill-server-mid-trade test (QA-040) shows no dup/loss after restart.
- `L-ts`: BarterMenu capture → `trade` builder; reconcile on reject.
- `G-self`: displayed prices vs server formula; `CompleteTrade` hook fires before the local transfer.
- `G-manual`: two players at a Diamond City vendor: buy, sell, the last-item race, restock after waiting 2 game days; reconnect.

## 7. Tasks
- [ ] **F23-T01** `Barter` (98) message with ops + TS mirror — S — Depends: NET-002, F04 (`ItemKey`) — Verify: L-unit, L-ts — Files: falloutmp-server/cpp/messages/BarterMessage.h, Messages.h; falloutmp-client/src/services/messages/
- [ ] **F23-T02** `VendorService` index: vendor factions → `VENC`, `VENV`, `VEND`; explicit loading of holding-cell containers — M — Depends: ESPM-010, REF-012 — Verify: L-unit, L-fixture — Files: falloutmp-server/cpp/server_guest_lib/fo4/vendor/VendorService.{h,cpp}
- [ ] **F23-T03** `PriceEngine` (pure): CHA formula, perk entry points, clamps, rounding, gamemode multiplier — S — Depends: SRV-021, SRV-022, F08 — Verify: L-unit — Files: fo4/vendor/PriceEngine.{h,cpp}
  - Accept: golden table matches the wiki-derived values.
- [ ] **F23-T04** Barter open/close sessions, multi-viewer `invVersion`, hours/distance/hostility checks — M — Depends: F23-T01, F23-T02, F13, F25 — Verify: L-unit
- [ ] **F23-T05** Trade transaction (atomic, keyword/stolen rules, caps), `onBarter`, corrections — M — Depends: F23-T03, F23-T04, F04, NET-007 — Verify: L-unit
  - Accept: every §6 reject asserts the correction set; the atomicity test passes.
- [ ] **F23-T06** Restock service (lazy + sweep) on the server clock, `onVendorRestock`, deferred with open sessions — S — Depends: F23-T02, SRV-070, SRV-080, F14 — Verify: L-unit
- [ ] **F23-T07** Cap Collector investment — S — Depends: F23-T05, SRV-021 — Verify: L-unit
- [ ] **F23-T08** Atomic multi-form flush for transactions: cross-cutting, implemented as SRV-092's batch upsert + a trade journal record; this task owns the F23 integration and tests — M (the driver work is in SRV-092) — Depends: REF-020, QA-040 — Verify: L-unit, L-int — Files: viet/include/save_storages/*, falloutmp-server/cpp/server_guest_lib/database_drivers/*
  - Accept: a fault-injected crash between the two writes never yields dup or loss after restart.
- [ ] **F23-T09** Platform BarterMenu hooks (`CompleteTrade`, `ConfirmInvestment`, refresh) and `ShowBarterMenu` wrapper — M — Depends: PLAT-042, PLAT-080 — Verify: W-ci, G-self — Files: fallout4-platform/src/.../BarterApi.cpp
- [ ] **F23-T10** Client `barterService.ts`: open flow, chest fill, capture, reconcile — M — Depends: F23-T01, F23-T09, CLI-050 — Verify: L-ts, G-manual — Files: falloutmp-client/src/services/services/barterService.ts
- [ ] **F23-T11** Server Papyrus `ShowBarterMenu`, `OnSell`; gamemode API docs (DOCS-003) — S — Depends: PVM-014, F23-T05 — Verify: L-unit
- [ ] **F23-T12** `G-manual` vendor scenario script and sign-off — S — Depends: F23-T10 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F23-vendors.md
- [ ] **F23-T13** (T2) Per-player vendor stock (`vendors.stockMode = perPlayer`) — M — Depends: F23-T05, F23-T06 — Verify: L-unit
- [ ] **F23-T14** (T2) `mp.exchangeItems` + default-gamemode player trade (CustomPacket + front widget) — M — Depends: F23-T05, F23-T08, GM-011, FRONT-003 — Verify: L-unit, L-int, G-manual

## 8. Open questions & risks
- Exact rounding and GMSTs for prices (`[inference]`). The client's displayed prices must equal the server's, or every trade is rejected; G-self comparison is mandatory before M9 exit.
- `iDaysToRespawnVendor` value and whether caps re-roll on the same timer (community-sourced 2 days; read the GMST, D-real).
- Whether `BarterMenu::CompleteTrade` can be cancelled before the local transfer (RE). The fallback overwrite path works, but flickers.
- How vanilla routes "Trade" dialogue to `ShowBarterMenu` (INFO fragment or flag). Until F27, opening barter by activation is a deliberate deviation from vanilla UX.
- Atomic flush across change forms is a cross-cutting persistence change (F23-T08). F06, F22 and F15 transfers benefit too.
