#pragma once
#include <cstdint>

enum class MsgType : uint8_t
{
  Invalid = 0,
  CustomPacket = 1,
  UpdateMovement = 2,
  UpdateAnimation = 3,
  UpdateAppearance = 4,
  UpdateEquipment = 5,
  Activate = 6,
  UpdateProperty = 7,
  PutItem = 8,
  TakeItem = 9,
  FinishSpSnippet = 10,
  OnEquip = 11,
  ConsoleCommand = 12,
  CraftItem = 13,
  Host = 14,
  CustomEvent = 15,
  ChangeValues = 16,
  OnHit = 17,
  DeathStateContainer = 18,
  DropItem = 19,
  Teleport = 20,
  OpenContainer = 21,
  PlayerBowShot = 22,
  SpellCast = 23,
  UpdateAnimVariables = 24,

  // ex-strings
  DestroyActor = 25,
  HostStart = 26,
  HostStop = 27,
  SetInventory = 28,
  SetRaceMenuOpen = 29,
  SpSnippet = 30,
  Teleport2 = 31,
  UpdateGamemodeData = 32,
  CreateActor = 33,

  // 34..63 are reserved for upstream SkyMP growth. Do not allocate them.
  kUpstreamReservedBegin = 34,
  kUpstreamReservedEnd = 63,

  // Fallout 4 messages. The registry with owners and payloads is
  // docs/falloutmp/01-sync-standard.md section 6. 64..79 are Fallout 4
  // twins of existing messages; 80..122 are new Fallout 4 messages.
  kFallout4Begin = 64,
  CreateActorFo4 = 64,
  UpdateMovementFo4 = 65,
  UpdateAppearanceFo4 = 66,
  UpdateEquipmentFo4 = 67,
  SetInventoryFo4 = 68,
  PutItemFo4 = 69,
  TakeItemFo4 = 70,
  DropItemFo4 = 71,
  ChangeValuesAv = 72,
  DeathStateContainerFo4 = 73,
  UpdateActions = 74,
  UpdateGraphVariables = 75,
  CraftItemFo4 = 76,
  WeaponFire = 80,
  WeaponReload = 81,
  HitReport = 82,
  DamageApplied = 83,
  ExplosionEvent = 84,
  ModItem = 85,
  ScrapItem = 86,
  PowerArmorTransition = 87,
  PowerArmorState = 88,
  ProgressionUpdate = 89,
  ProgressionRequest = 90,
  UseItem = 91,
  EffectsUpdate = 92,
  WorkshopMode = 93,
  WorkshopPlace = 94,
  WorkshopEdit = 95,
  WorkshopState = 96,
  WorkshopWire = 97,
  Barter = 98,
  LockpickAttempt = 99,
  TerminalAction = 100,
  VatsAction = 101,
  CompanionCommand = 102,
  MapDiscovery = 103,
  FastTravelRequest = 104,
  WorldTimeWeather = 105,
  DetectionState = 106,
  RequestResult = 107,
  WorkshopObjects = 108,
  WorkshopManage = 109,
  NoteAction = 110,
  NpcAiState = 111,
  CompanionState = 112,
  RestAction = 113,
  QuestUpdate = 114,
  DialogueAction = 115,
  ConsoleCommandResult = 116,
  ContainerPeek = 117,
  SetFavorites = 118,
  PickpocketAttempt = 119,
  PartyAction = 120,

  Max
};

// '{' (123) starts a JSON message, so binary message types must stay below.
static_assert(static_cast<int>(MsgType::Max) <= 123,
              "MsgType values must stay below '{' (123)");
static_assert(static_cast<int>(MsgType::CreateActor) <
                static_cast<int>(MsgType::kUpstreamReservedBegin),
              "Skyrim messages must stay below the upstream reserved range");

constexpr bool IsFallout4MsgType(MsgType t) noexcept
{
  return static_cast<int>(t) >= static_cast<int>(MsgType::kFallout4Begin) &&
    static_cast<int>(t) < static_cast<int>(MsgType::Max);
}
