#pragma once
// QuickJS host for the FalloutMP client bundle (ADR-004, amended: the first
// platform slice embeds QuickJS-ng instead of Node; see STATUS deviations).
//
// The boundary to JavaScript is deliberately narrow:
//   globalThis.__fmp.native(name, argsJson) -> resultJson   (sync game call)
//   globalThis.__fmp.send(json, reliable)                   (to the server)
//   globalThis.__fmp.connect(host, port) / disconnect()
//   globalThis.__fmp.log(level, text)
// and the host calls globalThis.__fmpOnEvent(kind, payloadJson) for
// connection events, server messages and the per-frame tick.
#include <functional>
#include <memory>
#include <string>

struct JSRuntime;
struct JSContext;

namespace fmp {

class JsHost
{
public:
  struct Callbacks
  {
    std::function<std::string(const std::string& name,
                              const std::string& argsJson)>
      native;
    std::function<void(const std::string& json, bool reliable)> send;
    std::function<void(const std::string& host, int port)> connect;
    std::function<void()> disconnect;
    std::function<void(const std::string& level, const std::string& text)>
      log;
  };

  explicit JsHost(Callbacks callbacks);
  ~JsHost();
  JsHost(const JsHost&) = delete;
  JsHost& operator=(const JsHost&) = delete;

  // Runs a script. Returns false and logs on an exception.
  bool Eval(const std::string& source, const std::string& fileName);

  // Calls globalThis.__fmpOnEvent(kind, payloadJson) if the script set it,
  // then runs pending promise jobs. Exceptions are logged, never thrown.
  void Emit(const std::string& kind, const std::string& payloadJson);

  // Runs queued promise jobs (Emit does this too).
  void RunJobs();

  const Callbacks& GetCallbacks() const { return callbacks; }

  // Number of exceptions logged so far (tests, self-test).
  int ErrorCount() const { return errorCount; }

private:
  void LogException();

  Callbacks callbacks;
  JSRuntime* rt = nullptr;
  JSContext* ctx = nullptr;
  int errorCount = 0;
};

}
