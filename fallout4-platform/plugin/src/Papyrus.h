#pragma once
// Calls into the game's Papyrus VM. Dispatched calls are queued by the VM
// and run on its threads; `done` callbacks are moved to the main thread
// (Platform::QueueTask) before they run.
//
// Papyrus checks argument counts: pass every parameter of the function,
// defaults included (Fallout 4 .psc signatures).
#include <RE/Fallout.h>
#include <nlohmann/json.hpp>

#include <functional>
#include <string>

namespace fmp::papyrus {

using Json = nlohmann::json;
// Result of a call: null, a number, a bool, a string, or a form id for
// forms ({"formId": id}).
using Done = std::function<void(const Json& result)>;

Json VariableToJson(const RE::BSScript::Variable& v);

RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> MakeCallback(
  Done done);

RE::BSTSmartPointer<RE::BSScript::IVirtualMachine> Vm();
uint64_t HandleOf(RE::TESForm* form);

template <class... Args>
bool CallMethod(RE::TESForm* self, const char* script, const char* function,
                Done done, Args... args)
{
  auto vm = Vm();
  if (!vm || !self) {
    return false;
  }
  auto cb = done ? MakeCallback(std::move(done))
                 : RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>{};
  return vm->DispatchMethodCall(HandleOf(self), RE::BSFixedString(script),
                                RE::BSFixedString(function), cb, args...);
}

template <class... Args>
bool CallStatic(const char* script, const char* function, Done done,
                Args... args)
{
  auto vm = Vm();
  if (!vm) {
    return false;
  }
  auto cb = done ? MakeCallback(std::move(done))
                 : RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>{};
  return vm->DispatchStaticCall(RE::BSFixedString(script),
                                RE::BSFixedString(function), cb, args...);
}

}
