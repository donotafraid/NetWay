#include "OPCUAPacking/ua.h"
#include <thread>
#include "PLC/ConversionDispatcher.h"
#include "OPCUAPacking/RealSdk.h"
#include "spdlog/spdlog.h"

//=================OPC_UA_Client=======================
namespace { // 匿名命名空间，实现内部链接
class ReadResponseGuard {
public:
  ReadResponseGuard(UA_ReadResponse *access) : m_access(access) {}

  ~ReadResponseGuard() {
    if (m_access) {
      UA_ReadResponse_clear(m_access);
    }
  }

  // 禁止拷贝
  ReadResponseGuard(const ReadResponseGuard &) = delete;
  ReadResponseGuard &operator=(const ReadResponseGuard &) = delete;
  // 禁止移动
  ReadResponseGuard(ReadResponseGuard &&other) = delete;
  ReadResponseGuard &operator=(ReadResponseGuard &&other) = delete;

private:
  UA_ReadResponse *m_access;
};

class WriteResponseGuard {
public:
  WriteResponseGuard(UA_WriteResponse *access) : m_access(access) {}

  ~WriteResponseGuard() {
    if (m_access) {
      UA_WriteResponse_clear(m_access); // 清空旧数据
    }
  }

  // 禁止拷贝
  WriteResponseGuard(const WriteResponseGuard &) = delete;
  WriteResponseGuard &operator=(const WriteResponseGuard &) = delete;

  // 禁止移动
  WriteResponseGuard(WriteResponseGuard &&other) = delete;
  WriteResponseGuard &operator=(WriteResponseGuard &&other) = delete;

private:
  UA_WriteResponse *m_access;
};

class WriteRequestGuard {
public:
  WriteRequestGuard(UA_WriteRequest *access) : m_access(access) {}

  ~WriteRequestGuard() {
    if (m_access)
      UA_WriteRequest_clear(m_access);
  }

  // 禁止拷贝
  WriteRequestGuard(const WriteRequestGuard &) = delete;
  WriteRequestGuard &operator=(const WriteRequestGuard &) = delete;
  WriteRequestGuard &operator=(WriteRequestGuard &&) = delete;
  WriteRequestGuard(WriteRequestGuard &&other) = delete;

private:
  UA_WriteRequest *m_access;
};

class ReadRequestGuard {
public:
  ReadRequestGuard(UA_ReadRequest *access) : m_access(access) {}

  ~ReadRequestGuard() {
    if (m_access) {
      UA_ReadRequest_clear(m_access);
    }
  }

  // 禁止拷贝
  ReadRequestGuard(const ReadRequestGuard &) = delete;
  ReadRequestGuard &operator=(const ReadRequestGuard &) = delete;
  // 禁止移动
  ReadRequestGuard(ReadRequestGuard &&other) = delete;
  ReadRequestGuard &operator=(ReadRequestGuard &&) = delete;

private:
  UA_ReadRequest *m_access;
};

class InFlightGuard {
public:
  enum class Mode { Shared, Exclusive };
  InFlightGuard(std::mutex &taskCvMx, std::condition_variable &taskCv,
                size_t &taskCounter)
      : m_taskCvMx(taskCvMx), m_taskCv(taskCv), m_taskCounter(taskCounter) {
    std::lock_guard<std::mutex> lock(m_taskCvMx);
    ++m_taskCounter;
  }

  ~InFlightGuard() {
    bool shouldNotify = false;
    {
      std::lock_guard<std::mutex> lock(m_taskCvMx);
      if (m_taskCounter > 0) {
        --m_taskCounter;
        if (m_taskCounter == 0)
          shouldNotify = true;
      }
    }
    if (shouldNotify)
      m_taskCv.notify_all();
  }

  // 禁止拷贝
  InFlightGuard(const InFlightGuard &) = delete;
  InFlightGuard &operator=(const InFlightGuard &) = delete;

  // 禁止移动构造
  InFlightGuard(InFlightGuard &&other) = delete;
  // 禁止移动赋值
  InFlightGuard &operator=(InFlightGuard &&) = delete;

private:
  std::mutex &m_taskCvMx;
  std::condition_variable &m_taskCv;
  size_t &m_taskCounter;
};

class CASGuard {
public:
  CASGuard(std::atomic<ConnState> &connState) : m_connState(connState) {}

  ~CASGuard() { m_connState.store(ConnState::IDLE, std::memory_order_release); }

  // 禁止拷贝
  CASGuard(const CASGuard &) = delete;
  CASGuard &operator=(const CASGuard &) = delete;

  // 禁止移动构造
  CASGuard(CASGuard &&other) = delete;
  // 禁止移动赋值
  CASGuard &operator=(CASGuard &&) = delete;

private:
  std::atomic<ConnState> &m_connState;
};

class RecreateGuard {
public:
  RecreateGuard(std::shared_ptr<OPC_UA_Client::RecreateSync> recreateSync)
      : m_recreateSync(recreateSync) {}

  ~RecreateGuard() {
    std::lock_guard<std::mutex> lock(m_recreateSync->m_recreateLock);
    if (!isReset) {
      isReset = true;
      m_recreateSync->m_recreating.store(false, std::memory_order_release);
      m_recreateSync->m_recreateCv
          .notify_all(); // ★ 持锁 notify，消除 CV 生命周期竞态
    }
  }

  void reset() {
    std::lock_guard<std::mutex> lock(m_recreateSync->m_recreateLock);
    if (!isReset) {
      isReset = true;
      m_recreateSync->m_recreating.store(false, std::memory_order_release);
      m_recreateSync->m_recreateCv
          .notify_all(); // ★ 持锁 notify，消除 CV 生命周期竞态
    }
  }

  // 禁止拷贝
  RecreateGuard(const RecreateGuard &) = delete;
  RecreateGuard &operator=(const RecreateGuard &) = delete;

  // 禁止移动构造
  RecreateGuard(RecreateGuard &&other) = delete;
  // 禁止移动赋值
  RecreateGuard &operator=(RecreateGuard &&) = delete;

private:
  bool isReset = false;
  std::shared_ptr<OPC_UA_Client::RecreateSync> m_recreateSync;
};

class ThreadGuard {
public:
  ThreadGuard(std::unique_ptr<std::thread> watchdogThread,
              std::condition_variable &watchdogCv)
      : m_watchdogThread(std::move(watchdogThread)), m_watchdogCv(watchdogCv) {}

  ~ThreadGuard() {
    if (m_watchdogThread && m_watchdogThread->joinable()) {
      m_watchdogCv.notify_all();
      m_watchdogThread->join();
    }
  }

  // 禁止拷贝
  ThreadGuard(const ThreadGuard &) = delete;
  ThreadGuard &operator=(const ThreadGuard &) = delete;

  // 禁止移动构造
  ThreadGuard(ThreadGuard &&other) = delete;
  // 禁止移动赋值
  ThreadGuard &operator=(ThreadGuard &&) = delete;

private:
  std::unique_ptr<std::thread> m_watchdogThread;
  std::condition_variable &m_watchdogCv;
};

class ApiLease {
public:
  explicit ApiLease(std::shared_ptr<OPC_UA_Client::RecreateSync> recreateSync)
      : s_(recreateSync) {
    {
      std::lock_guard<std::mutex> lock(s_->m_recreateLock);
      if (s_->m_terminated.load(std::memory_order_acquire)) {
        return;
      }
      if (s_->m_recreating.load(std::memory_order_acquire)) {
        return;
      }
      ++s_->m_inFlight;
    }
    active = true;
  }
  ~ApiLease() {
    if (!active)
      return;
    {
      std::lock_guard lk(s_->m_recreateLock);
      if (--s_->m_inFlight == 0)
        s_->m_inFlightCv.notify_all();
    }
  }
  ApiLease(const ApiLease &) = delete;
  ApiLease &operator=(const ApiLease &) = delete;
  ApiLease(ApiLease &&) = delete;
  ApiLease &operator=(ApiLease &&) = delete;
  //true:表示API计数成功；fasle:表示API因为m_recreating或m_terminated而失败导致未计数
  bool isActive() { return active; }

private:
  std::shared_ptr<OPC_UA_Client::RecreateSync> s_;
  bool active = false;
};

}; // namespace

namespace {
  //自定义Log格式，避免T5的并发导致的错误发生
  static void customLog(void * /*context*/,
                        UA_LogLevel /*level*/,
                        UA_LogCategory /*category*/,
                        const char *msg,
                        va_list args) {
      // 只用 UTC，不碰 localtime/mktime/tzset
      vfprintf(stderr, msg, args);
      fputc('\n', stderr);
  }
  
  // 2) 包成 UA_Logger。必须是静态存储期，因为 config 只存指针
  static UA_Logger g_customLogger = { customLog, nullptr };
};

/**
 * @brief Impl 生命周期与回调契约
 *
 * 1. 所有 SDK 调用（如 UA_Client_Service_read/write、UA_Client_run_iterate 等）
 *    必须在持有本 Impl 的 shared_ptr 快照期间进行，以保证 Impl 存活。
 **/
struct OPC_UA_Client::Impl
    : public std::enable_shared_from_this<OPC_UA_Client::Impl> {
  using UA_ClientPtr = std::shared_ptr<UA_Client>;
  UA_ClientPtr m_impl;
  std::shared_ptr<ISdk> m_sdk;
  mutable std::mutex m_lock;
  std::string endpointUrl;
  std::shared_ptr<CallbackContext> m_callbackContext;
  ClientConfig m_config;
  std::atomic<ConnState> m_connState{
      ConnState::
          IDLE}; // CAS状态机，描述此时是否存在线程进行连接/断连操作，connect()进行状态设置
  std::atomic<LifeState> m_lifeState{
      LifeState::RUNNING}; // Client
                           // 存活状态机，描述是否正常存活,看门狗进行状态修改
  std::atomic<bool> m_running{true}; // 看门狗运行状态,shutdown设置，没有重置
  std::atomic<bool> m_shutdown{false}; // 析构运行状态，shutdown设置，没有重置
  std::atomic<bool> connectionLost{
      false}; // 断开连接标志,回调函数里设置，在看门狗里重连中重置
  std::atomic<UA_StatusCode> sdkConnectStatus{
      UA_STATUSCODE_GOOD}; // sdk内部重连是否继续标志，回调函数里设置，看门狗中使用
  std::atomic<UA_SecureChannelState> sdkChannelState{
      UA_SECURECHANNELSTATE_CLOSED}; // sdk内部重连是否继续标志，回调函数里设置，看门狗中使用
  std::atomic<UA_SessionState> sdkSessionState{
      UA_SESSIONSTATE_CLOSED}; // sdk内部重连是否继续标志，回调函数里设置，看门狗中使用
  std::atomic<bool> m_stop{
      false}; // shutdown()保护变量，避免其他因素导致shutdown()未被调用
  std::mutex m_taskCvMx; // 专门保护任务计数和条件变量
  std::condition_variable m_taskCv;
  size_t m_taskCounter =
      0; // 由 m_taskCvMx
         // 保护,当前未完成的读写任务总量，在读写任务开始和结束时修改，用于shutdown()判断是否可以析构当前对象
  std::recursive_mutex
      m_sdkMutex; // 使得SDK 调用真正串行化，connect/disconnect 不再与读写交叠
  // 使用shared_from_this()
  // 阻止外部recreateImpl对象时，任务数!=0时Impl.reset()后可能造成:1.read/write并发访问已析构Impl对象里的内部成员
  // shutdown() 线程                           业务线程
  // A（仍在执行 __Client_Service）
  //  -------------------------------------
  //  wait_for 超时，m_taskCounter > 0
  //  doomed->disconnect()  <----------  A 线程正持有 clientMutex 或等待响应
  //                                      （disconnect 修改状态、关闭 socket
  //                                      等）
  //  doomed.reset()
  //    （引用计数仍 > 0，不释放）
  //                                      A 线程返回，释放 shared_ptr 快照
  //                                      引用计数变为 0，触发 UA_Client_delete
  //                                      UA_Client_delete 再次调用 disconnect
  //                                      并最终 free(client)
  //    A 线程在 __Client_Service 中释放了 clientMutex，正在等待
  //    clientCondition；

  // 此时 disconnect() 获取 clientMutex，设置客户端状态为
  // DISCONNECTED，关闭网络连接，然后可能唤醒等待的线程；

  // A
  // 线程被唤醒后检查状态发现已断开，于是尝试访问已关闭的连接对象或安全通道内部结构，这些结构可能已被
  // disconnect() 破坏或释放；

  // 如果 A 线程是最后一个持有 shared_ptr 快照的线程，它返回后会触发
  // UA_Client_delete。但此时 UA_Client_delete 内部的 UA_Client_disconnect
  // 会再次操作已经损坏的内部状态，可能导致双重释放或访问无效内存。

  // 看门狗辅助
  std::unique_ptr<std::thread> m_watchdogThread;
  std::condition_variable m_watchdogCv;
  mutable std::mutex m_watchDogMx;
  // 延时函数辅助
  std::mutex m_retryCvMx;
  std::condition_variable m_retryCv;
  // give-up 时间窗成员
  std::atomic<int64_t> m_lastBadStatusMs{
      0}; // 自首次观测到非 GOOD 起计时
          // 60s,针对可能的业务停摆（线程卡死/暂停/忘记轮询）超过
          // 60s，如果前状态是GOOD，会触发重连而非直接退出
  std::mutex m_BadStatusMsMx; // 专门保护m_lastBadStatusMs的赋值
  //  initialize function
  Impl(const ClientConfig &cfg, const std::string &Url, UA_ClientPtr client,
       std::shared_ptr<CallbackContext> callbackContext_,
       std::shared_ptr<ISdk> sdk) // ★
      : m_config(cfg), endpointUrl(std::move(Url)), m_impl(std::move(client)),
        m_callbackContext(callbackContext_), m_sdk(sdk) {}

  //   销毁前必须 join：任何直接或间接导致 Impl 析构的路径（如
  //   shutdown()、recreateGiveUpClient() 中的 shutdown()、OPC_UA_Client
  //   析构）都必须先调用 Impl::shutdown() 置位停止标志并 join 看门狗线程。
  // 防御性保障：~Impl() 本身会再次置位停止标志并 join，以防未来新增路径遗漏调用
  // shutdown()。
  // 析构不保证已完成清理
  ~Impl() {
    // 不预先置位 m_shutdown，让 shutdown() 完成断开 + 唤醒看门狗
    if (!m_stop.load(std::memory_order_acquire)) {
      shutdown();
    }
    // 防御性置位：确保看门狗线程能够退出（即使没有显式调用 shutdown()）
    m_shutdown.store(true, std::memory_order_release);
    m_running.store(false, std::memory_order_release);
    m_watchdogCv.notify_all(); // 唤醒可能在等待的看门狗
    m_retryCv.notify_all();

    if (m_watchdogThread)
      ThreadGuard threadGuard{std::move(m_watchdogThread), m_watchdogCv};
    doCleanup();
  }

  // “调用期间禁止修改/析构入参 vector”
  // 单次 SDK 调用阻塞上界 ≈ config.timeout，断线窗口叠加 connectSync
  /**
   * @brief 关于同步服务调用与看门狗重连的交互说明
   *
   * 设计意图：
   *   -
   * 连接状态管理（重连/断连）主要由看门狗线程负责；业务读写线程在检测到连接未就绪
   *     （RECOVERING 或
   * GIVEN_UP）时快速失败，不主动重连，避免业务线程长时间阻塞。
   *
   * SDK 实际行为（open62541 v1.4.14）：
   *   - 任何同步服务调用（如 UA_Client_Service_read/write）在发现 SecureChannel
   * 未打开 或 Session 未激活时，会自动调用 connectSync()
   * 尝试重新建立连接（阻塞至多 config.timeout），然后才继续服务请求。
   *   - noReconnect
   * 仅禁止通道被动关闭后的后台自动重连，不禁止服务调用前的补连； noNewSession
   * 才会在会话丢失时直接中止连接。
   *
   * 实际交互结果：
   *   - 即使在 RECOVERING 状态下，若业务线程已通过状态检查并进入 SDK
   * 调用，也可能触发 SDK 内部的 connectSync 重连。
   *   - 该 connectSync 与看门狗的重连操作通过 SDK 内部 clientMutex
   * 串行化，不会发生双重 connect
   * 或资源竞争，但可能导致业务线程阻塞比预期更长，且重连发起者不一定是看门狗。
   *
   * 结论：
   *   - 本封装“重连权威在
   * watchdog”的表述为近似成立，并非绝对。实际重连可能由读写线程
   *     触发，但最终连接状态仍由看门狗统一管理和恢复。
   *
   * 测试要求：
   *   - 需覆盖“断线瞬间有读写线程在途”的场景，验证：
   *     1) 不会出现双重 connect（无并发重复建链）；
   *     2) connectSync 与 watchdog 的 connect 调用被正确串行化；
   *     3) 最终连接恢复由 watchdog 接管（即 watchdog
   * 的恢复逻辑能正确处理竞态）。
   */
  Result<std::vector<ReadResult>, RichError>
  batchRead(const std::vector<ReadValue> &batchReadNodes);
  // “调用期间禁止修改/析构入参 vector”
  /**
   * @brief 执行批量写入（单次尝试，无内部自动重试）
   *
   * @note 由于 OPC UA 写入操作具有非幂等性（可能导致重复执行），
   *       本接口不对网络瞬断等可恢复错误进行底层重试。
   *       若写入失败（返回 Bad），调用方应根据业务逻辑自行决定是否重试。
   *       如需自动重试，请确保写入操作的幂等性，或在上层业务循环中调用本接口。
   */
  Result<std::vector<WriteResult>, RichError>
  batchWrite(const std::vector<WriteValue> &batchWriteNodes);
  // get client status safe-thread
  Result<bool, ConnectErrorState> isConnected();
  // create() 返回后，客户端一定已连接，调用方可以立即使用
  Result<Unit, ConnectErrorState> connect();
  // 等待连接就绪
  Result<Unit, DisconnectErrorState> disconnect();
  // shutdown():
  //   - 幂等：可随时调用。
  //   - 非阻塞：只置位 + notify，不主动 disconnect。
  //   - 实际断开由 ~Impl → doCleanup → m_impl.reset() → UA_Client_delete 完成。
  //   - 断开时机取决于最后一个 UA_ClientPtr 的释放时机，可能晚于 shutdown()
  //   返回。
  void shutdown();
  // 置位 + notify；实际断开由 ~Impl → doCleanup → UA_Client_delete 完成
  void signalShutdown();
  void joinWatchdog();
  // clear pointer
  void doCleanup() noexcept;
  // get client status safe-thread
  std::string safeStatusCodeName(UA_StatusCode code);
  /**
   * @brief 配置客户端参数（超时、重试策略等）
   **/
  Result<Unit, RichError> configureClient(bool useDefault);
  Result<Unit, RichError> validateConfiguration(bool useDefault);
  Result<Unit, RichError> applyConfiguration(bool useDefault);
  // 启动看门狗线程
  Result<Unit, RichError> startWatchdog();
  //  延时函数
  void delayFunction(uint32_t retry);
  // 看门狗循环
  // 断开重连退避最长 retryMaxBackoffMs
  void runWatchdog();
  void doWatchdogDecision(std::shared_ptr<Impl> ImplPtr);

  //  辅助函数:计算退避时间
  uint32_t calculateBackoff(uint32_t retry) const;

  // 辅助函数:剩余时间是否足够触发读写功能
  bool restTimeEnoughForSDK(uint64_t &delay, uint32_t &restTime) const;

  // 辅助函数：检查是否应该触发 GIVEN_UP
  // 判据：now - m_lastBadStatusMs >= m_config.giveUpThresholdMs
  // 注意：本函数**不读取 sdkConnectStatus**——这是刻意的，
  //       因为 noReconnect=true 时 SDK 不会自行放弃重连，
  //       "是否放弃"应由封装层的时间窗口决定，而非 SDK 状态。
  // 首次坏状态启动计时，恢复后由 resetGiveUpTimer() 重置
  bool shouldTriggerGiveUp();
  void updateBadStatusTime();
  void resetGiveUpTimer();

  // 辅助函数:LifeState状态切换对应的函数
  void TurnToRunning();
  void TurnToRecovring() noexcept;
  void TurnToGiveup();

  //  辅助函数:返回特定有效对象
  ClientConfig getEffectiveConfig() { return m_config; }

  //  辅助函数:返回是否停止运行信号
  // true:停止运行,false:允许继续运行
  // 判据：m_shutdown && !m_running，或 m_lifeState == GIVEN_UP。
  // 注意：GIVEN_UP 也算"停止信号"——这是刻意的，
  // 让 GIVEN_UP 后的所有读写与 connect 立即快速失败。
  bool getEffectiveStopSignal() {
    return (m_shutdown.load(std::memory_order_acquire) &&
            !m_running.load(std::memory_order_acquire)) ||
           m_lifeState.load(std::memory_order_acquire) == LifeState::GIVEN_UP;
  }

  // 辅助函数:返回健康状态
  // 判据：三元组全就绪（connectStatus==GOOD && channel==OPEN &&
  // session==ACTIVATED） RUNNING 的充要条件即本函数返回 true。
  bool isHealthy() const {
    // 使用原子三元组（这些值由 stateCallback 和 run_iterate 持续更新）
    return sdkConnectStatus.load(std::memory_order_acquire) ==
               UA_STATUSCODE_GOOD &&
           sdkChannelState.load(std::memory_order_acquire) ==
               UA_SECURECHANNELSTATE_OPEN &&
           sdkSessionState.load(std::memory_order_acquire) ==
               UA_SESSIONSTATE_ACTIVATED;
  }

  // 辅助函数:仅用于主动放弃该Client，用户需要明确状态后再使用
  void setGiveUpSignal() {
    m_lifeState.store(LifeState::GIVEN_UP, std::memory_order_release);
  }

  //  辅助函数:返回实时时间
  int64_t nowMsImpl() { return m_sdk->nowMs(); } // 新增

  //  辅助函数:外部sdk锁内调用UA_Client_run_iterate
  void callRunIterate(UA_ClientPtr &client) {
    std::lock_guard<std::recursive_mutex> lock(m_sdkMutex);
    m_sdk->runIterate(client.get(), 50); // ★
  }

};

struct OPC_UA_Client::UA_ClientDeleter {
  std::shared_ptr<ISdk> sdk;            // ★ 持有 sdk
  std::shared_ptr<CallbackContext> ctx; // ★ 关键：持有一份 shared_ptr

  void operator()(UA_Client *c) const noexcept {
    if (c && sdk) {
      // 此期间即使 clientContext 被回调读取，ctx 也一定活着
      sdk->clientDelete(c);
    }
    // deleter 作用域结束后 ctx 才释放。
    // 如果 Impl 早死、此处是最后一个引用，CallbackContext 在此销毁——
    // 此时已无任何 UA_Client 能触发回调，安全。
  }
};

struct OPC_UA_Client::CallbackContext {
  std::weak_ptr<Impl>
      implWeak; // 注意是 weak，不是
                // shared,确保只有在调用lock()转换为shared_ptr下才改变计数
};

Result<std::vector<OPC_UA_Client::ReadResult>, RichError>
OPC_UA_Client::Impl::batchRead(const std::vector<ReadValue> &batchReadNodes) {
  auto pImplQuote = shared_from_this();
  {
    std::lock_guard<std::mutex> lock(pImplQuote->m_lock);
    if (pImplQuote->getEffectiveStopSignal()) {
      return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
          RichError{RichError::ErrorCode::ALREADY_TERMINATED,
                    "client is shutting down"});
    }
  }
  InFlightGuard inFlightGuard{pImplQuote->m_taskCvMx, pImplQuote->m_taskCv,
                              pImplQuote->m_taskCounter};

  if (pImplQuote->m_lifeState.load(std::memory_order_acquire) ==
      LifeState::GIVEN_UP) {
    return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
        RichError{RichError::ErrorCode::NOT_CONNECTED,
                  "batchRead:client life state is GIVEN_UP"});
  }
  if (pImplQuote->m_lifeState.load(std::memory_order_acquire) ==
      LifeState::RECOVERING) {
    return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
        RichError{RichError::ErrorCode::NOT_CONNECTED,
                  "batchRead: client life state is RECOVERING"});
  }

  if (batchReadNodes.size() == 0) {
    return Result<std::vector<ReadResult>, RichError>::error(
        RichError{RichError::ErrorCode::INVALID_ARGUMENT,
                  "batchRead: nodes list is empty"});
  }

  bool result = isHealthy();
  if (!result) {
    return Result<std::vector<ReadResult>, RichError>::error(
        RichError{RichError::ErrorCode::NOT_CONNECTED,
                  "batchRead: connection is not healthy"});
  }

  UA_ReadRequest request;
  UA_ReadRequest_init(&request);
  ReadRequestGuard readRequest{&request};

  request.nodesToRead = (UA_ReadValueId *)UA_Array_new(
      batchReadNodes.size(), &UA_TYPES[UA_TYPES_READVALUEID]);
  if (!request.nodesToRead) {
    return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
        RichError{RichError::ErrorCode::INTERNAL_ERROR,
                  "batchRead: UA_Array_new failed"});
  }
  request.nodesToReadSize = batchReadNodes.size();
  request.timestampsToReturn = UA_TIMESTAMPSTORETURN_NEITHER;

  size_t index = 0;
  for (auto &readNode : batchReadNodes) {
    auto &node = request.nodesToRead[index];
    UA_ReadValueId_init(&node);
    if (readNode.node.id.empty()) {
      return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
          RichError{RichError::ErrorCode::INVALID_ARGUMENT,
                    "batchRead: node id is empty for ns=" +
                        std::to_string(readNode.node.ns)});
    }

    node.nodeId =
        UA_NODEID_STRING_ALLOC(readNode.node.ns, readNode.node.id.data());
    if (node.nodeId.identifierType != UA_NODEIDTYPE_STRING ||
        node.nodeId.identifier.string.data == nullptr) {
      return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
          RichError{RichError::ErrorCode::INTERNAL_ERROR,
                    "batchRead: UA_NODEID_STRING_ALLOC failed for ns=" +
                        std::to_string(readNode.node.ns)});
    }
    if (node.nodeId.identifier.string.length == 0) {
      return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
          RichError{RichError::ErrorCode::INVALID_ARGUMENT,
                    "batchRead: NodeId string length is 0"});
    }
    node.indexRange = UA_STRING_NULL;
    node.attributeId = UA_ATTRIBUTEID_VALUE;
    ++index;
  }

  UA_ClientPtr client;
  {
    std::lock_guard<std::mutex> lock(
        pImplQuote->m_lock); // 锁内拷贝 shared_ptr 快照，确保对象生命周期
                             // 1. 检查是否已连接
    if (!pImplQuote->m_impl) {
      return Result<std::vector<ReadResult>, RichError>::error(
          RichError{RichError::ErrorCode::NOT_INITIALIZED,
                    "batchRead: client is nullptr"});
    } else if (pImplQuote->getEffectiveStopSignal()) {
      return Result<std::vector<ReadResult>, RichError>::error(
          RichError{RichError::ErrorCode::ALREADY_TERMINATED,
                    "batchRead: client is shutting down"});
    }
    client = pImplQuote->m_impl;
  }

  uint32_t attempts = pImplQuote->m_config.maxRetries + 1;
  auto startTime = m_sdk->nowMs();
  for (uint32_t retry = 0; retry < attempts; ++retry) {

    {
      std::lock_guard<std::mutex> lock(pImplQuote->m_lock);
      if (pImplQuote->getEffectiveStopSignal()) {
        return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
            RichError{RichError::ErrorCode::ALREADY_TERMINATED,
                      "batchRead: client is shutting down"});
      }
    }

    if (pImplQuote->m_lifeState.load(std::memory_order_acquire) ==
        LifeState::GIVEN_UP) {
      return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
          RichError{RichError::ErrorCode::NOT_CONNECTED,
                    "batchRead:client life state is GIVEN_UP"});
    }
    if (pImplQuote->m_lifeState.load(std::memory_order_acquire) ==
        LifeState::RECOVERING) {
      return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
          RichError{RichError::ErrorCode::NOT_CONNECTED,
                    "batchRead:client life state is RECOVERING"});
    }
    const auto elapsed = m_sdk->nowMs() - startTime;
    //  每次单次调用最坏花费时间=servcie+connectSync≈2*timeOutMs
    if (elapsed > (pImplQuote->m_config.maxTotalWaitMs)) {
      return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
          RichError{RichError::ErrorCode::BUDGET_EXCEEDED,
                    "batchRead: total time budget exceeded"});
    }

    UA_ReadResponse response;
    {
      std::lock_guard<std::recursive_mutex> sdkLock(m_sdkMutex);
      response = m_sdk->serviceRead(client.get(), request); // ★
    }
    ReadResponseGuard guard(&response);
    UA_StatusCode sr = response.responseHeader.serviceResult;
    if (sr == UA_STATUSCODE_BADCONNECTIONCLOSED ||
        sr == UA_STATUSCODE_BADCOMMUNICATIONERROR ||
        sr == UA_STATUSCODE_BADSESSIONIDINVALID ||
        sr == UA_STATUSCODE_BADSECURECHANNELIDINVALID ||
        sr == UA_STATUSCODE_BADNOTCONNECTED ||
        sr == UA_STATUSCODE_BADSECURECHANNELCLOSED ||
        sr == UA_STATUSCODE_BADSERVERNOTCONNECTED ||
        sr == UA_STATUSCODE_BADTIMEOUT ||
        sr == UA_STATUSCODE_BADSESSIONCLOSED) {
      if (retry != pImplQuote->m_config.maxRetries) {
        delayFunction(retry);
      } else {
        return Result<std::vector<ReadResult>, RichError>::error(
            RichError{RichError::ErrorCode::SERVICE_RETRY_EXHAUSTED,
                      "batchRead: max retries exhausted, last status=" +
                          safeStatusCodeName(sr)});
      }
    } else {

      if (response.responseHeader.serviceResult != UA_STATUSCODE_GOOD ||
          response.resultsSize != batchReadNodes.size() || !response.results) {
        std::stringstream ss;
        ss << "read fail :"
           << " read_response.responseHeader.serviceResult : "
           << response.responseHeader.serviceResult
           << " read_response.resultsSize : " << response.resultsSize;

        return Result<std::vector<ReadResult>, RichError>::error(
            RichError{RichError::ErrorCode::SERVICE_FAILED, ss.str()});
      }

      std::vector<OPC_UA_Client::ReadResult> resultVec;
      resultVec.reserve(batchReadNodes.size());
      //  RECORD RESPONSE_VALUE
      for (size_t i = 0; i < batchReadNodes.size(); ++i) {
        const auto &item = response.results[i];
        const auto &dataType = batchReadNodes[i].node.dataType;

        ReadResult var;
        var.rawStatus = item.status;
        if (item.status == UA_STATUSCODE_GOOD) {
          opcua::readConversion::dispatch(dataType, item.value, var.value);
        } else {
          var.value = std::nullopt; // 显式置空，业务层通过 has_value() 判断
        }
        resultVec.push_back(std::move(var));
      }
      return Result<std::vector<ReadResult>, RichError>::success(
          std::move(resultVec));
    }
  }

  return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
      RichError{RichError::ErrorCode::INTERNAL_ERROR,
                "batchRead: unreachable code path"});
}

Result<std::vector<OPC_UA_Client::WriteResult>, RichError>
OPC_UA_Client::Impl::batchWrite(
    const std::vector<WriteValue> &batchWriteNodes) {
  auto pImplQuote = shared_from_this();
  {
    std::lock_guard<std::mutex> lock(pImplQuote->m_lock);
    if (pImplQuote->getEffectiveStopSignal()) {
      return Result<std::vector<OPC_UA_Client::WriteResult>, RichError>::error(
          RichError{RichError::ErrorCode::ALREADY_TERMINATED,
                    "batchWrite: client is shutting down"});
    }
  }
  InFlightGuard inFlightGuard{pImplQuote->m_taskCvMx, pImplQuote->m_taskCv,
                              pImplQuote->m_taskCounter};

  if (pImplQuote->m_lifeState.load(std::memory_order_acquire) ==
      LifeState::GIVEN_UP) {
    return Result<std::vector<WriteResult>, RichError>::error(
        RichError{RichError::ErrorCode::NOT_CONNECTED,
                  "batchWrite: client life state is GIVEN_UP"});
  }
  if (pImplQuote->m_lifeState.load(std::memory_order_acquire) ==
      LifeState::RECOVERING) {
    return Result<std::vector<WriteResult>, RichError>::error(
        RichError{RichError::ErrorCode::NOT_CONNECTED,
                  "batchWrite: client life state is RECOVERING"});
  }

  if (batchWriteNodes.size() == 0) {
    return Result<std::vector<WriteResult>, RichError>::error(
        RichError{RichError::ErrorCode::INVALID_ARGUMENT,
                  "batchWrite: nodes list is empty"});
  }

  bool result = isHealthy();
  if (!result) {
    return Result<std::vector<WriteResult>, RichError>::error(
        RichError{RichError::ErrorCode::NOT_CONNECTED,
                  "batchWrite: connection is not healthy"});
  }

  UA_WriteRequest request;
  UA_WriteRequest_init(&request);
  WriteRequestGuard writeRequest{&request};

  request.nodesToWrite = (UA_WriteValue *)UA_Array_new(
      batchWriteNodes.size(), &UA_TYPES[UA_TYPES_WRITEVALUE]);
  if (!request.nodesToWrite) {
    return Result<std::vector<OPC_UA_Client::WriteResult>, RichError>::error(
        RichError{RichError::ErrorCode::INTERNAL_ERROR,
                  "batchWrite: UA_Array_new failed"});
  }
  request.nodesToWriteSize = batchWriteNodes.size();

  size_t index = 0;
  for (auto &writeNode : batchWriteNodes) {
    auto &node = request.nodesToWrite[index];
    UA_WriteValue_init(&node);
    if (writeNode.node.id.empty()) {
      return Result<std::vector<OPC_UA_Client::WriteResult>, RichError>::error(
          RichError{RichError::ErrorCode::INVALID_ARGUMENT,
                    "batchWrite: node id is empty for ns=" +
                        std::to_string(writeNode.node.ns)});
    }

    auto writeResult = opcua::writeConversion::dispatch(
        writeNode.node.ns, writeNode.node.id, writeNode.node.dataType, node,
        writeNode.value);
    if (writeResult.is_fail()) {
      return Result<std::vector<OPC_UA_Client::WriteResult>, RichError>::error(
          RichError{writeResult.get_error()->code(),
                    writeResult.get_error()->what()});
    }
    if (node.nodeId.identifierType != UA_NODEIDTYPE_STRING ||
        node.nodeId.identifier.string.data == nullptr) {
      return Result<std::vector<OPC_UA_Client::WriteResult>, RichError>::error(
          RichError{RichError::ErrorCode::INTERNAL_ERROR,
                    "batchWrite: UA_NODEID_STRING_ALLOC failed for ns=" +
                        std::to_string(writeNode.node.ns)});
    }
    if (node.nodeId.identifier.string.length == 0) {
      return Result<std::vector<OPC_UA_Client::WriteResult>, RichError>::error(
          RichError{RichError::ErrorCode::INVALID_ARGUMENT,
                    "batchWrite: NodeId string length is 0"});
    }
    ++index;
  }

  UA_ClientPtr client;
  {
    std::lock_guard<std::mutex> lock(
        pImplQuote->m_lock); // 锁内拷贝 shared_ptr 快照，确保对象生命周期
    if (!pImplQuote->m_impl) {
      return Result<std::vector<WriteResult>, RichError>::error(
          RichError{RichError::ErrorCode::NOT_INITIALIZED,
                    "batchWrite: client is nullptr"});
    } else if (pImplQuote->getEffectiveStopSignal()) {
      return Result<std::vector<WriteResult>, RichError>::error(
          RichError{RichError::ErrorCode::ALREADY_TERMINATED,
                    "batchWrite: client is shutting down"});
    }
    client = pImplQuote->m_impl;
  }

  {
    std::lock_guard<std::mutex> lock(pImplQuote->m_lock);
    if (pImplQuote->getEffectiveStopSignal()) {
      return Result<std::vector<OPC_UA_Client::WriteResult>, RichError>::error(
          RichError{RichError::ErrorCode::ALREADY_TERMINATED,
                    "batchWrite: client is shutting down"});
    }
  }

  if (pImplQuote->m_lifeState.load(std::memory_order_acquire) ==
      LifeState::GIVEN_UP) {
    return Result<std::vector<WriteResult>, RichError>::error(
        RichError{RichError::ErrorCode::NOT_CONNECTED,
                  "batchWrite: client life state is GIVEN_UP"});
  }
  if (pImplQuote->m_lifeState.load(std::memory_order_acquire) ==
      LifeState::RECOVERING) {
    return Result<std::vector<WriteResult>, RichError>::error(
        RichError{RichError::ErrorCode::NOT_CONNECTED,
                  "batchWrite: client life state is RECOVERING"});
  }
  // 3. 执行批量写入
  UA_WriteResponse response;
  {
    std::lock_guard<std::recursive_mutex> sdkLock(m_sdkMutex);
    response = m_sdk->serviceWrite(client.get(), request); // ★
  }
  WriteResponseGuard write_response(&response);
  // 直接按值拷贝进 vector！深拷贝 4 字节整数，极速且安全！
  if (response.responseHeader.serviceResult == UA_STATUSCODE_GOOD &&
      response.resultsSize > 0 && response.results &&
      response.resultsSize == batchWriteNodes.size()) {
    std::vector<WriteResult> resultVec;
    resultVec.reserve(batchWriteNodes.size());
    for (size_t i = 0; i < response.resultsSize; ++i) {
      WriteResult var;
      if (response.results[i] == UA_STATUSCODE_GOOD) {
        var.status = OPC_UA_Client::WriteResult::Status::Good;
      } else {
        var.status = OPC_UA_Client::WriteResult::Status::Bad;
      }
      var.rawStatus = response.results[i];
      resultVec.push_back(std::move(var));
    }
    return Result<std::vector<WriteResult>, RichError>::success(
        std::move(resultVec));
  } else {
    return Result<std::vector<WriteResult>, RichError>::error(RichError{
        RichError::ErrorCode::SERVICE_FAILED,
        "batchWrite: service failed, serviceResult=" +
            safeStatusCodeName(
                response.responseHeader.serviceResult)}); // ← 明确返回失败
  }
}

Result<bool, ConnectErrorState>
OPC_UA_Client::Impl::isConnected()  {
  auto pImplQuote = shared_from_this();
  {
    std::lock_guard<std::mutex> lock(pImplQuote->m_lock);
    if (pImplQuote->getEffectiveStopSignal()) {
      return Result<bool, ConnectErrorState>::error(ConnectErrorState::SHUTDOWN);
    }
  }

  UA_ClientPtr client;
  {
    std::lock_guard<std::mutex> lock(
        pImplQuote->m_lock); // 锁内拷贝 shared_ptr 快照，确保对象生命周期
    // 1. 检查是否已连接
    if (!pImplQuote->m_impl) {
      return Result<bool, ConnectErrorState>::error(ConnectErrorState::NULLPTR);
    }
    client = pImplQuote->m_impl;
  }

UA_SecureChannelState channelState{UA_SECURECHANNELSTATE_CLOSED};
UA_StatusCode statusCode{UA_STATUSCODE_BADSECURECHANNELCLOSED};
UA_SessionState sessionState{UA_SESSIONSTATE_CLOSED};
// 2. 获取实际状态
callRunIterate(client);
{
  std::lock_guard<std::recursive_mutex> lock(m_sdkMutex);
  m_sdk->getState(client.get(), &channelState, &sessionState,
                  &statusCode); // ★
  }
  {
    sdkConnectStatus.store(statusCode, std::memory_order_release);
    sdkChannelState.store(channelState, std::memory_order_release);
    sdkSessionState.store(sessionState, std::memory_order_release);
  }

  if (statusCode != UA_STATUSCODE_GOOD) {
    pImplQuote->updateBadStatusTime();
  }
  return Result<bool, ConnectErrorState>::success(isHealthy());
}


Result<Unit, ConnectErrorState>
OPC_UA_Client::Impl::connect() {
  auto pImplQuote = shared_from_this();
  {
    if (pImplQuote->getEffectiveStopSignal()) {
      return Result<Unit, ConnectErrorState>::error(
          ConnectErrorState::SHUTDOWN);
    }
  }
  InFlightGuard InFlightGuard{pImplQuote->m_taskCvMx, pImplQuote->m_taskCv, pImplQuote->m_taskCounter};

  UA_ClientPtr client;
  {
    std::lock_guard<std::mutex> lock(pImplQuote->m_lock);
    if (pImplQuote->getEffectiveStopSignal()) {
      return Result<Unit, ConnectErrorState>::error(
          ConnectErrorState::SHUTDOWN);
    }
    if (!pImplQuote->m_impl) {
      return Result<Unit, ConnectErrorState>::error(ConnectErrorState::NULLPTR);
    }
    client = pImplQuote->m_impl;
  }

  // 3. CAS 单飞 (核心)
  ConnState expected = ConnState::IDLE;
  if (!pImplQuote->m_connState.compare_exchange_strong(expected, ConnState::CONNECTING)) {
    // CAS失败，说明有其他线程正在连接
    if (expected == ConnState::CONNECTING) {
      return Result<Unit, ConnectErrorState>::error(
          ConnectErrorState::THREADBUSY);
    } else {
      return Result<Unit, ConnectErrorState>::error(ConnectErrorState::UNKNOWN);
    }
  }
  CASGuard casGuard{pImplQuote->m_connState};

  {
    std::lock_guard<std::recursive_mutex> sdkLock(m_sdkMutex);
    UA_StatusCode retval =
        m_sdk->connectAsync(client.get(), endpointUrl.c_str()); // ★
    if (retval != UA_STATUSCODE_GOOD) {
      return Result<Unit, ConnectErrorState>::error(
          ConnectErrorState::CONNECT_FAILED);
    }
  }

  // ★ 等待连接就绪（超时上限 = timeoutMs）
  auto start = pImplQuote->nowMsImpl();
  while (true) {
    auto connectResult = isConnected();
    if (connectResult.is_fail()) {
      auto error = *connectResult.get_error();
      if (error == ConnectErrorState::SHUTDOWN) {
        return Result<Unit, ConnectErrorState>::error(
            ConnectErrorState::SHUTDOWN);
      }
      if (error == ConnectErrorState::NULLPTR) {
        return Result<Unit, ConnectErrorState>::error(
            ConnectErrorState::NULLPTR);
      }
    } else {
      auto connectResultValue = connectResult.value_or(false);
      if (connectResultValue) {
        //  健康度检测正常
        return Result<Unit, ConnectErrorState>::success(Unit{});
      }
    }

    // 检查超时
    auto elapsed = pImplQuote->nowMsImpl() - start;
    if (elapsed > static_cast<int64_t>(pImplQuote->m_config.timeoutMs)) {
      auto lastConnectResult = isConnected();
      if (lastConnectResult.is_fail()) {
        auto error = *lastConnectResult.get_error();
        if (error == ConnectErrorState::SHUTDOWN) {
          return Result<Unit, ConnectErrorState>::error(
              ConnectErrorState::SHUTDOWN);
        }
        if (error == ConnectErrorState::NULLPTR) {
          return Result<Unit, ConnectErrorState>::error(
              ConnectErrorState::NULLPTR);
        }
      } else {
        auto lastConnectResultValue = lastConnectResult.value_or(false);
        if (lastConnectResultValue) {
          return Result<Unit, ConnectErrorState>::success(Unit{});
        }
      }
      //  为后面的connect做好收尾工作
      {
        std::lock_guard<std::recursive_mutex> lock(m_sdkMutex);
        m_sdk->disconnect(client.get()); // ★
      }
      return Result<Unit, ConnectErrorState>::error(
          ConnectErrorState::TIMEOUT_NOTCONNECT);
    }

    {
      if (pImplQuote->getEffectiveStopSignal()) {
        return Result<Unit, ConnectErrorState>::error(
            ConnectErrorState::SHUTDOWN);
      }
    }
  }

  return Result<Unit, ConnectErrorState>::success(Unit{});
}

Result<Unit, DisconnectErrorState> OPC_UA_Client::Impl::disconnect() {
  auto pImplQuote = shared_from_this();

  UA_ClientPtr client;
  {
    std::lock_guard<std::mutex> lock(
        pImplQuote->m_lock); // 锁内拷贝 shared_ptr 快照，确保对象生命周期
                             // 1. 检查生命周期状态 (外部输入)
    // 1. 检查是否已连接
    if (!pImplQuote->m_impl) {
      return Result<Unit, DisconnectErrorState>::error(
          DisconnectErrorState::NULLPTR);
    }
    client = pImplQuote->m_impl;
  }

  InFlightGuard InFlightGuard{pImplQuote->m_taskCvMx, pImplQuote->m_taskCv,
                      pImplQuote->m_taskCounter};

  // 3. CAS 单飞 (核心)
  ConnState expected = ConnState::IDLE;
  if (!pImplQuote->m_connState.compare_exchange_strong(expected,
                                                       ConnState::CONNECTING)) {
    // CAS失败，说明有其他线程正在连接,失败的线程根据expected的状态直接返回
    if (expected == ConnState::CONNECTING) {
      return Result<Unit, DisconnectErrorState>::error(
          DisconnectErrorState::THREADBUSY);
    } else {
      return Result<Unit, DisconnectErrorState>::error(
          DisconnectErrorState::UNKNOWN);
    }
  }
  CASGuard casGuard{pImplQuote->m_connState};

  {
    std::lock_guard<std::recursive_mutex> sdkLock(m_sdkMutex);
    UA_StatusCode retval = m_sdk->disconnectAsync(client.get()); // ★
    if (retval != UA_STATUSCODE_GOOD) {
      return Result<Unit, DisconnectErrorState>::error(
          DisconnectErrorState::DISCONNECT_FAIL);
    }
  }

  // ★ 等待CLOSED就绪（超时上限 = timeoutMs）
  auto start = pImplQuote->nowMsImpl();
  bool isShutdown = false;
  while (!isShutdown) {
   
    auto result = pImplQuote->isConnected();
    if (result.is_fail()) {
      // 代表shutdown called 或者 client = nullptr
      auto error = *result.get_error();
      if (error == ConnectErrorState::SHUTDOWN) {
        return Result<Unit, DisconnectErrorState>::error(
            DisconnectErrorState::SHUTDOWN);
      } else {
        return Result<Unit, DisconnectErrorState>::error(
            DisconnectErrorState::NULLPTR);
      }
    }
    if (sdkConnectStatus.load(std::memory_order_acquire) !=
        UA_STATUSCODE_GOOD) {
      pImplQuote->updateBadStatusTime();
    }
    if (sdkChannelState.load(std::memory_order_acquire) ==
        UA_SECURECHANNELSTATE_CLOSED)
      break;

    // 检查超时
    auto elapsed =  pImplQuote->nowMsImpl() - start;
    if (elapsed > static_cast<int64_t>(pImplQuote->m_config.timeoutMs)) {
      //  查询
      //  UA_Client_getState（D7）据实修正返回，避免异步结果此时才更新，导致外部需要重新进入disconnect
      UA_SecureChannelState cs;
      UA_SessionState ss;
      UA_StatusCode sc;
      {
        std::lock_guard<std::recursive_mutex> lock(m_sdkMutex);
        m_sdk->getState(client.get(), &cs, &ss, &sc); // ★
      }
      if (cs == UA_SECURECHANNELSTATE_CLOSED)
        return Result<Unit, DisconnectErrorState>::success(Unit{});

      {
        std::lock_guard<std::recursive_mutex> lock(m_sdkMutex);
        m_sdk->disconnect(client.get()); // ★
      }
      return Result<Unit, DisconnectErrorState>::error(
          DisconnectErrorState::TIMEOUT_NOTDISCONNECT);
    }

    {
      if (pImplQuote->getEffectiveStopSignal()) {
        return Result<Unit, DisconnectErrorState>::error(
            DisconnectErrorState::SHUTDOWN);
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  return Result<Unit, DisconnectErrorState>::success(Unit{});
}

void OPC_UA_Client::Impl::shutdown() {
  signalShutdown();
  joinWatchdog();
  m_stop.store(true, std::memory_order_release);

  {
    std::unique_lock<std::mutex> lk(m_taskCvMx);

    auto timeoutMs =
        std::max(m_config.timeoutMs, m_config.watchdogIntervalMs * 2);
    auto timeoutDuration = std::chrono::milliseconds(timeoutMs);

    bool completed = m_taskCv.wait_for(lk, timeoutDuration,
                                       [&]() { return m_taskCounter == 0; });

    if (!completed) {
      spdlog::warn(
          "shutdownInternal: task counter did not reach zero within {} ms, "
          "forcing cleanup (counter={})",
          timeoutMs, m_taskCounter);
    }
  }
}

void OPC_UA_Client::Impl::signalShutdown() {
  {
    std::lock_guard<std::mutex> lock(m_lock);
    if (m_shutdown.load(std::memory_order_acquire)) {
      return;
    }
    m_running.store(false, std::memory_order_release);
    m_shutdown.store(true, std::memory_order_release);
  }
  if (m_stop.load(std::memory_order_acquire)) {
    return;
  }

  m_watchdogCv.notify_all();
  m_retryCv.notify_all();
}

void OPC_UA_Client::Impl::joinWatchdog() {
  if (m_watchdogThread && m_watchdogThread->joinable() &&
      m_watchdogThread->get_id() != std::this_thread::get_id()) {
    m_watchdogThread->join();
  }
}

void OPC_UA_Client::Impl::doCleanup() noexcept {
    std::lock_guard<std::mutex> lock(m_lock);
    if (m_impl) {
      // 禁止对 SDK 配置字段的修改（因为官方禁止、且无法安全同步）
      m_impl.reset();
    }
}

std::string OPC_UA_Client::Impl::safeStatusCodeName(UA_StatusCode code)
{
  const char *name = UA_StatusCode_name(code);
    if (!name || name[0] == '\0') {
      // 备选方案1：返回格式化的十六进制数（永不失败）
      // 使用固定大小的栈缓冲区，避免动态内存分配
      char buffer[16];
      snprintf(buffer, sizeof(buffer), "0x%08X", code);
      return std::string{buffer};
    }
    else
    { 
      return std::string{name};
    }
}

// ==================== 验证部分 ====================
Result<Unit, RichError> OPC_UA_Client::Impl::validateConfiguration(bool useDefault) {
    // 1. 检查多线程支持
#if UA_MULTITHREADING < 100
#error "OPC_UA_Client requires UA_MULTITHREADING >= 100 (default) for thread safety"
#endif

    // 2. 检查 m_impl 是否已初始化
    if (!m_impl) {
        return Result<Unit, RichError>::error(RichError{RichError::ErrorCode::NOT_INITIALIZED,"m_impl is uninitialized"});
    }

    // 3. 检查是否已经启动
    if (m_watchdogThread) {
        return Result<Unit, RichError>::error(RichError{RichError::ErrorCode::INVALID_STATE,"already started"});
    }

    // 4. 获取配置并验证
    auto config = m_sdk->getConfig(m_impl.get());   // ★
    if (!config) {
        return Result<Unit, RichError>::error(RichError{RichError::ErrorCode::SDK_ERROR,"get Config fail"});
    }

    // 5. 验证 m_config 的有效性
    auto validateResult = m_config.check();
    if (validateResult.has_value() && !useDefault) {
        // 配置无效且不允许使用默认值
        return Result<Unit, RichError>::error(RichError{
            RichError::ErrorCode::INVALID_CONFIG, validateResult.value()});
    }

    // 所有验证通过
    return Result<Unit, RichError>::success(Unit{});
}

// ==================== 赋值部分 ====================
Result<Unit, RichError>
OPC_UA_Client::Impl::applyConfiguration(bool useDefault) {
  // 1. 获取配置
  auto config = m_sdk->getConfig(m_impl.get()); // ★
  if (!config) {
    return Result<Unit, RichError>::error(RichError{RichError::ErrorCode::SDK_ERROR,"get Config fail"});
  }
  config->logging = &g_customLogger;

  // 2. 应用默认配置（如果需要）
  if (useDefault) {
    auto result=m_config.applyDefaultIfInvalid();
    if(result.is_fail())
    {
      return Result<Unit, RichError>::error(std::move(*result.get_error()));
    }
  }

  // 3. 应用超时配置
  config->timeout = m_config.timeoutMs;
  config->requestedSessionTimeout = m_config.sessionTimeoutMs;
  config->secureChannelLifeTime = m_config.secureChannelLifeTimeMs;

  // 4. 应用重试配置
  config->noReconnect = true;//固定设为true
  config->noNewSession = true;//固定设为true
  config->connectivityCheckInterval = m_config.connectivityCheckIntervalMs;

  // 5. 设置上下文
  m_callbackContext->implWeak = weak_from_this();
  config->clientContext = m_callbackContext.get();

  // 6. 注册回调函数
  // 回调在调用 run_iterate / 同步服务调用的线程内联执行，且可能持有 SDK clientMutex
  // 回调内只允许原子操作；禁止加 m_lock；禁止调用任何 Impl 方法（除纯原子 getter）。
  config->inactivityCallback = [](UA_Client *client) {
    OPC_UA_Client::CallbackContext *self = static_cast<
        OPC_UA_Client::CallbackContext *>(
        // UA_Client_getContext没有被SDK包含，因该回调函数无法传入外部对象，所以特定留存该原始API
        UA_Client_getContext(client));
    if (!self) {
      return;
    }
    if (auto impl = self->implWeak.lock()) {
      if (impl->getEffectiveStopSignal())
        return;
      impl->connectionLost.store(true, std::memory_order_release);
    }
  };

  config->stateCallback = [](UA_Client *client,
                             UA_SecureChannelState channelState,
                             UA_SessionState sessionState,
                             UA_StatusCode connectStatus) {
    // 获取上下文
    OPC_UA_Client::CallbackContext *self = static_cast<
        OPC_UA_Client::CallbackContext *>(
        // UA_Client_getContext没有被SDK包含，因该回调函数无法传入外部对象，所以特定留存该原始API
        UA_Client_getContext(client));
    if (!self) {
      return;
    }

    // 只有在确实需要访问 Impl 时才 lock：
    if (auto impl = self->implWeak.lock()) {
      impl->sdkConnectStatus.store(connectStatus, std::memory_order_release);
      impl->sdkChannelState.store(channelState, std::memory_order_release);
      impl->sdkSessionState.store(sessionState, std::memory_order_release);
    }
  };

  // ==================== 7. 初始化原子状态变量 ====================
  // 注意：这些初始化应该在对象构造时已完成，但为了确保配置应用后的状态一致性，
  // 在应用配置时重新设置为初始状态

  // 连接状态机 - 设置为空闲状态
  m_connState.store(ConnState::IDLE, std::memory_order_release);

  // 生命周期状态 - 设置为运行状态（假设配置应用后客户端可用）
  m_lifeState.store(LifeState::RUNNING, std::memory_order_release);

  // 运行标志 - 设置为 true（看门狗可以运行）
  m_running.store(true, std::memory_order_release);

  // 关闭标志 - 设置为 false（未关闭）
  m_shutdown.store(false, std::memory_order_release);


  // 连接丢失标志 - 设置为 false（初始连接未丢失）
  connectionLost.store(false, std::memory_order_release);

  // SDK 连接状态 - 设置为 GOOD（初始状态良好）
  sdkConnectStatus.store(UA_STATUSCODE_GOOD, std::memory_order_release);

  // SDK 通道状态 - 设置为 OPEN（初始通道打开）
  sdkChannelState.store(UA_SECURECHANNELSTATE_CLOSED, std::memory_order_release);

  // SDK 会话状态 - 设置为 ACTIVATED（初始会话激活）
  sdkSessionState.store(UA_SESSIONSTATE_CLOSED, std::memory_order_release);

  // 更新上面设置的原子变量对象
  UA_SecureChannelState cs;
  UA_SessionState ss;
  UA_StatusCode sc;
  {
    std::lock_guard<std::recursive_mutex> lock(m_sdkMutex);
    m_sdk->getState(m_impl.get(), &cs, &ss, &sc); // ★
  }
  {
    sdkConnectStatus.store(sc,std::memory_order_release);
    sdkChannelState.store(cs,std::memory_order_release);
    sdkSessionState.store(ss,std::memory_order_release);
  }
  return Result<Unit, RichError>::success(Unit{});
}

// ==================== 重构后的 configureClient ====================
Result<Unit, RichError> OPC_UA_Client::Impl::configureClient(bool useDefault) {
    // 第一步：验证
    auto validateResult = validateConfiguration(useDefault);
    if (validateResult.has_error()) {
        return validateResult;
    }

    // 第二步：应用配置
    return applyConfiguration(useDefault);
}

Result<Unit, RichError> OPC_UA_Client::Impl::startWatchdog() {
  if (m_watchdogThread && m_watchdogThread->joinable()) {
    spdlog::warn("Watchdog thread already running");
    return Result<Unit,RichError>::success(Unit{});
  }

  m_running.store(true, std::memory_order_release);
  m_watchdogThread =
      std::make_unique<std::thread>(&OPC_UA_Client::Impl::runWatchdog, this);
  if (m_watchdogThread) {
    spdlog::info("Watchdog thread started");
    return Result<Unit,RichError>::success(Unit{});
  } else {
    m_watchdogThread.reset();
    return Result<Unit,RichError>::error(RichError{RichError::ErrorCode::SDK_ERROR,"watchDog start fail"});
  }
}

void OPC_UA_Client::Impl::delayFunction(uint32_t retry) {
  // 计算退避时间
  uint32_t backoffMs = calculateBackoff(retry); // 根据重试次数计算退避时间
  if (backoffMs == 0) {
    return;
  }

  // 分段睡眠，每100ms检查一次shutdown状态
  const uint32_t segmentMs = 100; // 每段睡眠100ms
  uint32_t elapsedMs = 0;

  while (elapsedMs < backoffMs) {
    // 检查是否应该退出
    if (getEffectiveStopSignal()) {
      return;
    }

    // 计算本次睡眠时长（不超过剩余时间）
    uint32_t sleepMs = std::min(segmentMs, backoffMs - elapsedMs);

    // 使用条件变量等待，可以被shutdown唤醒
    {
      std::unique_lock<std::mutex> lock(m_retryCvMx);
      // 等待指定时间或直到被唤醒
      m_retryCv.wait_for(lock, std::chrono::milliseconds(sleepMs), [this]() {
        return getEffectiveStopSignal();
      });
    }
    elapsedMs += sleepMs;
  }
}

// 统一指数退避：delay = min(retryBackoffBaseMs * 2^retry, retryMaxBackoffMs)
uint32_t OPC_UA_Client::Impl::calculateBackoff(uint32_t retry) const {
  uint64_t base = m_config.retryBackoffBaseMs;
  uint64_t cap = m_config.retryMaxBackoffMs;

  // 防御性检查：retry 过大时直接返回上限，避免移位溢出
  if (retry >= 32) {
    return static_cast<uint32_t>(cap);
  }

  // 使用左移实现 base * 2^retry，避免浮点精度问题
  uint64_t delay = base << retry;

  // 限制在最大值内
  if (delay > cap) {
    delay = cap;
  }

  // 确保结果在 uint32_t 范围内（cap 本身就是 uint32_t，delay 被限制后自然满足）
  return static_cast<uint32_t>(delay);
}

void OPC_UA_Client::Impl::runWatchdog() {
  auto last_decision_time =  m_sdk->nowMs();
  uint32_t retryCounter = 0;
  auto pImplQuote = shared_from_this();
  while (!getEffectiveStopSignal()) {
    UA_ClientPtr client;
    {
      std::lock_guard lock(m_lock);
      if (!m_impl)
        return;
      client = m_impl;
    }
    if (client)
      callRunIterate(client);

    if (m_lifeState.load(std::memory_order_acquire) == LifeState::RECOVERING) {
      delayFunction(++retryCounter);
    }

     auto now =  m_sdk->nowMs();
     if (now - last_decision_time >= (m_config.watchdogIntervalMs)) {
       doWatchdogDecision(pImplQuote); // ← 一次裁决，一次尝试
       last_decision_time = now;

       const auto lifeAfter = m_lifeState.load(std::memory_order_acquire);
       if (lifeAfter == LifeState::RECOVERING) {
         ++retryCounter;
       } else {
         retryCounter = 0;
       }
     }

    std::unique_lock lock(m_watchDogMx);
    m_watchdogCv.wait_for(lock, std::chrono::milliseconds(100),
                          [this] { return getEffectiveStopSignal(); });
  }
}

void OPC_UA_Client::Impl::doWatchdogDecision(std::shared_ptr<Impl> ImplPtr) {
  if (getEffectiveStopSignal())
    return;

  UA_ClientPtr &client = ImplPtr->m_impl;
  // ---------- 健康检查 ----------
  bool healthyResult = false;
  {
    auto result = isConnected();
    if (result.is_success()) {
      healthyResult = result.value_or(false);
    } else {
      auto errorInfo = *result.get_error();
      if (errorInfo == ConnectErrorState::NULLPTR ||
          errorInfo == ConnectErrorState::SHUTDOWN) {
        return;
      }
    }
  }

  // ---------- 一次尝试，不做重试 ----------
  if (!healthyResult) {
    updateBadStatusTime();
    if (shouldTriggerGiveUp()) {
      TurnToGiveup();
      return;
    }
    callRunIterate(client);
    auto connectResult = connect();
    if (connectResult.has_value()) {
      TurnToRunning();
    } else {
      auto error = *connectResult.get_error();
      if (error == ConnectErrorState::SHUTDOWN ||
          error == ConnectErrorState::NULLPTR) {
        TurnToGiveup();
      } else {
        TurnToRecovring();
      }
    }
  } else if (connectionLost.load(std::memory_order_acquire)) {
    if (shouldTriggerGiveUp()) {
      TurnToGiveup();
      return;
    }
    // 先断开，再尝试重连（各一次）
    auto disconnectResult = disconnect();
    if (disconnectResult.is_fail()) {
      auto errorStatus = *disconnectResult.get_error();
      if (errorStatus == DisconnectErrorState::SHUTDOWN ||
          errorStatus == DisconnectErrorState::NULLPTR) {
        TurnToGiveup();
        return;
      }
    }
    callRunIterate(client);
    auto connectResult = connect();
    if (connectResult.is_fail()) {
      auto error = *connectResult.get_error();
      if (error == ConnectErrorState::SHUTDOWN ||
          error == ConnectErrorState::NULLPTR) {
        TurnToGiveup();
      } else {
        TurnToRecovring();
      }
    } else {
      TurnToRunning();
    }
  } else {
    TurnToRunning();
  }
}

Result<std::unique_ptr<OPC_UA_Client>, RichError>
OPC_UA_Client::create(const std::string &endpointUrl, ClientConfig config,
                      bool useDefault) {
  try {
    // new 失败会抛出 std::bad_alloc，由外层的 catch 捕获
    auto client = std::unique_ptr<OPC_UA_Client>(
        new OPC_UA_Client(endpointUrl, config, std::make_shared<RealSdk>()));

    auto configResult = client->pImpl->configureClient(useDefault);
    if (configResult.is_fail()) {
      return Result<std::unique_ptr<OPC_UA_Client>, RichError>::error(
          RichError{configResult.get_error()->code(),
                    configResult.get_error()->what()});
    }

    // 这是有意为之的“急切连接”策略，确保对象返回即可用。
    // 若需后台异步恢复，请上层捕获错误后自行重试。
    auto connectResult = client->connect();
    if (connectResult.is_fail()) {
      return Result<std::unique_ptr<OPC_UA_Client>, RichError>::error(
          RichError{connectResult.get_error()->what()});
    }

    // startWatchdog() 内部若 std::thread 创建失败会抛出 std::system_error
    auto wd = client->pImpl->startWatchdog();
    if (wd.is_fail())
    {
      return Result<std::unique_ptr<OPC_UA_Client>, RichError>::error(
          RichError{*wd.get_error()});
    }
    else
    {
      return Result<std::unique_ptr<OPC_UA_Client>, RichError>::success(
          std::move(client));
    }

  } catch (const std::bad_alloc &e) {
    // 处理内存分配失败
    return Result<std::unique_ptr<OPC_UA_Client>, RichError>::error(
        RichError{RichError::ErrorCode::INTERNAL_ERROR,"Memory allocation failed: " + std::string(e.what())});
  } catch (const std::system_error &e) {
    // 处理线程创建等系统级失败
    return Result<std::unique_ptr<OPC_UA_Client>, RichError>::error(
        RichError{RichError::ErrorCode::SDK_ERROR,"System error: " + std::string(e.what())});
  } catch (const std::exception &e) {
    // 捕获其他标准异常作为兜底
    return Result<std::unique_ptr<OPC_UA_Client>, RichError>::error(
        RichError{RichError::ErrorCode::INTERNAL_ERROR,"Unexpected error: " + std::string(e.what())});
  } catch (...) {
    // 捕获未知异常作为最终保障
    return Result<std::unique_ptr<OPC_UA_Client>, RichError>::error(
        RichError{RichError::ErrorCode::UNKNOWN,"Unknown error"});
  }
}

Result<std::unique_ptr<OPC_UA_Client>, RichError>
OPC_UA_Client::createWithSdk(std::shared_ptr<ISdk> sdk,
                             const std::string &url, ClientConfig cfg,bool useDefault,bool enableWatchDog) {
  try {
    // new 失败会抛出 std::bad_alloc，由外层的 catch 捕获
    auto client = std::unique_ptr<OPC_UA_Client>(
        new OPC_UA_Client(url, cfg,sdk));

    auto configResult = client->pImpl->configureClient(useDefault);
    if (configResult.is_fail()) {
      return Result<std::unique_ptr<OPC_UA_Client>, RichError>::error(
          RichError{configResult.get_error()->code(),
                    configResult.get_error()->what()});
    }

    // 这是有意为之的“急切连接”策略，确保对象返回即可用。
    // 若需后台异步恢复，请上层捕获错误后自行重试。
    auto connectResult = client->connect();
    if (connectResult.is_fail()) {
      return Result<std::unique_ptr<OPC_UA_Client>, RichError>::error(
          RichError{connectResult.get_error()->what()});
    }

    if(!enableWatchDog)
    {
      return Result<std::unique_ptr<OPC_UA_Client>, RichError>::success(
          std::move(client));
    }
    else
    {
      // startWatchdog() 内部若 std::thread 创建失败会抛出 std::system_error
      auto wd = client->pImpl->startWatchdog();
      if (wd.is_fail())
      {
        return Result<std::unique_ptr<OPC_UA_Client>, RichError>::error(
            RichError{*wd.get_error()});
      }
      else
      {
        return Result<std::unique_ptr<OPC_UA_Client>, RichError>::success(
            std::move(client));
      }
    }

  } catch (const std::bad_alloc &e) {
    // 处理内存分配失败
    return Result<std::unique_ptr<OPC_UA_Client>, RichError>::error(
        RichError{RichError::ErrorCode::INTERNAL_ERROR,"Memory allocation failed: " + std::string(e.what())});
  } catch (const std::system_error &e) {
    // 处理线程创建等系统级失败
    return Result<std::unique_ptr<OPC_UA_Client>, RichError>::error(
        RichError{RichError::ErrorCode::SDK_ERROR,"System error:" + std::string(e.what())});
  } catch (const std::exception &e) {
    // 捕获其他标准异常作为兜底
    return Result<std::unique_ptr<OPC_UA_Client>, RichError>::error(
        RichError{RichError::ErrorCode::INTERNAL_ERROR,"Unexpected error: " + std::string(e.what())});
  } catch (...) {
    // 捕获未知异常作为最终保障
    return Result<std::unique_ptr<OPC_UA_Client>, RichError>::error(
        RichError{RichError::ErrorCode::UNKNOWN,"Unknown error"});
  }
}

OPC_UA_Client::OPC_UA_Client(const std::string endpointUrl, ClientConfig config,
                             std::shared_ptr<ISdk> sdk) {
  m_recreateSync = std::make_shared<RecreateSync>();
  m_recreateSync->m_endpointUrl = endpointUrl;
  m_recreateSync->m_config = config;
  if(sdk)
  {
    m_recreateSync->m_sdkForRecreate = sdk;
  }
  else
  {
    m_recreateSync->m_sdkForRecreate = std::make_shared<RealSdk>();
  }
  std::shared_ptr<CallbackContext> callbackContext_ =
      std::make_shared<CallbackContext>();
  OPC_UA_Client::Impl::UA_ClientPtr raw(sdk->clientNew(nullptr), UA_ClientDeleter{m_recreateSync->m_sdkForRecreate, callbackContext_});
  if (raw) {
    pImpl = std::make_shared<Impl>(config, endpointUrl, std::move(raw),
                                   callbackContext_, m_recreateSync->m_sdkForRecreate);
  } else {
    throw std::bad_alloc(); // 内存分配失败
  }
}

OPC_UA_Client::~OPC_UA_Client() {
  {
    std::unique_lock<std::mutex> lock(m_recreateSync->m_recreateLock);
    m_recreateSync->m_terminated.store(std::memory_order_release);
    if (!m_recreateSync->m_recreateCv.wait_for(
            lock, std::chrono::seconds(10),
            [&] { return !m_recreateSync->m_recreating; })) {
      // 记录 fatal 日志, 走强制清理路径
      // (不抛异常, 不 terminate)
    }
  }
  shutdownInternal(); // 无条件清理,绕过业务早退
}

Result<Unit,RichError> OPC_UA_Client::shutdown() {
  {
    std::lock_guard<std::mutex> lock(m_recreateSync->m_recreateLock);
    if (m_recreateSync->m_recreating.load(std::memory_order_acquire)) {
        // 正在重建，拒绝关闭（或等待/记录日志）
        return Result<Unit, RichError>::error(
            RichError{RichError::ErrorCode::RECREATING,
                      "shutdown: client is being recreated, please retry later"});
    }
    if(m_recreateSync->m_terminated.load(std::memory_order_acquire))
    {
      return Result<Unit, RichError>::success(Unit{}); // 幂等成功
    }
    m_recreateSync->m_terminated.store(true,std::memory_order_release);
  }

  return shutdownInternal();
}

Result<Unit, RichError> OPC_UA_Client::shutdownInternal() {
  std::shared_ptr<Impl> doomed;
  {
    std::lock_guard<std::mutex> lock(m_recreateSync->m_lock);
    if (!pImpl) {
      // 幂等：已关闭，返回成功
      return Result<Unit, RichError>::success(Unit{});
    }
    doomed = std::move(pImpl);
  }

  doomed->shutdown();
  doomed.reset();
  return Result<Unit, RichError>::success(Unit{});
}

bool OPC_UA_Client::getRecreatingStatus() {
    std::lock_guard<std::mutex> lock(m_recreateSync->m_recreateLock);
    return m_recreateSync->m_recreating.load(std::memory_order_acquire);
}

// 辅助函数：将 ConnectErrorState 转换为字符串描述
std::string OPC_UA_Client::connectErrorStateToString(ConnectErrorState state) {
  switch (state) {
  case ConnectErrorState::SHUTDOWN:
    return "client is shut down";
  case ConnectErrorState::NULLPTR:
    return "client implementation is null";
  case ConnectErrorState::THREADBUSY:
    return "another thread is connecting/disconnecting";
  case ConnectErrorState::UNKNOWN:
    return "unknown connection state";
  case ConnectErrorState::CONNECT_FAILED:
    return "connect failed";
  case ConnectErrorState::TIMEOUT_NOTCONNECT:
    return "connect timeout and connection not established";
  default:
    return "unhandled connection error";
  }
}

std::string
OPC_UA_Client::disconnectErrorStateToString(DisconnectErrorState state) {
  switch (state) {
  case DisconnectErrorState::SHUTDOWN:
    return "client is shut down";
  case DisconnectErrorState::NULLPTR:
    return "client implementation is null";
  case DisconnectErrorState::THREADBUSY:
    return "another thread is connecting/disconnecting";
  case DisconnectErrorState::TIMEOUT_NOTDISCONNECT:
    return "disconnect timeout and connection not disconnected";
  case DisconnectErrorState::DISCONNECT_FAIL:
    return "async disconnect failed";
  case DisconnectErrorState::UNKNOWN:
    return "unknown disconnect state";
  default:
    return "unhandled disconnect error";
  }
}

RichError::ErrorCode OPC_UA_Client::toRichErrorCode(ConnectErrorState s) {
  switch (s) {
  case ConnectErrorState::SHUTDOWN:
    return RichError::ErrorCode::ALREADY_TERMINATED;
  case ConnectErrorState::NULLPTR:
    return RichError::ErrorCode::NOT_INITIALIZED;
  case ConnectErrorState::THREADBUSY:
    return RichError::ErrorCode::THREAD_BUSY;
  case ConnectErrorState::CONNECT_FAILED:
    return RichError::ErrorCode::SERVICE_FAILED;
  case ConnectErrorState::TIMEOUT_NOTCONNECT:
    return RichError::ErrorCode::CONNECT_TIMEOUT;
  case ConnectErrorState::UNKNOWN:
  default:
    return RichError::ErrorCode::UNKNOWN;
  }
}

RichError::ErrorCode OPC_UA_Client::toRichErrorCode(DisconnectErrorState s) {
  switch (s) {
  case DisconnectErrorState::SHUTDOWN:
    return RichError::ErrorCode::ALREADY_TERMINATED;
  case DisconnectErrorState::NULLPTR:
    return RichError::ErrorCode::NOT_INITIALIZED;
  case DisconnectErrorState::THREADBUSY:
    return RichError::ErrorCode::THREAD_BUSY;
  case DisconnectErrorState::TIMEOUT_NOTDISCONNECT:
    // RichError 未定义 DISCONNECT_TIMEOUT，借用 CONNECT_TIMEOUT 语义
    // （"超时未完成"）。若后续需要区分，可在枚举里补 DISCONNECT_TIMEOUT。
    return RichError::ErrorCode::CONNECT_TIMEOUT;
  case DisconnectErrorState::DISCONNECT_FAIL:
    return RichError::ErrorCode::SERVICE_FAILED;
  case DisconnectErrorState::UNKNOWN:
  default:
    return RichError::ErrorCode::UNKNOWN;
  }
}


Result<Unit, RichError> OPC_UA_Client::connect() {
  std::shared_ptr<Impl> ImplPtr;
  ApiLease lease{m_recreateSync};
  {
    std::lock_guard<std::mutex> lock(m_recreateSync->m_lock);
    if (pImpl) {
      ImplPtr = pImpl;
    } else {
      return Result<Unit, RichError>::error({RichError{RichError::ErrorCode::NOT_INITIALIZED, "connect: client is nullptr"}});
    }
  }

  if (!lease.isActive()) {
    if(m_recreateSync->m_recreating.load())
    {
    return Result<Unit, RichError>::error({RichError{RichError::ErrorCode::RECREATING,"connect: client is being recreated, please retry later"}});
    }
    if(m_recreateSync->m_terminated.load())
    {
    return Result<Unit, RichError>::error({RichError{RichError::ErrorCode::ALREADY_TERMINATED,"connect: client has been terminated"}});
    }
  }
  auto result = ImplPtr->connect();
  if (result.has_value()) {
    return Result<Unit, RichError>::success(Unit{});
  } else {
    ConnectErrorState errorState = *result.get_error();
    return Result<Unit, RichError>::error(RichError{
        toRichErrorCode(errorState), connectErrorStateToString(errorState)});
  }
}

Result<Unit, RichError> OPC_UA_Client::disconnect() {
  std::shared_ptr<Impl> ImplPtr;
  ApiLease lease{m_recreateSync};
  {
    std::lock_guard<std::mutex> lock(m_recreateSync->m_lock);
    if (pImpl) {
      ImplPtr = pImpl;
    } else {
      return Result<Unit, RichError>::error({RichError{RichError::ErrorCode::NOT_INITIALIZED, "disconnect: client is nullptr"}});
    }
  }

  if (!lease.isActive()) {
    if (m_recreateSync->m_recreating.load()) {
      return Result<Unit, RichError>::error({RichError{
          RichError::ErrorCode::RECREATING, "disconnect: client is being recreated, please retry later"}});
    }
    if (m_recreateSync->m_terminated.load()) {
      return Result<Unit, RichError>::error({RichError{
          RichError::ErrorCode::ALREADY_TERMINATED, "disconnect: client has been terminated"}});
    }
  }
  auto result = ImplPtr->disconnect();
  if (result.has_value()) {
    return Result<Unit, RichError>::success(Unit{});
  } else {
    DisconnectErrorState errorState = *result.get_error();
    return Result<Unit, RichError>::error(RichError{
        toRichErrorCode(errorState), disconnectErrorStateToString(errorState)});
  }
}

Result<ConnectionState, RichError> OPC_UA_Client::checkConnected() {
  std::shared_ptr<Impl> ImplPtr;
  ApiLease lease{m_recreateSync};
  {
    std::lock_guard<std::mutex> lock(m_recreateSync->m_lock);
    if (pImpl) {
      ImplPtr = pImpl;
    } else {
      return Result<ConnectionState, RichError>::error(
          {RichError{RichError::ErrorCode::NOT_INITIALIZED, "checkConnected: client is nullptr"}});
    }
  }

  if (!lease.isActive()) {
    if (m_recreateSync->m_recreating.load()) {
      return Result<ConnectionState, RichError>::error({RichError{
          RichError::ErrorCode::RECREATING, "checkConnected: client is being recreated, please retry later"}});
    }
    if (m_recreateSync->m_terminated.load()) {
      return  Result<ConnectionState, RichError>::error({RichError{
          RichError::ErrorCode::ALREADY_TERMINATED, "checkConnected: client has been terminated"}});
    }
  }

  // 关键改动：走 isConnected()，它会 callRunIterate + getState 刷新缓存
  auto r = ImplPtr->isConnected();
  if (r.has_value()) {
    auto status = r.value_or({false});
    if(status)
    {
      return Result<ConnectionState, RichError>::success(
          ConnectionState::CONNECTED);
    }
    else
    {
      return Result<ConnectionState, RichError>::success(
          ConnectionState::OBJECT_ONLY);
    }
  } else {
    auto errorInfo {*r.get_error()};
    return Result<ConnectionState, RichError>::error(
        RichError{toRichErrorCode(errorInfo),"checkConnected: fail"});
  }
}

Result<Unit, RichError> OPC_UA_Client::recreateGiveUpClient() {
  {
    std::lock_guard<std::mutex> lock(m_recreateSync->m_recreateLock);

    if (m_recreateSync->m_recreating.load(std::memory_order_acquire)) {
      return Result<Unit, RichError>::error({RichError{
          RichError::ErrorCode::RECREATING, "recreateGiveUpClient: client is being recreated, please retry later"}});
    }
    if (m_recreateSync->m_terminated.load(std::memory_order_acquire)) {
      return Result<Unit, RichError>::error({RichError{
          RichError::ErrorCode::ALREADY_TERMINATED, "recreateGiveUpClient: client has been terminated"}});
    }
    m_recreateSync->m_recreating.store(true, std::memory_order_release);
  }

  {
     std::unique_lock lk(m_recreateSync->m_recreateLock);
     auto timeoutDuration = std::chrono::milliseconds(2000);
     m_recreateSync->m_inFlightCv.wait_for(
         lk, timeoutDuration,[&] { return m_recreateSync->m_inFlight == 0; });
  }

  RecreateGuard recreateGuard{m_recreateSync};
  LifeState currentLifeState{LifeState::RUNNING};
  {
    std::lock_guard<std::mutex> lock(m_recreateSync->m_lock);
    if(pImpl)
    {
      currentLifeState= pImpl->m_lifeState.load(std::memory_order_acquire);
    }
  }

  if (currentLifeState != LifeState::GIVEN_UP) {
    return Result<Unit, RichError>::error({RichError{
        RichError::ErrorCode::NOT_GIVEN_UP, "recreateGiveUpClient: client is not in GIVEN_UP state"}});
  }

  // 获取配置：优先从现有 pImpl 获取有效配置，否则使用保存的初始配置
  ClientConfig effectiveConfig;
  {
    std::lock_guard<std::mutex> lock(m_recreateSync->m_lock);
    if (pImpl) {
      effectiveConfig = pImpl->getEffectiveConfig();
    } else {
      effectiveConfig = m_recreateSync->m_config; // 使用初始配置
    }
  }

  {
    std::lock_guard<std::mutex> lock(m_recreateSync->m_lock);
    if (!pImpl) {
      return Result<Unit, RichError>::error(
          RichError{RichError::ErrorCode::NOT_INITIALIZED, "client = nullptr"});
    }
  }

  try {
    std::shared_ptr<CallbackContext> callbackContext_ =
        std::make_shared<CallbackContext>();
    OPC_UA_Client::Impl::UA_ClientPtr raw(
        pImpl->m_sdk->clientNew(nullptr),
        UA_ClientDeleter{m_recreateSync->m_sdkForRecreate, callbackContext_});
    if (!raw) {
      std::lock_guard<std::mutex> lock(m_recreateSync->m_lock);
      return Result<Unit, RichError>::error(
          RichError{RichError::ErrorCode::SDK_ERROR,"UA_Client_new() failed"});
    }

    auto newImpl =
        std::make_shared<Impl>(effectiveConfig, m_recreateSync->m_endpointUrl, std::move(raw),
                               callbackContext_, m_recreateSync->m_sdkForRecreate);

    // 4. 完整初始化新 Impl（配置 → 连接 → 启动看门狗）
    //    这些操作可能耗时，但此时不持锁，不会阻塞其他线程
    auto configResult = newImpl->configureClient(true);
    if (configResult.is_fail()) { // useDefault = true
      // 配置失败，放弃新 Impl，并将 pImpl 置空,旧 pImpl 保持不变
      std::lock_guard<std::mutex> lock(m_recreateSync->m_lock);
      return Result<Unit, RichError>::error(RichError{
          RichError::ErrorCode::INVALID_CONFIG, "configureClient failed"});
    }

    auto connResult = newImpl->connect();
    if (connResult.is_fail()) {
         ConnectErrorState err = *connResult.get_error();
      return Result<Unit, RichError>::error(
          RichError{toRichErrorCode(err), connectErrorStateToString(err)});
    }

    // 启动看门狗
    {
      auto result = newImpl->startWatchdog();
      if (result.is_fail()) {
        std::lock_guard<std::mutex> lock(m_recreateSync->m_lock);
        return Result<Unit, RichError>::error(RichError{*result.get_error()});
      }
    }


    // 指针快照，为了避免对象在锁内析构造成可能的阻塞
    std::shared_ptr<Impl> old;
    {
      std::lock_guard<std::mutex> lock(m_recreateSync->m_lock);
      if (m_recreateSync->m_terminated.load(std::memory_order_acquire)) {
        return Result<Unit, RichError>::error(
            RichError{RichError::ErrorCode::ALREADY_TERMINATED,"shutdown called during recreate"});
      }
      old = std::move(pImpl);
      pImpl = newImpl;
    }

    recreateGuard.reset();

    if (old) {
      old->shutdown();
      old.reset();
    }

  } catch (const std::bad_alloc &e) {
    // 处理内存分配失败
    return Result<Unit, RichError>::error(
        RichError{RichError::ErrorCode::INTERNAL_ERROR,"Memory allocation failed: " + std::string(e.what())});
  } catch (const std::system_error &e) {
    // 处理线程创建等系统级失败
    return Result<Unit, RichError>::error(
        RichError{RichError::ErrorCode::SDK_ERROR,"System error:" + std::string(e.what())});
  } catch (const std::exception &e) {
    // 捕获其他标准异常作为兜底
    return Result<Unit, RichError>::error(
        RichError{RichError::ErrorCode::INTERNAL_ERROR,"Unexpected error: " + std::string(e.what())});
  } catch (...) {
    // 捕获未知异常作为最终保障
    return Result<Unit, RichError>::error(
        RichError{RichError::ErrorCode::UNKNOWN,"Unknown error"});
  }

  return Result<Unit, RichError>::success(Unit{});
}

Result<LifeState, RichError> OPC_UA_Client::checkLifeState()  {
  std::shared_ptr<Impl> ImplPtr;
  ApiLease lease{m_recreateSync};
  {
    std::lock_guard<std::mutex> lock(m_recreateSync->m_lock);
    if (pImpl) {
      ImplPtr = pImpl;
    } else {
      return Result<LifeState, RichError>::error(
          {RichError{RichError::ErrorCode::NOT_INITIALIZED, "checkLifeState: client is nullptr"}});
    }
  }
  if (!lease.isActive()) {
    if (m_recreateSync->m_recreating.load()) {
      return Result<LifeState, RichError>::error({RichError{
          RichError::ErrorCode::RECREATING, "checkLifeState: client is being recreated, please retry later"}});
    }
    if (m_recreateSync->m_terminated.load()) {
      return Result<LifeState, RichError>::error({RichError{
          RichError::ErrorCode::ALREADY_TERMINATED, "checkLifeState: client has been terminated"}});
    }
  }
  return Result<LifeState, RichError>::success(
      ImplPtr->m_lifeState.load(std::memory_order_acquire));
}

void OPC_UA_Client::setGiveUpSignal() {
  std::shared_ptr<Impl> ImplPtr;
  {
    std::lock_guard<std::mutex> lock(m_recreateSync->m_lock);
    ImplPtr = pImpl;
  }
  if (ImplPtr) {
    ImplPtr->setGiveUpSignal();
  }
}

void OPC_UA_Client::pumpWatchdogForTest() {
  std::shared_ptr<Impl> ImplPtr;
  {
    std::lock_guard<std::mutex> lock(m_recreateSync->m_lock);
    ImplPtr = pImpl;
  }
  if (ImplPtr) {
    ImplPtr->doWatchdogDecision(ImplPtr);
  }
}

// 该调用全程持有 SDK 内部互斥锁。若多线程并发，将在此处排队。
// 最坏等待时间 = 重试次数 × (同步重连 + timeoutMs)。
Result<std::vector<OPC_UA_Client::ReadResult>, RichError>
OPC_UA_Client::batchRead(const std::vector<ReadValue> &batchReadNodes) {
  std::shared_ptr<Impl> ImplPtr;
  ApiLease lease{m_recreateSync};
  {
    std::lock_guard<std::mutex> lock(m_recreateSync->m_lock);
    if (pImpl) {
      ImplPtr = pImpl;
    } else {
      return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
          {RichError{RichError::ErrorCode::NOT_INITIALIZED, "batchRead: client is nullptr"}});
    }
  }
  if (!lease.isActive()) {
    if(m_recreateSync->m_recreating.load())
    {
      return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
          {RichError{RichError::ErrorCode::RECREATING,
                     "batchRead: client is being recreated, please retry later"}});
    }
    if(m_recreateSync->m_terminated.load())
    {
      return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
          {RichError{RichError::ErrorCode::ALREADY_TERMINATED,
                     "batchRead: client has been terminated"}});
    }
  }
  return ImplPtr->batchRead(batchReadNodes);
}

Result<std::vector<OPC_UA_Client::WriteResult>, RichError>
OPC_UA_Client::batchWrite(const std::vector<WriteValue> &batchWriteNodes) {
  std::shared_ptr<Impl> ImplPtr;
  ApiLease lease{m_recreateSync};
  {
    std::lock_guard<std::mutex> lock(m_recreateSync->m_lock);
    if (pImpl) {
      ImplPtr = pImpl;
    } else {
      return Result<std::vector<OPC_UA_Client::WriteResult>, RichError>::error(
          {RichError{RichError::ErrorCode::NOT_INITIALIZED, "batchWrite: client is nullptr"}});
    }
  }
  if (!lease.isActive()) {
    if(m_recreateSync->m_recreating.load())
    {
      return Result<std::vector<OPC_UA_Client::WriteResult>, RichError>::error(
          {RichError{RichError::ErrorCode::RECREATING,
                     "batchWrite: client is being recreated, please retry later"}});
    }
    if(m_recreateSync->m_terminated.load())
    {
      return Result<std::vector<OPC_UA_Client::WriteResult>, RichError>::error(
          {RichError{RichError::ErrorCode::ALREADY_TERMINATED,
                     "batchWrite: client has been terminated"}});
    }
  }
    return ImplPtr->batchWrite(batchWriteNodes);
}

void OPC_UA_Client::Impl::updateBadStatusTime() {
  std::lock_guard<std::mutex> lock(m_BadStatusMsMx);
  if(m_lastBadStatusMs.load(std::memory_order_acquire)==0)
  {
    m_lastBadStatusMs.store(nowMsImpl(), std::memory_order_release);
  }
}

void OPC_UA_Client::Impl::resetGiveUpTimer() {
  std::lock_guard<std::mutex> lock(m_BadStatusMsMx);
  if (m_lastBadStatusMs.load(std::memory_order_acquire) != 0) {
    m_lastBadStatusMs.store(0, std::memory_order_release);
  }
}

void OPC_UA_Client::Impl::TurnToRecovring() noexcept {
  // ② 状态转换时排除 GIVEN_UP（同时只在 RUNNING 时允许转 RECOVERING）
  auto currentState = m_lifeState.load(std::memory_order_acquire);
  if (currentState == LifeState::RUNNING &&
      (sdkConnectStatus.load(std::memory_order_acquire) != UA_STATUSCODE_GOOD ||
       connectionLost.load(std::memory_order_acquire) ||
       sdkSessionState.load(std::memory_order_acquire) != UA_SESSIONSTATE_ACTIVATED)) {
    m_lifeState.store(LifeState::RECOVERING, std::memory_order_release);
  }
}

void OPC_UA_Client::Impl::TurnToRunning() {
  if (m_lifeState.load(std::memory_order_acquire) == LifeState::RECOVERING ||
      m_lifeState.load(std::memory_order_acquire) == LifeState::RUNNING) {
    connectionLost.store(false, std::memory_order_release);
    m_lifeState.store(LifeState::RUNNING, std::memory_order_release);
    resetGiveUpTimer();
  }
}

void OPC_UA_Client::Impl::TurnToGiveup() {
  if(m_lifeState.load(std::memory_order_acquire)!=LifeState::GIVEN_UP)
  {
    m_lifeState.store(LifeState::GIVEN_UP, std::memory_order_release);
    updateBadStatusTime();
  }
}

bool OPC_UA_Client::Impl::shouldTriggerGiveUp() {
  int64_t ts = m_lastBadStatusMs.load(std::memory_order_acquire);
  if (ts == 0)
    return false;
  return (nowMsImpl() - ts) >= m_config.giveUpThresholdMs; // ★ 用 config
}
