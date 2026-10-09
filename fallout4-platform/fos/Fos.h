#pragma once
// fos: Fallout 4 save files (.fos) for FalloutMP's entrance (F33).
//
// Reads a save into a model that keeps every byte it does not interpret,
// writes it back byte for byte, and patches the few fields the entrance
// changes (the player's position and the time) with a verify step after
// every write. Design: docs/falloutmp/features/F33-save-editor.md; format:
// docs/falloutmp/reference/fo4-save-entry.md.
//
// Pure functions, no global state, safe on a worker thread. Every failure
// is a fos::Error with a code; damaged input never reads out of bounds.
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fmp::fos {

using Bytes = std::vector<uint8_t>;

enum class ErrorCode
{
  Truncated,    // a read ran past the end of the file or a block
  BadMagic,     // not FO4_SAVEGAME
  Unsupported,  // header version or formVersion this code doesn't know
  BadLayout,    // sections, counts or offsets don't add up
  Inflate,      // bad zlib data in a change form
  TooLarge,     // a size limit was exceeded
  NotATemplate, // parses, but can't be used as an entry template
  PatchRefused, // the patch values are out of range or not encodable
  VerifyFailed, // the written file doesn't match what was intended
  Io            // reading or writing a file failed (fmp_savetool)
};

const char* ToString(ErrorCode code);

class Error : public std::runtime_error
{
public:
  Error(ErrorCode code, const std::string& message,
        std::optional<size_t> offset = std::nullopt);
  ErrorCode Code() const { return code; }
  std::optional<size_t> Offset() const { return offset; }

private:
  ErrorCode code;
  std::optional<size_t> offset;
};

// A 24-bit save reference. The top two bits are the kind.
struct RefId
{
  enum class Kind : uint8_t
  {
    Index = 0,   // FormID array entry value-1 (0 = none)
    Default = 1, // a form of the first plugin (Fallout4.esm)
    Created = 2, // 0xFF000000 | value
    Invalid = 3
  };

  uint32_t raw = 0;

  Kind GetKind() const { return static_cast<Kind>((raw >> 22) & 3); }
  uint32_t Value() const { return raw & 0x3FFFFF; }
  bool IsNull() const { return raw == 0; }
  // A Fallout4.esm form; nullopt when it doesn't fit 22 bits.
  static std::optional<RefId> FromDefaultForm(uint32_t formId);
  // The load-order form id, given the save's FormID array (0 = none or
  // unresolvable).
  uint32_t Resolve(const std::vector<uint32_t>& formIds) const;
  friend bool operator==(const RefId&, const RefId&) = default;
};

struct Header
{
  uint32_t version = 0;
  uint32_t saveNumber = 0;
  std::string playerName; // wstr contents kept as raw bytes
  uint32_t level = 0;
  std::string location;
  std::string playTime;
  std::string race;
  uint16_t sex = 0;
  float currentXp = 0;
  float nextLevelXp = 0;
  uint64_t fileTime = 0;
  uint32_t shotWidth = 0;
  uint32_t shotHeight = 0;
  Bytes extra; // bytes of headerSize after the known fields, kept as is
  friend bool operator==(const Header&, const Header&) = default;
};

struct GlobalDataBlock
{
  uint32_t type = 0;
  Bytes data;
  friend bool operator==(const GlobalDataBlock&,
                         const GlobalDataBlock&) = default;
};

struct ChangeForm
{
  RefId refId;
  uint32_t flags = 0;
  uint8_t type = 0;     // low 6 bits of the type byte
  uint8_t version = 0;  // per-form version (= formVersion in practice)
  uint8_t width = 0;    // length field width: 0 u8, 1 u16, 2 u32
  uint32_t length2 = 0; // inflated size; 0 = data stored uncompressed
  Bytes data;           // as stored in the file

  bool Compressed() const { return length2 != 0; }
  // The body, inflated when compressed. Throws Inflate/TooLarge.
  Bytes Body() const;
  // Replaces the body, compressed again if the form was compressed; widens
  // the length field when needed.
  void SetBody(const Bytes& body);
  friend bool operator==(const ChangeForm&, const ChangeForm&) = default;
};

// Change flags of references (ReSaver ChangeFlagConstantsRefr).
inline constexpr uint32_t kFlagMove = 0x2;
inline constexpr uint32_t kFlagHavokMove = 0x4;
inline constexpr uint32_t kFlagCellChanged = 0x8;
inline constexpr uint32_t kFlagPromoted = 0x2000000;

inline constexpr uint8_t kChangeFormRefr = 0;
inline constexpr uint8_t kChangeFormAchr = 1;
inline constexpr uint32_t kPlayerRefRaw = 0x400014; // kind 1, form 0x14

inline constexpr uint32_t kGlobalGameYear = 0x35;
inline constexpr uint32_t kGlobalGameMonth = 0x36;
inline constexpr uint32_t kGlobalGameDay = 0x37;
inline constexpr uint32_t kGlobalGameHour = 0x38;
inline constexpr uint32_t kGlobalGameDaysPassed = 0x39;
inline constexpr uint32_t kGlobalTimeScale = 0x3A;
inline constexpr uint32_t kCommonwealth = 0x3C;
inline constexpr uint32_t kPowerArmorFrameBase = 0x2079E;

struct SaveFile
{
  Header header;
  Bytes screenshot; // shotWidth * shotHeight * 4 bytes, RGBA
  uint8_t formVersion = 0;
  std::string gameVersion;
  std::vector<std::string> plugins;
  bool hasLightPluginList = false;
  std::vector<std::string> lightPlugins;
  std::array<uint32_t, 15> fltUnused{}; // FLT words 10..24
  std::vector<GlobalDataBlock> table1, table2, table3;
  std::vector<ChangeForm> changeForms;
  std::vector<uint32_t> formIds;
  std::vector<uint32_t> visitedWorldspaces;
  Bytes unknownTable3; // the bytes after its u32 size
  friend bool operator==(const SaveFile&, const SaveFile&) = default;
};

// Parse and write. A consistent file round-trips byte for byte.
SaveFile Parse(std::span<const uint8_t> bytes);
Bytes Serialize(const SaveFile& save);

// The file location table as Serialize computes it (also used by verify).
std::array<uint32_t, 25> ComputeFileLocationTable(const SaveFile& save);
// The file location table as stored in a file (only reads up to it).
std::array<uint32_t, 25> ReadFileLocationTable(std::span<const uint8_t> bytes);

// --- Views ---------------------------------------------------------------

struct PlayerLocation
{
  uint32_t nextObjectId = 0;
  RefId worldspace;
  int32_t gridX = 0;
  int32_t gridY = 0;
  RefId worldOrCell;
  std::array<float, 3> pos{};

  static constexpr size_t kSize = 30;
  // Table 1 type 1. Throws BadLayout when missing or not 30 bytes.
  static PlayerLocation Read(const SaveFile& save);
  void Write(SaveFile& save) const;
  friend bool operator==(const PlayerLocation&,
                         const PlayerLocation&) = default;
};

struct GlobalVariable
{
  RefId ref;
  float value = 0;
};

// Table 1 type 3. Entries in file order.
std::vector<GlobalVariable> ReadGlobals(const SaveFile& save);
// The value of a Fallout4.esm global, if the save has it.
std::optional<float> GetGlobal(const SaveFile& save, uint32_t formId);
// Sets a global in place; false when the save doesn't have it.
bool SetGlobal(SaveFile& save, uint32_t formId, float value);

// Weather (table 1 type 6) sky mode: 3 outdoors, 1 indoors.
std::optional<uint32_t> ReadSkyMode(const SaveFile& save);

struct InitialPrefix
{
  RefId space; // cell, or the worldspace for exteriors
  std::array<float, 3> pos{};
  std::array<float, 3> rot{}; // radians
};

// Initial data type of a reference change form (4, 5, 6) or 0 for none.
int InitialDataType(const ChangeForm& form);
size_t InitialDataSize(int initialType);

// The player's ACHR change form, or nullptr.
const ChangeForm* FindPlayerRecord(const SaveFile& save);
ChangeForm* FindPlayerRecord(SaveFile& save);
// Throws BadLayout when the record has no initial data.
InitialPrefix ReadInitialPrefix(const ChangeForm& form);

// --- The entry patch -----------------------------------------------------

struct Placement
{
  uint32_t worldspace = kCommonwealth; // a Fallout4.esm WRLD form id
  std::array<float, 3> pos{};
  float yawRadians = 0;
};

struct EntryPatch
{
  std::optional<Placement> placement;
  std::optional<float> gameHour;
  std::optional<float> gameDaysPassed;
};

// Throws NotATemplate with the reason when the save can't be patched.
void CheckEntryTemplate(const SaveFile& save);
// Applies the patch to a copy. Throws PatchRefused for bad values.
SaveFile ApplyEntryPatch(const SaveFile& templateSave,
                         const EntryPatch& patch);
// Throws VerifyFailed naming the first unexpected difference.
void VerifyEntryPatch(const SaveFile& templateSave, const SaveFile& output,
                      const EntryPatch& patch);
// Parse, check, patch, serialize, parse again and verify: the bytes to
// write as the entry save.
Bytes WriteEntrySave(std::span<const uint8_t> templateBytes,
                     const EntryPatch& patch);

// --- Template validation -------------------------------------------------

struct TemplateRules
{
  uint32_t headerVersion = 15;
  std::vector<uint8_t> formVersions{ 69 };
  std::string gameVersion; // empty = any
  std::vector<std::string> plugins{ "Fallout4.esm" };
  std::vector<std::string> lightPlugins; // compared as sets, ignoring case
  uint32_t worldspace = kCommonwealth;
};

// The rules for FalloutMP's entry template: Fallout4.esm plus the nine
// free Creations (STATUS decision 2026-10-09).
TemplateRules FalloutMpTemplateRules();

struct Finding
{
  enum class Severity
  {
    Error,
    Warning,
    Note
  } severity;
  std::string code;
  std::string message;
};

std::vector<Finding> ValidateTemplate(const SaveFile& save,
                                      const TemplateRules& rules);

// --- Diff, normalize ----------------------------------------------------

struct Difference
{
  std::string where; // e.g. "table1[type 1]", "changeForm 400014"
  std::string what;
};

std::vector<Difference> Diff(const SaveFile& a, const SaveFile& b);

// Header text for a shipped template. playTime "0d.0h.0m.0 days.0
// hours.0 minutes".
void SetHeaderText(SaveFile& save, const std::string& playerName,
                   uint32_t level, const std::string& playTime);
// rgba must hold width * height * 4 bytes.
void ReplaceScreenshot(SaveFile& save, uint32_t width, uint32_t height,
                       Bytes rgba);

// --- Integrity and templates.json ---------------------------------------

std::array<uint8_t, 32> Sha256(std::span<const uint8_t> data);
std::string Sha256Hex(std::span<const uint8_t> data);

struct TemplateInfo
{
  std::string id;
  std::string file;
  std::string sha256;
  uint64_t size = 0;
  std::string gameVersion;
  uint32_t headerVersion = 0;
  uint32_t formVersion = 0;
  std::vector<std::string> plugins;
  std::vector<std::string> lightPlugins;
  int sex = 0; // 0 male, 1 female
  std::string notes;
  std::string createdAt;
};

// templates.json: an array of entries (or {"templates": [...]}).
std::vector<TemplateInfo> ParseTemplates(std::string_view json);
std::string TemplateToJson(const TemplateInfo& info);
// Compares dotted versions ("1.11.240.0"); missing parts count as 0.
int CompareVersions(std::string_view a, std::string_view b);
// The newest template not newer than the runtime, in the same major.minor
// family, preferring the given sex (-1 = any). nullptr when none fits.
const TemplateInfo* SelectTemplate(const std::vector<TemplateInfo>& list,
                                   std::string_view runtimeVersion,
                                   int sex = -1);

}
