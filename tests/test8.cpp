
#include "tests/util/ClientTestFixture.h"
#include "OPCUAPacking/ua.h"
#include <open62541/server.h>

// 平台探测
#if defined(__GLIBC__)
#  include <malloc.h>
#  define HAS_MALLINFO2 1
#else
#  define HAS_MALLINFO2 0
#endif

#if defined(__linux__)
#  include <filesystem>
#  define HAS_PROC_TASK 1
#else
#  define HAS_PROC_TASK 0
#endif

#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define HAS_LSAN 1
#  endif
#endif
#if !defined(HAS_LSAN) && defined(__SANITIZE_ADDRESS__)
#  define HAS_LSAN 1
#endif
#if !defined(HAS_LSAN)
#  define HAS_LSAN 0
#endif

#if HAS_LSAN
extern "C" void __lsan_do_recoverable_leak_check();
#endif

namespace {
#if HAS_MALLINFO2
size_t heapUsedBytes() {
    struct mallinfo2 mi = mallinfo2();
    return mi.uordblks;
}
#else
size_t heapUsedBytes() { return 0; }
#endif

#if HAS_PROC_TASK
size_t threadCount() {
    size_t n = 0;
    for (auto& e : std::filesystem::directory_iterator("/proc/self/task")) {
        (void)e; ++n;
    }
    return n;
}
#else
size_t threadCount() { return 0; }
#endif
}  // namespace

TEST_F(ClientTest, T9_NoLeakOnRepeatedLifecycle) {
#if !HAS_MALLINFO2
    GTEST_SKIP() << "mallinfo2 不可用；泄漏 oracle 交给 CI 的 LSan 作业。";
#endif
    // 泄漏 oracle 交给 CI 的 LSan 作业；本测试只做粗粒度堆趋势护栏。
    constexpr int kWarmup = 5;
    constexpr int kIters  = 100;

    const uint16_t ns = 1;
    const std::string varName{"TestInt"};
    const S7DataType type = S7DataType::INT;
    OPC_UA_Client::NodeId node{ns, varName, type};
    std::vector<OPC_UA_Client::ReadValue> readValueVector{{node}};

    auto runOneLifecycle = [&]() -> bool {
        auto c = connectTo();
        {
            auto r = c->batchRead(readValueVector);
            if (!r.is_success()) {
                ADD_FAILURE() << (r.get_error() ? r.get_error()->what() : "no error");
                return false;
            }
        }
        {
            OPC_UA_Client::WriteValue var{node, int16_t(0)};
            std::vector<OPC_UA_Client::WriteValue> wv{var};
            auto w = c->batchWrite(wv);
            if (!w.is_success()) {
                ADD_FAILURE() << (w.get_error() ? w.get_error()->what() : "no error");
                return false;
            }
        }
        if (!c->shutdown().is_success()) {
            ADD_FAILURE() << "shutdown failed";
            return false;
        }
        return true;
    };

    for (int i = 0; i < kWarmup; ++i) {
        ASSERT_TRUE(runOneLifecycle()) << "warmup iteration " << i;
    }

    const size_t heapBefore = heapUsedBytes();

    for (int i = 0; i < kIters; ++i) {
        ASSERT_TRUE(runOneLifecycle()) << "iteration " << i;
#if HAS_LSAN
        if (i % 10 == 9) __lsan_do_recoverable_leak_check();
#endif
    }

    const size_t heapAfter = heapUsedBytes();

    // P1-8: 先转有符号再减，避免 size_t 下溢
    const int64_t delta = static_cast<int64_t>(heapAfter) -
                          static_cast<int64_t>(heapBefore);

    // P2-10: heap 趋势降级为诊断输出；泄漏由 LSan 唯一裁决
    std::cout << "[T9] heap delta = " << delta << " bytes over "
              << kIters << " iterations\n";

    // 弱护栏：只在明显暴涨时报警，容差放宽到 256KB
    EXPECT_LT(delta, 256 * 1024)
        << "heap delta=" << delta << " bytes over " << kIters << " iterations";
}

// 线程趋势拆成独立测试，避免 GTEST_SKIP 互相遮蔽（P3-3）
TEST_F(ClientTest, T9_ThreadTrend) {
#if !HAS_PROC_TASK
  GTEST_SKIP() << "/proc/self/task 不可用；线程趋势判据跳过。";
#endif
  constexpr int kWarmup = 5;
  constexpr int kIters = 100;

  const uint16_t ns = 1;
  const std::string varName{"TestInt"};
  const S7DataType type = S7DataType::INT;
  OPC_UA_Client::NodeId node{ns, varName, type};
  std::vector<OPC_UA_Client::ReadValue> readValueVector{{node}};

  auto runOneLifecycle = [&]() -> bool {
    auto c = connectTo();
    {
      auto r = c->batchRead(readValueVector);
      if (!r.is_success()) {
        ADD_FAILURE() << (r.get_error() ? r.get_error()->what() : "no error");
        return false;
      }
    }
    {
      OPC_UA_Client::WriteValue var{node, int16_t(0)};
      std::vector<OPC_UA_Client::WriteValue> wv{var};
      auto w = c->batchWrite(wv);
      if (!w.is_success()) {
        ADD_FAILURE() << (w.get_error() ? w.get_error()->what() : "no error");
        return false;
      }
    }
    if (!c->shutdown().is_success()) {
      ADD_FAILURE() << "shutdown failed";
      return false;
    }
    return true;
  };

  for (int i = 0; i < kWarmup; ++i) {
    ASSERT_TRUE(runOneLifecycle()) << "warmup iteration " << i;
  }

  const int threadBefore = threadCount();
  for (int i = 0; i < kIters; ++i) {
    ASSERT_TRUE(runOneLifecycle()) << "iteration " << i;
  }
  const int threadAfter = threadCount();

  EXPECT_LE(threadAfter, threadBefore + 1)
      << "thread count grew from " << threadBefore << " to " << threadAfter;
}
