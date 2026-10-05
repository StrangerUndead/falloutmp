// F20 effect visuals on puppets: applyEffectVisuals (falloutPlatform.ts
// EffectNatives).
//
// Cosmetic only: the server owns every effect and its gameplay. A puppet
// shows the visuals of the items whose effects are active on that player
// (chems, food, Stealth Boy...): for each magic effect of each source item,
// its effect shader (MGEF data.effectShader, Papyrus EffectShader) and its
// hit visual effect (data.hitVisuals, Papyrus VisualEffect), plus
// refraction for invisibility effects. Per puppet the plugin keeps what it
// plays and applies only the difference; an empty list clears everything.
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "GameUtil.h"
#include "Modules.h"
#include "Papyrus.h"
#include "Platform.h"
#include "Puppets.h"

#include <format>
#include <set>
#include <unordered_map>

namespace fmp::modules {

namespace {
// [verify] a refraction power that matches a vanilla Stealth Boy user.
constexpr float kRefractionPower = 1.f;

struct Shown
{
  std::set<uint32_t> forms; // TESEffectShader / BGSReferenceEffect ids
  bool refraction = false;
};
std::unordered_map<uint32_t, Shown> g_shown; // puppet form id -> visuals

Shown Collect(const Json& items)
{
  Shown out;
  auto avs = RE::ActorValue::GetSingleton();
  auto invisibility = avs ? avs->invisibility : nullptr;
  for (auto& item : items) {
    if (!item.is_number()) {
      continue;
    }
    // ALCH, ingredients, enchantments, spells: all MagicItems
    auto magic = game::Form<RE::MagicItem>(item.get<uint32_t>());
    if (!magic) {
      continue;
    }
    for (auto effect : magic->listOfEffects) {
      auto mgef = effect ? effect->effectSetting : nullptr;
      if (!mgef) {
        continue;
      }
      const auto& data = mgef->data;
      if (data.effectShader) {
        out.forms.insert(data.effectShader->GetFormID());
      }
      if (data.hitVisuals) {
        out.forms.insert(data.hitVisuals->GetFormID());
      }
      if (data.archetype == RE::EffectArchetypes::ArchetypeID::kInvisibility ||
          (invisibility && data.primaryAV == invisibility)) {
        out.refraction = true;
      }
    }
  }
  return out;
}

// See ActorValues.cpp: an Actor-typed script object for the target, which
// every ObjectReference parameter accepts.
RE::BSTSmartPointer<RE::BSScript::Object> ScriptObject(RE::TESForm* form,
                                                       const char* type)
{
  auto vm = papyrus::Vm();
  if (!vm || !form) {
    return nullptr;
  }
  auto handle = papyrus::HandleOf(form);
  if (handle == vm->GetObjectHandlePolicy().EmptyHandle()) {
    return nullptr;
  }
  RE::BSTSmartPointer<RE::BSScript::Object> object;
  if (!vm->FindBoundObject(handle, type, false, object, false) &&
      vm->CreateObject(RE::BSFixedString(type), object) && object) {
    vm->GetObjectBindPolicy().BindObject(object, handle);
  }
  return object;
}

// Starts (infinite duration) or stops one visual on a puppet.
// [verify] the VM binds EffectShader/VisualEffect objects to base forms
// that have no attached script (DispatchMethodCall on the form's handle),
// and a shader started while the puppet has no 3D shows once it loads.
void Play(RE::Actor* puppet, uint32_t formId, bool play)
{
  auto form = RE::TESForm::GetFormByID(formId);
  auto target = ScriptObject(puppet, "Actor");
  if (!form || !target) {
    return;
  }
  if (auto shader = form->As<RE::TESEffectShader>()) {
    if (play) {
      // Play(ObjectReference akObject, float afDuration = -1.0)
      papyrus::CallMethod(shader, "EffectShader", "Play", nullptr, target,
                          -1.f);
    } else {
      // Stop(ObjectReference akObject)
      papyrus::CallMethod(shader, "EffectShader", "Stop", nullptr, target);
    }
  } else if (auto visual = form->As<RE::BGSReferenceEffect>()) {
    if (play) {
      // Play(ObjectReference akObject, float afTime = -1.0,
      //      ObjectReference akFacingObject = None)
      papyrus::CallMethod(visual, "VisualEffect", "Play", nullptr, target,
                          -1.f, RE::BSTSmartPointer<RE::BSScript::Object>{});
    } else {
      // Stop(ObjectReference akObject)
      papyrus::CallMethod(visual, "VisualEffect", "Stop", nullptr, target);
    }
  }
}

void Apply(uint32_t actorId, const Json& items)
{
  auto puppet = puppets::Get(actorId);
  if (!puppet) {
    g_shown.erase(actorId);
    return;
  }
  Shown want = items.is_array() ? Collect(items) : Shown{};
  Shown& have = g_shown[actorId];
  for (auto id : have.forms) {
    if (!want.forms.contains(id)) {
      Play(puppet, id, false);
    }
  }
  for (auto id : want.forms) {
    if (!have.forms.contains(id)) {
      Play(puppet, id, true);
    }
  }
  if (want.refraction != have.refraction) {
    // [verify] the engine doesn't reset refraction on the puppet's next
    // update (UpdateAlpha).
    puppet->SetRefraction(want.refraction,
                          want.refraction ? kRefractionPower : 0.f);
  }
  REX::DEBUG("Puppet {:X}: {} effect visuals{}", actorId, want.forms.size(),
             want.refraction ? " + refraction" : "");
  if (want.forms.empty() && !want.refraction) {
    g_shown.erase(actorId);
  } else {
    have = std::move(want);
  }
}
}

void InstallEffects(Platform& p)
{
  p.RegisterNative("applyEffectVisuals", [](const Json& a) -> Json {
    Apply(a.at(0).get<uint32_t>(), a.at(1));
    return nullptr;
  });
  // Deleted puppets (and those removed before saves) take their visuals
  // with them; forget their entries.
  p.OnFrame([&p](float) {
    if (p.FrameCount() % 120 != 0) {
      return;
    }
    std::erase_if(g_shown, [](const auto& entry) {
      return !puppets::IsPuppet(entry.first);
    });
  });
}

}
