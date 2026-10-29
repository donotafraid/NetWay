// tests/test43.cpp
//
// Review follow-ups for the 2026-10-29 snapshot:
//   T163  Numeric NodeId (ns=1;i=20001) must reach the SDK as
//         UA_NODEIDTYPE_NUMERIC for batchRead.
//   T164  The same must hold for batchWrite. This failed before the fix:
//         uaNodeIDAlloc() built a numeric NodeId, then batchWrite rejected
//         every non-STRING identifier with an INTERNAL_ERROR.
//   T165  Mixed STRING/NUMERIC batches keep per-index alignment.
//   T166  A real open62541 server accepts a numeric NodeId for read/write
//         round-trip, so the mock seam is not the only evidence.
//   T167  Public APIs stay no-throw when ISdk::getState() throws, including
//         connect/disconnect CAS reset and recreate rollback.
//   T168  The no-throw contract is part of the public signatures.

#include "OPCUAPacking/ua.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "tests/include/ClientTestHooks.h"
#include "tests/include/MockSdk.h"
#include "tests/include/util/ClientTestFixture.h"

namespace {

std::unique_ptr<OPC_UA_Client> makeMockClient(const std::shared_ptr<MockSdk> &sdk) {
  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  auto r = OPCUAPacking::test::ClientTestHooks::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", cfg, /*usedefault=*/false,
      /*enableWatchDog=*/false);
  EXPECT_TRUE(r.has_value()) << (r.is_fail() ? r.get_error()->what() : "");
  if (r.is_fail())
    return nullptr;
  return std::move(*r.get());
}

OPC_UA_Client::NodeId numericInt16(uint16_t ns, uint32_t id) {
  return OPC_UA_Client::NodeId{ns, id, S7DataType::INT};
}

OPC_UA_Client::NodeId stringInt16(uint16_t ns, std::string id) {
  return OPC_UA_Client::NodeId{ns, std::move(id), S7DataType::INT};
}

} // namespace

// ---------------------------------------------------------------------------
// T163: numeric NodeId on the read path.
// ---------------------------------------------------------------------------
TEST(ClientNumericNodeId, T163_BatchRead_NumericNodeIdReachesSdk) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeMockClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                            /*allocResults=*/true);
  sdk->setReadItemInt16(0, 4321);

  std::vector<OPC_UA_Client::ReadValue> reads{
      {numericInt16(1, 20001)}};
  auto rr = c->batchRead(reads);
  ASSERT_TRUE(rr.is_success()) << (rr.is_fail() ? rr.get_error()->what() : "");
  const auto &out = rr.value_or({});
  ASSERT_EQ(out.size(), 1u);
  ASSERT_TRUE(out[0].value.has_value());
  EXPECT_EQ(std::get<int16_t>(*out[0].value), 4321);

  const auto types = sdk->lastReadNodeIdTypes();
  const auto nums = sdk->lastReadNumericIds();
  const auto ns = sdk->lastReadNamespaces();
  ASSERT_EQ(types.size(), 1u);
  ASSERT_EQ(nums.size(), 1u);
  ASSERT_EQ(ns.size(), 1u);
  EXPECT_EQ(types[0], static_cast<uint32_t>(UA_NODEIDTYPE_NUMERIC));
  EXPECT_EQ(nums[0], 20001u);
  EXPECT_EQ(ns[0], 1u);

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ---------------------------------------------------------------------------
// T164: numeric NodeId on the write path.
// ---------------------------------------------------------------------------
TEST(ClientNumericNodeId, T164_BatchWrite_NumericNodeIdReachesSdk) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeMockClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setWriteResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1);
  std::vector<OPC_UA_Client::WriteValue> writes{
      {numericInt16(1, 20001), int16_t(77)}};
  auto wr = c->batchWrite(writes);
  ASSERT_TRUE(wr.is_success())
      << "numeric NodeId 被写路径拒绝；actual="
      << (wr.is_fail() ? wr.get_error()->codeName() : "OK");
  const auto &out = wr.value_or({});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].status, OPC_UA_Client::WriteResult::Status::Good);

  const auto types = sdk->lastWriteNodeIdTypes();
  const auto nums = sdk->lastWriteNumericIds();
  const auto ns = sdk->lastWriteNamespaces();
  ASSERT_EQ(types.size(), 1u);
  ASSERT_EQ(nums.size(), 1u);
  ASSERT_EQ(ns.size(), 1u);
  EXPECT_EQ(types[0], static_cast<uint32_t>(UA_NODEIDTYPE_NUMERIC));
  EXPECT_EQ(nums[0], 20001u);
  EXPECT_EQ(ns[0], 1u);

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ---------------------------------------------------------------------------
// T165: a mixed batch must not shift NodeIds between items.
// ---------------------------------------------------------------------------
TEST(ClientNumericNodeId, T165_MixedStringAndNumeric_KeepsIndexAlignment) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeMockClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setWriteResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/2);
  std::vector<OPC_UA_Client::WriteValue> writes{
      {stringInt16(1, "A"), int16_t(1)},
      {numericInt16(1, 20001), int16_t(2)}};
  auto wr = c->batchWrite(writes);
  ASSERT_TRUE(wr.is_success()) << (wr.is_fail() ? wr.get_error()->what() : "");

  const auto types = sdk->lastWriteNodeIdTypes();
  const auto nums = sdk->lastWriteNumericIds();
  const auto obs = sdk->lastWriteObservation();
  const auto &lengths = obs.nodeIds;
  ASSERT_EQ(types.size(), 2u);
  ASSERT_EQ(nums.size(), 2u);
  ASSERT_EQ(lengths.size(), 2u);
  EXPECT_EQ(types[0], static_cast<uint32_t>(UA_NODEIDTYPE_STRING));
  EXPECT_EQ(types[1], static_cast<uint32_t>(UA_NODEIDTYPE_NUMERIC));
  EXPECT_EQ(nums[0], 0u);
  EXPECT_EQ(nums[1], 20001u);
  EXPECT_EQ(lengths[0], "A");
  EXPECT_EQ(lengths[1], "");

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ---------------------------------------------------------------------------
// T166: real server, numeric NodeId read/write round-trip.
// ---------------------------------------------------------------------------
TEST_F(ClientTest, T166_RealSdk_NumericNodeId_ReadWriteRoundTrip) {
  std::unique_ptr<OPC_UA_Client> c;
  ASSERT_NO_THROW(c = connectTo());
  ASSERT_NE(c, nullptr);

  std::vector<OPC_UA_Client::WriteValue> writes{
      {numericInt16(1, kTestIntNumericId), int16_t(77)}};
  auto wr = c->batchWrite(writes);
  ASSERT_TRUE(wr.is_success())
      << "real server numeric write failed: "
      << (wr.is_fail() ? wr.get_error()->what() : "");
  ASSERT_EQ(wr.value_or({}).size(), 1u);
  EXPECT_EQ(wr.value_or({})[0].status,
            OPC_UA_Client::WriteResult::Status::Good);

  std::vector<OPC_UA_Client::ReadValue> reads{
      {numericInt16(1, kTestIntNumericId)}};
  auto rr = c->batchRead(reads);
  ASSERT_TRUE(rr.is_success())
      << "real server numeric read failed: "
      << (rr.is_fail() ? rr.get_error()->what() : "");
  ASSERT_EQ(rr.value_or({}).size(), 1u);
  ASSERT_TRUE(rr.value_or({})[0].value.has_value());
  EXPECT_EQ(std::get<int16_t>(*rr.value_or({})[0].value), 77);

  EXPECT_TRUE(c->shutdown().is_success());
}

// ---------------------------------------------------------------------------
// T167: no-throw when the SDK seam throws. Also pins CAS reset and recreate
// rollback so a thrown getState cannot wedge the client in CONNECTING or
// leave m_recreating set.
// ---------------------------------------------------------------------------
TEST(ClientNoThrow, T167_PublicApisDoNotThrow_WhenSdkThrows) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeMockClient(sdk);
  ASSERT_NE(c, nullptr);

  const auto boom = [] { throw std::runtime_error("boom-getState"); };

  // checkConnected(): isConnected() -> getState() throws.
  sdk->setGetStateHook(boom);
  auto checked = c->checkConnected();
  ASSERT_TRUE(checked.is_fail());
  EXPECT_EQ(checked.get_error()->code(), RichError::ErrorCode::INTERNAL_ERROR)
      << checked.get_error()->codeName();

  // disconnect(): disconnectAsync() succeeds, then the CLOSED poll calls
  // getState(), which throws. CASGuard must still return connState to IDLE.
  sdk->setGetStateHook(boom);
  auto disc = c->disconnect();
  ASSERT_TRUE(disc.is_fail());
  EXPECT_EQ(disc.get_error()->code(), RichError::ErrorCode::INTERNAL_ERROR);

  // Clear the hook: a successful connect proves CAS was reset (otherwise the
  // next connect would return THREAD_BUSY without touching the SDK).
  sdk->setGetStateHook(nullptr);
  auto reconnected = c->connect();
  ASSERT_TRUE(reconnected.is_success())
      << (reconnected.is_fail() ? reconnected.get_error()->codeName() : "");

  // connect(): connectAsync() succeeds, then getState() throws.
  sdk->setGetStateHook(boom);
  auto failedConnect = c->connect();
  ASSERT_TRUE(failedConnect.is_fail());
  EXPECT_EQ(failedConnect.get_error()->code(),
            RichError::ErrorCode::INTERNAL_ERROR);

  sdk->setGetStateHook(nullptr);
  auto secondConnect = c->connect();
  ASSERT_TRUE(secondConnect.is_success())
      << (secondConnect.is_fail() ? secondConnect.get_error()->codeName() : "");

  // recreateGiveUpClient(): configureClient() probes isConnected(), so a
  // throwing getState() aborts the rebuild. The old Impl must survive and
  // m_recreating must be reset, otherwise the client is permanently stuck.
  ASSERT_TRUE(c->setGiveUpSignal().is_success());
  sdk->setGetStateHook(boom);
  auto rec1 = c->recreateGiveUpClient();
  EXPECT_TRUE(rec1.is_fail());
  EXPECT_FALSE(OPCUAPacking::test::ClientTestHooks::getRecreatingStatus(*c))
      << "recreate 异常后 m_recreating 未复位，后续 recreate 全部被拒";

  sdk->setGetStateHook(nullptr);
  auto rec2 = c->recreateGiveUpClient();
  ASSERT_TRUE(rec2.is_success())
      << (rec2.is_fail() ? rec2.get_error()->codeName() : "");

  EXPECT_TRUE(c->shutdown().is_success());
}

// ---------------------------------------------------------------------------
// T168: the no-throw promise is enforced by the compiler, not only by tests.
// ---------------------------------------------------------------------------
TEST(ClientNoThrow, T168_PublicApiNoexceptContract) {
  using C = OPC_UA_Client;
  using ReadVec = std::vector<C::ReadValue>;
  using WriteVec = std::vector<C::WriteValue>;

  static_assert(noexcept(C::create(std::declval<const std::string &>(),
                                   std::declval<ClientConfig>(), false)));
  static_assert(std::is_nothrow_destructible_v<C>);
  static_assert(noexcept(std::declval<C &>().batchRead(
      std::declval<const ReadVec &>())));
  static_assert(noexcept(std::declval<C &>().batchWrite(
      std::declval<const WriteVec &>())));
  static_assert(noexcept(std::declval<C &>().checkConnected()));
  static_assert(noexcept(std::declval<C &>().isHealthy()));
  static_assert(noexcept(std::declval<C &>().checkLifeState()));
  static_assert(noexcept(std::declval<C &>().setGiveUpSignal()));
  static_assert(noexcept(std::declval<C &>().recreateGiveUpClient()));
  static_assert(noexcept(std::declval<C &>().connect()));
  static_assert(noexcept(std::declval<C &>().disconnect()));
  static_assert(noexcept(std::declval<C &>().shutdown()));
  SUCCEED();
}
