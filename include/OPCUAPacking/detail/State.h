#pragma once

#include <iostream>
#include <optional>
#include <vector>
#include "Rust_error_deal/error_deal.h"

// 连接状态的枚举
enum class ConnectionState {
  OBJECT_ONLY,   // 对象存在但未连接
  CONNECTED     // 已连接
};

enum class ConnState : uint8_t {
    IDLE,       ///< 空闲，允许发起连接操作
    CONNECTING  ///< 正在执行连接操作（已持有互斥锁）
};

enum class LifeState : uint8_t {
  /// 三元组全部就绪：
  ///   sdkConnectStatus == GOOD
  ///   sdkChannelState  == OPEN
  ///   sdkSessionState  == ACTIVATED
  /// 由 TurnToRunning() 置位（看门狗判定健康时）。
  RUNNING,

  /// 已检测到连接不健康，但仍处于"自愈窗口"内：
  ///   now - m_lastBadStatusMs < giveUpThresholdMs
  /// 看门狗会继续尝试 connect()；若 connect 返回非
  /// SHUTDOWN/NULLPTR 的错误，则进入本状态。
  ///
  /// 注意：本状态期间 batchRead / batchWrite **均快速失败**
  /// （不是"只读"，而是读写都拒绝），避免业务线程在不健康连接上
  /// 长时间阻塞。恢复动作完全由看门狗承担。
  /// 由 TurnToRecovring() 置位。
  RECOVERING,

  /// 自愈窗口耗尽，或连接进入不可恢复状态：
  ///   - 主路径：now - m_lastBadStatusMs >= giveUpThresholdMs
  ///   - 辅路径：connect() 返回 SHUTDOWN 或 NULLPTR
  ///   - 旁路  ：业务主动调用 setGiveUpSignal()
  ///
  /// 进入本状态后，看门狗不再主动重连；需业务显式调用
  /// recreateGiveUpClient() 重建客户端。
  /// 由 TurnToGiveup() 或 setGiveUpSignal() 置位。
  GIVEN_UP,
};

enum class DisconnectErrorState {
  SHUTDOWN,              // 检测到shutdown =true
  NULLPTR,               // 检测到client=nullptr
  THREADBUSY,            // 检测到CAS状态竞争
  TIMEOUT_NOTDISCONNECT, // TIMEOUT分为两种情况：1.发起超时：在超时分支收尾处没有检测到disconnect成功;2.在途被放弃：在超时分支收尾处检测到disconnect成功
  DISCONNECT_FAIL, // 检测到异步断连失败
  UNKNOWN          // 检测到CAS状态机的未知错误
};

// 首先定义连接错误枚举
enum class ConnectErrorState {
  SHUTDOWN,
  NULLPTR,
  THREADBUSY,
  UNKNOWN,
  CONNECT_FAILED,
  TIMEOUT_NOTCONNECT // TIMEOUT分为两种情况：1.发起超时：在超时分支收尾处没有检测到connect连接成功；2.在途被放弃：在超时分支收尾处检测到connect连接成功
};