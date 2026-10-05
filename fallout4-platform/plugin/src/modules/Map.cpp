// F26 map markers and server-decided fast travel.
//
// setMapMarker: the marker's runtime flags live in MapMarkerData (byte
// 0x10: 0x01 visible, 0x02 can travel to; the members are private in
// CommonLibF4, so the byte is addressed by its offset). A marker that
// becomes visible goes through Papyrus ObjectReference.AddToMap, so the
// engine's own bookkeeping (Pip-Boy map data, compass) follows; hiding and
// the travel flag are written directly (no Papyrus API hides a marker).
//
// Fast travel: the Pip-Boy map's confirm stores the destination in
// PipboyManager::fastTravelLocation, and the engine travels once the
// Pip-Boy has closed (PlayerCharacter::QueueFastTravel). The handle is taken
// and cleared every frame before that, so the engine never travels; the
// marker goes to the server (fastTravelRequested), which teleports the
// player (teleportActor) when it allows the travel.
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "GameUtil.h"
#include "Modules.h"
#include "Papyrus.h"
#include "Platform.h"

#include <cstddef>
#include <cstdint>
#include <format>

namespace fmp::modules {

namespace {
constexpr std::size_t kFlagsOffset = 0x10; // MapMarkerData::flags
constexpr std::uint8_t kVisible = 0x01;
constexpr std::uint8_t kCanTravelTo = 0x02;

// The marker's runtime flags, or null for a reference without marker data.
std::uint8_t* MarkerFlags(RE::TESObjectREFR* ref)
{
  auto data = ref ? ref->GetMapMarkerData() : nullptr;
  return data ? reinterpret_cast<std::uint8_t*>(data) + kFlagsOffset : nullptr;
}

void SetMapMarker(Platform& p, uint32_t id, bool visible, bool canTravelTo)
{
  auto ref = game::Ref(id);
  if (!ref) {
    p.Log("warn", std::format("setMapMarker: unknown marker {:X}", id));
    return;
  }
  canTravelTo = visible && canTravelTo;
  auto flags = MarkerFlags(ref);
  if (!flags) {
    // No runtime marker data: Papyrus can still show it, not hide it.
    if (visible) {
      papyrus::CallMethod(ref, "ObjectReference", "AddToMap", nullptr,
                          canTravelTo);
    } else {
      p.Log("warn",
            std::format("setMapMarker: {:X} has no marker data to hide", id));
    }
    return;
  }
  const std::uint8_t before = *flags;
  if (visible && !(before & kVisible)) {
    // [verify] AddToMap shows the marker on the Pip-Boy map without a
    // "discovered" message or XP of its own
    papyrus::CallMethod(ref, "ObjectReference", "AddToMap", nullptr,
                        canTravelTo);
    if (!canTravelTo) {
      *flags = static_cast<std::uint8_t>(before & ~kCanTravelTo);
    }
    return;
  }
  auto after = static_cast<std::uint8_t>(before & ~(kVisible | kCanTravelTo));
  if (visible) {
    after |= kVisible;
  }
  if (canTravelTo) {
    after |= kCanTravelTo;
  }
  // [verify] the runtime flags use the FNAM bits (0x01 visible, 0x02 can
  // travel to), and a hidden marker leaves the Pip-Boy map and the compass
  // without a reload
  *flags = after;
}

// Takes the destination the Pip-Boy map queued and sends it to the server.
void PollFastTravel(Platform& p)
{
  auto pipboy = RE::PipboyManager::GetSingleton();
  if (!pipboy || !pipboy->fastTravelLocation) {
    return;
  }
  // [verify] the confirm sets fastTravelLocation, and the travel starts only
  // after the Pip-Boy closed (frames later), so clearing it here cancels it
  auto marker = pipboy->fastTravelLocation.get();
  pipboy->fastTravelLocation.reset();
  if (!marker) {
    return;
  }
  const auto id = marker->GetFormID();
  p.Emit("fastTravelRequested", Json{ { "marker", id } });
  p.Log("info", std::format("Fast travel to {:X} sent to the server", id));
}
}

void InstallMap(Platform& p)
{
  p.RegisterNative("setMapMarker", [&p](const Json& a) -> Json {
    const auto& ref = a.at(0);
    if (!ref.is_number()) {
      return nullptr; // marker not streamed in; the client refreshes later
    }
    SetMapMarker(p, ref.get<uint32_t>(), a.at(1).get<bool>(),
                 a.at(2).get<bool>());
    return nullptr;
  });

  // Without a server the game keeps its own fast travel.
  if (p.Config().serverIp.empty()) {
    return;
  }
  p.OnFrame([&p](float) { PollFastTravel(p); });
}

}
