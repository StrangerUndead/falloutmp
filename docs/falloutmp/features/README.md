# Feature Specs — Index

Every gameplay system has one spec, written with [_TEMPLATE.md](_TEMPLATE.md) and held to the [Sync Standard](../01-sync-standard.md). Tasks inside each spec use IDs `Fxx-Tnn`.

| ID | Spec | Tier | Target | SkyMP analogue (level) | Milestone |
|---|---|---|---|---|---|
| F00 | [Session lifecycle, world entry & streaming](F00-session-world-entry.md) | T0 | L4 | login/spawn/template save/grid (L4) | M4 |
| F01 | [Movement, aim & positioning](F01-movement.md) | T0 | L3+ | UpdateMovement (L3) | M5 |
| F02 | [Animation sync](F02-animation.md) | T0 | L3 | UpdateAnimation (L2–L3) | M5/M8 |
| F03 | [Appearance & character creation](F03-appearance-chargen.md) | T0 | L3 | RaceMenu appearance (L3) | M5/M7 |
| F04 | [Inventory & item instances (OMOD)](F04-inventory-items.md) | T0 | L4 | Inventory (L4) | M6 |
| F05 | [Equipment](F05-equipment.md) | T0 | L4 | Equipment (L3) | M6 |
| F06 | [Containers, looting, pickup, drop](F06-containers-looting.md) | T0 | L4 | Containers/pickup/drop (L4) | M6 |
| F07 | [Activation, doors, furniture, elevators](F07-activation-doors-furniture.md) | T0 | L4 | Activation/doors (L4) | M6 |
| F08 | [Actor values (Health, AP, Rads, limbs, SPECIAL-derived)](F08-actor-values.md) | T0 | L4 | H/M/S + regen (L3) | M7 |
| F09 | [Ranged combat (guns, ammo, reload, hit validation)](F09-ranged-combat.md) | T0 | L4 | Bow shots + OnHit (L3) | M8 |
| F10 | [Melee, explosives, turrets, traps, blocking](F10-melee-explosives-turrets.md) | T0/T1 | L4 | Melee/blocking (L3) | M8 |
| F11 | [Damage model](F11-damage-model.md) | T0 | L4 | TES5DamageFormula (L3) | M8 |
| F12 | [Death, respawn, bleedout](F12-death-respawn.md) | T0 | L4 | Death/respawn (L4) | M7 |
| F13 | [NPC spawning, hosting, AI, factions](F13-npc-hosting-ai.md) | T0 | L3+ | NPC hosting (L3) | M5–M8 |
| F14 | [Leveled lists, world respawn, reloot](F14-leveled-lists-world-respawn.md) | T0 | L4 | Leveled lists/reloot (L4) | M6 |
| F15 | [Crafting, components, scrapping](F15-crafting-components-scrapping.md) | T1 | L4 | Forge crafting (L3–L4) | M9 |
| F16 | [Weapon & armor modding (OMOD)](F16-weapon-armor-modding.md) | T1 | L4 | — | M9 |
| F17 | [Power armor](F17-power-armor.md) | T1 | L4 | — | M10 |
| F18 | [VATS](F18-vats.md) | T2 | L3 | — | M8 (disable) / M10 (lite) |
| F19 | [Progression: XP, levels, SPECIAL, perks, books](F19-progression.md) | T1 | L4 | learned spells (L4) | M7/M9 |
| F20 | [Consumables, effects, addiction, radiation, survival](F20-consumables-effects-survival.md) | T1 | L4 | Potions (L3) | M7/M10 |
| F21 | [Companions & Dogmeat](F21-companions.md) | T2 | L3 | — | M10 |
| F22 | [Workshop & settlements](F22-workshop-settlements.md) | T1/T2 | L4 | — | M11/M12 |
| F23 | [Vendors, barter, caps, trade](F23-vendors-barter-trade.md) | T1 | L4 | — | M9 |
| F24 | [Locks, terminals, hacking, holotapes](F24-locks-terminals-hacking.md) | T1 | L4 | (client unlocks locally) | M9 |
| F25 | [Time & weather](F25-time-weather.md) | T1 | L3 | TimeService (L2) | M6 (clock) / M9 |
| F26 | [Map markers, discovery, fast travel](F26-map-fast-travel.md) | T1 | L3 | — | M9 |
| F27 | [Quests & dialogue](F27-quests-dialogue.md) | T2 | L3 | quests (L0–L1) | M12 |
| F28 | [Pip-Boy, radio, HUD, flashlight](F28-pipboy-radio-hud.md) | T1/T2 | L2/L3 | — | M5 (menu policy) / M11 |
| F29 | [Stealth](F29-stealth.md) | T2 | L3 | — | M10 |
| F30 | [Chat, commands, admin](F30-chat-commands-admin.md) | T0 | L4 | Chat (gamemode), console (L3) | M6 |
| F31 | [Names, custom properties, gamemode extensibility](F31-names-properties-extensibility.md) | T0 | L4 | Nicknames, properties (L3–L4) | M5 |
| F32 | [Parties, teams & PvP rules](F32-parties-pvp.md) | T1 | L4 | — (SweetPie gamemode only) | M8/M9 |

**Candidate features without a spec yet** (add a spec before scheduling; none blocks 1.0): physics grab/carry of objects (`Z` grab, class B owner-validated like F06 drop); scripted transit (elevator cars with riders, Vertibird travel as validated fast travel through F26); admin spectator/free-camera mode (F30 extension); emotes (F02 whitelisted idle events); service NPCs (doctors, barbers via F23/F03); per-player stash containers (F06 `perPlayer` mode on a tagged container); radiant encounters (gamemode content on F13/F14 spawn API); voice chat (out of process, proximity data from F01; Mumble-style positional plugin); screenshot/photo mode (client-only, D).

**How to work a feature:**
1. Read the spec.
2. Check that its `Depends on` items are done (STATUS.md).
3. Implement its tasks in order.
4. Verify per task.
5. Fill the review checklist from 01-sync-standard.md §10.
6. Record the evidence in STATUS.md.
