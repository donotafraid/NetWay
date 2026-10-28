# 线程模型与生命周期

对象：`OPC_UA_Client`（公共 API）→ `OPC_UA_Client::Impl`（实现）。
底层：open62541 **v1.4.14**，`UA_MULTITHREADING >= 100`（`Impl::validateConfiguration`
用 `#error` 强制）。本文所有 SDK 事实均来自 v1.4.14 源码。

## 1. 两个状态机

### 1.1 ConnState（`src/OPCUAPacking/detail/State.h`）

```
IDLE ──CAS──▶ CONNECTING ──CASGuard 析构──▶ IDLE
```

- 只有 `connect()` / `disconnect()` 会做这次 CAS，保证同一时刻最多一个线程在改连接。
- 竞争失败者拿到 `THREAD_BUSY`。
- `batchRead` / `batchWrite` 进入时若发现 `!= IDLE` 会快速失败，不与连接操作交叠。

### 1.2 LifeState（`include/OPCUAPacking/Types.h`）

```
        TurnToRunning()                TurnToRecovring()
RUNNING ◀──────────────── RECOVERING ─────────────────▶ RUNNING
   │                          │
   │ TurnToGiveup()           │ TurnToGiveup()
   ▼                          ▼
        GIVEN_UP  ◀───────────┘      （setGiveUpSignal() 也可直接置位）
```

- `RUNNING` 的充要条件是 `isHealthyCached()`：
  `connectStatus == GOOD && channel == OPEN && session == ACTIVATED && !connectionLost`。
- `RECOVERING`：看门狗仍在自愈窗口内；**读写都快速失败**，不主动阻塞。
- `GIVEN_UP`：自愈窗口耗尽或主动放弃；看门狗停止重连，只能
  `recreateGiveUpClient()`。
- `TurnToRunning()` / `TurnToGiveup()` / `setGiveUpSignal()` 用 CAS，避免
  “看门狗 load→store”覆盖业务线程刚写入的 `GIVEN_UP`。
- `connect()` / `disconnect()` 的**失败出口**（`CONNECT_FAILED`、
  `TIMEOUT_NOTCONNECT`、`DISCONNECT_FAIL`、`TIMEOUT_NOTDISCONNECT`）在返回前
  也必须调用 `TurnToRecovring()`。否则当缓存三元组已不健康时，会出现
  `checkLifeState()==RUNNING` 与 `isHealthy()==false` 并存，破坏 `RUNNING`
  的充要条件（回归用例 T131/T132；T129 是同类问题的 disconnect 成功出口）。

## 2. 锁与计数器

| 成员 | 类型 | 保护对象 | 持有者 |
| --- | --- | --- | --- |
| `Impl::m_lock` | `std::mutex` | `m_impl` 快照、`getEffectiveStopSignal()` | 所有 API |
| `Impl::m_sdkMutex` | `std::mutex` | 全部 `UA_Client_*` 调用（含 `run_iterate`；空闲轮询用 `timeout=0`） | SDK 调用点 |
| `Impl::m_taskCvMx` + `m_taskCv` + `m_taskCounter` | `mutex`/`cv`/`size_t` | 在途读写计数（`InFlightGuard`），供 `shutdown()` drain | 读写、shutdown |
| `Impl::m_watchDogMx` + `m_watchdogCv` | `mutex`/`cv` | 看门狗等待与唤醒 | 看门狗、shutdown |
| `Impl::m_retryCvMx` + `m_retryCv` | `mutex`/`cv` | `delayFunction()` 退避，可被 shutdown 唤醒 | 读写、看门狗 |
| `Impl::m_BadStatusMsMx` | `std::mutex` | `m_lastBadStatusMs` | 看门狗、`isConnected()` |
| `Impl::m_stateEpoch` | `std::atomic<uint64_t>` | `stateCallback` 与 `getState` 快照的代际；防止旧快照覆盖新回调 | `stateCallback`、`isConnected()` |
| `Impl::m_stateWriteMx` | `std::mutex` | 三元组的写者串行化（`stateCallback` 与 `isConnected` 的发布段） | 两者；读者不拿 |
| `Impl::m_logLevel` | `std::atomic<uint32_t>` | SDK logger 的最小级别；地址交给 `UA_Logger.context`，`customLog()` 只读 | `applyConfiguration` 写，SDK 日志回调读 |
| `RecreateSync::m_recreateLock` | `std::mutex` | `m_recreating`、`m_shutdownInFlight`、`m_inFlight`、`m_recreateCv` | API 租约、recreate、shutdown、析构 |
| `RecreateSync::m_shutdownInFlight` | `int`（受 `m_recreateLock`） | 已赢得 `shutdown()` 的调用者仍在 `shutdownInternal()` 内 | shutdown、析构 |
| `RecreateSync::m_lock` | `std::mutex` | `pImpl` shared_ptr 快照 | 公开 API 入口 |
| `Impl::m_watchdogThread` 的 join | — | `shutdown()`/`~Impl()` 先 join 再清理 | teardown |

锁序：任何时候都是 **`m_recreateLock` → `m_lock` → `m_sdkMutex`**；
`m_recreateLock` 的 `wait` 会在等待期间释放，因此 `ApiLease` 析构可以递减计数。
回调（`stateCallback` / `inactivityCallback`）不拿 `m_lock` / `m_sdkMutex`；
`stateCallback` 只拿 `m_stateWriteMx`（与 `isConnected` 的发布段互斥），
`inactivityCallback` 只碰 `connectionLost` 原子量。
`stateCallback` 在 `m_stateWriteMx` 下把 `m_stateEpoch` 先加 1（奇数，
发布中）再发布三元组，最后再加 1（偶数，稳定）。`isConnected()` 在
`getState()` 前等待偶数代际，取得快照后在 `m_stateWriteMx` 下比较代际；
若期间有过回调则丢弃这次 `getState` 快照（T159）。奇数/偶数让“回调已
递增但尚未写入”的窗口可见；写者互斥让“比较 + 发布”原子。读者
`isHealthyCached()` 仍不加锁。三元组本身仍是三个独立原子量，极端情况下
读者可能看到过渡组合；要彻底消除需把三元组打包成单个 64 位快照（后续项，
不在当前补丁内）。

## 3. 运行时序

### 3.1 create

```
OPC_UA_Client::create
 ├─ new OPC_UA_Client(RealSdk)          // clientNew(nullptr) + UA_ClientDeleter(持有 ctx)
 ├─ Impl::configureClient(useDefault)
 │    ├─ validateConfiguration()        // UA_MULTITHREADING / m_config.check()
 │    └─ applyConfiguration()           // 写 config->timeout / sessionTimeout /
 │                                      // noReconnect=true / noNewSession=true /
 │                                      // clientContext / stateCallback / inactivityCallback
 ├─ connect()                           // 急切连接：返回时对象即可用
 └─ startWatchdog()
```

连接预算是 `ConnectErrorState` 的来源：`Impl::connect()` 用 `m_config.timeoutMs`
作为“整条握手”（TCP + OPN + CreateSession + ActivateSession）的总预算，
`connectAsync` 失败映射 `CONNECT_FAILED`，握手在预算内未就绪映射
`TIMEOUT_NOTCONNECT`（`RichError` 层为 `CONNECT_TIMEOUT`）。高 RTT 链路必须
调大 `timeoutMs`：`PROFILE_LOCAL` 的 5s 在 200ms 单程（RTT≈400ms）下不够，
见 `docs/limitations.md` 第 4 节。

### 3.2 batchRead（`Impl::batchRead`，`src/OPCUAPacking/ua.cpp`）

```
ApiLease(m_recreateSync)                 // terminated/recreating 直接拒绝
InFlightGuard(m_taskCounter)             // shutdown drain 计数
预检 lifeState != GIVEN_UP/RECOVERING、connState == IDLE、队列非空、isHealthyCached()
构造 UA_ReadRequest + ReadRequestGuard   // 所有提前 return 都释放
for retry in [0, maxRetries]:
  ├─ 预算检查 elapsed > maxTotalWaitMs → BUDGET_EXCEEDED
  ├─ UA_ReadResponse + ReadResponseGuard // 每轮独立守卫，重试不累积
  ├─ lock(m_sdkMutex) → ISdk::serviceRead → UA_Client_Service_read
  └─ 可重试码 → delayFunction(retry)；否则逐项映射返回
```

可重试码集合：`BADCONNECTIONCLOSED / BADCOMMUNICATIONERROR / BADSESSIONIDINVALID /
BADSECURECHANNELIDINVALID / BADNOTCONNECTED / BADSECURECHANNELCLOSED /
BADSERVERNOTCONNECTED / BADTIMEOUT / BADSESSIONCLOSED`。

`batchWrite` 与读共用预检，但**只尝试一次**：写入非幂等，自动重试可能重复执行。

### 3.3 看门狗裁决（`Impl::doWatchdogDecision`）

```
isConnected()（内部 callRunIterate + getState，刷新三元组）
 ├─ 健康 → TurnToRunning(); return
 └─ 不健康：
    ├─ connectionLost || session != ACTIVATED
    │    → 记账 updateBadStatusTime；shouldTriggerGiveUp? → TurnToGiveup
    │    → disconnect() → connect()
    ├─ channel != CLOSED（OPEN）
    │    → 记账；shouldTriggerGiveUp? → TurnToGiveup
    │    → callRunIterate → disconnect() → connect()
    ├─ isConnectBad() || !isHealthyCached()
    │    → 记账；shouldTriggerGiveUp? → TurnToGiveup
    │    → disconnect() → connect()
    └─ else → TurnToRunning()
```

三条恢复分支都必须先 `updateBadStatusTime()`，否则 `m_lastBadStatusMs`
永远是 0，`shouldTriggerGiveUp()` 永远为 false，`GIVEN_UP` 不可达
（T128 针对的就是第一分支漏记账）。

### 3.4 shutdown / recreate

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
`ApiLease` 保证 `m_terminated` 之后普通 API 不再进入。T150 用
`ISdk::clientDelete` 阻塞钩子钉死 shutdown 赢家；T153 用同一个钩子钉死
recreate 尾巴——`m_recreating` 必须在 `old->shutdown()` 之后才复位，否则
析构会在旧 Impl 仍阻塞于 `clientDelete` 时返回。

## 4. open62541 v1.4.14 事实清单

| # | 事实 | 官方位置 |
| --- | --- | --- |
| A | 客户端没有自己的线程，必须周期调用 `UA_Client_run_iterate` | `include/open62541/client.h:46-50` |
| B | `isFullyConnected` 只看 `noSession`，不看 `noReconnect/noNewSession` | `src/client/ua_client_connect.c:121-128` |
| C | `noReconnect` 只禁止“被动失联后的后台重连” | `include/open62541/client.h:112-114` |
| D | `noNewSession`：会话失效即主动关通道，恢复责任回到封装层 | `include/open62541/client.h:114-116`、`src/client/ua_client.c:465-487` |
| E | 同步服务调用发现未连接会自动 `connectSync()` | `src/client/ua_client.c:569-578` |
| F | 通道未 CLOSED 时 `initConnect` 直接 return，无法真正重建 | `src/client/ua_client_connect.c:1699` |
| G | “坏”必须按 severity 位判断：`UA_StatusCode_isBad()` | `include/open62541/types.h:163-166` |
| H | `disconnectAsync` 必须被 `run_iterate` 驱动到 CLOSED | `include/open62541/client.h:468-470` |
| I | `UA_ReadResponse_clear` 走 `UA_clear`；`clearStructure` 对非可选数组**无条件**调用 `UA_Array_delete(ptr, length, type)` | `types_generated_handling.h:9293`、`src/ua_types.c:1393` |
| J | `UA_TYPES_DIAGNOSTICINFO::pointerFree == false`，因此空指针 + 长度 > 0 会在 `UA_Array_delete` 内解引用空指针 | `types_generated.c` 的 DiagnosticInfo 类型项 |
| K | `UA_Client_run_iterate` 在 `el->run(timeout)` 前释放 clientMutex；SDK 允许它与服务调用并发 | `src/client/ua_client.c:1043-1060` |
| L | `UA_EventLoop::run(timeout)`：`timeout==0` 只处理已到达事件，不阻塞 | `include/open62541/plugin/eventloop.h:111-114` |
| M | `UA_Client_Service_read` → `__UA_Client_Service` 在 clientMutex 内调用 `__Client_Service`；未连接时还会在内层 `connectSync()` | `include/open62541/client.h:612-616`、`src/client/ua_client.c:554-578`、`:668-673` |
| N | Good 包含 `GOOD_CLAMPED` 等信息位；`== UA_STATUSCODE_GOOD` 只匹配 0，会丢掉 GoodInfo 的值 | `include/open62541/types.h:163-176`、`statuscodes.h:190` |
| O | `UA_ClientConfig_setDefault` 用 `UA_Log_Stdout_new` **堆分配** logger；`UA_Client_newWithConfig` 浅拷贝；`UA_Client_delete` → `Config_clear` 调 `logging->clear` 释放 | `plugins/ua_config_default.c:1120-1121`、`plugins/ua_log_stdout.c:96-117`、`src/client/ua_client.c:107-125`、`:128-175`、`:236-242` |
| P | `UA_LogLevel` 为 TRACE=100…FATAL=600；`UA_LOGLEVEL` 是编译期门槛（本机构建为 100），logger 仍会收到全部级别，需由 logger 自己过滤 | `include/open62541/plugin/log.h:34-41`；`build/src_generated/open62541/config.h` 的 `UA_LOGLEVEL` |

K/L/M 是尾延迟修复的依据：包装层在 `run_iterate` 外面持有
`m_sdkMutex`，所以即使 SDK 在 `el->run()` 期间不持 clientMutex，空闲轮询
`timeout=50ms` 仍会把所有服务调用挡在包装层锁外。把空闲轮询和看门狗健康
探针改为 `timeout=0` 后，阻塞只保留在真正需要握手推进的
`connect()/disconnect()` 分支。

I/J 是 T130 的依据：出口守卫既然拦截 `results == nullptr && resultsSize > 0`，
就必须同样拦截 `diagnosticInfos == nullptr && diagnosticInfosSize > 0`。
N 是 T157/T158 的依据：读/写逐项状态必须用 `UA_StatusCode_isGood()`，不能
与 0 做等值比较。
