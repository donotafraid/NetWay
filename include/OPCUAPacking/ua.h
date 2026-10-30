// include/OPCUAPacking/ua.h
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include <string>
#include "Rust_error_deal/error_deal.h"
#include "OPCUAPacking/ValueTypes.h"

#include "OPCUAPacking/Types.h"

namespace OPCUAPacking {
namespace internal {
struct RecreateSync;
class ISdk;
} // namespace internal
} // namespace OPCUAPacking

#ifdef OPCUAPACKING_ENABLE_TEST_HOOKS
namespace OPCUAPacking::test { struct ClientTestHooks; }
#define OPCUAPACKING_FRIEND_TEST_HOOKS \
    friend struct OPCUAPacking::test::ClientTestHooks;
#else
#define OPCUAPACKING_FRIEND_TEST_HOOKS
#endif

class OPC_UA_Client final  {
public:
  // 使用现代 C++ 领域类型，彻底取代 UA_* 类型。
  // id 的 variant 选择 NodeId 的标识类型：
  //   std::string  -> ns=<ns>;s=<id>  (UA_NODEIDTYPE_STRING)
  //   uint32_t     -> ns=<ns>;i=<id>  (UA_NODEIDTYPE_NUMERIC)
  // 不支持的标识类型（GUID / ByteString）无法通过该 variant 表达。
  struct NodeId {
    uint16_t ns{0};
    std::variant<std::string,uint32_t> id;
    // An omitted dataType must not silently become S7DataType::BOOL (the
    // zero enumerator). UNKNOWN reaches the converter as TYPE_NOTSUPPORT:
    // reads report it per item, writes reject the batch before the SDK call.
    S7DataType dataType{S7DataType::UNKNOWN};
  };
  struct ReadValue {
    NodeId node;
  };
  struct WriteValue {
    NodeId node;
    ValueType value;
  };
  struct ReadResult {
    uint32_t rawStatus{0}; // 仅日志/调试用，业务层可忽略
    RichError::ErrorCode errorCode{RichError::ErrorCode::OK};
    std::optional<ValueType> value;
  };
  struct WriteResult {
    enum class Status { Good, Bad } status{Status::Bad};
    RichError::ErrorCode errorCode{RichError::ErrorCode::OK};
    uint32_t rawStatus{0}; // 仅日志/调试用，业务层可忽略
  };

  // 所有公开入口统一为 no-throw：失败通过 Result 返回。实现必须捕获
  // std::bad_alloc / std::system_error / std::exception / ...，不得让异常
  // 穿透到调用方。该契约由 tests/test43.cpp 的 static_assert 固定。
  static Result<std::unique_ptr<OPC_UA_Client>, RichError>
  create(const std::string &endpointUrl, ClientConfig config,
         bool useDefault) noexcept;

  OPC_UA_Client(const OPC_UA_Client &) = delete;
  OPC_UA_Client &operator=(const OPC_UA_Client &) = delete;
  OPC_UA_Client(OPC_UA_Client &&other) = delete;
  OPC_UA_Client &operator=(OPC_UA_Client &&other) = delete;

  ~OPC_UA_Client() noexcept;
  // 线程模型：同一实例可在多线程下调用；底层同步服务调用在内部串行化，
  //   SDK 侧由 open62541 v1.4.14 的内部 clientMutex 保护（UA_MULTITHREADING>=100）。
  // 阻塞预算：maxTotalWaitMs 是软预算，只在每次尝试开始前检查；超出后返回
  //   BUDGET_EXCEEDED。单次在途尝试最坏 ≈ service + SDK 内部 connectSync
  //   ≤ 2×timeoutMs，不会被预算抢占。断线时 SDK 会在同步服务调用前自动补连
  //   （noReconnect=true 只约束被动状态机，不阻止服务调用前的补连）。
  // 调用期间禁止修改/析构入参 vector；NodeId.id 不得包含内嵌 '\0'。
  Result<std::vector<OPC_UA_Client::ReadResult>, RichError>
  batchRead(const std::vector<ReadValue> &batchReadNodes) noexcept;
  // 同 batchRead 的线程/阻塞约定；写入非幂等，封装层不自动重试。
  Result<std::vector<OPC_UA_Client::WriteResult>, RichError>
  batchWrite(const std::vector<WriteValue> &batchWriteNodes) noexcept;

  // 业务线程在定期巡检（例如每 4~5 秒）中调用 checkConnected()
  // checkConnected 会主动驱动一次事件循环以刷新状态。
  // 代价：会与看门狗线程争抢 SDK eventLoop 锁。
  // 建议：仅在低频监控路径调用；高频健康检查请用 isHealthy()（只读缓存）。
  Result<Unit, RichError> checkConnected() noexcept;
  Result<Unit,RichError> isHealthy() noexcept;
  // 重建失败时旧 pImpl 保持不变，上层可再次调用重试
  // recreate/shutdown 替换前等 m_inFlight == 0
  // 契约：进入 API 后要么被明确拒绝，要么完整执行完
  // 代价：in-flight drain 上限 2s；旧 Impl 的 shutdown/join 仍受
  //       connect()/disconnect() 的墙钟兜底（2×timeoutMs+500ms）约束。
  Result<Unit, RichError> recreateGiveUpClient() noexcept;
  // 业务可以通过这里获取客户端的存活状态
  // 语义：
  //   RUNNING     —— 三元组全就绪，可正常读写
  //   RECOVERING  —— 自愈窗口内，看门狗正在尝试恢复；此时读写均快速失败
  //   GIVEN_UP    —— 自愈窗口耗尽或不可恢复；需调用 recreateGiveUpClient()
  // 注意：本 API 走 ApiLease 入口，m_recreating / m_terminated 期间会返回错误。
  Result<LifeState, RichError> checkLifeState() noexcept;
  //  业务主动设置Client状态为Give-Up
  Result<Unit, RichError> setGiveUpSignal() noexcept;

  // create() 返回后，客户端一定已连接，调用方可以立即使用
  Result<Unit, RichError> connect() noexcept;
  // 配合connect()使用
  Result<Unit, RichError> disconnect() noexcept;
  // shutdown():
  //   - 幂等：可随时调用；已关闭状态下调用返回 success。
  //   - 阻塞：先 join 看门狗线程（join 无独立超时，最坏受 connect/disconnect
  //           的 2×timeoutMs+500ms 墙钟兜底约束），再等待任务计数器归零，
  //           drain 上限为 max(timeoutMs, 2×watchdogIntervalMs)。
  // 超时后在途请求由各自出口守卫清理，shutdown 不强行释放。
  // 并发调用安全：两个线程同时调用，仅一个执行关闭，另一个立即返回 success（不等待关闭完成）。
  Result<Unit,RichError> shutdown() noexcept;

 
private:
  OPCUAPACKING_FRIEND_TEST_HOOKS
  // 仅测试使用
  void pumpWatchdogForTest();
  // 仅为了测试T8-C使用
  bool getRecreatingStatus();
  // 仅为测试使用
  static Result<std::unique_ptr<OPC_UA_Client>, RichError>
  createWithSdk(std::shared_ptr<OPCUAPacking::internal::ISdk> sdk, const std::string &endpointUrl,
                ClientConfig config, bool usedefault = false,
                bool enableWatchDog = true) noexcept;
  /*
    为了避免shutdown()后对象由空->非空,需要确保shutdown()调用时代表整个OPC_UA_Client对象的终结，建立了私有内部清理函数，表示内部清理使用
  */
  Result<Unit,RichError> shutdownInternal();

  explicit OPC_UA_Client(const std::string endpointUrl, ClientConfig config,
                         std::shared_ptr<OPCUAPacking::internal::ISdk> sdk);

  // 关键：前向声明 + 不透明指针
  struct Impl;
  std::shared_ptr<Impl> pImpl;
  // Impl需要的回调上下文
  struct CallbackContext;
  // 使用 RAII 包装器
  struct UA_ClientDeleter ;
 
  std::shared_ptr<OPCUAPacking::internal::RecreateSync> m_recreateSync; // 与 RecreateGuard 共享
};