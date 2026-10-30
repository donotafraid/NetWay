# 已知限制

本文只写“当前实现真的做不到 / 尚未验证 / 有明确窗口”的事情。每条尽量给出
影响、触发条件与应对方式；当前性能数字引用 `20261030_141909_slo` 的 15 次
中位数，历史 5 次归档只作对照并明确标注。

## 1. 功能与语义

- **NodeId 只支持 STRING 与 NUMERIC。** `OPC_UA_Client::NodeId.id` 是
  `std::variant<std::string, uint32_t>`：
  - `std::string` → `ns=<ns>;s=<id>`（`UA_NODEID_STRING_ALLOC`）
  - `uint32_t` → `ns=<ns>;i=<id>`（`UA_NODEID_NUMERIC`）

  GUID / ByteString 标识无法表达。数字型 NodeId 的读、写、混合批处理与真实
  server 往返由 T163~T166 覆盖；GUID/ByteString 需要给 `NodeId` 增加 variant
  分支并补 SDK 路径。
- **STRING 型 NodeId 不允许内嵌 `'\0'`，也不允许空串。**
  `UA_NODEID_STRING_ALLOC` 内部走 `strlen`，会把 `"A\0B"` 截断成 `"A"`。
  读、写两条路径在进入 SDK 前显式拒绝并返回 `INVALID_ARGUMENT`。
  NUMERIC 的 0 目前不做额外拒绝，由 server 决定（通常是
  `BadNodeIdInvalid`）；如果业务需要更早失败，应在 `uaNodeIDAlloc()` 里加
  策略。
- **STRING 值的读、写不对称。** 读路径用 `src->length` 原样返回内嵌 `'\0'`
  （T75）；写路径显式拒绝内嵌 `'\0'`（`detail/TypeTraits.h`），避免
  `strlen` 截断。这个不对称是 SDK 表示方式决定的，文档必须写清楚。
- **只读/写 VALUE 属性。** 请求固定 `attributeId = UA_ATTRIBUTEID_VALUE`、
  `indexRange = UA_STRING_NULL`。不支持读 NodeId/DisplayName 等其他属性，
  不支持数组下标范围读取。
- **没有 MethodCall / Browse / Subscription。** 当前 API 只有批量读、批量写、
  连接/健康/生命周期管理。报警、历史数据、订阅需要另开 API 面。
- **没有加密、证书或用户身份配置。** `RealSdk` 用
  `UA_ClientConfig_setDefault()` 的默认值；在没有证书/身份配置时实际只会
  选 None 安全策略 + 匿名端点。`ClientConfig` 没有 securityMode/证书/
  用户名密码字段。它只适合受控网络或前置安全网关；不要把“SecureChannel”
  误认为“已加密”。
- **没有断线后的会话转移。** `noNewSession=true` 时会话丢失会让 SDK 关闭
  通道；封装层的重试只能重新建会话，不能保留原 Session 的订阅/状态。
- **无最大批量上限。** `batchRead` / `batchWrite` 对元素个数没有上限，只受
  内存与 `maxTotalWaitMs` 约束。`UA_Array_new` 在超大 size 时可能失败，实现
  返回 `INTERNAL_ERROR`。工业现场建议调用方分批，或在 `ClientConfig` 增加
  `maxBatchSize` 并在进入 SDK 前拒绝。
- **写不自动重试。** OPC UA 写入非幂等；`batchWrite` 只尝试一次，逐项状态
  原样返回，由业务决定是否重试。读在可重试码上有 `maxRetries` 次指数退避。
- **`disconnect()` 不是“保持断开”开关。** 断开后 `lifeState` 进入
  `RECOVERING`，看门狗会在下一个 `watchdogIntervalMs` 尝试重连；业务线程的
  同步服务调用也可能触发 SDK 的 `connectSync()`（v1.4.14
  `src/client/ua_client.c:569-578`），最终状态由看门狗收敛。若要长期停用，
  请调用 `shutdown()` 或 `setGiveUpSignal()`。
- **`GIVEN_UP` 也计入 stop signal。** 一旦进入 `GIVEN_UP`，读写与 `connect()`
  都快速失败，必须 `recreateGiveUpClient()`。
- **`recreateGiveUpClient()` 只接受 `GIVEN_UP`。** 其他状态返回
  `NOT_GIVEN_UP`，不会隐式先 `setGiveUpSignal()`；这是为了避免业务误重建。

## 2. 阻塞与并发

- **单实例串行化。** 所有 `UA_Client_*` 调用经 `m_sdkMutex` 串行；并发读写在
  排队，吞吐不随线程数线性扩展。这是“多线程共享同一实例”的代价，不是锁
  写错。需要线性扩展时用 per-thread client 或 client pool，并接受多 session
  的代价。
- **看门狗空闲轮询不得阻塞。** `UA_Client_run_iterate(client, timeout)` 在
  `timeout>0` 时会阻塞到有事件或超时；封装在它外面还持有 `m_sdkMutex`。
  当前空闲轮询和健康探针用 `timeout=0`，只处理已到达事件；
  `disconnect()`/`connect()` 的恢复分支仍然允许阻塞，但只发生在看门狗判定
  不健康时。
- **健康探针可能把服务调用堵在包装锁外。** `isConnected(0)` 在
  `m_sdkMutex` 内调用 `UA_Client_getState()`；`getState` 还要拿 SDK
  `clientMutex`。如果服务调用正持有 `clientMutex` 等 server，1s 一次的
  探针会持 `m_sdkMutex` 排队，后续业务调用跟着排队。这是头阻塞，不是数据
  竞争；修复方向是探针用 `try_lock` 跳过或把 `getState` 移出包装锁，并用
  固定率 A/B 量化。
- **配置在 `UA_Client_newWithConfig()` 之后被就地修改。**
  `applyConfiguration()` 改 timeout/callback/logger，发生在 `connect()` 之前
  的单线程窗口；没有悬垂指针，但违反官方“configuration should not be
  modified after it is passed to a client”的约定。更干净的做法是在
  `clientNew()` 之前填完 `UA_ClientConfig`。
- **TSan 曾经最终落到 `-O0`。** `ENABLE_TSAN` 追加 `-O1` 后，Top-level CMake
  的 Debug 分支又追加 `-O0`；当前已改为 TSan 跳过 `-O0`。旧 TSan 日志不能
  声称覆盖 `-O1` 下的竞态。
- **单次调用阻塞上界。** 预算只在每次尝试**开始前**检查：一次 `batchRead`
  最坏 ≈ `maxTotalWaitMs + 一次在途调用`，而单次在途调用还包含 SDK 内部的
  `connectSync`，可能接近 `2 × timeoutMs`。`maxTotalWaitMs` 是软预算，不会
  抢占已经在 SDK 内的同步调用。高频健康检查请用 `isHealthy()`（只读缓存），
  不要用 `checkConnected()`（会争抢 eventLoop 锁）。
- **`shutdown()` 的第二个调用者立即返回 success，不等待第一个调用者 drain
  完成**（契约如此，T77 钉死）。但析构会等待已赢得 shutdown 的调用者退出
  `shutdownInternal()`（T150）；不要据此认为关闭已经完成。
- **`recreateGiveUpClient()` 在旧 Impl 收尾完成前保持 `m_recreating=true`。**
  析构会等到旧 Impl 的 `shutdown()` 返回，代价是 recreate 期间所有业务 API
  返回 `RECREATING`，直到收尾结束。
- **看门狗线程 join 无独立超时。** 依赖 `connect()` / `disconnect()` 自身的
  墙钟兜底（`2 × timeoutMs + 500ms`）与 `delayFunction()` 的分段可唤醒。
- **`connectionLost` 的清除存在一个竞争窗口。** `inactivityCallback` 在
  重连判定窗口内置位，`TurnToRunning()` 成功后无条件清零，可能吞掉一次刚
  发生的失联；`isHealthy()` 最多晚一个 `watchdogIntervalMs` 才报不健康。
  修复需要给失联事件配代际号，或把清零移到 `connect()` 之前；本轮保留窗口
  并在这里明确记录。
- **状态三元组的 version 只有 16 位。** 单次 `getState()` 窗口内需要
  65,536 次 `stateCallback` 才回绕，实际不可达；恶意 ISdk 可以构造该场景。
  需要绝对防御时把 version 扩到 32 位或改用独立 tag。
- **公开 API 已统一 no-throw，但 `ISdk` 必须配合。** 所有 public 入口捕获
  异常并返回 `Result`，`tests/test43.cpp` 用 `static_assert(noexcept(...))`
  钉死。自定义 `ISdk` 仍被要求不抛；`clientDelete` 有一层兜底 `try/catch`，
  违约时可能泄漏 client 而不是 terminate。`~OPC_UA_Client` 是 best-effort，
  锁/条件变量失败会吞掉，不会终止进程。
- **`recreateGiveUpClient()` 的 drain 上限 2s**，超时返回
  `IN_FLIGHT_TIMEOUT`；此时旧实例保持原样，可重试。
- **recreate 与析构的候选 Impl 泄漏（已修）。** 旧代码在
  `startWatchdog()` 之后、`pImpl` 换入之前命中 `m_terminated` 时直接返回；
  候选 Impl 只被自己的 watchdog 以 `shared_from_this()` 持有，线程和
  `UA_Client` 都不会释放。现在 `CandidateGuard` 在未收养时调用
  `shutdown()`，T169 用 client 新建/删除计数钉死。

## 3. 测试与文档边界

- **MockSdk 不建模**：SDK 服务调用前的 `connectSync`、`noNewSession` 关通道、
  `connectAsync` 的真实异步/超时、`run_iterate` 的 timeout 参数、channel/
  session 的独立迁移。因此“断线恢复是否真的走通”必须由真实 server 的集成
  用例（T110/T116/T117/T166）保证，不能只靠 Mock。
- **数字 NodeId 的真实 server 覆盖只有一个变量。** T166 用
  `ns=1;i=20001` 做 INT16 往返；没有覆盖 NUMERIC 的越界/不存在节点、复数
  命名空间、GUID/ByteString。后者是能力缺口，不是测试缺口。
- **CI 只门禁已归档的 capacity/CI 指标，不跑完整 benchmark。** `bench/` 与
  `scripts/run_slo_bench.sh` 覆盖 8 线程 `batchRead`/`batchWrite` 的
  P50/P99、断线恢复耗时分布、长时间 RSS 曲线；这些是观测值，需要人工跑并
  归档原始结果。`check_bench_slo.py --profile capacity --ci-upper-ms 1.0
  --min-runs 15` 只对已提交的 `median.txt`/`ci.txt` 做 1T p99.9、8T 容量
  下限和 errors 校验；8T 严格线性与固定率诊断仍是人工判断，不设为 CI 门禁。
- **单次延迟 p99 不可复现，且 p99 依赖吞吐。** 同一份代码、同一台机器，
  单次 8 线程 p99 可以在数毫秒到数十毫秒之间波动；当前归档的 `8t` 9.308ms
  是 8376.1 calls/s 的饱和点，固定 4268 calls/s 的 `8t_r80` 是 1.794ms。
  引用 p99 必须同时给 calls/s、构建类型和中位数；不能把上一轮的 25.740ms
  或本轮的 9.308ms 单独当成稳定容量。
- **1T p99.9 的 1ms 判据在这一轮通过，但不是跨机器保证。** 当前归档 15 次
  中位数 0.715ms、bootstrap 95% CI 上界 0.763ms，15 个原始 run 的 p99.9
  最大值 0.773ms；`check_bench_slo.py --ci-upper-ms 1.0 --min-runs 15` 会
  同时检查中位数、CI 上界和样本数。仍要按本机/本构建引用；换主机需重跑。
- **benchmark 证据必须带“二进制不旧于源码 + 构建类型 + 源码哈希”的证明。**
  `scripts/run_slo_bench.sh` 的 `env.txt` 记录 `bin vs src`、`CMAKE_BUILD_TYPE`、
  `-O*`、源码/二进制 SHA-256、cgroup 配额和 `git diff --stat`；发布前必须
  确认 binary 晚于 `src/`、`include/` 的最后修改，且工作区干净或哈希可追溯。
- **归档必须真实存在且已纳入 git。** 旧文档出现过的 `20261028_101222_slo`
  和 `20261029_131811_slo` 都不在当前 checkout；前者从未存在，后者只保留在
  历史证据目录。`scripts/check_bench_refs.py` 在 CI 中拒绝悬空的
  `bench-results/<stamp>_slo` 路径和未 `git add` 的归档；数字没有可获取的
  归档就不是证据。
- **日志默认门槛是 WARNING，不是“全量 trace”。** SDK 以
  `UA_LOGLEVEL=100` 编译；如果 `customLog()` 不按 `UA_LogLevel` 过滤，
  一次 10 万次批量读会写出约 55MB stderr。现在
  `ClientConfig::logLevel` 默认 `LOG_WARNING`，`bench/` 用 `LOG_ERROR`；
  排查时显式调到 `LOG_INFO`/`LOG_DEBUG`，不要在产线默认打开。
- **per-thread client 没有每线程绑核，也不需要把它当成代码优化项。**
  `8t_ptc` 只用 `taskset -c 0-7` 允许迁移；当前 15 次中位数
  p99=0.669ms、23483.2 calls/s，严格判据通过。8 个 client + 8 个看门狗 + server 在
  8 vCPU 上无法一对一绑定；要降低线程成本，优先做“所有 client 共享一个
  watchdog”或固定 client 池，而不是绑核。绑核属于 benchmark 控制变量。
- **RSS soak 必须让 server 跑在独立进程。** `bench_rss` 读的是自身
  `/proc/self/statm`；若用默认的 in-process server，server 的分配会混进
  “客户端”曲线。`scripts/run_bench.sh` 用 `bench_server` 把 server 放到
  子进程，`bench_rss --endpoint` 只测客户端。
- **真实网络抖动需要人工注入。** `scripts/netem_faults.sh` 用 `tc netem` /
  `iptables` 在回环上制造高延迟、丢包与半开连接；数据来自本机回环，不代表
  真实工业现场的多跳网络。
- **`bench_rss` 的长时间曲线依赖进程存活。** 机器重启 / OOM 会截断曲线；
  归档时保留 CSV 原始数据，`check_rss.py --min-hours` 用于拒绝被截断的曲线。
  `run_bench.sh` 的 soak 段是前台进程，会话退出会被 `trap` 连同独立 server
  一起终止；长跑必须用 `setsid nohup ... < /dev/null &` 脱离终端。
- **当前 SLO 归档是 `bench-results/20261030_141909_slo/`。**
  RelWithDebInfo/-O2，15×6 = 90 个 run，errors=0；HEAD `228df04` 干净，
  `dirty src/include` 为空。`20261030_083718_slo` 是 5 次历史归档，
  `20261029_141357_slo` 是补丁前基线，两者只作跨会话方差对照。复跑必须用
  `docs/benchmark.md` 的命令生成新时间戳归档，并先 commit 再跑。
- **旧 `median.txt` 不含 errors。** 原始 run 文件里有 `errors=0`；只看旧
  中位数无法发现某一次 run 出错。新脚本在汇总前拒绝 `errors!=0`，并把
  `errors: 0` 写进 median；`check_bench_slo.py --require-errors` 会对缺少
  该字段的旧归档明确报错。

## 4. 高延迟链路上的连接预算

`tc netem delay 200ms`（回环上单程 200ms，RTT ≈ 400ms）时，`create()` 在约
5s 后返回 `CONNECT_TIMEOUT`。根因是**预算，不是竞态**：`bench_latency` 固定用
`PROFILE_LOCAL`（`timeoutMs = 5000`），而 `Impl::connect()` 把这个
`timeoutMs` 当作整条连接握手（TCP + OPN + CreateSession + ActivateSession，
若干次往返）的总预算；400ms RTT 下握手所需的往返数超过 5s。同一份代码在
50ms 单程（RTT ≈ 100ms）下稳定 PASS（见 `bench-results/netem/summary.txt`）。

应对：高 RTT 链路用 `PROFILE_REMOTE`（`timeoutMs = 15000`）或显式调大
`timeoutMs`；`bench_latency` 目前只支持默认 `PROFILE_LOCAL`，需要在注入
高延迟时加一个 `--profile` 开关。该结论来自本机回环注入，不代表真实多跳
网络，现场需复核。

## 5. 错误码约定

- `NOT_CONNECTED`：只读查询在 stop / 不健康时的统一错误；读写则用
  `ALREADY_TERMINATED`（语义不同，见 `ua.cpp` 注释）。
- `RECREATING` / `THREAD_BUSY` / `ALREADY_TERMINATED` 是并发窗口的可重试结果，
  不表示请求本身失败。
- `rawStatus` / `what()` 仅用于日志与诊断，分支判断一律用 `code()`。
- 逐项状态用 `UA_StatusCode_isGood()`（`include/open62541/types.h:163-176`），
  不与其他值做等值比较：`GOOD_CLAMPED` 等 GoodInfo 仍是成功。
