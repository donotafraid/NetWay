// tests/test44.cpp
//
// Regression coverage for the 2026-10-29 snapshot:
//   T169  A destructor racing recreateGiveUpClient() after startWatchdog()
//         must not leak the unadopted candidate Impl (its watchdog keeps a
//         shared_from_this() reference alive).
//   T170  An omitted NodeId::dataType must not silently become BOOL (the zero
//         enumerator); UNKNOWN reaches the converter as TYPE_NOTSUPPORT.
//   T171  Result::map() must compile and work for both void and non-void
//         callables; the four overloads previously constructed Result with a
//         nonexistent single-argument constructor.
//   T172  Result::map_or() / map_or_else() returned a plain value but tried to
//         construct it with Result's tag types; instantiating either adapter
//         was a compile error. Also covers map_err / and_then / or_else.
//
// Run: ctest --test-dir build-asan -R 'ClientLifecycle.T169|ClientNodeId.T170|ClientResult.T17[12]'

#include "OPCUAPacking/ua.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "tests/include/ClientTestHooks.h"
#include "tests/include/MockSdk.h"
#include "tests/include/util/wait_for.h"

using namespace std::chrono_literals;

namespace {

std::unique_ptr<OPC_UA_Client>
makeMockClient(const std::shared_ptr<MockSdk> &sdk, bool enableWatchdog) {
  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  cfg.watchdogIntervalMs = 1000;
  cfg.giveUpThresholdMs = 60000;
  auto r = OPCUAPacking::test::ClientTestHooks::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", cfg, /*usedefault=*/false,
      enableWatchdog);
  EXPECT_TRUE(r.has_value()) << (r.is_fail() ? r.get_error()->what() : "");
  if (r.is_fail())
    return nullptr;
  return std::move(*r.get());
}

} // namespace

// ---------------------------------------------------------------------------
// T169: destroy the client while recreate is between the new Impl's
// startWatchdog() and the swap into pImpl.
//
// The destructor sets m_terminated, so recreate returns ALREADY_TERMINATED
// before the swap. Without an adoption guard, the candidate Impl is only
// referenced by its own watchdog thread (shared_from_this). It is never
// destroyed and the UA_Client is never deleted. The count assertion is the
// oracle; the 300 ms wait only establishes the race window.
// ---------------------------------------------------------------------------
TEST(ClientLifecycle, T169_RecreateTerminatedRace_DoesNotLeakCandidate) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeMockClient(sdk, /*enableWatchdog=*/false);
  ASSERT_NE(c, nullptr);

  ASSERT_TRUE(c->setGiveUpSignal().is_success());
  ASSERT_EQ(c->checkLifeState().value_or(LifeState::RUNNING),
            LifeState::GIVEN_UP);

  std::mutex m;
  std::condition_variable cv;
  bool entered = false;
  bool release = false;
  sdk->setGetStateHook([&] {
    std::unique_lock<std::mutex> lk(m);
    if (!entered) {
      entered = true;
      cv.notify_all();
      cv.wait(lk, [&] { return release; });
    }
  });

  OPC_UA_Client *raw = c.get();
  std::atomic<bool> recreateReturned{false};
  std::thread recreator([&] {
    (void)raw->recreateGiveUpClient();
    recreateReturned.store(true, std::memory_order_release);
  });

  bool enteredOk = false;
  {
    std::unique_lock<std::mutex> lk(m);
    enteredOk = cv.wait_for(lk, 5s, [&] { return entered; });
  }
  if (!enteredOk) {
    {
      std::lock_guard<std::mutex> lk(m);
      release = true;
    }
    cv.notify_all();
    recreator.join();
    FAIL() << "recreate 未进入候选 Impl 的 getState";
  }

  std::thread destroyer([&] { c.reset(); });
  std::this_thread::sleep_for(300ms);

  {
    std::lock_guard<std::mutex> lk(m);
    release = true;
  }
  cv.notify_all();

  const bool returned = waitFor([&] { return recreateReturned.load(); }, 5s);
  recreator.join();
  destroyer.join();
  EXPECT_TRUE(returned) << "放行 getState 后 recreate 未返回";

  EXPECT_EQ(sdk->clientNewCallCount(), sdk->clientDeleteCallCount())
      << "候选 Impl 的 watchdog 仍持有 shared_from_this，UA_Client 未释放";
  sdk->setGetStateHook(nullptr);
}

// ---------------------------------------------------------------------------
// T170: an omitted dataType must be UNKNOWN, not BOOL.
//
// The old aggregate left dataType value-initialized to S7DataType::BOOL.
// A caller that supplied only ns/id silently got a BOOL decode. UNKNOWN is
// deterministic: the read is sent (the wrapper does not know the remote type
// before the response), then the per-item converter reports TYPE_NOTSUPPORT.
// ---------------------------------------------------------------------------
TEST(ClientNodeId, T170_OmittedDataTypeIsUnknownNotBool) {
  OPC_UA_Client::NodeId omitted{1, "A"};
  EXPECT_EQ(omitted.dataType, S7DataType::UNKNOWN)
      << "缺省 dataType 被值初始化为 BOOL；调用方会静默按 BOOL 解释";

  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeMockClient(sdk, /*enableWatchdog=*/false);
  ASSERT_NE(c, nullptr);

  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                            /*allocResults=*/true);
  sdk->setReadItemInt16(0, 7);

  std::vector<OPC_UA_Client::ReadValue> reads{{omitted}};
  auto rr = c->batchRead(reads);
  ASSERT_TRUE(rr.is_success()) << (rr.is_fail() ? rr.get_error()->what() : "");
  const auto &out = rr.value_or({});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_FALSE(out[0].value.has_value());
  EXPECT_EQ(out[0].errorCode, RichError::ErrorCode::TYPE_NOTSUPPORT);

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ---------------------------------------------------------------------------
// T171: all four Result::map() overloads.
// ---------------------------------------------------------------------------
TEST(ClientResult, T171_MapVoidAndValueOverloads) {
  auto value = Result<int, RichError>::success(41).map(
      [](int v) { return v + 1; });
  ASSERT_TRUE(value.is_success());
  EXPECT_EQ(value.value_or(0), 42);

  auto unit = Result<Unit, RichError>::success(Unit{}).map([](Unit) {});
  EXPECT_TRUE(unit.is_success());

  auto bad = Result<int, RichError>::error(
                 RichError{RichError::ErrorCode::SDK_ERROR, "boom"})
                 .map([](int v) { return v + 1; });
  ASSERT_TRUE(bad.is_fail());
  EXPECT_EQ(bad.get_error()->code(), RichError::ErrorCode::SDK_ERROR);

  const auto cv = Result<int, RichError>::success(7);
  auto fromConstLvalue = cv.map([](const int &v) { return v * 2; });
  EXPECT_EQ(fromConstLvalue.value_or(0), 14);

  auto mv = Result<int, RichError>::success(8);
  auto fromRvalue = std::move(mv).map([](int &&v) { return v * 2; });
  EXPECT_EQ(fromRvalue.value_or(0), 16);

  const auto cmv = Result<int, RichError>::success(9);
  auto fromConstRvalue = std::move(cmv).map([](const int &&v) { return v * 2; });
  EXPECT_EQ(fromConstRvalue.value_or(0), 18);
}

// ---------------------------------------------------------------------------
// T172: map_or / map_or_else return a plain value, not a Result. The old
// implementation returned `ReturnType(SuccessTag{}, ...)`, which is invalid
// for scalar ReturnType; merely instantiating the template failed to compile.
// The same test exercises the other public adapters so a broken tag
// construction cannot hide behind an uninstantiated template again.
// ---------------------------------------------------------------------------
TEST(ClientResult, T172_Adapters_MapOrMapOrElseAndChain) {
  auto ok = Result<int, RichError>::success(7);
  EXPECT_EQ(ok.map_or([](int v) { return v + 1; }, 0), 8);

  const auto cok = Result<int, RichError>::success(9);
  EXPECT_EQ(cok.map_or([](const int &v) { return v + 1; }, 0), 10);

  auto rvalueMapped = Result<int, RichError>::success(11).map_or(
      [](int &&v) { return v + 1; }, 0);
  EXPECT_EQ(rvalueMapped, 12);

  const auto cok2 = Result<int, RichError>::success(13);
  EXPECT_EQ(std::move(cok2).map_or([](const int &&v) { return v + 1; }, 0),
            14);

  auto bad = Result<int, RichError>::error(
      RichError{RichError::ErrorCode::SDK_ERROR, "boom"});
  EXPECT_EQ(bad.map_or([](int v) { return v + 1; }, -1), -1);
  EXPECT_EQ(bad.map_or_else([](int v) { return v + 1; }, [] { return -2; }),
            -2);
  EXPECT_EQ(ok.map_or_else([](int v) { return v + 1; }, [] { return -2; }), 8);

  auto mappedError = ok.map_err(
      [](const RichError &e) { return e.code(); });
  ASSERT_TRUE(mappedError.is_success());
  EXPECT_EQ(mappedError.value_or(-1), 7);

  auto mappedBad = bad.map_err(
      [](const RichError &e) { return e.code(); });
  ASSERT_TRUE(mappedBad.is_fail());
  ASSERT_NE(mappedBad.get_error(), nullptr);
  EXPECT_EQ(*mappedBad.get_error(), RichError::ErrorCode::SDK_ERROR);

  auto chained = ok.and_then([](int v) {
    return Result<std::string, RichError>::success(std::to_string(v));
  });
  ASSERT_TRUE(chained.is_success());
  EXPECT_EQ(chained.value_or(""), "7");

  auto recovered = bad.or_else([](const RichError &) {
    return Result<int, RichError>::success(0);
  });
  ASSERT_TRUE(recovered.is_success());
  EXPECT_EQ(recovered.value_or(-1), 0);
}
