#include "Papyrus.h"

#include "Platform.h"

// Declared in CommonLibF4 without a definition; our functor needs one.
RE::BSScript::IStackCallbackFunctor::~IStackCallbackFunctor() = default;

namespace fmp::papyrus {

namespace {
class Callback final : public RE::BSScript::IStackCallbackFunctor
{
public:
  explicit Callback(Done done_)
    : done(std::move(done_))
  {
  }

  void CallQueued() override {}
  void CallCanceled() override { Finish(nullptr); }
  void StartMultiDispatch() override {}
  void EndMultiDispatch() override {}
  void operator()(RE::BSScript::Variable v) override
  {
    Finish(VariableToJson(v));
  }

private:
  void Finish(Json result)
  {
    if (!done) {
      return;
    }
    auto fn = std::move(done);
    done = nullptr;
    Platform::Get().QueueTask(
      [fn = std::move(fn), result = std::move(result)] { fn(result); });
  }

  Done done;
};
}

Json VariableToJson(const RE::BSScript::Variable& v)
{
  using namespace RE::BSScript;
  if (v.is<std::nullptr_t>()) {
    return nullptr;
  }
  if (v.is<bool>()) {
    return get<bool>(v);
  }
  if (v.is<std::int32_t>()) {
    return get<std::int32_t>(v);
  }
  if (v.is<std::uint32_t>()) {
    return get<std::uint32_t>(v);
  }
  if (v.is<float>()) {
    return get<float>(v);
  }
  if (v.is<RE::BSFixedString>()) {
    return std::string(get<RE::BSFixedString>(v).c_str());
  }
  if (v.is<Object>()) {
    if (auto form = UnpackVariable<RE::TESForm>(v)) {
      return Json{ { "formId", form->GetFormID() } };
    }
    return nullptr;
  }
  return nullptr;
}

RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> MakeCallback(
  Done done)
{
  return RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>{
    new Callback(std::move(done))
  };
}

RE::BSTSmartPointer<RE::BSScript::IVirtualMachine> Vm()
{
  auto gameVm = RE::GameVM::GetSingleton();
  return gameVm ? gameVm->GetVM() : nullptr;
}

uint64_t HandleOf(RE::TESForm* form)
{
  auto vm = Vm();
  if (!vm || !form) {
    return 0;
  }
  return vm->GetObjectHandlePolicy().GetHandleForObject(
    static_cast<std::uint32_t>(form->GetFormType()), form);
}

}
