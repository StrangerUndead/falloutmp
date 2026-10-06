#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace espm {

class RecordHeader;
class CompressedFieldsCache;

// Which game a plugin file was made for (ESPM-001).
enum class Game : uint8_t
{
  Unknown = 0,
  Skyrim,
  Fallout4,
};

const char* GameToString(Game game) noexcept;

// Facts read from a plugin's TES4 header that identify the game.
struct PluginGameInfo
{
  Game game = Game::Unknown;
  uint16_t formVersion = 0; // record header version field of TES4
  float hedrVersion = 0;
  bool isLight = false;  // ESL flag (0x200)
  bool isMaster = false; // ESM flag (0x1)
  std::vector<std::string> masters;
};

// Detection rules, in order:
// 1. TES4 form version: 131 (Fallout 4) vs 43/44 (Skyrim LE/SE).
// 2. HEDR version: 0.95 and 1.0 are Fallout 4; 0.94, 1.7, 1.71 are Skyrim.
// 3. Master file names (Fallout4.esm vs Skyrim.esm).
PluginGameInfo DetectGame(const RecordHeader* tes4,
                          CompressedFieldsCache& cache) noexcept;

// Throws std::runtime_error naming both files if games are mixed.
Game RequireSingleGame(const std::vector<std::pair<std::string, Game>>& files);

}
