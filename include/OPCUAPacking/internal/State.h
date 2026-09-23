#pragma once

#include <iostream>
#include <optional>
#include <vector>
#include "Rust_error_deal/error_deal.h"

enum class ConnState : uint8_t {
    IDLE,       ///< 空闲，允许发起连接操作
    CONNECTING  ///< 正在执行连接操作（已持有互斥锁）
};

enum class DisconnectErrorState : uint8_t {
  SHUTDOWN,              // 检测到shutdown =true
  NULLPTR,               // 检测到client=nullptr
  THREADBUSY,            // 检测到CAS状态竞争
  TIMEOUT_NOTDISCONNECT, // TIMEOUT分为两种情况：1.发起超时：在超时分支收尾处没有检测到disconnect成功;2.在途被放弃：在超时分支收尾处检测到disconnect成功
  DISCONNECT_FAIL,       // 检测到异步断连失败
  UNKNOWN                // 检测到CAS状态机的未知错误
};

// 首先定义连接错误枚举
enum class ConnectErrorState : uint8_t {
  SHUTDOWN,
  NULLPTR,
  THREADBUSY,
  UNKNOWN,
  CONNECT_FAILED,
  TIMEOUT_NOTCONNECT // TIMEOUT分为两种情况：1.发起超时：在超时分支收尾处没有检测到connect连接成功；2.在途被放弃：在超时分支收尾处检测到connect连接成功
};