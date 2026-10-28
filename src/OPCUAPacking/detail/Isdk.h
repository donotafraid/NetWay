#pragma once 

#include <open62541/nodeids.h>
#include <open62541/client.h>
#include <open62541/config.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_config_default.h>
#include <iostream>

class UA_Client;

// open62541 的 ClientConfig 只保存 logger 的指针，因此 logger 必须有静态
// 存储期。这里只声明访问函数，定义放在 ua.cpp：RealSdk::clientNew 是内联
// 函数，若它引用的是每个 TU 各自一份的匿名命名空间对象，就构成 ODR 违规
// （IFNDR）。函数内静态对象保证全进程只有一个 logger。
// 说明：该 logger 只用 UTC（vfprintf/fputc），不碰 localtime/mktime/tzset。

UA_Logger &customLogger();

// sdk.h — 只声明 Impl 真正要拦截的调用

namespace OPCUAPacking {
namespace internal {
class ISdk {
public:
    virtual ~ISdk() = default;

    // 生命周期
    // config==nullptr：实现自行 setDefault；
    // config!=nullptr：成功时所有权转移，调用方不得 clear；
    // 失败时所有权仍在调用方。
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
    // ISdk::serviceRead/Write 返回的 response 所有权转移给调用方；
    // 自定义 ISdk 不得返回栈上数组或内部借用的数组，否则 Guard 的 clear 会
    // 释放非堆内存。
    // 对 MockSdk 的畸形响应注入只应出现在测试；生产 ISdk 不应返回
    // null+size>0 的数组。
    virtual UA_ReadResponse  serviceRead(UA_Client* c, const UA_ReadRequest& req) = 0;
    virtual UA_WriteResponse serviceWrite(UA_Client* c, const UA_WriteRequest& req) = 0;

    //  返回当前时间
    virtual int64_t nowMs() = 0;

};
} // namespace internal
} // namespace OPCUAPacking

