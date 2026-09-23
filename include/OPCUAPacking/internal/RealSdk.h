// sdk/RealSdk.h
#pragma once
#include "OPCUAPacking/Isdk.h"

class RealSdk : public ISdk {
public:
  UA_Client *clientNew(const UA_ClientConfig *cfg) override {
    UA_Client *client;

   if (cfg) {
      client = UA_Client_newWithConfig(cfg);
    } else {
      UA_ClientConfig cfg_;
      std::memset(&cfg_, 0, sizeof(cfg_)); // ★ 先清零，杜绝栈垃圾
      UA_StatusCode rc = UA_ClientConfig_setDefault(&cfg_);
      if (rc != UA_STATUSCODE_GOOD) {
        return nullptr; // 让 create()/createWithSdk() 走失败分支
      }

      if (!cfg_.logging) {
        // setDefault 没给 logger（ABI 不匹配 / 日志被禁用 / 其他）
        // 直接落到静态 logger，clear = nullptr，不需要释放
        cfg_.logging = &g_customLogger;
      } else {
        // 就地复用默认 logger，保留 clear，避免泄漏
        cfg_.logging->log = g_customLogger.log;
        cfg_.logging->context = g_customLogger.context;
      }

      client = UA_Client_newWithConfig(&cfg_);
    }

    return client;
  }

  void clientDelete(UA_Client *c) override {
    if (c)
      UA_Client_delete(c);
  }

  UA_StatusCode connectAsync(UA_Client *c, const char *url) override {
    return UA_Client_connectAsync(c, url);
  }
  UA_StatusCode disconnectAsync(UA_Client *c) override {
    return UA_Client_disconnectAsync(c);
  }
  UA_StatusCode disconnect(UA_Client *c) override {
    return UA_Client_disconnect(c);
  }

  UA_StatusCode runIterate(UA_Client *c, UA_UInt32 t) override {
    return UA_Client_run_iterate(c, t);
  }
  void getState(UA_Client *c, UA_SecureChannelState *ch, UA_SessionState *ss,
                UA_StatusCode *sc) override {
    UA_Client_getState(c, ch, ss, sc);
  }
  UA_ClientConfig *getConfig(UA_Client *c) override {
    return UA_Client_getConfig(c);
  }
  void *getContext(UA_Client *c) override { return UA_Client_getContext(c); }

  UA_ReadResponse serviceRead(UA_Client *c, const UA_ReadRequest &r) override {
    return UA_Client_Service_read(c, r);
  }
  UA_WriteResponse serviceWrite(UA_Client *c,
                                const UA_WriteRequest &r) override {
    return UA_Client_Service_write(c, r);
  }
  int64_t nowMs() override {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }
};