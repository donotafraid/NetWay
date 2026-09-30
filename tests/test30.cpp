// applyDefaultIfInvalid() 用布尔返回值区分“本来就合法”和“被修复”，
// T23/T61 只间接验证了修复效果，没有钉死返回语义。这里直接锁定契约。

#include "OPCUAPacking/ua.h"
#include <gtest/gtest.h>

TEST(ClientConfigTest, T93_ApplyDefaultIfInvalid_ReportsRepair) {
  // 已合法：返回 success(false)，表示“没有改动”。
  ClientConfig good;
  good.applyProfile(ClientConfig::PROFILE_LOCAL);
  ASSERT_FALSE(good.check().has_value()) << *good.check();
  auto r1 = good.applyDefaultIfInvalid();
  ASSERT_TRUE(r1.is_success());
  EXPECT_FALSE(r1.value_or(true)) << "已合法配置不应报告被修复";

  // 非法：返回 success(true)，且修复后 check() 必须通过。
  ClientConfig bad; // maxTotalWaitMs == 0 → check() 失败
  ASSERT_TRUE(bad.check().has_value());
  auto r2 = bad.applyDefaultIfInvalid();
  ASSERT_TRUE(r2.is_success());
  EXPECT_TRUE(r2.value_or(false)) << "非法配置被修复后应返回 true";
  EXPECT_FALSE(bad.check().has_value())
      << "applyDefaultIfInvalid 修复后配置仍非法: " << *bad.check();
}