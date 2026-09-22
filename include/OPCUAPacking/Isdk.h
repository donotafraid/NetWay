#pragma once 

#include <open62541/nodeids.h>
#include <open62541/client.h>
#include <open62541/config.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_config_default.h>
#include <iostream>

class UA_Client;

namespace {
  //自定义Log格式，避免T5的并发导致的错误发生
  static void customLog(void * /*context*/,
                        UA_LogLevel /*level*/,
                        UA_LogCategory /*category*/,
                        const char *msg,
                        va_list args) {
      // 只用 UTC，不碰 localtime/mktime/tzset
      vfprintf(stderr, msg, args);
      fputc('\n', stderr);
  }
  
  // 2) 包成 UA_Logger。必须是静态存储期，因为 config 只存指针
  static UA_Logger g_customLogger = { customLog, nullptr,nullptr };
};


// sdk.h — 只声明 Impl 真正要拦截的调用
class ISdk {
public:
    virtual ~ISdk() = default;

    // 生命周期
    virtual UA_Client* clientNew(const UA_ClientConfig* config) = 0;
    virtual void clientDelete(UA_Client *client) = 0;

    // 连接
    virtual UA_StatusCode connectAsync(UA_Client* c, const char* url) = 0;
    virtual UA_StatusCode disconnectAsync(UA_Client* c) = 0;
    virtual UA_StatusCode disconnect(UA_Client* c) = 0;

    // 驱动 & 状态
    virtual UA_StatusCode runIterate(UA_Client* c, UA_UInt32 timeoutMs) = 0;
    virtual void getState(UA_Client* c,
                          UA_SecureChannelState* channel,
                          UA_SessionState* session,
                          UA_StatusCode* status) = 0;
    virtual UA_ClientConfig* getConfig(UA_Client* c) = 0;
    virtual void* getContext(UA_Client* c) = 0;

    // 服务
    virtual UA_ReadResponse  serviceRead(UA_Client* c, const UA_ReadRequest& req) = 0;
    virtual UA_WriteResponse serviceWrite(UA_Client* c, const UA_WriteRequest& req) = 0;

    //  返回当前时间
    virtual int64_t nowMs() = 0;

};