#include "JsHost.h"

#include <quickjs.h>

namespace fmp {

namespace {
JsHost* HostOf(JSContext* ctx)
{
  return static_cast<JsHost*>(JS_GetContextOpaque(ctx));
}

std::string ToString(JSContext* ctx, JSValueConst v)
{
  size_t len = 0;
  const char* s = JS_ToCStringLen(ctx, &len, v);
  if (!s) {
    return {};
  }
  std::string res(s, len);
  JS_FreeCString(ctx, s);
  return res;
}

JSValue NativeCall(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
  auto& cb = HostOf(ctx)->GetCallbacks();
  if (argc < 1 || !cb.native) {
    return JS_ThrowTypeError(ctx, "__fmp.native(name, argsJson)");
  }
  std::string name = ToString(ctx, argv[0]);
  std::string args = argc > 1 ? ToString(ctx, argv[1]) : "[]";
  try {
    std::string res = cb.native(name, args);
    return JS_NewStringLen(ctx, res.data(), res.size());
  } catch (std::exception& e) {
    return JS_ThrowInternalError(ctx, "native '%s' failed: %s", name.data(),
                                 e.what());
  }
}

JSValue NativeSend(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
  auto& cb = HostOf(ctx)->GetCallbacks();
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "__fmp.send(json, reliable)");
  }
  bool reliable = argc > 1 && JS_ToBool(ctx, argv[1]) == 1;
  if (cb.send) {
    cb.send(ToString(ctx, argv[0]), reliable);
  }
  return JS_UNDEFINED;
}

JSValue NativeConnect(JSContext* ctx, JSValueConst, int argc,
                      JSValueConst* argv)
{
  auto& cb = HostOf(ctx)->GetCallbacks();
  int32_t port = 0;
  if (argc < 2 || JS_ToInt32(ctx, &port, argv[1]) < 0) {
    return JS_ThrowTypeError(ctx, "__fmp.connect(host, port)");
  }
  if (cb.connect) {
    try {
      cb.connect(ToString(ctx, argv[0]), port);
    } catch (std::exception& e) {
      return JS_ThrowInternalError(ctx, "connect failed: %s", e.what());
    }
  }
  return JS_UNDEFINED;
}

JSValue NativeDisconnect(JSContext* ctx, JSValueConst, int, JSValueConst*)
{
  auto& cb = HostOf(ctx)->GetCallbacks();
  if (cb.disconnect) {
    cb.disconnect();
  }
  return JS_UNDEFINED;
}

JSValue NativeLog(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
  auto& cb = HostOf(ctx)->GetCallbacks();
  std::string level = argc > 0 ? ToString(ctx, argv[0]) : "info";
  std::string text;
  for (int i = 1; i < argc; ++i) {
    if (i > 1) {
      text += ' ';
    }
    text += ToString(ctx, argv[i]);
  }
  if (cb.log) {
    cb.log(level, text);
  }
  return JS_UNDEFINED;
}

void OnRejection(JSContext* ctx, JSValueConst, JSValueConst reason,
                 bool isHandled, void* opaque)
{
  if (isHandled) {
    return;
  }
  auto host = static_cast<JsHost*>(opaque);
  std::string text = ToString(ctx, reason);
  if (JS_IsError(reason)) {
    JSValue stack = JS_GetPropertyStr(ctx, reason, "stack");
    if (!JS_IsUndefined(stack)) {
      text += "\n" + ToString(ctx, stack);
    }
    JS_FreeValue(ctx, stack);
  }
  if (host->GetCallbacks().log) {
    host->GetCallbacks().log("error", "Unhandled promise rejection: " + text);
  }
}

void SetFn(JSContext* ctx, JSValueConst obj, const char* name, JSCFunction* fn,
           int length)
{
  JS_SetPropertyStr(ctx, obj, name, JS_NewCFunction(ctx, fn, name, length));
}
}

JsHost::JsHost(Callbacks callbacks_)
  : callbacks(std::move(callbacks_))
{
  rt = JS_NewRuntime();
  JS_SetMemoryLimit(rt, 512u * 1024u * 1024u);
  // The game calls in on its main thread, whose stack is shared with the
  // engine: keep QuickJS's own limit well below it.
  JS_SetMaxStackSize(rt, 384u * 1024u);
  JS_SetHostPromiseRejectionTracker(rt, OnRejection, this);
  ctx = JS_NewContext(rt);
  JS_SetContextOpaque(ctx, this);

  JSValue global = JS_GetGlobalObject(ctx);
  JSValue fmp = JS_NewObject(ctx);
  SetFn(ctx, fmp, "native", NativeCall, 2);
  SetFn(ctx, fmp, "send", NativeSend, 2);
  SetFn(ctx, fmp, "connect", NativeConnect, 2);
  SetFn(ctx, fmp, "disconnect", NativeDisconnect, 0);
  SetFn(ctx, fmp, "log", NativeLog, 2);
  JS_SetPropertyStr(ctx, global, "__fmp", fmp);
  JS_FreeValue(ctx, global);
}

JsHost::~JsHost()
{
  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);
}

bool JsHost::Eval(const std::string& source, const std::string& fileName)
{
  JSValue res = JS_Eval(ctx, source.data(), source.size(), fileName.data(),
                        JS_EVAL_TYPE_GLOBAL);
  bool ok = !JS_IsException(res);
  if (!ok) {
    LogException();
  }
  JS_FreeValue(ctx, res);
  RunJobs();
  return ok;
}

void JsHost::Emit(const std::string& kind, const std::string& payloadJson)
{
  JSValue global = JS_GetGlobalObject(ctx);
  JSValue fn = JS_GetPropertyStr(ctx, global, "__fmpOnEvent");
  if (JS_IsFunction(ctx, fn)) {
    JSValue args[2] = {
      JS_NewStringLen(ctx, kind.data(), kind.size()),
      JS_NewStringLen(ctx, payloadJson.data(), payloadJson.size())
    };
    JSValue res = JS_Call(ctx, fn, global, 2, args);
    if (JS_IsException(res)) {
      LogException();
    }
    JS_FreeValue(ctx, res);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
  }
  JS_FreeValue(ctx, fn);
  JS_FreeValue(ctx, global);
  RunJobs();
}

void JsHost::RunJobs()
{
  for (int i = 0; i < 10000; ++i) {
    JSContext* jobCtx = nullptr;
    int r = JS_ExecutePendingJob(rt, &jobCtx);
    if (r == 0) {
      return;
    }
    if (r < 0) {
      LogException();
    }
  }
}

void JsHost::LogException()
{
  ++errorCount;
  JSValue exc = JS_GetException(ctx);
  std::string text = ToString(ctx, exc);
  if (JS_IsError(exc)) {
    JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
    if (!JS_IsUndefined(stack)) {
      text += "\n" + ToString(ctx, stack);
    }
    JS_FreeValue(ctx, stack);
  }
  JS_FreeValue(ctx, exc);
  if (callbacks.log) {
    callbacks.log("error", "JavaScript exception: " + text);
  }
}

}
