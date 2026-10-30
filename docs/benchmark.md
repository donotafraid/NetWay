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
`cpu.max`/`cpu.stat` 和 `loadavg`。没有这些字段的归档不能用于跨会话比较。

## 2. 证据清单（先看归档是否存在、是否已提交）

| 项 | 内容 |
| --- | --- |
| 当前归档 | `bench-results/20261030_083718_slo/` |
| 构建 | RelWithDebInfo，Clang 17，`-O2` |
| 源码/二进制 | `ua.cpp` 2026-10-29 20:56:57；`bench_latency`/`bench_server` 2026-10-30 08:36:57 |
| 源码哈希 | `ua.cpp` `6a769e00…`；`ValueTypes.h`/`ua.h` 等见 `env.txt` |
| run 数 | 5 次 × 6 label = 30 个原始 run；全部 `errors=0` |
| 工作区 | HEAD `4627583` + 已应用补丁；`env.txt` 记录了 `git status`、`dirty src/include` 和源码哈希。归档可追溯到源码哈希；发布证据前建议 commit 后重跑（见 §8） |

上一轮归档 `bench-results/20261029_141357_slo/` 只作**基线对照**：它的
`ua.cpp` 是补丁前版本（115877 字节；当前为 117057 字节），所以不能当作
当前代码的证据。更早的 `20261029_131811_slo`、`20261028_*_slo` 不在当前
checkout，不再引用。

`scripts/check_bench_refs.py` 在 CI 中拒绝两类问题：文档引用了不存在的
`bench-results/<stamp>_slo`，或引用的归档没有纳入 git。归档只存在于本地
构建目录时，GitHub 上的评审者看不到数字的来源。

## 3. 当前归档的 5 次中位数（2026-10-30 08:37）

归档：`bench-results/20261030_083718_slo/median.txt`。环境：WSL2 8 vCPU
（i7-7700HQ，4C/8T）、server 绑 CPU7、Clang 17、libc++、RelWithDebInfo
（`-O2`）、batch=16、10 万样本；30 个 run 全部 `errors=0`。

| label      | calls/s | p50/ms | p90/ms | p99/ms | p99.9/ms | max/ms |
| ---------- | ------: | -----: | -----: | -----: | -------: | -----: |
| 1t         | 10273.3 |  0.083 |  0.141 |  0.223 |    0.641 | 10.864 |
| 4t         |  8440.8 |  0.113 |  0.160 |  8.133 |   43.998 | 151.925 |
| 8t         |  7982.5 |  0.116 |  0.168 | 25.740 |  119.785 | 332.661 |
| 8t_r80     |  4267.7 |  0.921 |  1.506 |  2.205 |   26.928 | 59.846 |
| 8t_ptc     | 22753.8 |  0.275 |  0.540 |  0.706 |    0.944 |  1.879 |
| 8t_ptc_r80 |  2854.8 |  0.303 |  0.461 |  0.614 |    0.922 |  3.588 |

bootstrap 95% CI（`ci.txt`，10k 次重采样）：

| label | p99 中位数 | p99 CI | p99.9 中位数 | p99.9 CI |
| --- | ---: | ---: | ---: | ---: |
| 1t | 0.223 | [0.208, 0.301] | 0.641 | [0.296, 1.546] |
| 4t | 8.133 | [6.949, 11.304] | 43.998 | [40.355, 51.200] |
| 8t | 25.740 | [24.302, 27.152] | 119.785 | [97.029, 126.911] |
| 8t_r80 | 2.205 | [1.900, 24.274] | 26.928 | [5.241, 89.539] |
| 8t_ptc | 0.706 | [0.653, 0.744] | 0.944 | [0.833, 1.040] |
| 8t_ptc_r80 | 0.614 | [0.595, 0.746] | 0.922 | [0.860, 1.037] |

**1T p99.9 的 CI 必须被看见。** 中位数 0.641ms 低于 1ms，但 CI 上界
1.546ms；原始 5 次里有 2 次超过 1ms（1.546、1.039）。因此“1T p99.9 ≤ 1ms”
在当前主机/当前样本量下只能算**中位数判据通过**，不能宣称硬保证。要把它
变成发布门禁，至少 `RUNS=15` 并同时报告 CI 上界；或在更安静的主机上重跑。
这不是代码回归：同一轮里 8T 的中位数与上一轮只差 2.5%，1T 的差异属于宿主
调度噪声。

## 4. 如何读这组数据：先看吞吐，再看 p99

上一轮（`20261029_141357_slo`，补丁前代码）与当前归档的对照：

| label | 上一轮 calls/s | 上一轮 p99/ms | 当前 calls/s | 当前 p99/ms | 说明 |
| --- | ---: | ---: | ---: | ---: | --- |
| 1t | 11077.2 | 0.194 | 10273.3 | 0.223 | 当前慢 7.3%；1T p99.9 CI 也变宽 |
| 4t | 8520.9 | 8.254 | 8440.8 | 8.133 | 吞吐 -0.9%，p99 -1.5% |
| 8t | 8039.5 | 24.179 | 7982.5 | 25.740 | 吞吐 -0.7%，p99 +6.5% |
| 8t_r80 | 4267.7 | 1.839 | 4267.7 | 2.205 | 同固定负载，p99 +20% |
| 8t_ptc | 23009.1 | 0.672 | 22753.8 | 0.706 | 无共享锁，吞吐 -1.1% |
| 8t_ptc_r80 | 2850.6 | 0.815 | 2854.8 | 0.614 | 同固定负载，p99 反而下降 |

结论不是“补丁让代码变慢”，也不是“补丁让代码变快”：

- 两轮相隔约 18 小时，构建类型相同，但宿主负载/热节流不同；1T/8T 的差异
  在 10% 量级，属于跨会话方差。
- 8t_r80 的 p99 从 1.839 涨到 2.205，但 CI 是 `[1.900, 24.274]`，说明这条
  固定率曲线本身对宿主抖动很敏感；不能拿单次 2.205 与 1.839 做代码归因。
- 8t 的 p99 从 24.179 涨到 25.740，吞吐反而略降；这正是“饱和压测”的
  特征：p99 由队列位置决定，不由平均服务时间决定。
- 当前真正可下的结论是：**补丁没有改变结构性结论**——共享 8T 仍不线性，
  per-thread 仍能过严格判据。数字要引用当前归档，且必须带 CI。

## 5. PASS/FAIL

容量判据（单 session 部署下限，不是线性扩展承诺）：

| 判据                   | 当前归档 | 判定 |
| ---------------------- | -------: | ---- |
| 1T p99.9 ≤ 1ms        |  0.641ms（CI 上界 1.546） | 中位数 PASS，CI 不保证 |
| 8T p99 ≤ 50ms         | 25.740ms | PASS |
| 8T calls/s ≥ 3000     |   7982.5 | PASS |
| 8T calls/s ≥ 0.45×1T |   0.777× | PASS |

`check_bench_slo.py --profile capacity` 仍输出 6/6 PASS；它的 1T 判据使用
中位数，不读 CI。引用时必须补一句“1T p99.9 的 CI 上界超过 1ms”，否则是把
统计不确定度藏起来。

严格判据（共享单 client，无速率上限）：

| 判据                               | 当前归档 | 判定 |
| ---------------------------------- | -------: | ---- |
| 8T p99 ≤ 3×1T p99 + RTT（≈1.669ms） | 25.740 > 1.669ms | **FAIL** |
| 8T calls/s ≥ 1T calls/s           | 7982.5 < 10273.3 | **FAIL** |

固定到达率诊断（`--profile fixed-rate`；target 只比较 p99，不比较被 rate
限制的吞吐，1T p99.9/errors 仍检查）：

| target | 到达率 | p99 | 3×1T p99 + RTT | 判定 |
| --- | ---: | ---: | ---: | --- |
| `8t_r80` | 4267.7 | 2.205ms | 1.669ms | **FAIL**（差距 0.536ms） |
| `8t_ptc_r80` | 2854.8 | 0.614ms | 1.669ms | PASS |

per-thread client（同一严格判据，`--target 8t_ptc`）：

| 判据 | 当前归档 | 判定 |
| --- | -------: | ---- |
| 8T p99 ≤ 3×1T p99 + RTT | 0.706 ≤ 1.669ms | PASS |
| 8T calls/s ≥ 1T calls/s | 22753.8 ≥ 10273.3 | PASS |

所以正确表述是：

> 共享同一 client 的 8T 容量下限通过（p99 25.740ms、7982.5 calls/s），
> 严格线性 SLO 不通过；固定 4268 calls/s 下 p99 2.205ms 仍高于严格界，
> 说明共享实例的排队开销不是只在饱和点才出现。per-thread client 在 8T 下
> p99=0.706ms、22753.8 calls/s，严格判据通过。1T p99.9 的中位数是
> 0.641ms，但 CI 上界 1.546ms，不能把它说成 1ms 硬保证。

## 6. 为什么共享 8T 仍然不线性

`batchRead` 的同步服务调用路径（open62541 v1.4.14）：

```
OPC_UA_Client::batchRead
  → ApiLease / InFlightGuard / isHealthyCached
  → Impl::batchRead
  → std::lock_guard<std::mutex> m_sdkMutex              ← 第 1 层（封装）
  → RealSdk::serviceRead
  → UA_Client_Service_read
  → __UA_Client_Service
  → lockClient(): client->clientMutex                   ← 第 2 层（SDK）
  → __Client_Service → sendRequest → el->run(timeout)
  → server event loop（单线程）                          ← 第 3 层（server）
```

第 1 层把同一实例的同步服务调用串行化；第 2 层是 SDK 对同一 client 的锁；
第 3 层是 server 单事件循环。三层叠加，8 个线程共享一个 session 时，
`calls/s` 只能到 1T 的 0.777 倍，p99 涨到 115 倍。这是“共享实例”的代价，
不是锁写错。

另一个会放大尾延迟的路径是健康探针：`doWatchdogDecision()` 每秒调用
`isConnected(0)`，后者在持有 `m_sdkMutex` 时调用 `UA_Client_getState()`；
`getState` 自己还要拿 SDK 的 `clientMutex`。如果此时有一个服务调用正持有
`clientMutex` 等 server 响应，探针会持 `m_sdkMutex` 排队，所有业务调用也
跟着排队。当前没有把这条路径单独 A/B 出来，因此它是“尾延迟可能来源”，
不是已量化的根因；要归因，应在同一构建上交替跑 `watchdogIntervalMs=1000`
与 `60000` 两组，或给空闲探针加 `try_lock` 后重测。

## 7. per-thread client 与绑核

**per-thread client 是真实的扩展路径，不是“只换尾部”的安慰剂。** 当前归档：

- 8T_ptc 22753.8 calls/s，是 8T 共享（7982.5）的 2.85 倍，是 1T 的 2.21 倍；
- p99 0.706ms，是 8T 共享（25.740ms）的 1/36；
- p50 0.275ms，**高于** 8T 共享的 0.116ms；共享实例在低分位更快，代价集中
  在尾部（p99 25.740ms），不是 p50；
- 代价：8 条 session、8 个看门狗线程、server 侧会话/队列压力；写入不再有
  “单 session 内顺序”的全局保证。

### 还需要做“per-thread client 的绑核优化”吗？

**不需要把它当成代码优化项。** 当前 `8t_ptc` 只用了 `taskset -c 0-7`
（允许在 8 个逻辑核上迁移），没有做“worker i 固定 CPU i”，已经得到
p99=0.706ms、22753.8 calls/s，严格判据通过。绑核最多进一步降低调度抖动，
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

- 当前归档是 **RelWithDebInfo（`-O2`）**，不是 Debug。上一轮 141357 也是
  RelWithDebInfo；上一会话的 Debug 数字（包括 2.037ms）不在仓库，不能引用。
- 8T/8T_ptc 都跑在 0–7，server 在 CPU7，存在过订阅；8T 的 p99 对宿主
  调度/热节流敏感，绝对值不可跨机器复用。
- 单次 p99 仍可能差一个数量级；必须报 5 次中位数 + CI。当前 1T p99.9 的 CI
  上界 1.546ms，就是“中位数通过不等于硬保证”的实例。
- `8t`/`8t_ptc` 是无速率上限的饱和压测；`8t_r80`/`8t_ptc_r80` 才是固定
  到达率诊断。两者回答不同问题，不能混用判据。
- 所有 PASS 都要同时有 `errors=0` 的原始 stdout；`median.txt` 本身含
  `errors: 0`（新脚本写入），旧归档只有原始 run 可查。
- 归档必须真实存在、已纳入 git，且 `bin vs src` 通过；文档路径由
  `scripts/check_bench_refs.py` 在 CI 中检查。
- 当前归档来自 dirty tree。发布证据前先 commit，用同一脚本重跑，得到
  `git status` 干净、HEAD 可追溯的新时间戳归档。

## 9. 建议的门槛

回归（每次提交/发布）：

| 项 | 阈值 | 理由 |
| --- | --- | --- |
| 1T p99.9 | 中位数 ≤ 1ms 且 CI 上界 ≤ 1.5ms | 钉死空闲轮询阻塞；当前 0.641ms / CI 1.546ms |
| 1T calls/s | ≥ 5000 | 参考机基线；当前 10273.3 |
| errors | 0 | 正确性；必须从原始 run 读取 |
| 8T p99（容量） | ≤ 50ms | 单 session 部署下限；当前 25.740ms |
| 8T calls/s（容量） | ≥ 3000 且 ≥0.45×1T | 现场最低吞吐；当前 0.777× |
| 8T p99（严格） | ≤ 3×1T p99 + RTT | 共享实例**预期 FAIL**；用于证明没有把它误报成线性 |
| 8T_r80 p99（固定率） | ≤ 3×1T p99 + RTT | 固定 4268 calls/s 的排队诊断；当前 FAIL |
| 8T_ptc p99（严格） | ≤ 3×1T p99 + RTT | per-thread 模式；当前 PASS |

严格判据对共享实例是“诊断项”，不是发布门禁；把 FAIL 写进文档，是为了
避免把“容量下限”说成“线性扩展”。固定率诊断不是发布门禁，但它能在吞吐
变化时保持可比性。

## 10. 复跑命令

```sh
# 容量：只门禁 1T p99.9、8T p99/吞吐下限
python3 scripts/check_bench_slo.py \
  bench-results/20261030_083718_slo/median.txt --profile capacity --require-errors

# 严格：共享 8T 预期 FAIL
python3 scripts/check_bench_slo.py \
  bench-results/20261030_083718_slo/median.txt --profile strict --require-errors

# 固定率诊断：只比较 p99，不比较被 rate 限制的吞吐
python3 scripts/check_bench_slo.py \
  bench-results/20261030_083718_slo/median.txt \
  --profile fixed-rate --target 8t_r80 --expected-rate 4268 --require-errors

# per-thread client 严格判据
python3 scripts/check_bench_slo.py \
  bench-results/20261030_083718_slo/median.txt \
  --profile strict --target 8t_ptc --require-errors

# per-thread 固定率诊断
python3 scripts/check_bench_slo.py \
  bench-results/20261030_083718_slo/median.txt \
  --profile fixed-rate --target 8t_ptc_r80 --expected-rate 2864 --require-errors

# 文档里的归档路径必须存在且已纳入 git
python3 scripts/check_bench_refs.py
```

## 11. 一句话结论

共享实例的**容量下限**通过；**严格线性**不通过；固定 4268 calls/s 下
p99 2.205ms 仍高于严格界；per-thread client 严格判据通过。`8t` 的 p99
是饱和压测值，不是可跨会话复用的容量承诺；1T p99.9 的中位数通过但 CI 上界
超过 1ms，引用时必须同时给出。
