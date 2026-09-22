#pragma once

#include <iostream>
#include <optional>
#include <vector>
#include "Rust_error_deal/error_deal.h"

struct ClientConfig {
  // ==================== 超时配置 ====================
  uint32_t timeoutMs = 10000;
  uint32_t sessionTimeoutMs = 600000;
  uint32_t secureChannelLifeTimeMs = 600000;
  uint32_t maxTotalWaitMs = 0;
  int64_t giveUpThresholdMs =
      60000; // 用于看门狗后台重连，超出该时间 LifeState 未恢复至 Running，则置为 GIVE-UP
             // 约束：>= 2 * watchdogIntervalMs，建议 >= maxTotalWaitMs 且 <
             // sessionTimeoutMs

  // ==================== 重试配置 ====================
  uint32_t maxRetries = 3;
  uint32_t retryBackoffBaseMs = 1000;
  uint32_t retryMaxBackoffMs = 8000;

  // ==================== 心跳 & 保活配置 ====================
  uint32_t connectivityCheckIntervalMs = 10000;
  uint32_t watchdogIntervalMs = 5000;

  // ==================== 网络环境预设 ====================
  enum NetworkProfile {
    PROFILE_LOCAL,
    PROFILE_REMOTE,
    PROFILE_MOBILE,
    PROFILE_UNSTABLE
  };

  void applyProfile(NetworkProfile profile) {
    switch (profile) {
    case PROFILE_LOCAL:
      timeoutMs = 5000;
      sessionTimeoutMs = 600000;
      secureChannelLifeTimeMs = sessionTimeoutMs;

      maxRetries = 2;
      retryBackoffBaseMs = 500;
      retryMaxBackoffMs = 4000;

      maxTotalWaitMs = 30000;

      connectivityCheckIntervalMs = 30000;
      watchdogIntervalMs = 10000;

      // giveUpThresholdMs 约束：
      // 1. >= 2 * watchdogIntervalMs (2 * 10000 = 20000)
      // 2. >= maxTotalWaitMs (30000)
      // 3. < sessionTimeoutMs (600000)
      // 取 60000：给一次会话内的重连/恢复留出充分余量
      giveUpThresholdMs = 60000; // 60秒
      break;

    case PROFILE_REMOTE:
      timeoutMs = 15000;
      sessionTimeoutMs = 600000;
      secureChannelLifeTimeMs = sessionTimeoutMs;

      maxRetries = 3;
      retryBackoffBaseMs = 1000;
      retryMaxBackoffMs = 10000;

      maxTotalWaitMs = 120000;

      connectivityCheckIntervalMs = 15000;
      watchdogIntervalMs = 8000;

      // giveUpThresholdMs 约束：
      // 1. >= 2 * watchdogIntervalMs (2 * 8000 = 16000)
      // 2. >= maxTotalWaitMs (120000)
      // 3. < sessionTimeoutMs (600000)
      giveUpThresholdMs = 180000; // 180秒（3分钟）
      break;

    case PROFILE_MOBILE:
      timeoutMs = 30000;
      sessionTimeoutMs = 600000;
      secureChannelLifeTimeMs = sessionTimeoutMs;

      maxRetries = 4;
      retryBackoffBaseMs = 2000;
      retryMaxBackoffMs = 20000;

      maxTotalWaitMs = 300000;

      connectivityCheckIntervalMs = 10000;
      watchdogIntervalMs = 5000;

      // giveUpThresholdMs 约束：
      // 1. >= 2 * watchdogIntervalMs (2 * 5000 = 10000)
      // 2. >= maxTotalWaitMs (300000)
      // 3. < sessionTimeoutMs (600000)
      giveUpThresholdMs = 360000; // 360秒（6分钟）
      break;

    case PROFILE_UNSTABLE:
      timeoutMs = 20000;
      sessionTimeoutMs = 300000;
      secureChannelLifeTimeMs = sessionTimeoutMs;

      maxRetries = 5;
      retryBackoffBaseMs = 1000;
      retryMaxBackoffMs = 15000;

      maxTotalWaitMs = 240000;

      connectivityCheckIntervalMs = 5000;
      watchdogIntervalMs = 3000;

      // giveUpThresholdMs 约束：
      // 1. >= 2 * watchdogIntervalMs (2 * 3000 = 6000)
      // 2. >= maxTotalWaitMs (240000)
      // 3. < sessionTimeoutMs (300000)
      // 为避免等于 maxTotalWaitMs 时无余量，建议略大；这里取 270000
      giveUpThresholdMs = 270000; // 270秒（4.5分钟）
      break;
    }
  }

  // ==================== 只读校验（不修改任何成员） ====================
  std::optional<std::string> check() const {
    std::vector<std::string> errors;

    auto add_error = [&](const std::string &msg) { errors.push_back(msg); };

    if (timeoutMs == 0) {
      add_error("timeoutMs must be > 0");
    }
    if (sessionTimeoutMs == 0) {
      add_error("sessionTimeoutMs must be > 0");
    }
    if (secureChannelLifeTimeMs < sessionTimeoutMs) {
      add_error("secureChannelLifeTimeMs (" +
                std::to_string(secureChannelLifeTimeMs) +
                ") must be >= sessionTimeoutMs (" +
                std::to_string(sessionTimeoutMs) + ")");
    }
    if (maxRetries > 10) {
      add_error("maxRetries (" + std::to_string(maxRetries) +
                ") must be <= 10");
    }
    if (watchdogIntervalMs < 1000 || watchdogIntervalMs > 30000) {
      add_error("watchdogIntervalMs (" + std::to_string(watchdogIntervalMs) +
                ") must be in [1000, 30000]");
    }
    if (watchdogIntervalMs >= sessionTimeoutMs && sessionTimeoutMs > 0) {
      add_error("watchdogIntervalMs (" + std::to_string(watchdogIntervalMs) +
                ") must be < sessionTimeoutMs (" +
                std::to_string(sessionTimeoutMs) + ")");
    }
    if (retryBackoffBaseMs == 0) {
      add_error("retryBackoffBaseMs must be > 0");
    }
    if (retryMaxBackoffMs < retryBackoffBaseMs) {
      add_error("retryMaxBackoffMs (" + std::to_string(retryMaxBackoffMs) +
                ") must be >= retryBackoffBaseMs (" +
                std::to_string(retryBackoffBaseMs) + ")");
    }
    if (connectivityCheckIntervalMs < 1000) {
      add_error("connectivityCheckIntervalMs (" +
                std::to_string(connectivityCheckIntervalMs) +
                ") must be >= 1000");
    }
    if (connectivityCheckIntervalMs >= sessionTimeoutMs &&
        sessionTimeoutMs > 0) {
      add_error("connectivityCheckIntervalMs (" +
                std::to_string(connectivityCheckIntervalMs) +
                ") must be < sessionTimeoutMs (" +
                std::to_string(sessionTimeoutMs) + ")");
    }
    if (retryBackoffBaseMs >= sessionTimeoutMs && sessionTimeoutMs > 0) {
      add_error("retryBackoffBaseMs (" + std::to_string(retryBackoffBaseMs) +
                ") must be < sessionTimeoutMs (" +
                std::to_string(sessionTimeoutMs) + ")");
    }
    if (timeoutMs >= sessionTimeoutMs && sessionTimeoutMs > 0) {
      add_error("timeoutMs (" + std::to_string(timeoutMs) +
                ") must be < sessionTimeoutMs (" +
                std::to_string(sessionTimeoutMs) + ")");
    }
    if (retryMaxBackoffMs >= timeoutMs && timeoutMs > 0) {
      add_error("retryMaxBackoffMs (" + std::to_string(retryMaxBackoffMs) +
                ") must be < timeoutMs (" + std::to_string(timeoutMs) + ")");
    }

    // ==================== giveUpThresholdMs 校验 ====================
    if (giveUpThresholdMs < static_cast<int64_t>(watchdogIntervalMs) * 2) {
      add_error("giveUpThresholdMs (" + std::to_string(giveUpThresholdMs) +
                ") must be >= 2 * watchdogIntervalMs (" +
                std::to_string(static_cast<int64_t>(watchdogIntervalMs) * 2) +
                ")");
    }
    if (giveUpThresholdMs <= 0) {
      add_error("giveUpThresholdMs must be > 0");
    }
    if (sessionTimeoutMs > 0 &&
        giveUpThresholdMs >= static_cast<int64_t>(sessionTimeoutMs)) {
      add_error("giveUpThresholdMs (" + std::to_string(giveUpThresholdMs) +
                ") must be < sessionTimeoutMs (" +
                std::to_string(sessionTimeoutMs) + ")");
    }

    // 验证 maxTotalWaitMs
    if (maxTotalWaitMs > 0) {
      if (maxTotalWaitMs <= timeoutMs) {
        add_error("maxTotalWaitMs (" + std::to_string(maxTotalWaitMs) +
                  ") must be > timeoutMs (" + std::to_string(timeoutMs) + ")");
      }
      if (maxTotalWaitMs >= sessionTimeoutMs && sessionTimeoutMs > 0) {
        add_error("maxTotalWaitMs (" + std::to_string(maxTotalWaitMs) +
                  ") must be < sessionTimeoutMs (" +
                  std::to_string(sessionTimeoutMs) + ")");
      }
      if (maxRetries > 0 && maxTotalWaitMs < timeoutMs + retryMaxBackoffMs) {
        add_error("maxTotalWaitMs (" + std::to_string(maxTotalWaitMs) +
                  ") must be >= timeoutMs + retryMaxBackoffMs (" +
                  std::to_string(timeoutMs + retryMaxBackoffMs) +
                  ") when maxRetries > 0");
      }

      uint64_t requiredBudget =
          2ULL * timeoutMs * (static_cast<uint64_t>(maxRetries) + 1);
      if (requiredBudget > std::numeric_limits<uint32_t>::max()) {
        add_error("maxTotalWaitMs cannot satisfy required budget (overflow)");
      } else if (maxTotalWaitMs < static_cast<uint32_t>(requiredBudget)) {
        add_error("maxTotalWaitMs (" + std::to_string(maxTotalWaitMs) +
                  ") must be >= 2*timeoutMs*(maxRetries+1) = " +
                  std::to_string(requiredBudget) +
                  " (timeoutMs=" + std::to_string(timeoutMs) +
                  ", maxRetries=" + std::to_string(maxRetries) + ")");
      }
    } else {
      add_error("maxTotalWaitMs must be > 0; "
                "call applyProfile() or applyDefaultIfInvalid()");
    }

    if (errors.empty()) {
      return std::nullopt;
    }

    std::string combined;
    for (size_t i = 0; i < errors.size(); ++i) {
      if (i > 0)
        combined += "; ";
      combined += errors[i];
    }
    return combined;
  }

  // ==================== 显式修正（调用方主动调用） ====================
  Result<bool, RichError> applyDefaultIfInvalid() {
    auto error = check();
    if (!error.has_value()) {
      return Result<bool, RichError>::success(false);
    }

    applyProfile(PROFILE_LOCAL);

    auto errorAfterFix = check();
    if (errorAfterFix.has_value()) {
      return Result<bool, RichError>::error(
          RichError{
          RichError::ErrorCode::INVALID_CONFIG,
          "ClientConfig: default config still invalid after applyProfile"});
    }

    return Result<bool, RichError>::success(true);
  }
};