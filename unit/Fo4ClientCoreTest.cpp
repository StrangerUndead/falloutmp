// fallout4-platform/core: the QuickJS host, wire codec and runtime.
#ifdef WITH_FMP_CORE
#  include "Codec.h"
#  include "JsHost.h"
#  include "Runtime.h"
#  include <catch2/catch_all.hpp>
#  include <nlohmann/json.hpp>

using nlohmann::json;

namespace {
struct Captured
{
  std::vector<std::pair<std::string, std::string>> logs;
  std::vector<std::pair<std::string, bool>> sent;
  bool HasLog(const std::string& part) const
  {
    for (auto& [l, t] : logs) {
      if (t.find(part) != std::string::npos) {
        return true;
      }
    }
    return false;
  }
};

fmp::JsHost MakeHost(Captured& c)
{
  fmp::JsHost::Callbacks cb;
  cb.native = [](const std::string& name, const std::string& args) {
    if (name == "add") {
      auto a = json::parse(args);
      return json{ { "r", a[0].get<int>() + a[1].get<int>() } }.dump();
    }
    throw std::runtime_error("boom");
  };
  cb.send = [&](const std::string& j, bool r) { c.sent.push_back({ j, r }); };
  cb.log = [&](const std::string& l, const std::string& t) {
    c.logs.push_back({ l, t });
  };
  return fmp::JsHost(std::move(cb));
}

struct FakeGame : fmp::Game
{
  std::vector<std::string> logs;
  bool CallNative(const std::string& name, const json& args,
                  json& result) override
  {
    if (name == "echo") {
      result = args;
      return true;
    }
    return false;
  }
  void Log(const std::string&, const std::string& text) override
  {
    logs.push_back(text);
  }
};
}

TEST_CASE("JsHost runs scripts and calls natives", "[fo4][ClientCore]")
{
  Captured c;
  auto host = MakeHost(c);
  REQUIRE(host.Eval(R"(
    const r = JSON.parse(__fmp.native("add", JSON.stringify([2, 3]))).r;
    __fmp.send(JSON.stringify({ t: 1, r }), true);
    __fmp.log("info", "hello", 42);
  )",
                    "test.js"));
  REQUIRE(c.sent.size() == 1);
  REQUIRE(json::parse(c.sent[0].first)["r"] == 5);
  REQUIRE(c.sent[0].second);
  REQUIRE(c.HasLog("hello 42"));
  REQUIRE(host.ErrorCount() == 0);
}

TEST_CASE("JsHost reports exceptions, native failures and rejections",
          "[fo4][ClientCore]")
{
  Captured c;
  auto host = MakeHost(c);
  REQUIRE_FALSE(host.Eval("throw new Error('bad script')", "a.js"));
  REQUIRE(c.HasLog("bad script"));
  REQUIRE(host.Eval(R"(
    globalThis.__fmpOnEvent = (kind, payload) => {
      if (kind === "native") __fmp.native("nope", "[]");
      if (kind === "async") Promise.reject(new Error("lost promise"));
      if (kind === "job") Promise.resolve().then(() => __fmp.send(payload, false));
    };
  )",
                    "b.js"));
  host.Emit("native", "{}");
  REQUIRE(c.HasLog("native 'nope' failed: boom"));
  host.Emit("async", "{}");
  REQUIRE(c.HasLog("lost promise"));
  // Promise jobs run before Emit returns
  host.Emit("job", R"({"x":1})");
  REQUIRE(c.sent.size() == 1);
  REQUIRE(c.sent[0].first == R"({"x":1})");
  REQUIRE(host.ErrorCount() == 2);
}

TEST_CASE("Codec writes known messages in the binary format",
          "[fo4][ClientCore]")
{
  fmp::Codec codec;
  json mov = {
    { "t", 65 },      { "idx", 0xff000001u },     { "seq", 7 },
    { "ts", 1234 },   { "worldOrCell", 0x3c },    { "pos", { 1.5, 2, 3 } },
    { "yaw", 90 },    { "aimPitch", 0 },          { "aimHeading", 0 },
    { "speed", 300 }, { "direction", 0 },         { "velZ", 0 },
    { "flags", 2 },   { "healthPercentage", 100 }
  };
  auto bytes = codec.Encode(mov.dump());
  REQUIRE(bytes.size() > 2);
  REQUIRE(bytes[0] == 134); // MinPacketId
  REQUIRE(bytes[1] == 65);  // binary, not '{'
  auto back = json::parse(codec.Decode(bytes.data(), bytes.size()));
  REQUIRE(back["idx"] == 0xff000001u);
  REQUIRE(back["seq"] == 7);
  REQUIRE(back["pos"][0] == 1.5);
  REQUIRE(back["flags"] == 2);

  // The login custom packet round-trips too
  json login = { { "t", 1 }, { "contentJsonDump", R"({"a":1})" } };
  auto lb = codec.Encode(login.dump());
  REQUIRE(json::parse(codec.Decode(lb.data(), lb.size()))["contentJsonDump"] ==
          R"({"a":1})");

  // Unregistered types travel as JSON text
  auto raw = codec.Encode(R"({"t":119,"x":1})");
  REQUIRE(raw[1] == '{');
  REQUIRE(json::parse(codec.Decode(raw.data(), raw.size()))["x"] == 1);
}

TEST_CASE("Runtime forwards ticks and natives to the script",
          "[fo4][ClientCore]")
{
  FakeGame game;
  fmp::Runtime rt(game, {});
  REQUIRE(rt.LoadScript(R"(
    globalThis.ticks = [];
    globalThis.__fmpOnEvent = (kind, payload) => {
      if (kind !== "tick") return;
      const now = JSON.parse(payload).nowMs;
      const echo = JSON.parse(__fmp.native("echo", JSON.stringify([now])));
      const missing = JSON.parse(__fmp.native("unknown", "[]"));
      __fmp.log("info", `tick ${echo.r[0]} ${missing.missing}`);
    };
  )",
                        "rt.js"));
  rt.Tick(16);
  rt.Tick(33);
  REQUIRE(game.logs ==
          std::vector<std::string>{ "tick 16 true", "tick 33 true" });
  REQUIRE_FALSE(rt.IsConnected());
  // Sending while offline is a no-op, not an error
  REQUIRE(rt.LoadScript(
    R"(__fmp.send(JSON.stringify({ t: 1, contentJsonDump: "{}" }), true))",
    "s.js"));
  // A bad port is a script exception, not a crash
  REQUIRE_FALSE(rt.Js().Eval("__fmp.connect('127.0.0.1', 0)", "c.js"));
  REQUIRE(game.logs.back().find("bad port 0") != std::string::npos);
}
#endif
