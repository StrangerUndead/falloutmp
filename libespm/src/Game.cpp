#include "libespm/Game.h"
#include "libespm/RecordHeader.h"
#include "libespm/RecordHeaderAccess.h"
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace espm {

namespace {
constexpr uint32_t kMasterFlag = 0x1;
constexpr uint32_t kLightFlag = 0x200;

bool NearlyEqual(float a, float b)
{
  return std::fabs(a - b) < 0.0005f;
}

bool IEquals(const std::string& a, const char* b)
{
  if (a.size() != std::strlen(b)) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}
}

const char* GameToString(Game game) noexcept
{
  switch (game) {
    case Game::Skyrim:
      return "Skyrim";
    case Game::Fallout4:
      return "Fallout4";
    default:
      return "Unknown";
  }
}

PluginGameInfo DetectGame(const RecordHeader* tes4,
                          CompressedFieldsCache& cache) noexcept
{
  PluginGameInfo info;
  if (!tes4) {
    return info;
  }
  info.formVersion = tes4->GetVersion();
  info.isLight = (tes4->GetFlags() & kLightFlag) != 0;
  info.isMaster = (tes4->GetFlags() & kMasterFlag) != 0;

  RecordHeaderAccess::IterateFields(
    tes4,
    [&](const char* type, uint32_t size, const char* data) {
      if (!std::memcmp(type, "HEDR", 4) && size >= 4) {
        std::memcpy(&info.hedrVersion, data, 4);
      } else if (!std::memcmp(type, "MAST", 4) && size > 0) {
        info.masters.emplace_back(data, strnlen(data, size));
      }
    },
    cache);

  if (info.formVersion >= 131 && info.formVersion < 200) {
    info.game = Game::Fallout4;
  } else if (info.formVersion == 43 || info.formVersion == 44) {
    info.game = Game::Skyrim;
  } else if (NearlyEqual(info.hedrVersion, 0.95f) ||
             NearlyEqual(info.hedrVersion, 1.0f)) {
    info.game = Game::Fallout4;
  } else if (NearlyEqual(info.hedrVersion, 0.94f) ||
             NearlyEqual(info.hedrVersion, 1.7f) ||
             NearlyEqual(info.hedrVersion, 1.71f)) {
    info.game = Game::Skyrim;
  } else {
    for (auto& m : info.masters) {
      if (IEquals(m, "Fallout4.esm")) {
        info.game = Game::Fallout4;
      } else if (IEquals(m, "Skyrim.esm")) {
        info.game = Game::Skyrim;
      }
    }
  }
  return info;
}

Game RequireSingleGame(const std::vector<std::pair<std::string, Game>>& files)
{
  Game result = Game::Unknown;
  std::string first;
  for (auto& [name, game] : files) {
    if (game == Game::Unknown) {
      continue;
    }
    if (result == Game::Unknown) {
      result = game;
      first = name;
    } else if (result != game) {
      throw std::runtime_error("Load order mixes games: '" + first + "' is " +
                               GameToString(result) + " but '" + name +
                               "' is " + GameToString(game));
    }
  }
  return result;
}

}
