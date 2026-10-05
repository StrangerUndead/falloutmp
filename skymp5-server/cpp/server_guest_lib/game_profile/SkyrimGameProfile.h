#pragma once
#include "GameProfile.h"

class SkyrimGameProfile : public GameProfile
{
public:
  GameId GetGameId() const noexcept override { return GameId::Skyrim; }
  std::vector<std::string> GetDefaultLoadOrder() const override;
  std::string_view GetArchiveExtension() const noexcept override
  {
    return ".bsa";
  }
  uint32_t GetPlayerBaseFormId() const noexcept override { return 0x7; }
  uint32_t GetPlayerRefFormId() const noexcept override { return 0x14; }
  uint32_t GetDefaultWorldspace() const noexcept override { return 0x3c; }
  uint32_t GetCurrencyFormId() const noexcept override { return 0xf; }
  StartPoint GetDefaultStartPoint() const noexcept override;
  const std::vector<uint32_t>& GetBannedCharacterRaceIds()
    const noexcept override;
  bool IsItemRecordType(std::string_view type) const noexcept override;
  const std::set<std::string>& GetSyncedReferenceBaseTypes()
    const noexcept override;
  float GetActivationReach() const noexcept override { return 512.f; }
  float GetMaxRunSpeed() const noexcept override { return 800.f; }
};
