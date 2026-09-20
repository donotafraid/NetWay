// sdk/RealSdk.h
#pragma once
#include "OPCUAPacking/Isdk.h"
#include <iostream>

class RealSdk : public ISdk {
public:
  UA_Client *clientNew(const UA_ClientConfig *c) override {
    UA_Client *client = c ? UA_Client_newWithConfig(c) : UA_Client_new();
    return client;
  }
    void clientDelete(UA_Client* c) override { if (c) UA_Client_delete(c); }

    UA_StatusCode connectAsync(UA_Client* c, const char* url) override { return UA_Client_connectAsync(c, url); }
    UA_StatusCode disconnectAsync(UA_Client* c) override { return UA_Client_disconnectAsync(c); }
    UA_StatusCode disconnect(UA_Client* c) override { return UA_Client_disconnect(c); }

    UA_StatusCode runIterate(UA_Client* c, UA_UInt32 t) override { return UA_Client_run_iterate(c, t); }
    void getState(UA_Client* c, UA_SecureChannelState* ch, UA_SessionState* ss, UA_StatusCode* sc) override {
        UA_Client_getState(c, ch, ss, sc);
    }
    UA_ClientConfig* getConfig(UA_Client* c) override { return UA_Client_getConfig(c); }
    void* getContext(UA_Client* c) override { return UA_Client_getContext(c); }

    UA_ReadResponse serviceRead(UA_Client* c, const UA_ReadRequest& r) override {
        return UA_Client_Service_read(c, r);
    }
    UA_WriteResponse serviceWrite(UA_Client* c, const UA_WriteRequest& r) override {
        return UA_Client_Service_write(c, r);
    }
    int64_t nowMs() override {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now().time_since_epoch())
          .count();
    }
};