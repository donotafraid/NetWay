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
| `8t_r80` | 8 | 共享实例 | 4268 calls/s |
| `8t_ptc` | 8 | 每线程一个 client | 不限 |
| `8t_ptc_r80` | 8 | 每线程一个 client | 2864 calls/s |

环境：WSL2 8 vCPU（i7-7700HQ，4C/8T）、RelWithDebInfo、Clang 17、libc++、
server 绑 CPU7。8 个客户端线程占用 0–7，server 与其中一个客户端共享 CPU7；
8T 的 p99 里包含排队与被调度器踢下 CPU 的时间。`--per-thread-client` 还会
额外创建 8 个看门狗线程，实际是 17 个可运行线程抢 8 个核。

## 2. 归档数据（2026-10-28 10:12，必须带“日志污染”标签）

`bench-results/20261028_101222_slo/median.txt`：

| label | calls/s | p50/ms | p90/ms | p99/ms | p99.9/ms | max/ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1t | 6724.2 | 0.141 | 0.181 | 0.262 | 0.506 | 10.601 |
| 4t | 5330.4 | 0.180 | 0.244 | 18.360 | 66.018 | 198.578 |
| 8t | 5146.1 | 0.183 | 0.258 | 44.846 | 149.195 | 397.471 |
| 8t_r80 | 4267.7 | 1.275 | 8.629 | 44.382 | 97.375 | 154.954 |
| 8t_ptc | 3613.9 | 2.110 | 3.048 | 4.257 | 12.489 | 37.791 |
| 8t_ptc_r80 | 2857.1 | 0.636 | 1.518 | 37.679 | 86.758 | 98.541 |

`8t_ptc_r80` 的 p99 五次是 `58.434 / 69.100 / 16.390 / 12.584 / 37.679`，
bootstrap 95% CI 为 `[12.584, 69.100]`，跨 5 倍，不能作为稳定 SLO。

**这组数据不可作为发布证据：** 30 个 run 文件每个约 55MB，总计约 1.6GB。
open62541 以 `UA_LOGLEVEL=100` 编译，而 `customLog()` 原来不按 `UA_LogLevel`
过滤，每次 Read 的 trace/debug 行都写 stderr；`run_slo_bench.sh` 又把
`2>&1` 合并进结果文件。8 个客户端线程写同一个 stderr，日志锁和写 syscall
直接进入 p99，per-thread client 还额外多 8 个看门狗线程。上表的
`8t_ptc=3613.9 calls/s` 就是被日志打压后的结果，不能用来判断 per-thread
client 的扩展性。

## 3. 去日志后的本地复核（补丁后，5 次中位数，Debug 构建）

同一台机器、同一 server 端口、`batch=16`、10 万样本；client 设
`ClientConfig::logLevel = LOG_ERROR`，server 日志也只保留 WARNING 以上。
**构建是 Debug（本机 Conan 只有 Debug 配置，-O0），不能和归档的
RelWithDebInfo 直接比绝对值；下面的同构建 A/B 才是日志变量的干净对照。**
每组 5 次：

```sh
1t:      taskset -c 0     bench_latency --endpoint ... --threads 1
8t:      taskset -c 0-7   bench_latency --endpoint ... --threads 8
8t_ptc:  taskset -c 0-7   bench_latency --endpoint ... --threads 8 \
             --per-thread-client
```

| label | calls/s | p50/ms | p90/ms | p99/ms | p99.9/ms | max/ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1t | 8770.4 | 0.098 | 0.154 | 0.241 | 0.503 | 2.078 |
| 8t | 7032.4 | 1.045 | 1.452 | 3.104 | 8.616 | 21.884 |
| 8t_ptc | 21599.6 | 0.286 | 0.525 | 0.786 | 1.055 | 12.242 |

客户端 stderr 都是 65–585 字节（只有 spdlog 的 watchdog 启动行），server
stderr 191 字节。与归档数据对比：1T 吞吐 +30%，8T p99 从 44.846ms 降到
3.104ms，8T_ptc 吞吐从 3613.9 升到 21599.6（约 6 倍），p99 从 4.257ms
降到 0.786ms。**日志 I/O 是上一轮 8T 尾部的主要混杂因素。**

同构建 A/B（`--log-level 100` vs `500`，50k ops，batch 16）：

| label | log=TRACE | log=ERROR | 变化 |
| --- | ---: | ---: | --- |
| 1T calls/s | 5465.4 | 7573.3 | +38.6% |
| 1T p99 | 0.423ms | 0.288ms | −32% |
| 8T calls/s | 4657.0 | 7060.3 | +51.6% |
| 8T p99 | 7.416ms | 3.204ms | −57% |
| 8T stderr | 28.3MB | 65B | — |

这组 A/B 是同一份 Debug 二进制、同一个 server、同一个 log-level 开关，
排除了构建类型/机器差异。它直接证明：**归档里 55MB/run 的日志不是
“无关紧要的输出”，而是 8T p99 的主要混杂因素之一。**

## 4. PASS/FAIL

容量判据（按现场机器标定，不是线性扩展承诺）：

| 判据 | 归档 8t | 去日志 8t | 判定 |
| --- | ---: | ---: | --- |
| 8T p99 ≤ 50ms | 44.846ms | 3.104ms | PASS |
| 8T calls/s ≥ 3000 | 5146.1 | 7032.4 | PASS |
| 8T calls/s ≥ 0.45×1T | 0.765× | 0.802× | PASS |

严格判据（共享单 client）：

| 判据 | 归档 8t | 去日志 8t | 判定 |
| --- | ---: | ---: | --- |
| 8T p99 ≤ 3×1T p99 + RTT（≈1ms） | 44.846 > 1.786 | 3.104 > 1.723 | **FAIL** |
| 8T calls/s ≥ 1T calls/s | 0.765× | 0.802× | **FAIL** |

per-thread client（同一严格判据，`--target 8t_ptc`）：

| 判据 | 结果 | 判定 |
| --- | ---: | --- |
| 8T p99 ≤ 3×1T p99 + RTT | 0.786 ≤ 1.723ms | PASS |
| 8T calls/s ≥ 1T calls/s | 21599.6 ≥ 8770.4 | PASS |

所以正确表述是：

> 共享同一 client 的 8T 容量下限通过，严格线性 SLO 不通过；这是单会话
> 串行化的结构结果。per-thread client（每线程一个会话）在不去日志的旧数据
> 里被日志 I/O 掩盖，去掉日志后 8T p99=0.786ms、21599.6 calls/s，严格判据
> 也通过。

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
`calls/s` 只能到 1T 的 0.8 倍，p99 却涨到 12.9 倍。这是“共享实例”的代价，
不是锁写错。

## 6. per-thread client 与绑核

**per-thread client 是真实的扩展路径，不是“只换尾部”的安慰剂。** 去日志后：

- 8T_ptc 21599.6 calls/s，是 8T 共享（7032.4）的 3.07 倍，是 1T 的 2.46 倍；
- p99 0.786ms，是 8T 共享（3.104ms）的 1/4；
- p50 0.286ms，低于 8T 共享（1.045ms）；
- 代价：8 条 session、8 个看门狗线程、server 侧会话/队列压力；写入不再有
  “单 session 内顺序”的全局保证。

### 还需要做“per-thread client 的绑核优化”吗？

**不需要把每线程绑核当成代码优化项。** 当前 `8t_ptc` 只用了
`taskset -c 0-7`（允许在 8 个逻辑核上迁移），没有做“worker i 固定 CPU i”，
已经得到 p99=0.786ms、21599.6 calls/s，严格判据通过。绑核最多进一步降低
调度抖动，不能改变单 session 的串行上限；而且 8 个 worker + 8 个看门狗 +
server 需要至少 17 个核才能一对一，这台 8 vCPU 机器做不到。

如果要把 per-thread client 做成可部署模式，优先级更高的是：

1. **共享一个 watchdog**（或每个 client 只在连接不稳定时启用 watchdog），
   把 8 个额外线程降为 1 个；
2. 固定 client 数量（例如 4 个，而不是“每业务线程一个”），避免 session
   数量随线程数无限增长；
3. 用连接池 + 每个 client 的请求队列做批处理，减少 server 会话切换；
4. 若要证明线性扩展，再做“server 独立 + worker/watchdog 分核 + 固定到达率 +
   bootstrap CI”的实验设计；绑核属于实验控制，不属于库实现。

## 7. 这组数据的有效性边界

- 去日志复核是**本地 Debug 构建**的 5 次中位数；源目录只读，未写入
  `bench-results/`。Debug 不能代表 RelWithDebInfo 的绝对性能；同构建
  A/B（log=100/500）只用于证明日志变量的方向。要作为发布证据，应用补丁
  后用 RelWithDebInfo 在项目内重跑并归档为
  `bench-results/20261028_clean_logfix/`。
- 8T/8T_ptc 都跑在 0–7，server 在 CPU7，存在过订阅；绝对值不可跨机器复用。
- 单次 p99 仍可能差一个数量级；必须报 5 次中位数 + CI，不能引用单次。
- `8t_r80`/`8t_ptc_r80` 的到达率是基于旧容量的硬编码（4268/2864），
  只用于观察“80% 附近”的尾部，不作为门禁。换机器必须重新标定。
- 所有 PASS 都要同时有 `errors=0` 的原始 stdout；`median.txt` 本身不含
  errors，不能只看中位数。

## 8. 建议的门槛

回归（每次提交/发布）：

| 项 | 阈值 | 理由 |
| --- | --- | --- |
| 1T p99.9 | ≤ 1ms | 钉死 50ms 轮询台阶 |
| 1T calls/s | ≥ 5000 | 参考机基线；去日志后 8770 |
| errors | 0 | 正确性 |
| 1T max | 仅记录 | 主机调度噪声 |

容量（按现场机器标定）：

| 项 | 示例 | 说明 |
| --- | ---: | --- |
| 8T calls/s | ≥ 3000 | 共享 client 部署下限 |
| 8T p99 | ≤ 50ms | 排队 SLO，不是 RPC 时间 |
| 8T_ptc p99 | ≤ 2ms | per-thread 模式建议门槛；当前 0.786ms |
| 8T p99.9 | 仅记录 | 样本 100 个，仅作趋势 |

## 9. 一句话结论

> 共享单 client 的 8T 容量下限已经过线，严格线性 SLO 没有通过；per-thread
> client（每线程一个 session）在去日志后 p99=0.786ms、21599.6 calls/s，
> 严格判据通过。不需要再为 per-thread client 做每线程绑核的代码优化；先
> 解决“每个 client 一个看门狗”的线程成本，再谈大规模部署。
