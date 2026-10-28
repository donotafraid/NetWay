# Benchmark 方法与 PASS 判据

本文只回答一个问题：**当前 `bench_latency` 的数字能证明什么，不能证明什么。**
它不是“8 线程 p99 越小越好”的排行榜；共享同一 `OPC_UA_Client` 的同步服务
调用在结构上就是串行的。

## 1. 被测对象与命令

`bench/bench_latency.cpp` 默认在进程内起一个 open62541 server，暴露一个
`ns=1` 的 INT16 节点；客户端用 `PROFILE_LOCAL`，`watchdogIntervalMs=1000`，
`batch=16`，每个线程调用 `batchRead`/`batchWrite`。

2026-10-27 复跑（`tmp/result.txt`）：

```sh
taskset -c 0     bench_latency --threads 1 --ops 100000 --batch 16 --repeats 1
taskset -c 0,2,4,6 bench_latency --threads 4 --ops 100000 --batch 16 --repeats 1
taskset -c 0-7   bench_latency --threads 8 --ops 100000 --batch 16 --repeats 1
# 每组 5 次，取中位数
```

环境：WSL2、8 vCPU、RelWithDebInfo、Clang 17、libc++。**server 线程和看门狗
线程没有绑核**；这是下文 8T 尾延迟的主要混杂因素之一。

## 2. 原始结果（5 次中位数）

| 线程 | calls/s | p50/ms | p90/ms | p99/ms | p99.9/ms | max/ms | mean/ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 6707.1 | 0.131 | 0.195 | 0.326 | 0.514 | 12.265 | 0.149 |
| 4 | 3428.5 | 0.278 | 0.348 | 9.832 | 182.783 | 610.884 | 1.092 |
| 8 | 3361.5 | 0.282 | 0.357 | 20.956 | 436.297 | 1604.378 | 2.253 |

单次 8T p99 的 5 个值是 `25.468 / 0.894 / 20.956 / 2.773 / 28.575` ms。
同代码、同机器能差 30 倍，说明单次 p99 不能当 SLO；这也是 `--repeats` 输出
中位数的原因。

## 3. 这组数据证明了什么

### 3.1 证明了：50ms 空闲轮询台阶消失

修复前（`bench-results/20261024_clean`，修复前归档；与 2026-10-27 的
`ops=100000` 不同，样本量与构建批次不同，只作台阶量级对照）：

```
1T p99=0.869ms  p99.9=50.191ms  max=99.791ms
8T p99=147.505ms p99.9=412.307ms
```

`p99.9` 正好卡在 50ms，是看门狗每 100ms 在持有 `m_sdkMutex` 时调用
`UA_Client_run_iterate(client, 50)` 的确定性证据。修复为空闲轮询/健康探针
`timeout=0` 后，1T p99.9 降到 0.514ms，1T 吞吐从 1326.9 提升到 6707.1
calls/s。这条回归由 T152/T154 在契约层钉住，不依赖机器速度。

### 3.2 没有证明：8T 的 p99/吞吐达到线性扩展

8T 的 `calls_s=3361.5` 只有 1T 的 **0.50 倍**，p99 是 1T 的 **64 倍**。
如果沿用上一轮的严格判据：

| 严格判据 | 结果 | 判定 |
| --- | ---: | --- |
| 1T p99.9 ≤ 1ms | 0.514ms | **PASS** |
| 8T p99 ≤ 3×1T p99 + RTT（≈1ms） | 20.956ms | **FAIL** |
| 8T calls/s ≥ 1T calls/s | 0.50× | **FAIL** |
| errors = 0 | 0 | **PASS** |

所以正确表述是：**“50ms 台阶已修复；8T 严格 SLO 未通过。”**
不能把“errors=0”或“中位数看起来还行”当成 8T PASS。

## 4. 为什么 8T 会这样：三层串行化 + 过订阅

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

- 第 1 层是封装自己的串行化；第 2 层是 SDK 对同一 client 的锁；第 3 层是
  in-process server 的单事件循环。三层叠加后，8 个线程不可能线性扩展。
- 8T 跑在 `0-7`，8 个客户端线程已占满 8 个 vCPU；server 线程和看门狗线程
  没有绑核，实际是 10 个可运行线程抢 8 个核。p99 里包含**排队 + 被调度器
  踢下 CPU**的时间，不是单次 RPC 的服务时间。
- 4T 跑在 `0,2,4,6`，6 个线程抢 8 核，p99 仍到 9.8ms，说明即使不过订阅，
  单 client 串行 + server 单线程也已经接近饱和；4T 吞吐 3428 只有 1T 的 0.51。

因此，8T p99 是“饱和负载下的排队尾部”，不是“客户端实现好坏”的直接指标。
要测客户端 SLO，必须控制到达率，而不是让 8 个线程无限压。

## 5. 建议的两套判据

### 5.1 回归判据（每次提交/发布必须绿）

| 项 | 阈值 | 理由 |
| --- | --- | --- |
| 1T p99.9 | ≤ 1ms | 钉死 50ms 轮询台阶；当前 0.514ms |
| 1T calls/s | ≥ 5000 | 参考机基线；当前 6707 |
| 所有线程 errors | 0 | 正确性 |
| 1T max | 仅记录，不门禁 | 主机调度噪声；当前 12ms |

### 5.2 容量判据（按现场机器定，不与 1T 做线性比较）

| 项 | 示例阈值 | 当前 | 说明 |
| --- | ---: | ---: | --- |
| 8T calls/s | ≥ 3000 | 3361.5 | 部署容量下限 |
| 8T p99 | ≤ 50ms | 20.956 | 排队 SLO，不是 RPC 时间 |
| 8T p99.9 | 仅记录 | 436ms | 样本 100 个，仅作趋势 |

容量判据必须写清“参考机、绑核、batch、ops、server 是否独立进程”。换机器
就重新标定；不要把某台 WSL2 的 3000 当成工业现场承诺。

## 6. 要得到可辩护的 8T 数字，下一步怎么做

1. **把 server 放到独立进程并绑核**（`bench_server` + `--endpoint`），
   server 固定一个核，客户端 8 线程固定另外 8 个核（或 4+4），看门狗固定
   一个核；避免 10 个线程抢 8 个核。
2. **加固定到达率模式**：`bench_latency --rate R`，每线程按目标总速率节流，
   测“80% 容量下”的 p99；全饱和只用于找容量，不用于 SLO。
3. **per-thread client 基线**：同一台机器上每个线程一个 `OPC_UA_Client`，
   用来证明“8T 线性扩展是 server/网络上限，不是封装锁导致”；共享实例的
   0.5× 是设计代价，不是 bug。
4. **报告置信区间**：5 次中位数 + bootstrap 95% CI；p99.9 至少 10 万样本。
5. **归档原始文件**：把 `tmp/result.txt` 移到
   `bench-results/20261027_slo/`，并在目录内写 `env.txt`（CPU 频率、绑核、
   build type、git SHA），否则以后无法复现。`20261024_clean` 标注为
   `pre-fix`。

## 7. 一句话结论

> 1T 的 50ms 台阶已经用数据证明修复；8T 的严格线性/p99 判据**没有通过**。
> 现有 8T 数字是共享 client 饱和 + 主机过订阅的排队结果，应改用固定到达率
> 和独立 server 进程来测；不要把 errors=0 当作 SLO PASS。
