# 线程模型与生命周期

对象：`OPC_UA_Client`（公共 API）→ `OPC_UA_Client::Impl`（实现）。
底层：open62541 **v1.4.14**，`UA_MULTITHREADING >= 100`（`Impl::validateConfiguration`
用 `#error` 强制）。本文的 SDK 事实全部来自 v1.4.14 源码，路径相对于
open62541 仓库根目录。

目标读者是维护者与面试官：既说明“现在怎么跑”，也明确“哪些窗口还没消除”。

## 1. 先看三条 SDK 事实

open62541 的客户端不是“后台自动运行”的对象。理解下面三条，才能理解为什么
封装层需要看门狗、为什么同步调用会与看门狗互动。

| # | 事实 | 官方位置 |
| --- | --- | --- |
| S1 | 客户端没有自己的线程；必须周期调用 `UA_Client_run_iterate` 才能处理事件、维持连接 | `include/open62541/client.h:46-50` |
| S2 | 同步服务调用发现未连接时会先 `connectSync()` 补连，然后才发请求 | `src/client/ua_client.c:569-578` |
| S3 | `noReconnect` 只禁止“被动失联后的后台重连”，不阻止 S2；`noNewSession` 在会话丢失时主动中止连接 | `include/open62541/client.h:112-116` |

另外两条与尾延迟/资源出口直接相关：

| # | 事实 | 官方位置 |
| --- | --- | --- |
| S4 | `UA_Client_run_iterate` 在 `el->run(timeout)` 前释放 clientMutex；SDK 允许它与服务调用并发 | `src/client/ua_client.c:1043-1060` |
| S5 | `UA_EventLoop::run(timeout)` 在 `timeout==0` 时只处理已到达事件，不阻塞 | `include/open62541/plugin/eventloop.h:111-114` |

因此封装层的“串行化”是**包装层自己的 `m_sdkMutex`**，不是 SDK 强制的一把
全局锁；S4/S5 只说明 SDK 内部不会因为 `run_iterate` 而阻塞服务调用，但包装层
如果持有 `m_sdkMutex` 调 `run_iterate(50ms)`，仍然会把业务调用挡在锁外。
空闲轮询因此固定用 `timeout=0`。

## 2. 分层与调用路径

```
业务线程
  │  batchRead / batchWrite / checkConnected / isHealthy / checkLifeState
  ▼
OPC_UA_Client                       include/OPCUAPacking/ua.h
  │  ApiLease（m_recreateLock 下的 in-flight 计数）
  ▼
OPC_UA_Client::Impl                 src/OPCUAPacking/ua.cpp
  │  ISdk 接缝
  ├── RealSdk                       detail/RealSdk.h      UA_Client_*
  └── MockSdk                       tests/include/MockSdk.h 虚拟时钟/响应注入
  ▼
open62541 v1.4.14
```

`batchRead` 的完整路径：

```
OPC_UA_Client::batchRead
  → ApiLease                                 拒绝 terminated/recreating
  → Impl::batchRead
      → InFlightGuard                        m_taskCounter++
      → 预检                                  GIVEN_UP/RECOVERING/THREAD_BUSY
      → ReadRequestGuard                     UA_ReadRequest 出口
      → for retry in [0, maxRetries]:
          → 预算检查                          elapsed > maxTotalWaitMs
          → ReadResponseGuard                每轮独立；出口 clear
          → m_sdkMutex
          → RealSdk::serviceRead
          → UA_Client_Service_read
          → __UA_Client_Service              SDK clientMutex
          → 未连接则 connectSync()           S2
      → 逐项 UA_StatusCode_isGood + 类型转换
```

`batchWrite` 与读共用预检，但**只尝试一次**：写非幂等，自动重试可能重复执行。
写路径的 NodeId 与值转换是两条独立步骤：先 `uaNodeIDAlloc()`，再
`writeConversion::dispatch()`。STRING 和 NUMERIC 走同一条出口。

## 3. 两个状态机

### 3.1 ConnState（`src/OPCUAPacking/detail/State.h`）

```
IDLE ──CAS──▶ CONNECTING ──CASGuard 析构──▶ IDLE
```

`connect()` / `disconnect()` 共用 `CONNECTING` 作为“有人正在改连接”的单飞标志；
竞争失败者拿到 `THREAD_BUSY`。`CASGuard` 在正常返回、提前 return、异常展开三条
路径上都把状态复位。`batchRead` / `batchWrite` 进入时若发现 `!= IDLE` 快速失败，
不与连接操作交叠。

### 3.2 LifeState（`include/OPCUAPacking/Types.h`）

```
        TurnToRunning()                TurnToRecovring()
RUNNING ◀──────────────── RECOVERING ─────────────────▶ RUNNING
   │                          │
   │ TurnToGiveup()           │ TurnToGiveup()
   ▼                          ▼
        GIVEN_UP  ◀───────────┘      （setGiveUpSignal() 也可直接置位）
```

- `RUNNING` 的充要条件是 `isHealthyCached()`：
  `connectionLost == false && status is Good && channel == OPEN &&
  session == ACTIVATED`。
- `RECOVERING`：看门狗仍在自愈窗口内；读写都快速失败，不主动阻塞。
- `GIVEN_UP`：自愈窗口耗尽或主动放弃；看门狗停止重连，只能
  `recreateGiveUpClient()`。`GIVEN_UP` 也算 stop signal，读写与 `connect()`
  都快速失败。
- `TurnToRunning()` / `TurnToGiveup()` / `setGiveUpSignal()` 都用 CAS，避免
  “看门狗 load→store”覆盖业务线程刚写入的 `GIVEN_UP`。
- `connect()` / `disconnect()` 的失败出口（`CONNECT_FAILED`、
  `TIMEOUT_NOTCONNECT`、`DISCONNECT_FAIL`、`TIMEOUT_NOTDISCONNECT`）在返回前
  也必须 `TurnToRecovring()`。否则缓存三元组已不健康时，会出现
  `checkLifeState()==RUNNING` 与 `isHealthy()==false` 并存，破坏 `RUNNING`
  的契约（T131/T132；T129 是 disconnect 成功出口的同类问题）。

## 4. 状态快照是单个 64 位原子量

三元组（status / channel / session）与一个 16 位 version 被打包进
`std::atomic<uint64_t> state_packed`：

```
[63:48] version   16 bit   每次写 +1
[47:16] status    32 bit   UA_StatusCode
[15:8]  channel    8 bit   UA_SecureChannelState
[7:0]   session    8 bit   UA_SessionState
```

- 读者 `isHealthyCached()` / `isChannelClosed()` / `isConnectBad()` 只做一次
  `load(acquire)`，不会再看到“status 已更新、channel 还是旧值”的过渡组合。
- 权威写者 `stateCallback` 走 `updatePacked()`：读出快照 → 改三个字段 →
  version+1 → `compare_exchange_weak`；失败重试。
- 缓存写者 `isConnected()` 的 `getState()` 写回走
  `tryPublishIfUnchanged(expected, ...)`：只有 `state_packed` 仍等于进入
  `getState()` 前的快照时才 CAS 发布；期间若有 `stateCallback` 写过，本次
  结果视为过期并丢弃（T159）。
- version 只用于“这次 getState 窗口内有没有别人写过”，不是全局序号；16 位
  回绕在单次 `getState()` 窗口内需要 65,536 次回调，实际不可达。若要防御
  恶意 ISdk，可把 version 扩到 32 位（状态字段仍放得下）或改用独立 tag。

`connectionLost` 仍是独立原子量（由 `inactivityCallback` 置位）。因此
`isHealthyCached()` 读的是“一个 64 位快照 + 一个 bool”，不是单原子事务；
极端情况下可能看到 `connectionLost` 与 packed 的短暂不一致。这是当前已知
窗口，见第 7 节。

## 5. 锁、计数器与锁序

| 成员 | 类型 | 保护对象 | 持有者 |
| --- | --- | --- | --- |
| `Impl::m_lock` | `std::mutex` | `m_impl` 快照、`getEffectiveStopSignal()` | 所有 API |
| `Impl::m_sdkMutex` | `std::mutex` | 全部 `UA_Client_*` 调用（含 `run_iterate`；空闲轮询用 `timeout=0`） | SDK 调用点 |
| `Impl::m_taskCvMx` + `m_taskCv` + `m_taskCounter` | `mutex`/`cv`/`size_t` | 在途读写计数（`InFlightGuard`），供 `shutdown()` drain | 读写、shutdown |
| `Impl::m_watchDogMx` + `m_watchdogCv` | `mutex`/`cv` | 看门狗等待与唤醒 | 看门狗、shutdown |
| `Impl::m_retryCvMx` + `m_retryCv` | `mutex`/`cv` | `delayFunction()` 退避，可被 shutdown 唤醒 | 读写、看门狗 |
| `Impl::m_BadStatusMsMx` | `std::mutex` | `m_lastBadStatusMs` | 看门狗、`isConnected()` |
| `Impl::state_packed` | `std::atomic<uint64_t>` | 三元组 + version 的一致快照 | 回调、`isConnected`、读者 |
| `Impl::m_logLevel` | `std::atomic<uint32_t>` | SDK logger 的最小级别；地址交给 `UA_Logger.context`，`customLog()` 只读 | `applyConfiguration` 写，SDK 日志回调读 |
| `RecreateSync::m_recreateLock` | `std::mutex` | `m_recreating`、`m_shutdownInFlight`、`m_inFlight`、`m_recreateCv` | API 租约、recreate、shutdown、析构 |
| `RecreateSync::m_shutdownInFlight` | `int`（受 `m_recreateLock`） | 已赢得 `shutdown()` 的调用者仍在 `shutdownInternal()` 内 | shutdown、析构 |
| `RecreateSync::m_lock` | `std::mutex` | `pImpl` shared_ptr 快照 | 公开 API 入口 |
| `Impl::m_watchdogThread` 的 join | — | `shutdown()`/`~Impl()` 先 join 再清理 | teardown |

锁序：**`m_recreateLock` → `m_lock` → `m_sdkMutex`**。
`m_recreateLock` 的 `wait` 在等待期间释放，因此 `ApiLease` 析构可以递减计数。
`m_taskCvMx`、`m_watchDogMx`、`m_retryCvMx`、`m_BadStatusMsMx` 都是叶子锁，
不会在持有时再去拿 `m_lock` / `m_sdkMutex`。

回调约束：`stateCallback` / `inactivityCallback` 在调用 `run_iterate` 或同步
服务调用的线程内联执行，且可能持有 SDK clientMutex；它们只碰原子量/
`weak_ptr`，不拿 `m_lock` / `m_sdkMutex`。若未来回调需要访问加锁方法，必须
重新审查锁序，不能直接改成 recursive mutex 了事。

## 6. 关键路径

### 6.1 create

```
OPC_UA_Client::create
 ├─ new OPC_UA_Client(RealSdk)          // clientNew + UA_ClientDeleter(持有 ctx)
 ├─ Impl::configureClient(useDefault)
 │    ├─ validateConfiguration()        // UA_MULTITHREADING / m_config.check()
 │    └─ applyConfiguration()           // timeout / sessionTimeout /
 │                                      // noReconnect=true / noNewSession=true /
 │                                      // clientContext / stateCallback /
 │                                      // inactivityCallback / logger
 ├─ connect()                           // 急切连接：返回时对象即可用
 └─ startWatchdog()
```

`connect()` 用 `m_config.timeoutMs` 作为整条握手（TCP + OPN + CreateSession +
ActivateSession）的总预算；高 RTT 链路必须调大 `timeoutMs`。墙钟兜底为
`2 × timeoutMs + 500ms`，防止自定义 ISdk 冻结虚拟时钟时无界忙等
（T120/T121）。

### 6.2 看门狗一次裁决

```
runWatchdog:
  while !stop:
    callRunIterate(client, 0)           // S4/S5：只处理已到达事件
    if lifeState == RECOVERING: backoff
    if now - lastDecision >= watchdogIntervalMs:
      doWatchdogDecision():
        isConnected(0)                  // 内部 callRunIterate + getState
        ├─ 健康 → TurnToRunning()
        └─ 不健康：
           ├─ connectionLost || session != ACTIVATED
           │    → updateBadStatusTime → shouldTriggerGiveUp?
           │    → disconnect() → connect()
           ├─ channel != CLOSED
           │    → updateBadStatusTime → shouldTriggerGiveUp?
           │    → callRunIterate → disconnect() → connect()
           ├─ isConnectBad() || !isHealthyCached()
           │    → updateBadStatusTime → shouldTriggerGiveUp?
           │    → disconnect() → connect()
           └─ else → TurnToRunning()
```

三条恢复分支都必须先 `updateBadStatusTime()`；否则 `m_lastBadStatusMs` 永远
为 0，`shouldTriggerGiveUp()` 永远 false，`GIVEN_UP` 不可达（T128）。

### 6.3 batchRead

```
ApiLease                                 // terminated/recreating 拒绝
InFlightGuard                            // shutdown drain 计数
预检 lifeState / connState / 空队列 / isHealthyCached
构造 UA_ReadRequest + ReadRequestGuard
for retry in [0, maxRetries]:
  ├─ 预算检查 elapsed > maxTotalWaitMs → BUDGET_EXCEEDED
  ├─ UA_ReadResponse + ReadResponseGuard // 每轮独立，重试不累积
  ├─ lock(m_sdkMutex) → ISdk::serviceRead → UA_Client_Service_read
  └─ 可重试码 → delayFunction(retry)；否则逐项映射返回
```

可重试码集合：`BADCONNECTIONCLOSED / BADCOMMUNICATIONERROR /
BADSESSIONIDINVALID / BADSECURECHANNELIDINVALID / BADNOTCONNECTED /
BADSECURECHANNELCLOSED / BADSERVERNOTCONNECTED / BADTIMEOUT /
BADSESSIONCLOSED`。

`maxTotalWaitMs` 是“下一次尝试前”的软预算，单次在途调用最坏
`service + connectSync ≈ 2 × timeoutMs`，不会被预算抢占；最后一个重试点返回
后也不再补一次预算检查。

### 6.4 batchWrite

与读共用预检，只执行一次。每个 item 先 `uaNodeIDAlloc()`（STRING 或
NUMERIC），再 `writeConversion::dispatch()`；逐项 `UA_StatusCode_isGood()`
映射 `Good/Bad`。写入非幂等，封装层不自动重试。

### 6.5 shutdown / recreate

```
shutdown() 赢家:                    shutdown() 输家 / 析构:
 ├─ m_recreateLock 下               ├─ 输家看到 m_terminated → 立即 success
 │  置 m_terminated                 └─ ~OPC_UA_Client：
 │  ++m_shutdownInFlight              置 m_terminated
 ├─ shutdownInternal()                 等 m_recreating == false
 │   ├─ pImpl → doomed                且 m_shutdownInFlight == 0
 │   ├─ doomed->shutdown()          └─ shutdownInternal()（pImpl 已空则直接返回）
 │   └─ doomed.reset()
 └─ ShutdownCallGuard 递减/notify

recreateGiveUpClient():
 ├─ m_recreateLock 下 CAS m_recreating
 ├─ 等 m_inFlight == 0（2s 上限）
 ├─ 要求 lifeState == GIVEN_UP
 ├─ new Impl + configure + connect + startWatchdog
 ├─ 换入 pImpl
 ├─ old->shutdown(); old.reset()      // 旧 Impl 完整收尾
 └─ RecreateGuard 复位 m_recreating    // 最后才发布“recreate 完成”
```

`~OPC_UA_Client` 先置 `m_terminated`，并等待 `m_recreating == false` 与
`m_shutdownInFlight == 0`：recreate 和“已赢得 shutdown 的调用者”是仅有的
两条在对象析构后仍可能访问 `this->pImpl` / `m_recreateSync` 的路径。
`ApiLease` 保证 `m_terminated` 之后普通 API 不再进入。T150/T153 分别用
`ISdk::clientDelete` 阻塞钩子钉死这两条尾巴。

## 7. 已知窗口（不要假装不存在）

1. **`connectionLost` 与 `TurnToRunning()` 的竞争。**
   `inactivityCallback` 在重连成功判定的同一窗口里置位 `connectionLost`，
   `TurnToRunning()` 成功后无条件清零，可能吞掉一次刚发生的失联事件。
   后果是 `isHealthy()` 最多晚一个 `watchdogIntervalMs` 才报不健康。
   彻底修复需要给“失联事件”配代际号，或只在 `connect()` 前清零。
   当前选择保守记录，不在本轮扩大改动面。
2. **16 位 version 回绕。** 见第 4 节；单次 `getState()` 窗口内不可达。
3. **`shutdown()` 的第二个调用者立即返回 success**，不等待赢家 drain 完成
   （T77）。但析构会等待赢家退出 `shutdownInternal()`（T150）；不要据此
   认为关闭已完成。
4. **`recreateGiveUpClient()` 的 drain 上限 2s**，超时返回
   `IN_FLIGHT_TIMEOUT`；旧实例保持原样，可重试。
5. **看门狗线程 join 无独立超时**，依赖 `connect()` / `disconnect()` 的
   墙钟兜底和 `delayFunction()` 的分段可唤醒。

## 8. 异常与 no-throw 契约

公开入口（`create` / `createWithSdk` / `batchRead` / `batchWrite` /
`connect` / `disconnect` / `checkConnected` / `isHealthy` / `checkLifeState` /
`setGiveUpSignal` / `recreateGiveUpClient` / `shutdown`）统一为 `noexcept`，
失败通过 `Result<T, RichError>` 返回。`tests/test43.cpp` 用
`static_assert(noexcept(...))` 把契约钉在编译期，T167 用会抛异常的
`MockSdk::getState()` 验证运行时不会穿透。

实现约定：

- 所有 public 入口捕获 `std::bad_alloc` / `std::system_error` /
  `std::exception` / `...`，映射到 `INTERNAL_ERROR` / `SDK_ERROR` /
  `UNKNOWN`。
- `~OPC_UA_Client` 不抛：等待与 `shutdownInternal()` 都在 `try/catch` 内；
  `Impl::shutdown()` 本身是 `noexcept` 的 best-effort。
- `ISdk` 的契约是**不抛**。`clientDelete` 在 `UA_ClientDeleter` 中还有一层
  兜底 `try/catch`，因为 `shared_ptr` 的 deleter 是 `noexcept`；如果自定义
  ISdk 违约，封装层选择吞掉异常而不是 `std::terminate`（可能泄漏 client）。
- `default_value_for()` 是值类型工具，不在客户端 no-throw 契约内；它为
  ARRAY/STRUCT/UNKNOWN 抛 `std::invalid_argument`，这是刻意的。

## 9. 测试映射

| 主题 | 代表用例 |
| --- | --- |
| 真实 server 读写 | T94/T110/T111/T116/T117/T166 |
| 数字 NodeId | T163/T164/T165（Mock）、T166（真实 server） |
| 响应出口/畸形响应 | T97/T106/T124/T130 |
| 重试/预算 | T21/T22/T38 |
| 断线/自愈/GIVEN_UP | T19/T44/T100/T119/T128/T131/T132 |
| shutdown/recreate 生命周期 | T16/T17/T18/T150/T153 |
| 异常安全/no-throw | T135/T136/T137/T167/T168 |
| 三元组快照 | T159 |
| 封装完整性 | T87/T101、`tests/compile/*` |
