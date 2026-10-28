// include/OPCUAPacking/ua.h
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <string>
#include "Rust_error_deal/error_deal.h"
#include "OPCUAPacking/WriteRequestAddres.h"

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
  // 使用现代 C++ 领域类型，彻底取代 UA_* 类型 
  struct NodeId {
    uint16_t ns;
    std::string id;
    S7DataType dataType;
  };
  struct ReadValue {
    NodeId node;
  };
  struct WriteValue {
    NodeId node;
    ValueType value;
  };
  struct ReadResult {
    uint32_t rawStatus; // 仅日志/调试用，业务层可忽略
    RichError::ErrorCode errorCode{RichError::ErrorCode::OK};
    std::optional<ValueType> value;
  };
  struct WriteResult {
    enum class Status { Good, Bad } status;
    RichError::ErrorCode errorCode{RichError::ErrorCode::OK};
    uint32_t rawStatus; // 仅日志/调试用，业务层可忽略
  };

  static Result<std::unique_ptr<OPC_UA_Client>, RichError>
  create(const std::string &endpointUrl, ClientConfig config, bool useDefault);

  OPC_UA_Client(const OPC_UA_Client &) = delete;
  OPC_UA_Client &operator=(const OPC_UA_Client &) = delete;
  OPC_UA_Client(OPC_UA_Client &&other) = delete;
  OPC_UA_Client &operator=(OPC_UA_Client &&other) = delete;

  ~OPC_UA_Client() ;
  // 线程模型：同一实例可在多线程下调用；底层同步服务调用在内部串行化，
  //   SDK 侧由 open62541 v1.4.14 的内部 clientMutex 保护（UA_MULTITHREADING>=100）。
  // 阻塞预算：maxTotalWaitMs 是软预算，只在每次尝试开始前检查；超出后返回
  //   BUDGET_EXCEEDED。单次在途尝试最坏 ≈ service + SDK 内部 connectSync
  //   ≤ 2×timeoutMs，不会被预算抢占。断线时 SDK 会在同步服务调用前自动补连
  //   （noReconnect=true 只约束被动状态机，不阻止服务调用前的补连）。
  // 调用期间禁止修改/析构入参 vector；NodeId.id 不得包含内嵌 '\0'。
  Result<std::vector<OPC_UA_Client::ReadResult>, RichError>
  batchRead(const std::vector<ReadValue> &batchReadNodes);
  // 同 batchRead 的线程/阻塞约定；写入非幂等，封装层不自动重试。
  Result<std::vector<OPC_UA_Client::WriteResult>, RichError>
  batchWrite(const std::vector<WriteValue> &batchWriteNodes);

  // 业务线程在定期巡检（例如每 4~5 秒）中调用 checkConnected()
  // checkConnected 会主动驱动一次事件循环以刷新状态。
  // 代价：会与看门狗线程争抢 SDK eventLoop 锁。
  // 建议：仅在低频监控路径调用；高频健康检查请用 isHealthy()（只读缓存）。
  Result<Unit, RichError> checkConnected() ;
  Result<Unit,RichError> isHealthy();
  // 重建失败时旧 pImpl 保持不变，上层可再次调用重试
  // recreate/shutdown 替换前等 m_inFlight == 0
  // 契约：进入 API 后要么被明确拒绝，要么完整执行完
  // 代价：recreate/shutdown 会阻塞在长操作上（用有界等待 + 超时兜底
  Result<Unit, RichError> recreateGiveUpClient() ;
  // 业务可以通过这里获取客户端的存活状态
  // 语义：
  //   RUNNING     —— 三元组全就绪，可正常读写
  //   RECOVERING  —— 自愈窗口内，看门狗正在尝试恢复；此时读写均快速失败
  //   GIVEN_UP    —— 自愈窗口耗尽或不可恢复；需调用 recreateGiveUpClient()
  // 注意：本 API 走 ApiLease 入口，m_recreating / m_terminated 期间会返回错误。
  Result<LifeState, RichError> checkLifeState() ;
  //  业务主动设置Client状态为Give-Up
  Result<Unit, RichError> setGiveUpSignal();

  // create() 返回后，客户端一定已连接，调用方可以立即使用
  Result<Unit, RichError> connect();
  // 配合connect()使用
  Result<Unit, RichError> disconnect();
  // shutdown():
  //   - 幂等：可随时调用；已关闭状态下调用返回 success。
  //   - 阻塞：本函数会等待任务计数器归零，超时上限为
  //           max(timeoutMs, 2×watchdogIntervalMs)。
  // 超时后在途请求由各自出口守卫清理，shutdown 不强行释放。
  // 并发调用安全：两个线程同时调用，仅一个执行关闭，另一个立即返回 success（不等待关闭完成）。
  Result<Unit,RichError> shutdown() ;

 
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
                bool enableWatchDog = true);
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