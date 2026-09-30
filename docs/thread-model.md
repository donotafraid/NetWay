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
| `Impl::m_sdkMutex` | `std::recursive_mutex` | 全部 `UA_Client_*` 调用 | SDK 调用点 |
| `Impl::m_taskCvMx` + `m_taskCv` + `m_taskCounter` | `mutex`/`cv`/`size_t` | 在途读写计数（`InFlightGuard`），供 `shutdown()` drain | 读写、shutdown |
| `Impl::m_watchDogMx` + `m_watchdogCv` | `mutex`/`cv` | 看门狗等待与唤醒 | 看门狗、shutdown |
| `Impl::m_retryCvMx` + `m_retryCv` | `mutex`/`cv` | `delayFunction()` 退避，可被 shutdown 唤醒 | 读写、看门狗 |
| `Impl::m_BadStatusMsMx` | `std::mutex` | `m_lastBadStatusMs` | 看门狗、`isConnected()` |
| `RecreateSync::m_recreateLock` | `std::mutex` | `m_recreating`、`m_inFlight`、`m_recreateCv` | API 租约、recreate、析构 |
| `RecreateSync::m_lock` | `std::mutex` | `pImpl` shared_ptr 快照 | 公开 API 入口 |
| `Impl::m_watchdogThread` 的 join | — | `shutdown()`/`~Impl()` 先 join 再清理 | teardown |

锁序：任何时候都是 **`m_recreateLock` → `m_lock` → `m_sdkMutex`**；
`m_recreateLock` 的 `wait` 会在等待期间释放，因此 `ApiLease` 析构可以递减计数。
回调（`stateCallback` / `inactivityCallback`）**只做原子写，不加任何锁**。

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
shutdown():                       recreateGiveUpClient():
 ├─ CAS m_terminated               ├─ CAS m_recreating
 ├─ drain m_taskCounter == 0       ├─ 等 m_inFlight == 0（2s 上限）
 └─ shutdownInternal()             ├─ 要求 lifeState == GIVEN_UP
     ├─ pImpl → doomed            ├─ new Impl + configure + connect + startWatchdog
     ├─ doomed->shutdown()        └─ 换入 pImpl，旧 Impl shutdown
     └─ doomed.reset()
```

`~OPC_UA_Client` 先置 `m_terminated` 并等待 `m_recreating == false`（recreate
是唯一还会解引用 `this` 的路径），再 `shutdownInternal()`。`ApiLease` 保证
`m_terminated` 之后普通 API 不再进入。

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

I/J 是 T130 的依据：出口守卫既然拦截 `results == nullptr && resultsSize > 0`，
就必须同样拦截 `diagnosticInfos == nullptr && diagnosticInfosSize > 0`。
