#include "Platform.h"

#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "Runtime.h"

#include <chrono>
#include <fstream>
#include <sstream>

namespace fmp {

namespace {
const auto kStart = std::chrono::steady_clock::now();
}

Platform& Platform::Get()
{
  static Platform instance;
  return instance;
}

double Platform::NowMs() const
{
  return std::chrono::duration<double, std::milli>(
           std::chrono::steady_clock::now() - kStart)
    .count();
}

bool Platform::Start(PluginConfig config_, const std::string& scriptPath)
{
  config = std::move(config_);
  std::ifstream f(scriptPath, std::ios::binary);
  if (!f) {
    REX::ERROR("Missing {}: reinstall FalloutMP", scriptPath);
    RE::SendHUDMessage::ShowHUDMessage(
      "FalloutMP: client script missing, see FalloutMP.log", nullptr, false,
      true);
    return false;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  runtime = std::make_unique<Runtime>(*this, Runtime::Options{});
  if (!runtime->LoadScript(ss.str(), "falloutmp-client.js")) {
    REX::ERROR("The client script failed to load");
    return false;
  }
  REX::INFO("Client script loaded");
  return true;
}

void Platform::RegisterNative(const std::string& name, NativeFn fn)
{
  if (natives.count(name)) {
    REX::WARN("Native '{}' registered twice; keeping the last", name);
  }
  natives[name] = std::move(fn);
}

bool Platform::HasNative(const std::string& name) const
{
  return natives.count(name) > 0;
}

std::vector<std::string> Platform::NativeNames() const
{
  std::vector<std::string> out;
  for (auto& [name, fn] : natives) {
    out.push_back(name);
  }
  return out;
}

uint32_t Platform::NewAsyncId()
{
  std::lock_guard l(queueMutex);
  return nextAsyncId++;
}

void Platform::Resolve(uint32_t id, Json value)
{
  std::lock_guard l(queueMutex);
  queue.push_back({ Queued::Kind::Resolve, {}, std::move(value), id, {} });
}

void Platform::Emit(const std::string& name, Json data)
{
  std::lock_guard l(queueMutex);
  queue.push_back({ Queued::Kind::Event, name, std::move(data), 0, {} });
}

void Platform::EmitLifecycle(const std::string& kind, Json payload)
{
  std::lock_guard l(queueMutex);
  queue.push_back(
    { Queued::Kind::Lifecycle, kind, std::move(payload), 0, {} });
}

void Platform::QueueTask(std::function<void()> task)
{
  std::lock_guard l(queueMutex);
  queue.push_back({ Queued::Kind::Task, {}, nullptr, 0, std::move(task) });
}

void Platform::OnFrame(std::function<void(float)> fn)
{
  frameCallbacks.push_back(std::move(fn));
}

void Platform::OnBeforeSave(std::function<void()> fn)
{
  beforeSave.push_back(std::move(fn));
}

void Platform::OnAfterSave(std::function<void()> fn)
{
  afterSave.push_back(std::move(fn));
}

void Platform::RunBeforeSave()
{
  for (auto& fn : beforeSave) {
    try {
      fn();
    } catch (std::exception& e) {
      REX::ERROR("Before-save step failed: {}", e.what());
    }
  }
}

void Platform::RunAfterSave()
{
  for (auto& fn : afterSave) {
    try {
      fn();
    } catch (std::exception& e) {
      REX::ERROR("After-save step failed: {}", e.what());
    }
  }
  EmitLifecycle("saveFinished");
}

void Platform::Drain()
{
  std::vector<Queued> items;
  {
    std::lock_guard l(queueMutex);
    items.swap(queue);
  }
  for (auto& q : items) {
    try {
      switch (q.kind) {
        case Queued::Kind::Event:
          runtime->Emit("platformEvent",
                        Json{ { "name", q.name }, { "data", q.data } });
          break;
        case Queued::Kind::Lifecycle:
          runtime->Emit(q.name, q.data);
          break;
        case Queued::Kind::Resolve:
          runtime->Emit("nativeResolved",
                        Json{ { "id", q.id }, { "value", q.data } });
          break;
        case Queued::Kind::Task:
          q.task();
          break;
      }
    } catch (std::exception& e) {
      REX::ERROR("Queued item failed: {}", e.what());
    }
  }
}

void Platform::Tick(float dtSec)
{
  if (!runtime) {
    return;
  }
  ++frame;
  for (auto& fn : frameCallbacks) {
    try {
      fn(dtSec);
    } catch (std::exception& e) {
      REX::ERROR("Frame callback failed: {}", e.what());
    }
  }
  Drain();
  runtime->Tick(NowMs());
}

bool Platform::CallNative(const std::string& name, const Json& args,
                          Json& result)
{
  auto it = natives.find(name);
  if (it == natives.end()) {
    return false;
  }
  result = it->second(args);
  return true;
}

void Platform::Log(const std::string& level, const std::string& text)
{
  if (level == "error") {
    REX::ERROR("{}", text);
  } else if (level == "warn") {
    REX::WARN("{}", text);
  } else if (level == "trace") {
    REX::DEBUG("{}", text);
  } else {
    REX::INFO("{}", text);
  }
  if (level == "error" || level == "warn") {
    if (auto console = RE::ConsoleLog::GetSingleton()) {
      console->PrintLine("[FalloutMP] %s", text.c_str());
    }
  }
}

}
