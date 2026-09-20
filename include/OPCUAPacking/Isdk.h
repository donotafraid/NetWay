#pragma once 

#include <open62541/nodeids.h>
#include <open62541/client.h>
#include <open62541/config.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_config_default.h>

class UA_Client;

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