#include "OPCUAPacking/ua.h"
#include <gtest/gtest.h>
#include "tests/MockSdk.h"
#include "tests/util/wait_for.h"
#include "tests/util/ClientTestFixture.h"

TEST_F(ClientTest, T4_RecreateAfterGiveUpSignal) {
  using namespace std::chrono_literals;

  auto c = connectTo();
  ASSERT_NE(c, nullptr);

  auto lifeStateIs = [&](LifeState target) {
    auto r = c->checkLifeState();
    return r.has_value() && r.value_or(LifeState::GIVEN_UP) == target;
  };

  {
    auto r = c->checkConnected();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value_or(ConnectionState::OBJECT_ONLY),
              ConnectionState::CONNECTED);
  }

  c->setGiveUpSignal();

  bool givenUp = waitFor([&] { return lifeStateIs(LifeState::GIVEN_UP); },
                         std::chrono::seconds(10));
  ASSERT_TRUE(givenUp) << "未在阈值内进入 GIVEN_UP";

  auto r = c->recreateGiveUpClient();
  EXPECT_TRUE(r.is_success())
      << "recreate failed: " << (r.is_fail() ? r.get_error()->what() : "");

  // ★ 强判据：recreate 后不仅检查 RUNNING，还做一次真实读写证明恢复
  EXPECT_TRUE(lifeStateIs(LifeState::RUNNING));

  OPC_UA_Client::NodeId node{1, "TestInt", S7DataType::INT};
  std::vector<OPC_UA_Client::ReadValue> rv{{node}};
  auto readResult = c->batchRead(rv);
  EXPECT_TRUE(readResult.is_success())
      << "recreate 后读写失败，说明恢复未真正发生: "
      << (readResult.is_fail() ? readResult.get_error()->what() : "");
}
