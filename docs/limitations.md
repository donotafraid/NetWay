# 已知限制

本文只写“当前实现真的做不到 / 尚未验证”的事情，方便面试时主动交底，也方便
后续排期。每条尽量给出应对方式。

## 1. 功能与语义限制

- **只支持 STRING 型 NodeId。** `OPC_UA_Client::NodeId` 的 `id` 是
  `std::string`，实现统一走 `UA_NODEID_STRING_ALLOC`。数字型 NodeId
  （`UA_NODEID_NUMERIC`）目前无法表达。
  应对：需要时给 `NodeId` 增加 variant 或 `uint32_t numericId` 字段。
- **NodeId 不允许内嵌 `'\0'`。** `UA_NODEID_STRING_ALLOC` 内部走 `strlen`，
  会把 `"A\0B"` 截断成 `"A"`。实现已在读、写两条路径显式拒绝并返回
  `INVALID_ARGUMENT`，而不是静默截断（`detail/TypeTraits.h`）。
- **STRING 值的读、写不对称。** 读路径用 `src->length` 原样返回内嵌 `'\0'`
  （T75 钉死）；写路径显式拒绝内嵌 `'\0'`（`detail/TypeTraits.h`），避免
  `strlen` 截断。
- **无最大批量上限。** `batchRead` / `batchWrite` 对元素个数没有上限，只受
  内存与 `maxTotalWaitMs` 约束。官方 `UA_Array_new` 在超大 size 时可能失败，
  实现在该处返回 `INTERNAL_ERROR`。工业现场建议由调用方分批，或后续在
  `ClientConfig` 增加 `maxBatchSize` 并在进入 SDK 前拒绝。
- **写不自动重试。** OPC UA 写入非幂等；`batchWrite` 只尝试一次，逐项状态
  原样返回，由业务决定是否重试。读在可重试码上有 `maxRetries` 次指数退避。
- **`disconnect()` 不是“保持断开”开关。** 断开后 `lifeState` 进入
  `RECOVERING`，看门狗会在下一个 `watchdogIntervalMs` 尝试重连；业务线程的
  同步服务调用也可能触发 SDK 的 `connectSync()`，最终状态由看门狗收敛。
  这正是“自愈”的设计意图。若要长期停用，请调用 `shutdown()` 或
  `setGiveUpSignal()`。
- **`GIVEN_UP` 也计入 stop signal。** 一旦进入 `GIVEN_UP`，读写与 `connect()`
  都快速失败，必须 `recreateGiveUpClient()`。

## 2. 阻塞与并发

- **单实例串行化。** 所有 `UA_Client_*` 调用经 `m_sdkMutex` 串行；并发读写在
  排队，吞吐不随线程数扩展。这是“多线程共享同一实例”的代价，而非缺陷。
- **看门狗空闲轮询不得阻塞。** `UA_Client_run_iterate(client, timeout)` 在
  `timeout>0` 时会阻塞到有事件或超时；封装在它外面还持有 `m_sdkMutex`，因此
  空闲轮询用 `timeout>0` 会把每个服务调用挡在锁外最长 `timeout` 毫秒。
  当前实现把空闲轮询和健康探针改为 `timeout=0`，只处理已到达事件；
  `disconnect()`/`connect()` 的恢复分支仍然允许阻塞，但只发生在看门狗
  判定不健康时，而不是每 100ms 一次。
- **单次调用阻塞上界。** 预算只在每次尝试**开始前**检查：一次 `batchRead`
  最坏 ≈ `maxTotalWaitMs + 一次在途调用`，而单次在途调用还包含 SDK 内部的
  `connectSync`，可能接近 `2 × timeoutMs`。因此 `maxTotalWaitMs` 是软预算，
  不会抢占已经在 SDK 内的同步调用；最后一个重试点返回后也不再补一次预算检查。
  高频健康检查请用 `isHealthy()`（只读缓存），不要用 `checkConnected()`
  （会争抢 eventLoop 锁）。
- **`shutdown()` 的第二个调用者立即返回 success，不等待第一个调用者 drain 完成**
  （契约如此，测试 T77 钉死）。但析构会等待已赢得 shutdown 的调用者退出
  `shutdownInternal()`（T150），所以“第二个调用者返回后销毁对象”不会与 drain
  竞争；不要据此认为关闭已经完成。
- **`recreateGiveUpClient()` 在旧 Impl 收尾完成前保持 `m_recreating=true`。**
  旧实现先把 `m_recreating` 复位、再 `old->shutdown()`，析构可能在这条尾巴
  运行期间释放对象（T153 红）。现在析构会等到旧 Impl 的 `shutdown()` 返回；
  代价是 recreate 期间所有业务 API 都返回 `RECREATING`，直到收尾结束。
- **看门狗线程 join 无独立超时。** 依赖 `connect()` / `disconnect()` 自身的
  墙钟兜底（`2 × timeoutMs + 500ms`）与 `delayFunction()` 的分段可唤醒。
- **公开 API 的异常约定不统一。** `create` / `createWithSdk` / `batchRead` /
  `batchWrite` 捕获异常并返回 `Result`；`connect` / `disconnect` /
  `checkConnected` / `isHealthy` / `checkLifeState` / `setGiveUpSignal` /
  `recreateGiveUpClient` / `shutdown` 未捕获。走 `RealSdk` 时只有
  `std::bad_alloc` 等极少数异常，但自定义 `ISdk`（或测试钩子）抛异常会直接
  穿透公开 API。RAII 守卫保证不泄漏、状态机不会卡在 `CONNECTING`
  （T135~T137），需要统一 no-throw 时应在这些入口补 `try/catch`。
- **`recreateGiveUpClient()` 的 drain 上限 2s**，超时返回
  `IN_FLIGHT_TIMEOUT`；此时旧实例保持原样，可重试。

## 3. 测试与文档边界

- **MockSdk 不建模**：SDK 服务调用前的 `connectSync`、`noNewSession` 关通道、
  `connectAsync` 的真实异步/超时、`run_iterate` 的 timeout 参数、
  channel/session 的独立迁移。因此“断线恢复是否真的走通”必须由真实
  server 的集成用例（T110/T116/T117）保证，不能只靠 Mock。
- **benchmark 已提供，但不在 CI 门禁内。** `bench/` 与 `scripts/run_bench.sh`
  覆盖 8 线程 `batchRead`/`batchWrite` 的 P50/P99、断线恢复耗时分布、
  长时间 RSS 曲线。这些是观测值，需要人工跑并归档原始结果，CI 不设阈值。
  `scripts/check_rss.py` 与 `scripts/run_netem_matrix.sh` 提供可复现的
  PASS/FAIL 判据，但仍需人工在有 root / 长时间窗口的机器上触发。
- **单次延迟 p99 不可复现。** 同一份代码、同一台机器，单次 8 线程 p99 可以在
  0.9ms~28.6ms 之间波动；`bench_latency --repeats N` 的中位数才是可引用值。
  8 线程吞吐并不随线程数线性扩展：`m_sdkMutex` 与 open62541 内部 clientMutex
  把同步服务调用串行化，线程越多 p99 越大。2026-10-24 的 clean 归档（修复前）
  为 t=1 p99=0.869ms、t=8 p99=147.505ms、p99.9=412.307ms。把空闲轮询与健康
  探针改为 `timeout=0` 后，2026-10-27 复跑（WSL2、8 vCPU、RelWithDebInfo、
  batch=16、10 万样本、5 次中位数）为：
  t=1 calls_s=6707.1、p99=0.326ms、p99.9=0.514ms；
  t=4 calls_s=3428.5、p99=9.832ms、p99.9=182.783ms；
  t=8 calls_s=3361.5、p99=20.956ms、p99.9=436.297ms。
  1 线程 p99.9 正好落在 50ms 附近，是旧实现“每 100ms 抢锁 50ms”的确定性证据；
  修复后 1T p99.9 降到 0.514ms。**但 8T 严格判据（p99 ≤ 3×1T p99 + RTT、
  吞吐不低于 1T）没有通过**：8T 是单 client 饱和 + server/看门狗未绑核的排队
  尾部，不是线性扩展承诺。完整分析、PASS/FAIL 表和下一步实验见
  [docs/benchmark.md](benchmark.md)。
- **RSS soak 必须让 server 跑在独立进程。** `bench_rss` 读的是自身
  `/proc/self/statm`；若用默认的 in-process server，server 的分配会混进
  “客户端”曲线。`scripts/run_bench.sh` 现在用 `bench_server` 把 server 放到
  子进程，`bench_rss --endpoint` 只测客户端。
- **真实网络抖动需要人工注入。** `scripts/netem_faults.sh` 用 `tc netem` /
  `iptables` 在回环上制造高延迟、丢包与半开连接；`scripts/run_netem_matrix.sh`
  把四种故障串成一次可判定的矩阵。数据来自本机回环，不代表真实工业现场的
  多跳网络。
- **`bench_rss` 的长时间曲线依赖进程存活。** 机器重启 / OOM 会截断曲线，
  归档时保留 CSV 原始数据；`check_rss.py --min-hours` 用于拒绝被截断的曲线。
  `run_bench.sh` 的 soak 段是前台进程，会话退出会被 `trap` 连同独立 server
  一起终止（实测一次 24h soak 在 43 min 处被截断）。长跑必须用
  `setsid nohup ... < /dev/null &` 脱离终端，跑完再执行
  `check_rss.py --min-hours 23.5`；`run_bench.sh` 现已自动传入该阈值并归档
  `rss_check.txt`，失败时脚本非零退出。
- **benchmark 的 stdout / 历史产物。** 当前 `bench/bench_common.h` 已把
  open62541 内建 server 的 logger 重定向到 stderr；新跑出的
  `bench_latency` / `bench_recovery` stdout 只含 `run=` / `median` /
  `*_ms` 行。已归档的旧文件（例如 2026-10 之前生成的
  `bench-results/latency_8thread_*.txt`）仍混有 server 日志，解析时继续用
  `grep '^median '`；不要把旧文件的首行当作机器可读格式。

## 4. 高延迟链路上的连接预算

`tc netem delay 200ms`（回环上单程 200ms，RTT ≈ 400ms）时，`create()` 在约
5s 后返回 `CONNECT_TIMEOUT`：已归档的 `bench-results/netem_200ms/`
`latency_delay200.txt` 末尾即为 `create failed: CONNECT_TIMEOUT`。

根因是**预算，不是竞态**：`bench_latency` 固定用 `PROFILE_LOCAL`
（`timeoutMs = 5000`），而 `Impl::connect()` 把这个 `timeoutMs` 当作整条连接
握手（TCP + OPN + CreateSession + ActivateSession，若干次往返）的总预算；
400ms RTT 下握手所需的往返数超过 5s。同一份代码在 50ms 单程（RTT ≈ 100ms）
下稳定 PASS（见 `bench-results/netem/summary.txt`）。

应对：高 RTT 链路用 `PROFILE_REMOTE`（`timeoutMs = 15000`）或显式调大
`timeoutMs`；`bench_latency` 目前只支持默认 `PROFILE_LOCAL`，需要在注入
高延迟时加一个 `--profile` 开关。该结论来自本机回环注入，不代表真实多跳
网络，现场需复核。`bench-results/netem_200ms/` 是这次失败样本，已被 50ms 的
`bench-results/netem/` 取代，建议删除或改名归档，避免把失败样本当成当前能力。

## 5. 错误码约定

- `NOT_CONNECTED`：只读查询在 stop / 不健康时的统一错误；读写则用
  `ALREADY_TERMINATED`（语义不同，见 `ua.cpp` 注释）。
- `RECREATING` / `THREAD_BUSY` / `ALREADY_TERMINATED` 是并发窗口的可重试结果，
  不表示请求本身失败。
- `what()` 仅用于日志与诊断，分支判断一律用 `code()`。
