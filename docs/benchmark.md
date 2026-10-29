# Benchmark 方法与 PASS 判据

本文只回答一个问题：**当前 `bench_latency` 的数字能证明什么，不能证明什么。**
它不是“8 线程 p99 越小越好”的排行榜；共享同一 `OPC_UA_Client` 的同步服务
调用在结构上就是串行的。

## 1. 被测对象与命令

`bench/bench_latency.cpp` 默认在进程内起一个 open62541 server；SLO 归档用
`bench_server` 把 server 放到独立进程，并用 `taskset` 绑核。客户端用
`PROFILE_LOCAL`，`watchdogIntervalMs=1000`，`batch=16`。每组 5 次，取中位数。

```sh
RUNS=5 OPS=100000 BATCH=16 SRV_CPUS=7 ./scripts/run_slo_bench.sh
```

六个 label：

| label | 线程 | client 模型 | 到达率 |
| --- | ---: | --- | --- |
| `1t` | 1 | 共享实例 | 不限 |
| `4t` | 4 | 共享实例 | 不限 |
| `8t` | 8 | 共享实例 | 不限 |
| `8t_r80` | 8 | 共享实例 | 4268 calls/s（历史固定负载） |
| `8t_ptc` | 8 | 每线程一个 client | 不限 |
| `8t_ptc_r80` | 8 | 每线程一个 client | 2864 calls/s（历史固定负载） |

`RATE_80` / `RATE_80_PTC` 不是“当前容量的 80%”，只是兼容旧归档的固定负载；
换机器/换构建后必须重新标定。

## 2. 证据清单（先看时间戳，再看数字）

benchmark 归档只有在**二进制不旧于源码**时才代表当前代码。`env.txt` 的
`bin vs src` 段是发布前必须检查的证据。

| 归档 | 构建 | 源码/二进制 | 证据强度 |
| --- | --- | --- | --- |
| `bench-results/20261029_131811_slo` | Debug, Clang 17 | 源码 13:09:37，binary 13:18:08 | **当前补丁后有效**；5×6 run，errors=0 |
| `bench-results/20261028_133818_slo` | RelWithDebInfo | 源码 12:16，binary 13:38 | 有效，但只对应 commit `87c3094`，不含 10-29 的 numeric/no-throw 改动 |
| `bench-results/20261028_232459_slo` | RelWithDebInfo | 源码 23:11，binary 13:38 | **无效**：binary 早于源码，且工作区有未提交改动；只能作历史对照 |
| `bench-results/20261028_101222_slo` | — | — | **当前 checkout 不存在**；旧文档引用它，不能作为可审计证据 |

`20261028_232459_slo/median.txt` 的 8T p99=20.253ms 与 `20261029_131811_slo`
的 2.037ms 差距，不是代码在一天内变快 8 倍，而是前者跑了旧 binary/受宿主
调度影响。发布时只引用能通过 `bin vs src` 检查的归档。

## 3. 当前源码的 5 次中位数（2026-10-29，Debug）

归档：`bench-results/20261029_131811_slo/median.txt`。环境：WSL2 8 vCPU
（i7-7700HQ，4C/8T）、server 绑 CPU7、Clang 17、libc++、Debug、batch=16、
10 万样本；30 个 run 全部 `errors=0`，客户端 stderr 只有 65B 的 watchdog
启动行。

| label | calls/s | p50/ms | p90/ms | p99/ms | p99.9/ms | max/ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1t | 8560.4 | 0.101 | 0.152 | 0.243 | 0.697 | 0.909 |
| 4t | 7583.7 | 0.504 | 0.607 | 0.981 | 1.708 | 13.394 |
| 8t | 7372.9 | 1.054 | 1.229 | 2.037 | 5.348 | 20.074 |
| 8t_r80 | 4267.7 | 0.991 | 1.558 | 2.331 | 11.379 | 18.617 |
| 8t_ptc | 21772.0 | 0.268 | 0.511 | 0.734 | 1.080 | 16.543 |
| 8t_ptc_r80 | 2850.6 | 0.404 | 0.629 | 0.874 | 1.066 | 7.637 |

bootstrap 95% CI（`ci.txt`）：1t p99 `[0.230, 0.257]`；8t p99
`[2.007, 2.502]`；8t_ptc p99 `[0.705, 0.746]`。5 次中位数 + CI 是引用下限，
单次值不具可比性。

历史 RelWithDebInfo 归档 `20261028_133818_slo`（commit `87c3094`）用于看
方向，不与 Debug 绝对值直接比较：

| label | calls/s | p99/ms | p99.9/ms |
| --- | ---: | ---: | ---: |
| 1t | 10692.9 | 0.214 | 0.714 |
| 4t | 8549.6 | 6.883 | 52.559 |
| 8t | 8383.5 | 21.450 | 134.521 |
| 8t_ptc | 23237.5 | 0.696 | 0.942 |

## 4. PASS/FAIL

容量判据（按现场机器标定，不是线性扩展承诺）：

| 判据 | 2026-10-29 当前源码 | 判定 |
| --- | ---: | --- |
| 1T p99.9 ≤ 1ms | 0.697ms | PASS |
| 8T p99 ≤ 50ms | 2.037ms | PASS |
| 8T calls/s ≥ 3000 | 7372.9 | PASS |
| 8T calls/s ≥ 0.45×1T | 0.861× | PASS |

严格判据（共享单 client）：

| 判据 | 2026-10-29 当前源码 | 判定 |
| --- | ---: | --- |
| 8T p99 ≤ 3×1T p99 + RTT（≈1ms） | 2.037 > 1.729ms | **FAIL** |
| 8T calls/s ≥ 1T calls/s | 7372.9 < 8560.4 | **FAIL** |

per-thread client（同一严格判据，`--target 8t_ptc`）：

| 判据 | 2026-10-29 当前源码 | 判定 |
| --- | ---: | --- |
| 8T p99 ≤ 3×1T p99 + RTT | 0.734 ≤ 1.729ms | PASS |
| 8T calls/s ≥ 1T calls/s | 21772.0 ≥ 8560.4 | PASS |

所以正确表述是：

> 共享同一 client 的 8T 容量下限通过，严格线性 SLO 不通过；这是单会话
> 串行化的结构结果。per-thread client（每线程一个会话）在 8T 下
> p99=0.734ms、21772.0 calls/s，严格判据通过。

复跑命令：

```sh
python3 scripts/check_bench_slo.py \
  bench-results/20261029_131811_slo/median.txt --profile capacity --require-errors
python3 scripts/check_bench_slo.py \
  bench-results/20261029_131811_slo/median.txt --profile strict --require-errors
python3 scripts/check_bench_slo.py \
  bench-results/20261029_131811_slo/median.txt \
  --profile strict --target 8t_ptc --require-errors
```

## 5. 为什么共享 8T 仍然不线性

`batchRead` 的同步服务调用路径（open62541 v1.4.14）：

```
OPC_UA_Client::batchRead
  → ApiLease / InFlightGuard / isHealthyCached
  → Impl::batchRead
  → std::lock_guard<std::mutex> m_sdkMutex              ← 第 1 层（封装）
  → RealSdk::serviceRead
  → UA_Client_Service_read
  → __UA_Client_Service
  → lockClient(): eventLoop->lock + client->clientMutex ← 第 2 层（SDK）
  → __Client_Service → sendRequest → el->run(timeout)
  → server event loop（单线程）                          ← 第 3 层（server）
```

第 1 层把同一实例的同步服务调用串行化；第 2 层是 SDK 对同一 client 的锁；
第 3 层是 server 单事件循环。三层叠加，8 个线程共享一个 session 时，
`calls/s` 只能到 1T 的 0.86 倍，p99 涨到 8.4 倍。这是“共享实例”的代价，
不是锁写错。`m_sdkMutex` 是必要的：`UA_Client_*` 的同步状态机不是为并发
服务调用设计的，封装层必须自己串行化。

## 6. per-thread client 与绑核

**per-thread client 是真实的扩展路径，不是“只换尾部”的安慰剂。** 当前源码：

- 8T_ptc 21772.0 calls/s，是 8T 共享（7372.9）的 2.95 倍，是 1T 的 2.54 倍；
- p99 0.734ms，是 8T 共享（2.037ms）的 1/2.8；
- p50 0.268ms，低于 8T 共享（1.054ms）；
- 代价：8 条 session、8 个看门狗线程、server 侧会话/队列压力；写入不再有
  “单 session 内顺序”的全局保证。

### 还需要做“per-thread client 的绑核优化”吗？

**不需要把它当成代码优化项。** 当前 `8t_ptc` 只用了 `taskset -c 0-7`
（允许在 8 个逻辑核上迁移），没有做“worker i 固定 CPU i”，已经得到
p99=0.734ms、21772.0 calls/s，严格判据通过。绑核最多进一步降低调度抖动，
不能改变单 session 的串行上限；而且 8 个 worker + 8 个看门狗 + server 需要
至少 17 个核才能一对一，这台 8 vCPU 机器做不到。绑核属于 benchmark 控制
变量，不属于库实现。

如果要把 per-thread client 做成可部署模式，优先级更高的是：

1. **共享一个 watchdog**（或每个 client 只在连接不稳定时启用 watchdog），
   把 8 个额外线程降为 1 个；
2. 固定 client 数量（例如 4 个，而不是“每业务线程一个”），避免 session
   数量随线程数无限增长；
3. 用连接池 + 每个 client 的请求队列做批处理，减少 server 会话切换；
4. 若要证明线性扩展，再做“server 独立 + worker/watchdog 分核 + 固定到达率 +
   bootstrap CI”的实验设计；绑核属于实验控制，不属于库设计。

## 7. 这组数据的有效性边界

- 当前源码复跑是 **Debug 构建**（`-O0`），不能与 RelWithDebInfo/Release
  直接比绝对值；它用于验证“补丁后没有回归”和 PASS/FAIL 判据。发布证据应
  用 RelWithDebInfo 重跑同一脚本，并保留 `env.txt` 的 `bin vs src`。
- 8T/8T_ptc 都跑在 0–7，server 在 CPU7，存在过订阅；绝对值不可跨机器复用。
- 单次 p99 仍可能差一个数量级；必须报 5 次中位数 + CI。
- `8t_r80`/`8t_ptc_r80` 的到达率是历史固定值，只用于观察固定负载下的尾部，
  不作为容量门禁。
- 所有 PASS 都要同时有 `errors=0` 的原始 stdout；`median.txt` 本身不含
  errors，不能只看中位数。
- 新归档的 `median.txt` 带 `errors: 0`，`run_slo_bench.sh` 在汇总前也会
  直接拒绝 `errors!=0` 的原始 run；旧归档没有该字段时用
  `--require-errors` 会明确报错，而不是静默 PASS。

## 8. 建议的门槛

回归（每次提交/发布）：

| 项 | 阈值 | 理由 |
| --- | --- | --- |
| 1T p99.9 | ≤ 1ms | 钉死空闲轮询阻塞 |
| 1T calls/s | ≥ 5000 | 参考机基线；当前 Debug 8560.4 |
| errors | 0 | 正确性；必须从原始 run 读取 |
| 8T p99（容量） | ≤ 50ms | 单 session 部署下限 |
| 8T calls/s（容量） | ≥ 3000 且 ≥0.45×1T | 现场最低吞吐 |
| 8T p99（严格） | ≤ 3×1T p99 + RTT | 共享实例**预期 FAIL**；用于证明没有把它误报成线性 |
| 8T_ptc p99（严格） | ≤ 3×1T p99 + RTT | per-thread 模式；当前 PASS |

严格判据对共享实例是“诊断项”，不是发布门禁；把 FAIL 写进文档，是为了
避免面试时把“容量下限”吹成“线性扩展”。
