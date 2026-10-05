#pragma once
#include "GameProfile.h"

// Fallout 4 (Anniversary Edition 1.11.x, ADR-001). Values marked "verify"
// in docs/falloutmp/STATUS.md are checked against real data by ESPM-015.
class Fallout4GameProfile : public GameProfile
{
public:
  GameId GetGameId() const noexcept override { return GameId::Fallout4; }
  std::vector<std::string> GetDefaultLoadOrder() const override;
  std::string_view GetArchiveExtension() const noexcept override
  {
    return ".ba2";
  }
  uint32_t GetPlayerBaseFormId() const noexcept override { return 0x7; }
  uint32_t GetPlayerRefFormId() const noexcept override { return 0x14; }
  // Commonwealth worldspace
  uint32_t GetDefaultWorldspace() const noexcept override { return 0x3c; }
  // Caps001
  uint32_t GetCurrencyFormId() const noexcept override { return 0xf; }
  StartPoint GetDefaultStartPoint() const noexcept override;
  const std::vector<uint32_t>& GetBannedCharacterRaceIds()
    const noexcept override;
  bool IsItemRecordType(std::string_view type) const noexcept override;
  const std::set<std::string>& GetSyncedReferenceBaseTypes()
    const noexcept override;
  // F07: 300 units plus validation slack (see 01-sync-standard.md)
  float GetActivationReach() const noexcept override { return 300.f; }
  // Sprint speed with Agility/Moving Target headroom; jetpack is handled by
  // the movement validator separately (F01-T05).
  float GetMaxRunSpeed() const noexcept override { return 900.f; }
};
