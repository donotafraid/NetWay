# Benchmark 方法与 PASS 判据

本文只回答一个问题：**当前 `bench_latency` 的数字能证明什么，不能证明什么。**
它不是“8 线程 p99 越小越好”的排行榜；共享同一 `OPC_UA_Client` 的同步服务
调用在结构上就是串行的。

## 1. 被测对象与命令

`bench/bench_latency.cpp` 默认在进程内起一个 open62541 server；SLO 归档用
`bench_server` 把 server 放到独立进程，并用 `taskset` 绑核。客户端用
`PROFILE_LOCAL`，`watchdogIntervalMs=1000`，`batch=16`。当前归档每组跑
15 次，取中位数；历史归档有 5 次，引用时必须同时看样本数。

```sh
RUNS=15 OPS=100000 BATCH=16 SRV_CPUS=7 ./scripts/run_slo_bench.sh
```

六个 label：

| label          | 线程 | client 模型       | 到达率                       |
| -------------- | ---: | ----------------- | ---------------------------- |
| `1t`         |    1 | 共享实例          | 不限                         |
| `4t`         |    4 | 共享实例          | 不限                         |
| `8t`         |    8 | 共享实例          | 不限                         |
| `8t_r80`     |    8 | 共享实例          | 4268 calls/s（历史固定负载） |
| `8t_ptc`     |    8 | 每线程一个 client | 不限                         |
| `8t_ptc_r80` |    8 | 每线程一个 client | 2864 calls/s（历史固定负载） |

`RATE_80` / `RATE_80_PTC` 不是“当前容量的 80%”，只是兼容旧归档的固定负载；
换机器/换构建后必须重新标定。

`run_slo_bench.sh` 在 `env.txt` 记录 git HEAD、`src/include` 的未提交改动、
`CMAKE_BUILD_TYPE`、编译器、首个 `-O*` 选项、源码/二进制 SHA-256、cgroup
`cpu.max`/`cpu.stat` 和 `loadavg`。脚本还会在汇总前检查每个 label 恰好有
`RUNS` 个可解析的 run；缺样本或缺少 `calls_s`/`p50`/`errors` 的归档直接
失败，不会把 14 个样本写成“median over 15 runs”。

没有这些字段的归档不能用于跨会话比较。

## 2. 证据清单（先看归档是否存在、是否已提交）

| 项 | 内容 |
| --- | --- |
| 当前归档 | `bench-results/20261030_141909_slo/` |
| 构建 | RelWithDebInfo，Clang 17，`-O2` |
| 源码/二进制 | `ua.cpp` 2026-10-30 11:09:56；`bench_latency`/`bench_server` 2026-10-30 14:18 |
| 源码哈希 | `ua.cpp` `6a769e00…`；其余见 `env.txt` |
| run 数 | 15 次 × 6 label = 90 个原始 run；全部 `errors=0` |
| 工作区 | HEAD `228df04`，`env.txt` 的 `dirty src/include` 为空；源码哈希可追溯 |
| CI | `ci.txt` 为 10k 次 bootstrap 重采样，n=15 |

历史归档：

- `bench-results/20261030_083718_slo/`：5 次 × 6 label，当前代码的上一轮
  证据（同一 `ua.cpp` 哈希，但工作区当时有未提交改动）。保留作 5 次/15 次
  方差对照。
- `bench-results/20261029_141357_slo/`：补丁前基线（`ua.cpp` 115877 字节；
  当前 117057 字节），只作历史对照，不能当当前代码的证据。

`scripts/check_bench_refs.py` 在 CI 中拒绝两类问题：文档引用了不存在的
`bench-results/<stamp>_slo`，或引用的归档没有纳入 git。归档只存在于本地
构建目录时，GitHub 上的评审者看不到数字的来源。本轮把
`20261030_141909_slo` 加入 `.gitignore` 白名单；提交时仍需显式
`git add bench-results/20261030_141909_slo`，否则 `check_bench_refs.py`
会在 CI 失败。

## 3. 当前归档的 15 次中位数（2026-10-30 14:19）

归档：`bench-results/20261030_141909_slo/median.txt`。环境：WSL2 8 vCPU
（i7-7700HQ，4C/8T）、server 绑 CPU7、Clang 17、libc++、RelWithDebInfo
（`-O2`）、batch=16、10 万样本；90 个 run 全部 `errors=0`。

| label      | calls/s | p50/ms | p90/ms | p99/ms | p99.9/ms | max/ms |
| ---------- | ------: | -----: | -----: | -----: | -------: | -----: |
| 1t         | 11497.6 |  0.081 |  0.095 |  0.173 |    0.715 |  1.300 |
| 4t         |  8662.0 |  0.111 |  0.140 |  4.166 |   57.124 | 245.400 |
| 8t         |  8376.1 |  0.115 |  0.148 |  9.308 |  164.188 | 580.165 |
| 8t_r80     |  4267.7 |  0.928 |  1.453 |  1.794 |    3.350 | 11.565 |
| 8t_ptc     | 23483.2 |  0.268 |  0.529 |  0.669 |    0.892 |  2.443 |
| 8t_ptc_r80 |  2857.1 |  0.329 |  0.509 |  0.764 |    0.933 |  1.656 |

bootstrap 95% CI（`ci.txt`，10k 次重采样，n=15）：

| label | p99 中位数 | p99 CI | p99.9 中位数 | p99.9 CI |
| --- | ---: | ---: | ---: | ---: |
| 1t | 0.173 | [0.170, 0.189] | 0.715 | [0.289, 0.763] |
| 4t | 4.166 | [3.358, 4.920] | 57.124 | [51.688, 63.646] |
| 8t | 9.308 | [7.863, 17.234] | 164.188 | [151.680, 170.311] |
| 8t_r80 | 1.794 | [1.762, 1.823] | 3.350 | [3.210, 14.108] |
| 8t_ptc | 0.669 | [0.665, 0.684] | 0.892 | [0.879, 0.986] |
| 8t_ptc_r80 | 0.764 | [0.719, 0.797] | 0.933 | [0.884, 0.972] |

**1T p99.9：这一轮可以按 15 次样本下结论。** 中位数 0.715 ms，bootstrap
95% CI 为 `[0.289, 0.763]`，上界 0.763 ms；15 个原始 run 的 p99.9 最大值为
0.773 ms。也就是说，不仅中位数低于 1 ms，所有原始 run 和 CI 上界也都低于
1 ms。`scripts/check_bench_slo.py` 的 `--ci-upper-ms 1.0 --min-runs 15`
会同时检查这两件事；这比只看 median 更接近“可审计的 PASS”。

这仍然不是“任何机器上的硬保证”：它是这台 8 vCPU WSL2 主机、这个构建、
这组 100k 样本的结论。跨机器/跨内核要重新跑。

## 4. 如何读这组数据：先看吞吐，再看 p99

15 次归档与上一份 5 次归档（`20261030_083718_slo`）的对照：

| label | 5 次 calls/s | 5 次 p99/ms | 15 次 calls/s | 15 次 p99/ms | 说明 |
| --- | ---: | ---: | ---: | ---: | --- |
| 1t | 10273.3 | 0.223 | 11497.6 | 0.173 | 15 次吞吐 +11.9%，p99 -22.4% |
| 4t |  8440.8 | 8.133 |  8662.0 | 4.166 | 饱和压测，p99 对样本窗口敏感 |
| 8t |  7982.5 | 25.740 |  8376.1 | 9.308 | 同一结构结论，绝对值不可跨轮直接比较 |
| 8t_r80 | 4267.7 | 2.205 | 4267.7 | 1.794 | 同一固定到达率，p99 -18.6% |
| 8t_ptc | 22753.8 | 0.706 | 23483.2 | 0.669 | per-thread 模式 |
| 8t_ptc_r80 | 2854.8 | 0.614 | 2857.1 | 0.764 | 同一固定到达率 |

结论不是“15 次让代码变快”，也不是“5 次不可信”：

- 两轮相隔约 6 小时，构建类型相同但宿主热节流/负载不同；1T/8T 的
  calls/s 差 10% 量级，p99 可以差数倍，这是跨会话方差。
- 8t 的无速率上限压测 p99 从 25.740 降到 9.308，同时 calls/s 只从 7982.5
  升到 8376.1；p99 由队列位置和宿主调度决定，不能把这次下降归因于某一行
  代码。
- 8t_r80 的 p99 从 2.205 降到 1.794，仍高于严格界 1.519；它的绝对值和
  1T p99 都随 15 次样本变化，不能拿单点做代码归因。
- 当前真正可下的结论是：**结构性结论没有变**——共享 8T 仍不线性；
  per-thread client 仍能过严格判据；1T p99.9 在 15 次样本下通过 CI 判据。

## 5. PASS/FAIL

容量判据（单 session 部署下限，不是线性扩展承诺）：

| 判据                   | 当前归档 | 判定 |
| ---------------------- | -------: | ---- |
| 1T p99.9 ≤ 1ms        |  0.715ms（CI 上界 0.763） | PASS（中位数 + CI 上界 + n=15） |
| 8T p99 ≤ 50ms         | 9.308ms | PASS |
| 8T calls/s ≥ 3000     | 8376.1 | PASS |
| 8T calls/s ≥ 0.45×1T | 0.728× | PASS |

`check_bench_slo.py --profile capacity --require-errors --ci-upper-ms 1.0
--min-runs 15` 输出 8/8 PASS：1T 中位数、1T CI 上界、样本数、两个 errors、
8T p99、8T 吞吐、8T/1T 比例。这个脚本现在把“1T p99.9 的 CI 上界”也变成
可执行判据，而不是只写在文档里。

严格判据（共享单 client，无速率上限）：

| 判据                               | 当前归档 | 判定 |
| ---------------------------------- | -------: | ---- |
| 8T p99 ≤ 3×1T p99 + RTT（1.519ms） | 9.308 > 1.519ms | **FAIL** |
| 8T calls/s ≥ 1T calls/s           | 8376.1 < 11497.6 | **FAIL** |

固定到达率诊断（`--profile fixed-rate`；target 只比较 p99，不比较被 rate
限制的吞吐，1T p99.9/errors 仍检查）：

| target | 到达率 | p99 | 3×1T p99 + RTT | 判定 |
| --- | ---: | ---: | ---: | --- |
| `8t_r80` | 4267.7 | 1.794ms | 1.519ms | **FAIL**（差距 0.275ms） |
| `8t_ptc_r80` | 2857.1 | 0.764ms | 1.519ms | PASS |

per-thread client（同一严格判据，`--target 8t_ptc`）：

| 判据 | 当前归档 | 判定 |
| --- | -------: | ---- |
| 8T p99 ≤ 3×1T p99 + RTT | 0.669 ≤ 1.519ms | PASS |
| 8T calls/s ≥ 1T calls/s | 23483.2 ≥ 11497.6 | PASS |

所以正确表述是：

> 共享同一 client 的 8T 容量下限通过（p99 9.308ms、8376.1 calls/s），
> 严格线性 SLO 不通过；固定 4268 calls/s 下 p99 1.794ms 仍高于严格界，
> 说明共享实例的排队开销不是只在饱和点才出现。per-thread client 在 8T 下
> p99=0.669ms、23483.2 calls/s，严格判据通过。1T p99.9 的 15 次中位数是
> 0.715ms，CI 上界 0.763ms，所有原始 run 都低于 1ms；这是一台主机上的
> 统计结论，不是跨机器硬保证。

## 6. 为什么共享 8T 仍然不线性

`batchRead` 的同步服务调用路径（open62541 v1.4.14）：

```
OPC_UA_Client::batchRead
  → ApiLease / InFlightGuard / isHealthyCached
  → Impl::batchRead
  → std::lock_guard<std::mutex> m_sdkMutex              ← 第 1 层（封装）
  → RealSdk::serviceRead
  → UA_Client_Service_read                              client.h:611-617
  → __UA_Client_Service                                 ua_client.c:668-674
  → lockClient(): client->clientMutex                   ← 第 2 层（SDK）
  → __Client_Service                                    ua_client.c:540-...
  → 未连接则 connectSync()                              ua_client.c:569-578
  → sendRequest → el->run(timeout)                      ua_client.c:624-628
  → server event loop（单线程）                          ← 第 3 层（server）
```

第 1 层把同一实例的同步服务调用串行化；第 2 层是 SDK 对同一 client 的锁；
第 3 层是 server 单事件循环。三层叠加，8 个线程共享一个 session 时，
15 次归档里 `calls/s` 只有 1T 的 `0.728` 倍，p99 是 1T 的 53.8 倍。这是
“共享实例”的代价，不是锁写错。

另一个会放大尾延迟的路径是健康探针：`doWatchdogDecision()` 每秒调用
`isConnected(0)`，后者在持有 `m_sdkMutex` 时调用 `UA_Client_getState()`；
`getState` 自己还要拿 SDK 的 `clientMutex`。如果此时有一个服务调用正持有
`clientMutex` 等 server 响应，探针会持 `m_sdkMutex` 排队，所有业务调用也
跟着排队。当前没有把这条路径单独 A/B 出来，因此它是“尾延迟可能来源”，
不是已量化的根因；要归因，应在同一构建上交替跑 `watchdogIntervalMs=1000`
与 `60000` 两组，或给空闲探针加 `try_lock` 后重测。

## 7. per-thread client 与绑核

**per-thread client 是真实的扩展路径，不是“只换尾部”的安慰剂。** 当前归档：

- 8T_ptc 23483.2 calls/s，是 8T 共享（8376.1）的 2.80 倍，是 1T 的 2.04 倍；
- p99 0.669ms，是 8T 共享（9.308ms）的约 1/14；
- p50 0.268ms，**高于** 8T 共享的 0.115ms；共享实例在低分位更快，代价集中
  在尾部（p99 9.308ms），不是 p50；
- 代价：8 条 session、8 个看门狗线程、server 侧会话/队列压力；写入不再有
  “单 session 内顺序”的全局保证。

### 还需要做“per-thread client 的绑核优化”吗？

**不需要把它当成代码优化项。** 当前 `8t_ptc` 只用了 `taskset -c 0-7`
（允许在 8 个逻辑核上迁移），没有做“worker i 固定 CPU i”，已经得到
p99=0.669ms、23483.2 calls/s，严格判据通过。绑核最多进一步降低调度抖动，
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

## 8. 这组数据的有效性边界

- 当前归档是 **RelWithDebInfo（`-O2`）**，不是 Debug。历史 Debug 数字不在
  仓库，不能引用。
- 8T/8T_ptc 都跑在 0–7，server 在 CPU7，存在过订阅；8T 的 p99 对宿主
  调度/热节流敏感，绝对值不可跨机器复用。
- 单次 p99 仍可能差一个数量级；必须报 15 次中位数 + CI。当前 1T p99.9 的
  CI 上界 0.763ms 低于 1ms，但这是本机/本样本的结论；换主机要按同一流程
  重跑。
- 15 次归档的 `1t` p99.9 原始 run 最大 0.773ms；如果未来某次 run 超过 1ms，
  `median.txt` 的 median 仍可能 PASS，因此发布时要用
  `--ci-upper-ms`（或直接看 `ci.txt`），不要只看 median。
- `8t`/`8t_ptc` 是无速率上限的饱和压测；`8t_r80`/`8t_ptc_r80` 才是固定
  到达率诊断。两者回答不同问题，不能混用判据。
- 所有 PASS 都要同时有 `errors=0` 的原始 stdout；`median.txt` 本身含
  `errors: 0`（新脚本写入），旧归档只有原始 run 可查。
- 归档必须真实存在、已纳入 git，且 `bin vs src` 通过；文档路径由
  `scripts/check_bench_refs.py` 在 CI 中检查。
- 当前归档来自干净 HEAD `228df04`；`env.txt` 的 `dirty src/include` 为空。
  发布前仍建议先 commit 再重跑，得到与发布 commit 一一对应的新时间戳归档。

## 9. 建议的门槛

回归（每次提交/发布）：

| 项 | 阈值 | 理由 |
| --- | --- | --- |
| 1T p99.9 | 中位数 ≤ 1ms 且 CI 上界 ≤ 1ms，n≥15 | 钉死空闲轮询阻塞；当前 0.715ms / CI 0.763ms |
| 1T calls/s | ≥ 5000 | 参考机基线；当前 11497.6 |
| errors | 0 | 正确性；必须从原始 run 读取 |
| 8T p99（容量） | ≤ 50ms | 单 session 部署下限；当前 9.308ms |
| 8T calls/s（容量） | ≥ 3000 且 ≥0.45×1T | 现场最低吞吐；当前 0.728× |
| 8T p99（严格） | ≤ 3×1T p99 + RTT | 共享实例**预期 FAIL**；用于证明没有把它误报成线性 |
| 8T_r80 p99（固定率） | ≤ 3×1T p99 + RTT | 固定 4268 calls/s 的排队诊断；当前 FAIL |
| 8T_ptc p99（严格） | ≤ 3×1T p99 + RTT | per-thread 模式；当前 PASS |

严格判据对共享实例是“诊断项”，不是发布门禁；把 FAIL 写进文档，是为了
避免把“容量下限”说成“线性扩展”。固定率诊断不是发布门禁，但它能在吞吐
变化时保持可比性。

## 10. 复跑命令

```sh
# 容量：1T p999 中位数 + CI 上界 + 样本数、8T p99/吞吐下限、errors
python3 scripts/check_bench_slo.py \
  bench-results/20261030_141909_slo/median.txt \
  --profile capacity --require-errors --ci-upper-ms 1.0 --min-runs 15

# 严格：共享 8T 预期 FAIL
python3 scripts/check_bench_slo.py \
  bench-results/20261030_141909_slo/median.txt --profile strict --require-errors

# 固定率诊断：只比较 p99，不比较被 rate 限制的吞吐
python3 scripts/check_bench_slo.py \
  bench-results/20261030_141909_slo/median.txt \
  --profile fixed-rate --target 8t_r80 --expected-rate 4268 --require-errors

# per-thread client 严格判据
python3 scripts/check_bench_slo.py \
  bench-results/20261030_141909_slo/median.txt \
  --profile strict --target 8t_ptc --require-errors

# per-thread 固定率诊断
python3 scripts/check_bench_slo.py \
  bench-results/20261030_141909_slo/median.txt \
  --profile fixed-rate --target 8t_ptc_r80 --expected-rate 2864 --require-errors

# 文档里的归档路径必须存在且已纳入 git
python3 scripts/check_bench_refs.py
```

## 11. 一句话结论

共享实例的**容量下限**通过（8T p99 9.308ms、8376.1 calls/s，0.728×1T）；
**严格线性**不通过；固定 4268 calls/s 下 p99 1.794ms 仍高于严格界；per-thread
client 严格判据通过。1T p99.9 的 15 次中位数 0.715ms、CI 上界 0.763ms、
所有原始 run 低于 1ms，因此这一轮可以按“PASS”引用；但它是本机统计结论，
不是跨机器保证。`8t` 的 p99 是饱和压测值，不是可跨会话复用的容量承诺。
