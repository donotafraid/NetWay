// tests/MockSdk.h
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <tuple>
#include <optional>

extern "C" {
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/types.h>
}

#include "OPCUAPacking/Isdk.h" // 按工程实际路径

class MockSdk final : public ISdk {
public:
  enum class Mode {
    Unreachable,
    Connected,
    ChannelOpenSessionClosed,
    Disconnected
  };

  MockSdk() {}

  ~MockSdk() override {
    {
      std::lock_guard lk(clientMu_);
      if (realClient_) {
        UA_Client_delete(realClient_);
        realClient_ = nullptr;
      }
    }
  }

  // ---- 模式（原子：测试线程写 + 看门狗/业务线程读） ----
  //   allowAuto=true（默认）：connect 时自动变 Connected（现有行为
  //   allowAuto=false：即使封装层尝试 connect，也保持 Unreachable
  //                     —— T26 用它保证 recreate 的 connect 必然失败
  void setMode(Mode m, bool allowAuto = true) noexcept {
    mode_.store(m, std::memory_order_release);
    allowImplicitConversion.store(allowAuto, std::memory_order_release);
  }
  Mode mode() const noexcept { return mode_.load(std::memory_order_acquire); }

  // ============================================================
  //  ISdk: client lifecycle
  // ============================================================
  UA_Client *clientNew(const UA_ClientConfig *cfg) override {
    std::lock_guard lk(clientMu_);
    UA_Client *client = nullptr;

    if (cfg) {
      client = UA_Client_newWithConfig(cfg);
    } else {
      UA_ClientConfig cfg_;
      std::memset(&cfg_, 0, sizeof(cfg_)); // ★ 先清零，杜绝栈垃圾
      UA_ClientConfig_setDefault(&cfg_);

      if (!cfg_.logging) {
        // setDefault 没给 logger（ABI 不匹配 / 日志被禁用 / 其他）
        // 直接落到静态 logger，clear = nullptr，不需要释放
        cfg_.logging = &g_customLogger;
      } else {
        // 就地复用默认 logger，保留 clear，避免泄漏
        cfg_.logging->log = g_customLogger.log;
        cfg_.logging->context = g_customLogger.context;
      }

      client = UA_Client_newWithConfig(&cfg_);
    }

    realClient_ = client; // ★ 必须在 return 之前
    return client;
  }

  void clientDelete(UA_Client *c) override {
    std::lock_guard lk(clientMu_);
    if (!c)
      return;
    UA_Client_delete(c);
    if (realClient_ == c)
      realClient_ = nullptr;
  }

  // ============================================================
  //  ISdk: connect
  // ============================================================
  UA_StatusCode connectAsync(UA_Client *, const char *) override {
    std::function<void()> h;
    {
      std::lock_guard lk(hookMu_);
      h = connectHook_;
    }
    if (h)
      h(); // 钩子保留（T8C / TA2 用）

    if (allowImplicitConversion.load(std::memory_order_acquire))
      mode_ = Mode::Connected;
    return UA_STATUSCODE_GOOD; // ★ 始终 GOOD：SDK 接受请求
  }

  UA_StatusCode disconnectAsync(UA_Client *) override {
    if (allowImplicitConversion.load(std::memory_order_acquire))
      mode_ = Mode::Disconnected;
    return UA_STATUSCODE_GOOD;
  }
  UA_StatusCode disconnect(UA_Client *) override {
    if (allowImplicitConversion.load(std::memory_order_acquire))
      mode_ = Mode::Disconnected;
    return UA_STATUSCODE_GOOD;
  }

  // ============================================================
  //  ISdk: event loop
  // ============================================================
  // ★ 纯虚拟推进：每次驱动推进 autoAdvanceMs_，不做墙钟 sleep
  UA_StatusCode runIterate(UA_Client *, UA_UInt32) override {
    runIterateCalls_.fetch_add(1, std::memory_order_relaxed);
    const int64_t step = autoAdvanceMs_.load(std::memory_order_relaxed);
    if (step > 0) {
      nowMs_.fetch_add(step, std::memory_order_relaxed);
    }
    return UA_STATUSCODE_GOOD;
  }

  // ============================================================
  //  ISdk: state
  // ============================================================
  void getState(UA_Client *, UA_SecureChannelState *ch, UA_SessionState *ss,
                UA_StatusCode *sc) override {
    // ★ 优先消费脚本队列（T19）
    {
      std::lock_guard lk(stateMu_);
      if (!stateQueue_.empty()) {
        const auto [s, c, se] = stateQueue_.front();
        stateQueue_.pop_front();
        *sc = static_cast<UA_StatusCode>(s);
        *ch = static_cast<UA_SecureChannelState>(c);
        *ss = static_cast<UA_SessionState>(se);
        return;
      }
    }
    switch (mode_.load(std::memory_order_acquire)) {
    case Mode::Unreachable:
      *ch = UA_SECURECHANNELSTATE_CLOSED;
      *ss = UA_SESSIONSTATE_CLOSED;
      *sc = UA_STATUSCODE_BADCONNECTIONCLOSED;
      break;
    case Mode::Connected:
      *ch = UA_SECURECHANNELSTATE_OPEN;
      *ss = UA_SESSIONSTATE_ACTIVATED;
      *sc = UA_STATUSCODE_GOOD;
      break;
    case Mode::ChannelOpenSessionClosed:
      *ch = UA_SECURECHANNELSTATE_OPEN;
      *ss = UA_SESSIONSTATE_CLOSED;
      *sc = UA_STATUSCODE_GOOD;
      break;
    case Mode::Disconnected:
      *ch = UA_SECURECHANNELSTATE_CLOSED;
      *ss = UA_SESSIONSTATE_CLOSED;
      *sc = UA_STATUSCODE_GOOD;
      break;
    }
  }

  UA_ClientConfig *getConfig(UA_Client *c) override {
    return UA_Client_getConfig(c); // ★ 谁问就给谁的
  }
  void *getContext(UA_Client *c) override {
    return UA_Client_getConfig(c)->clientContext;
  }

  // ============================================================
  //  ISdk: services
  // ============================================================
  UA_ReadResponse serviceRead(UA_Client *, const UA_ReadRequest &req) override {
    readCalls_.fetch_add(1, std::memory_order_relaxed);

    struct InReadGuard {
      std::atomic<unsigned> &c;
      explicit InReadGuard(std::atomic<unsigned> &c_) : c(c_) {
        c.fetch_add(1, std::memory_order_relaxed);
      }
      ~InReadGuard() { c.fetch_sub(1, std::memory_order_relaxed); }
    } inReadGuard{inRead_};

    const int64_t step = autoAdvanceMs_.load(std::memory_order_acquire);
    if (step > 0)
      nowMs_.fetch_add(step, std::memory_order_release);

    {
      std::function<void()> h;
      {
        std::lock_guard lk(hookMu_);
        h = readHook_;
      }
      if (h)
        h();
    }

    const int delayMs = readDelayMs_.load(std::memory_order_relaxed);
    if (delayMs > 0)
      std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));

    // ★ 新增：优先消费 shape seam（T28/T29）
    {
      std::lock_guard lk(shapeMu_);
      if (readShape_.has_value()) {
        return buildShapedReadResponse(*readShape_, req);
      }
    }

    // ---- 以下保持原逻辑（脚本队列 + 完美响应） ----
    UA_StatusCode code = UA_STATUSCODE_BADNOTCONNECTED;
    {
      std::lock_guard lk(svcMu_);
      if (!readScript_.empty()) {
        code = static_cast<UA_StatusCode>(readScript_.front());
        readScript_.pop_front();
      }
    }

    UA_ReadResponse r;
    UA_ReadResponse_init(&r);
    r.responseHeader.serviceResult = code;

    if (code == UA_STATUSCODE_GOOD && req.nodesToReadSize > 0) {
      r.resultsSize = req.nodesToReadSize;
      r.results = static_cast<UA_DataValue *>(
          UA_Array_new(req.nodesToReadSize, &UA_TYPES[UA_TYPES_DATAVALUE]));
      for (size_t i = 0; i < req.nodesToReadSize; ++i) {
        UA_DataValue_init(&r.results[i]);
        r.results[i].hasStatus = true;
        r.results[i].status = UA_STATUSCODE_GOOD;
        r.results[i].hasValue = false;
      }
    }
    return r;
  }

  UA_WriteResponse serviceWrite(UA_Client *,
                                const UA_WriteRequest &req) override {
    writeCalls_.fetch_add(1, std::memory_order_relaxed);

    // ★ 新增：优先消费 shape seam（T30）
    {
      std::lock_guard lk(shapeMu_);
      if (writeShape_.has_value()) {
        return buildShapedWriteResponse(*writeShape_, req);
      }
    }

    // ---- 以下保持原逻辑 ----
    UA_StatusCode code = UA_STATUSCODE_BADNOTCONNECTED;
    {
      std::lock_guard lk(svcMu_);
      if (!writeScript_.empty()) {
        code = static_cast<UA_StatusCode>(writeScript_.front());
        writeScript_.pop_front();
      }
    }

    UA_WriteResponse r;
    UA_WriteResponse_init(&r);
    r.responseHeader.serviceResult = code;

    if (code == UA_STATUSCODE_GOOD && req.nodesToWriteSize > 0) {
      r.resultsSize = req.nodesToWriteSize;
      r.results = static_cast<UA_StatusCode *>(
          UA_Array_new(req.nodesToWriteSize, &UA_TYPES[UA_TYPES_STATUSCODE]));
      for (size_t i = 0; i < req.nodesToWriteSize; ++i) {
        r.results[i] = UA_STATUSCODE_GOOD;
      }
    }
    return r;
  }

  // ============================================================
  //  测试接缝 (1)：确定性虚拟时钟
  // ============================================================
  int64_t nowMs() override {
    return nowMs_.load(std::memory_order_relaxed); // ★ 纯读，不推进
  }
  void setNowMs(int64_t v) { nowMs_.store(v, std::memory_order_relaxed); }
  void advanceNowMs(int64_t d) {
    nowMs_.fetch_add(d, std::memory_order_relaxed);
  }
  void setAutoAdvancePerIterateMs(int64_t d) {
    autoAdvanceMs_.store(d, std::memory_order_relaxed);
  }
  int64_t autoAdvanceMs() const {
    return autoAdvanceMs_.load(std::memory_order_relaxed);
  }

  // ============================================================
  //  测试接缝 (2)：getState 脚本队列
  // ============================================================
  void enqueueState(uint32_t status, uint32_t channel, uint32_t session) {
    std::lock_guard lk(stateMu_);
    stateQueue_.emplace_back(status, channel, session);
  }

  // ============================================================
  //  测试接缝 (3)：serviceRead/Write 脚本 + 计数
  // ============================================================
  void enqueueReadServiceResult(uint32_t code) {
    std::lock_guard lk(svcMu_);
    readScript_.push_back(code);
  }
  void enqueueWriteServiceResult(uint32_t code) {
    std::lock_guard lk(svcMu_);
    writeScript_.push_back(code);
  }
  unsigned runIterateCallCount() const {
    return runIterateCalls_.load(std::memory_order_relaxed);
  }
  unsigned serviceReadCallCount() const {
    return readCalls_.load(std::memory_order_relaxed);
  }
  unsigned serviceWriteCallCount() const {
    return writeCalls_.load(std::memory_order_relaxed);
  }
  unsigned inServiceRead() const {
    return inRead_.load(std::memory_order_relaxed);
  }

  // ============================================================
  //  测试接缝 (4)：阻塞钩子
  // ============================================================
  void setConnectAsyncHook(std::function<void()> h) {
    std::lock_guard lk(hookMu_);
    connectHook_ = std::move(h);
  }
  void setServiceReadHook(std::function<void()> h) {
    std::lock_guard lk(hookMu_);
    readHook_ = std::move(h);
  }
  void setReadDelayMs(int ms) {
    readDelayMs_.store(ms, std::memory_order_relaxed);
  }

  // seam 1: 控制整个 ReadResponse 的形状
  void setReadResponseShape(uint32_t serviceResult, size_t resultsSize,
                            bool allocResults) {
    std::lock_guard lk(shapeMu_);
    readShape_ = std::make_tuple(serviceResult, resultsSize, allocResults);
  }

  // seam 2: 按项设置 status / 值（仅当 readShape_ 未设置时生效）
  void setReadItemStatus(size_t i, uint32_t status) {
    std::lock_guard lk(shapeMu_);
    if (itemStatus_.size() <= i)
      itemStatus_.resize(i + 1, UA_STATUSCODE_GOOD);
    itemStatus_[i] = status;
  }
  void setReadItemInt16(size_t i, int16_t value) {
    std::lock_guard lk(shapeMu_);
    if (itemInt16_.size() <= i)
      itemInt16_.resize(i + 1, 0);
    itemInt16_[i] = value;
  }

  // seam 3: 控制 WriteResponse 形状
  void setWriteResponseShape(uint32_t serviceResult, size_t resultsSize) {
    std::lock_guard lk(shapeMu_);
    writeShape_ = std::make_tuple(serviceResult, resultsSize);
  }

  // 清空所有 seam（用例之间隔离）
  void resetResponseShape() {
    std::lock_guard lk(shapeMu_);
    readShape_.reset();
    writeShape_.reset();
    itemStatus_.clear();
    itemInt16_.clear();
    std::lock_guard lk2(svcMu_);
    readScript_.clear();
    writeScript_.clear();
  }

  // private function:
private:
  UA_ReadResponse
  buildShapedReadResponse(const std::tuple<uint32_t, size_t, bool> &shape,
                          const UA_ReadRequest &req) {
    const auto [code, size, alloc] = shape;

    UA_ReadResponse r;
    UA_ReadResponse_init(&r);
    r.responseHeader.serviceResult = static_cast<UA_StatusCode>(code);

    // alloc=false 或 size=0：只设 serviceResult，results 保持 null
    if (!alloc || size == 0) {
      return r;
    }

    // 按 shape 声明的大小分配（故意可能 != req.nodesToReadSize，T28 用它触发
    // size 不匹配）
    r.resultsSize = size;
    r.results = static_cast<UA_DataValue *>(
        UA_Array_new(size, &UA_TYPES[UA_TYPES_DATAVALUE]));

    for (size_t i = 0; i < size; ++i) {
      UA_DataValue_init(&r.results[i]);

      // 按项 status：默认 GOOD
      uint32_t itemStatus = UA_STATUSCODE_GOOD;
      if (i < itemStatus_.size())
        itemStatus = itemStatus_[i];
      r.results[i].hasStatus = true;
      r.results[i].status = itemStatus;

      // 按项 value：仅当 status==GOOD 且 itemInt16_ 有值时填 INT16
      if (itemStatus == UA_STATUSCODE_GOOD && i < itemInt16_.size()) {
        const int16_t v = itemInt16_[i];
        const UA_StatusCode rc = UA_Variant_setScalarCopy(
            &r.results[i].value, &v, &UA_TYPES[UA_TYPES_INT16]);
        if (rc == UA_STATUSCODE_GOOD) {
          r.results[i].hasValue = true;
        }
      }
    }
    return r;
  }

  private:
    UA_WriteResponse
    buildShapedWriteResponse(const std::tuple<uint32_t, size_t> &shape,
                             const UA_WriteRequest & /*req*/) {
      const auto [code, size] = shape;

      UA_WriteResponse r;
      UA_WriteResponse_init(&r);
      r.responseHeader.serviceResult = static_cast<UA_StatusCode>(code);

      // 只按 shape 分配；size 可以 != req.nodesToWriteSize（T30
      // 用它触发不匹配）
      if (size == 0)
        return r;

      r.resultsSize = size;
      r.results = static_cast<UA_StatusCode *>(
          UA_Array_new(size, &UA_TYPES[UA_TYPES_STATUSCODE]));
      for (size_t i = 0; i < size; ++i) {
        r.results[i] = UA_STATUSCODE_GOOD;
      }
      return r;
    }

  // private member:
private:
  UA_Client *realClient_ = nullptr;
  mutable std::mutex clientMu_;

  std::atomic<Mode> mode_{Mode::Unreachable};
  std::atomic<unsigned> runIterateCalls_{0};

  std::atomic<int64_t> nowMs_{0};
  std::atomic<int64_t> autoAdvanceMs_{0};

  mutable std::mutex stateMu_;
  std::deque<std::tuple<uint32_t, uint32_t, uint32_t>> stateQueue_;

  mutable std::mutex svcMu_;
  std::deque<uint32_t> readScript_;
  std::deque<uint32_t> writeScript_;

  std::atomic<unsigned> readCalls_{0};
  std::atomic<unsigned> writeCalls_{0};
  std::atomic<unsigned> inRead_{0};

  std::mutex hookMu_;
  std::function<void()> connectHook_;
  std::function<void()> readHook_;

  std::atomic<int> readDelayMs_{0};
  // 通过SetMode()可以修改该标志，控制隐式转换Mode的过程
  std::atomic<bool> allowImplicitConversion{true};

  mutable std::mutex shapeMu_;
  std::optional<std::tuple<uint32_t, size_t, bool>> readShape_;
  std::optional<std::tuple<uint32_t, size_t>> writeShape_;
  std::vector<uint32_t> itemStatus_;
  std::vector<int16_t> itemInt16_;
};