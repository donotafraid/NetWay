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
#include <string>
#include <vector>

extern "C" {
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/types.h>
}

#include "src/OPCUAPacking/detail/Isdk.h" // 按工程实际路径


class MockSdk final : public ISdk {
  
public:
  enum class Mode {
    Unreachable,
    Connected,
    ChannelOpenSessionClosed,
    Disconnected
  };

  struct WriteObservation {
    std::vector<std::string> nodeIds;
    std::vector<int> typeIndices;          // UA_TYPES 下标；-1 表示无值
    std::vector<std::string> valueStrings; // 仅 STRING 项携带字节
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
    clientNewCalls_.fetch_add(1, std::memory_order_relaxed);
    UA_Client *client = nullptr;

    if (cfg) {
      client = UA_Client_newWithConfig(cfg);
    } else {
      UA_ClientConfig cfg_;
      std::memset(&cfg_, 0, sizeof(cfg_)); // ★ 先清零，杜绝栈垃圾
      UA_StatusCode rc = UA_ClientConfig_setDefault(&cfg_);
      if (rc != UA_STATUSCODE_GOOD) {
        // 与 RealSdk 对齐：setDefault 可能已分配 logger/eventLoop/
        // securityPolicies，直接 return 会泄漏这些插件。
        UA_ClientConfig_clear(&cfg_);
        return nullptr; // 让 create()/createWithSdk() 走失败分支
      }

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
      if (!client) {
        // newWithConfig 失败时所有权仍在 cfg_；与 RealSdk 对齐，必须清理，
        // 否则 logger/eventLoop 等全部泄漏。
        UA_ClientConfig_clear(&cfg_);
      }
    }

    realClient_ = client; // ★ 必须在 return 之前
    return client;
  }

   unsigned syncDisconnectCallCount() const {
    return syncDisconnectCalls_.load(std::memory_order_relaxed);
  }
  unsigned clientNewCallCount() const {
    return clientNewCalls_.load(std::memory_order_relaxed);
  }
  unsigned clientDeleteCallCount() const {
    return clientDeleteCalls_.load(std::memory_order_relaxed);
  }

  void clientDelete(UA_Client *c) override {
    std::lock_guard lk(clientMu_);
    if (!c)
      return;
    clientDeleteCalls_.fetch_add(1, std::memory_order_relaxed);
    UA_Client_delete(c);
    if (realClient_ == c)
      realClient_ = nullptr;
  }

  // ============================================================
  //  ISdk: connect
  // ============================================================
  UA_StatusCode connectAsync(UA_Client *, const char *url) override {
    connectAsyncCalls_.fetch_add(1, std::memory_order_relaxed);
    {
      std::lock_guard lk(urlMu_);
      lastConnectUrl_ = (url ? std::string(url) : std::string());
    }

    std::function<void()> h;
    {
      std::lock_guard lk(hookMu_);
      h = connectHook_;
    }
    if (h)
      h(); // 钩子保留（T8C / TA2 用）

    if (allowImplicitConversion.load(std::memory_order_acquire))
      mode_ = Mode::Connected;
    return connectAsyncResult_.load(std::memory_order_relaxed);
  }
  UA_StatusCode disconnectAsync(UA_Client *) override {
    disconnectAsyncCalls_.fetch_add(1, std::memory_order_relaxed);

    std::function<void()> h;
    {
      std::lock_guard lk(hookMu_);
      h = disconnectHook_;
    }
    if (h)
      h(); // 阻塞点必须在改状态之前

    if (allowImplicitConversion.load(std::memory_order_acquire))
      mode_ = Mode::Disconnected;
    return disconnectAsyncResult_.load(std::memory_order_relaxed);
  }
  UA_StatusCode disconnect(UA_Client *) override {
    syncDisconnectCalls_.fetch_add(1, std::memory_order_relaxed);
    if (allowImplicitConversion.load(std::memory_order_acquire))
      mode_ = Mode::Disconnected;
    return UA_STATUSCODE_GOOD;
  }

  std::string lastConnectUrl() const {
    std::lock_guard lk(urlMu_);
    return lastConnectUrl_;
  }
  unsigned connectAsyncCallCount() const {
    return connectAsyncCalls_.load(std::memory_order_relaxed);
  }

  void setDisconnectAsyncHook(std::function<void()> h) {
    std::lock_guard lk(hookMu_);
    disconnectHook_ = std::move(h);
  }
  void setConnectAsyncResult(UA_StatusCode code) {
    connectAsyncResult_.store(code, std::memory_order_relaxed);
  }
   void setDisconnectAsyncResult(UA_StatusCode code) {
    disconnectAsyncResult_.store(code, std::memory_order_relaxed);
  }
  unsigned disconnectAsyncCallCount() const {
    return disconnectAsyncCalls_.load(std::memory_order_relaxed);
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

    // 记录下发到 SDK 的 NodeId 字节长度（用于验证封装层没有用 strlen 截断
    // 含内嵌 '\0' 的 std::string）。
    {
      std::lock_guard lk(requestMu_);
      lastReadNodeIdLengths_.clear();
      lastReadNamespaces_.clear();
      for (size_t i = 0; i < req.nodesToReadSize; ++i) {
        lastReadNodeIdLengths_.push_back(
            req.nodesToRead[i].nodeId.identifier.string.length);
        // 观察点：封装层必须把 NodeId.ns 原样传给 SDK，不得固定为 0。
        lastReadNamespaces_.push_back(
            req.nodesToRead[i].nodeId.namespaceIndex);
      }
    }

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

    // malformed seam: resultsSize>0 with results==nullptr
    {
      std::lock_guard lk(shapeMu_);
       if (readNullResultsSize_.has_value()) {
        UA_ReadResponse r;
        UA_ReadResponse_init(&r);
        r.responseHeader.serviceResult = UA_STATUSCODE_GOOD;
        r.resultsSize = *readNullResultsSize_;
        r.results = nullptr;
        return r;
      }
      if (readShape_.has_value()) {
        UA_ReadResponse r = buildShapedReadResponse(*readShape_, req);
        // seam 3d: build a malformed response with diagnosticInfosSize > 0 and
        // diagnosticInfos == nullptr, to prove the response guard neutralises
        // the diagnosticInfos array exactly like it does for results (T130).
        if (readNullDiagInfosSize_.has_value() && *readNullDiagInfosSize_ > 0) {
          r.diagnosticInfosSize = *readNullDiagInfosSize_;
          r.diagnosticInfos = nullptr;
        }
        return r;
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

    struct InWriteGuard {
      std::atomic<unsigned> &c;
      explicit InWriteGuard(std::atomic<unsigned> &c_) : c(c_) {
        c.fetch_add(1, std::memory_order_relaxed);
      }
      ~InWriteGuard() { c.fetch_sub(1, std::memory_order_relaxed); }
    } inWriteGuard{inWrite_};

    // 观察点：记录封装层真正下发的 NodeId / 值类型 / 字符串字节。
    {
      std::lock_guard lk(requestMu_);

      {
        std::function<void()> h;
        {
          std::lock_guard lk(hookMu_);
          h = writeHook_;
        }
        if (h)
          h();
      }

      lastWriteNodeIds_.clear();
      lastWriteTypeIndices_.clear();
      lastWriteValueStrings_.clear();
      lastWriteNamespaces_.clear();
      for (size_t i = 0; i < req.nodesToWriteSize; ++i) {
        const UA_WriteValue &w = req.nodesToWrite[i];
        lastWriteNamespaces_.push_back(w.nodeId.namespaceIndex);
        if (w.nodeId.identifierType == UA_NODEIDTYPE_STRING &&
            w.nodeId.identifier.string.data) {
          lastWriteNodeIds_.emplace_back(
              reinterpret_cast<const char *>(w.nodeId.identifier.string.data),
              w.nodeId.identifier.string.length);
        } else {
          lastWriteNodeIds_.emplace_back();
        }

        int typeIndex = -1;
        if (w.value.hasValue && w.value.value.type) {
          for (int k = 0; k < UA_TYPES_COUNT; ++k) {
            if (w.value.value.type == &UA_TYPES[k]) {
              typeIndex = k;
              break;
            }
          }
        }
        lastWriteTypeIndices_.push_back(typeIndex);

        if (typeIndex == UA_TYPES_STRING && w.value.value.data) {
          const UA_String *sv =
              static_cast<const UA_String *>(w.value.value.data);
          if (sv->data && sv->length)
            lastWriteValueStrings_.emplace_back(
                reinterpret_cast<const char *>(sv->data), sv->length);
          else
            lastWriteValueStrings_.emplace_back();
        } else {
          lastWriteValueStrings_.emplace_back();
        }
      }
    }

    // ★ 新增：优先消费 shape seam（T30）
    {
      std::lock_guard lk(shapeMu_);
      // seam 3c：构造 resultsSize>0 且 results==nullptr 的写响应（T106）。
      // 与读路径 setReadResponseNullResults 对称。
      if (writeNullResultsSize_.has_value()) {
        UA_WriteResponse r;
        UA_WriteResponse_init(&r);
        r.responseHeader.serviceResult = UA_STATUSCODE_GOOD;
        r.resultsSize = *writeNullResultsSize_;
        r.results = nullptr;
        return r;
      }
      if (writeShape_.has_value()) {
        UA_WriteResponse r = buildShapedWriteResponse(*writeShape_, req);
        // seam 3d (write): symmetric to the read seam above.
        if (writeNullDiagInfosSize_.has_value() && *writeNullDiagInfosSize_ > 0) {
          r.diagnosticInfosSize = *writeNullDiagInfosSize_;
          r.diagnosticInfos = nullptr;
        }
        return r;
      }
    }

    // ---- 以下保持原逻辑 ----
    // UA_StatusCode code = UA_STATUSCODE_BADNOTCONNECTED;
    UA_StatusCode code = UA_STATUSCODE_GOOD;
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
  //  ISdk: string read 
  // ============================================================
  void setReadItemString(size_t i, const std::string &value) {
    std::lock_guard lk(shapeMu_);
    if (itemString_.size() <= i)
      itemString_.resize(i + 1);
    if (itemStringSet_.size() <= i)
      itemStringSet_.resize(i + 1, 0);
    itemString_[i] = value;
    itemStringSet_[i] = 1;
  }

  void clearLastConnectUrl() {
    std::lock_guard lk(urlMu_);
    lastConnectUrl_.clear();
  }
  // ============================================================
  //  ISdk: callback
  // ============================================================
  void fireStateCallback(uint32_t status, uint32_t channel, uint32_t session) {
    std::lock_guard lk(clientMu_);
    if (!realClient_)
      return;
    UA_ClientConfig *cfg = UA_Client_getConfig(realClient_);
    if (cfg && cfg->stateCallback)
      cfg->stateCallback(realClient_,
                         static_cast<UA_SecureChannelState>(channel),
                         static_cast<UA_SessionState>(session),
                         static_cast<UA_StatusCode>(status));
  }

  void fireInactivityCallback() {
    std::lock_guard lk(clientMu_);
    if (!realClient_)
      return;
    UA_ClientConfig *cfg = UA_Client_getConfig(realClient_);
    if (cfg && cfg->inactivityCallback)
      cfg->inactivityCallback(realClient_);
  }

  // ============================================================
  //  ISdk: rawClient
  // ============================================================
  UA_Client *rawClient() const {
    std::lock_guard lk(clientMu_);
    return realClient_;
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
  unsigned inServiceWrite() const {
    return inWrite_.load(std::memory_order_relaxed);
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
  void setServiceWriteHook(std::function<void()> h) {
    std::lock_guard lk(hookMu_);
    writeHook_ = std::move(h);
  }
  void setReadDelayMs(int ms) {
    readDelayMs_.store(ms, std::memory_order_relaxed);
  }
  // seam: construct a malformed response with resultsSize == requested but
  // results == nullptr, to reach the `!response.results` exit in batchRead.
  void setReadResponseNullResults(size_t resultsSize) {
    std::lock_guard lk(shapeMu_);
    readNullResultsSize_ = resultsSize;
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

  // 通用标量 seam：按给定 UA 类型深拷贝一个标量到第 i 个结果项。
  // 用于覆盖 TypeTraits 的全部 12 个 S7DataType 特化（T83）。
  void setReadItemScalar(size_t i, const void *p, const UA_DataType *type) {
    auto holder = std::make_shared<ScalarHolder>();
    holder->type = type;
    holder->data = UA_new(type);
    if (holder->data)
      UA_copy(p, holder->data, type);
    std::lock_guard lk(shapeMu_);
    if (itemScalar_.size() <= i)
      itemScalar_.resize(i+ 1);
    itemScalar_[i] = std::move(holder);
  }

    // 数组 seam：构造 arrayLength>0 的 INT16 结果项，
  // 用于验证读转换不会把数组当作标量。
  void setReadItemInt16Array(size_t i, const std::vector<int16_t> &values) {
    auto holder = std::make_shared<ScalarHolder>();
    holder->type = &UA_TYPES[UA_TYPES_INT16];
    if (!values.empty()) {
      holder->data = UA_Array_new(values.size(), &UA_TYPES[UA_TYPES_INT16]);
      if (holder->data) {
        std::memcpy(holder->data, values.data(),
                    values.size() * sizeof(int16_t));
        holder->arrayLength = values.size();
      }
    }
    std::lock_guard lk(shapeMu_);
    if (itemScalar_.size() <= i)
      itemScalar_.resize(i +1);
    itemScalar_[i] = std::move(holder);
  }

  // seam 3: 控制 WriteResponse 形状
  void setWriteResponseShape(uint32_t serviceResult, size_t resultsSize) {
    std::lock_guard lk(shapeMu_);
    writeShape_ = std::make_tuple(serviceResult, resultsSize);
  }

  // seam 3c: 构造 resultsSize>0 且 results==nullptr 的写响应。
  // 用于验证 WriteResponseGuard 面对畸形响应不崩溃、不泄漏（T106）。
  void setWriteResponseNullResults(size_t resultsSize) {
    std::lock_guard lk(shapeMu_);
    writeNullResultsSize_ = resultsSize;
  }

  // seam 3d: diagnosticInfosSize > 0 with diagnosticInfos == nullptr.
  void setReadResponseNullDiagnosticInfos(size_t n) {
    std::lock_guard lk(shapeMu_);
    readNullDiagInfosSize_ = n;
  }
  void setWriteResponseNullDiagnosticInfos(size_t n) {
    std::lock_guard lk(shapeMu_);
    writeNullDiagInfosSize_ = n;
  }

  WriteObservation lastWriteObservation() const {
    std::lock_guard lk(requestMu_);
    return WriteObservation{lastWriteNodeIds_, lastWriteTypeIndices_,
                            lastWriteValueStrings_};
  }

  // seam 3b: 按项设置 write status（仅当 writeShape_ 已设置时生效）
  void setWriteItemStatus(size_t i, uint32_t status) {
    std::lock_guard lk(shapeMu_);
    if (writeItemStatus_.size() <= i)
      writeItemStatus_.resize(i + 1, UA_STATUSCODE_GOOD);
    writeItemStatus_[i] = status;
  }

  // 观察点：最近一次 serviceRead 收到的各 NodeId 字符串长度
  std::vector<size_t> lastReadNodeIdLengths() const {
    std::lock_guard lk(requestMu_);
    return lastReadNodeIdLengths_;
  }

  // 观察点：最近一次 serviceRead / serviceWrite 收到的各 NodeId 命名空间下标
  std::vector<uint32_t> lastReadNamespaces() const {
    std::lock_guard lk(requestMu_);
    return lastReadNamespaces_;
  }
  std::vector<uint32_t> lastWriteNamespaces() const {
    std::lock_guard lk(requestMu_);
    return lastWriteNamespaces_;
  }

  // 清空所有 seam（用例之间隔离）
  void resetResponseShape() {
    std::lock_guard lk(shapeMu_);
    readShape_.reset();
    readNullResultsSize_.reset();
    readNullDiagInfosSize_.reset();
    writeShape_.reset();
    writeNullResultsSize_.reset();
    writeNullDiagInfosSize_.reset();
    itemStatus_.clear();
    itemInt16_.clear();
    writeItemStatus_.clear();
    std::lock_guard lk2(svcMu_);
    readScript_.clear();
    writeScript_.clear();

    itemString_.clear();
    itemStringSet_.clear();
    itemScalar_.clear();
    {
      std::lock_guard lkObs(requestMu_);
      lastReadNodeIdLengths_.clear();
      lastWriteNodeIds_.clear();
      lastWriteTypeIndices_.clear();
      lastWriteValueStrings_.clear();
      lastReadNamespaces_.clear();
      lastWriteNamespaces_.clear();
    }

    connectAsyncCalls_.store(0, std::memory_order_relaxed);
    disconnectAsyncCalls_.store(0, std::memory_order_relaxed);
    {
      std::lock_guard lk(urlMu_);
      lastConnectUrl_.clear();
    }

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

      // 按项 value：status==GOOD 时按 STRING / 通用标量 / INT16 优先级填充。
      {
        if (itemStatus == UA_STATUSCODE_GOOD) {
          if (i < itemStringSet_.size() && itemStringSet_[i]) {
            {
              // 长度保真：UA_STRING_ALLOC()/UA_String_fromChars() 走 strlen，
              // 会把内嵌 '\0' 截断。这里手工构造 UA_String。
              const std::string &raw = itemString_[i];
              UA_String s;
              s.length = raw.size();
              s.data =
                  static_cast<UA_Byte *>(UA_malloc(s.length ? s.length : 1));
              if (s.data) {
                if (s.length)
                  std::memcpy(s.data, raw.data(), s.length);
                // 注意：UA_Variant_setScalarCopy 内部走 UA_Variant_setScalar
                // -> UA_Variant_init，会重置 value 而不会释放旧分配。因此这里
                // 只能调用一次；旧实现先调一次 UA_STRING_ALLOC 版本再调本分支，
                // 会把第一次分配直接泄漏掉。
                const UA_StatusCode rc = UA_Variant_setScalarCopy(
                    &r.results[i].value, &s, &UA_TYPES[UA_TYPES_STRING]);
                UA_free(s.data);
                if (rc == UA_STATUSCODE_GOOD)
                  r.results[i].hasValue = true;
              }
            }
          } else if (i < itemScalar_.size() && itemScalar_[i] &&
                     itemScalar_[i]->data) {
          const auto &holder = itemScalar_[i];
          const UA_StatusCode rc =
              holder->arrayLength > 0
                  ? UA_Variant_setArrayCopy(&r.results[i].value, holder->data,
                                            holder->arrayLength, holder->type)
                  : UA_Variant_setScalarCopy(&r.results[i].value, holder->data,
                                             holder->type);

          if (rc == UA_STATUSCODE_GOOD) {
            r.results[i].hasValue = true;
            }
          }
          else if (i < itemInt16_.size()) {
            const int16_t v = itemInt16_[i];
            const UA_StatusCode rc = UA_Variant_setScalarCopy(
                &r.results[i].value, &v, &UA_TYPES[UA_TYPES_INT16]);
            if (rc == UA_STATUSCODE_GOOD) {
              r.results[i].hasValue = true;
            }
          }
        }
      }
    }
    return r;
  }

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
      r.results[i] = (i < writeItemStatus_.size()) ? writeItemStatus_[i]
                                                   : UA_STATUSCODE_GOOD;
    }
    return r;
  }

  // private member:
private:
   struct ScalarHolder {
     void *data = nullptr;
     size_t arrayLength = 0; // >0 表示 data 是数组（T90）
     const UA_DataType *type = nullptr;
     ScalarHolder() = default;
     ScalarHolder(const ScalarHolder &) = delete;
     ScalarHolder &operator=(const ScalarHolder &) = delete;
     ~ScalarHolder() {
       if (arrayLength > 0) {
         UA_Array_delete(data, arrayLength, type);
         data = nullptr;
       }
       if (data) {
         UA_delete(data, type);
         data = nullptr;
       }
     }
   };

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
  std::atomic<unsigned> inWrite_{0};

  std::mutex hookMu_;
  std::function<void()> connectHook_;
  std::function<void()> readHook_;
  std::function<void()> writeHook_;

  std::atomic<int> readDelayMs_{0};
  // 通过SetMode()可以修改该标志，控制隐式转换Mode的过程
  std::atomic<bool> allowImplicitConversion{true};

  mutable std::mutex shapeMu_;
  std::optional<std::tuple<uint32_t, size_t, bool>> readShape_;
  std::optional<size_t> readNullResultsSize_;
  std::optional<size_t> readNullDiagInfosSize_;
  std::optional<std::tuple<uint32_t, size_t>> writeShape_;
  std::optional<size_t> writeNullResultsSize_;
  std::optional<size_t> writeNullDiagInfosSize_;
  std::vector<uint32_t> itemStatus_;
  std::vector<int16_t> itemInt16_;
  std::vector<uint32_t> writeItemStatus_;
  std::vector<std::shared_ptr<ScalarHolder>> itemScalar_;

  mutable std::mutex requestMu_;
  std::vector<size_t> lastReadNodeIdLengths_;
  std::vector<uint32_t> lastReadNamespaces_;
  std::vector<std::string> lastWriteNodeIds_;
  std::vector<int> lastWriteTypeIndices_;
  std::vector<std::string> lastWriteValueStrings_;
  std::vector<uint32_t> lastWriteNamespaces_;

  std::atomic<unsigned> connectAsyncCalls_{0};
  std::atomic<UA_StatusCode> connectAsyncResult_{UA_STATUSCODE_GOOD};
  mutable std::mutex urlMu_;
  std::string lastConnectUrl_;

  std::atomic<unsigned> disconnectAsyncCalls_{0};
  std::atomic<UA_StatusCode> disconnectAsyncResult_{UA_STATUSCODE_GOOD};
  std::function<void()> disconnectHook_;

  std::vector<std::string> itemString_;
  std::vector<uint8_t> itemStringSet_;

   std::atomic<unsigned> syncDisconnectCalls_{0};
   std::atomic<unsigned> clientNewCalls_{0};
   std::atomic<unsigned> clientDeleteCalls_{0};


};