# OPC UA Client 封装（OPCUAPacking）

面向工业现场的多线程 OPC UA 客户端封装，锁定的底层 SDK 是 **open62541 v1.4.14**，
C++17 编译，C++ 侧通过 pImpl 隐藏全部 `UA_*` 类型。

同一 `OPC_UA_Client` 实例可以被多个业务线程共享：底层同步服务调用在封装内串行化，
连接/断开的恢复由后台看门狗统一裁决。调用方只需要关心 `Result<T, RichError>`。

## 一眼看懂的分层

```
业务线程
  │  batchRead / batchWrite / checkConnected / isHealthy / checkLifeState / ...
  ▼
OPC_UA_Client                     include/OPCUAPacking/ua.h     ← 公共头，零 open62541 泄漏
  │  ApiLease（生命周期租约）+ pImpl
  ▼
OPC_UA_Client::Impl               src/OPCUAPacking/ua.cpp       ← 连接状态机 / 看门狗 / 重试预算
  │  ISdk 抽象接缝（src/OPCUAPacking/detail/Isdk.h）
  ├── RealSdk                     detail/RealSdk.h              ← 生产：UA_Client_*
  └── MockSdk                     tests/include/MockSdk.h       ← 测试：虚拟时钟 / 可编排响应
  ▼
open62541 v1.4.14
```

## Quick Start

```cpp
#include "OPCUAPacking/ua.h"

ClientConfig cfg;
cfg.applyProfile(ClientConfig::PROFILE_LOCAL); // 本地现场预设，check() 必须通过

auto created = OPC_UA_Client::create("opc.tcp://127.0.0.1:4840", cfg, /*useDefault=*/false);
if (created.is_fail()) { /* created.get_error()->code()/what() */ }
std::unique_ptr<OPC_UA_Client> client = std::move(*created.get());

// 读：NodeId 是领域类型，不出现 UA_*
std::vector<OPC_UA_Client::ReadValue> reads{
    {{1, "TestInt", S7DataType::INT}}};
auto rr = client->batchRead(reads);
if (rr.is_success()) {
  for (const auto &item : rr.value_or({})) {
    if (item.value.has_value())
      int16_t v = std::get<int16_t>(*item.value);
  }
}

// 写：非幂等，封装层不自动重试
std::vector<OPC_UA_Client::WriteValue> writes{
    {{1, "TestInt", S7DataType::INT}, int16_t(42)}};
auto wr = client->batchWrite(writes);

client->shutdown(); // 幂等；析构也会兜底
```

## 线程模型（摘要，完整版见 [docs/thread-model.md](docs/thread-model.md)）

- 同一实例可被多线程调用；`m_sdkMutex` 把 `UA_Client_*` 同步调用串行化。
- SDK 本身没有后台线程，必须周期调用 `UA_Client_run_iterate`；封装用看门狗线程承担。
- 默认 `noReconnect = true, noNewSession = true`：看门狗负责恢复裁决；但
  open62541 的同步服务调用在发现未连接时会先 `connectSync()` 补连，因此重连
  也可能由业务线程触发，最终状态仍由看门狗收敛（见 `docs/limitations.md`）。
- `maxTotalWaitMs` 是“下一次尝试前”的预算：单次在途调用最坏 ≈ `service +
  connectSync ≈ 2×timeoutMs`，超出剩余预算要到下一个重试点才返回
  `BUDGET_EXCEEDED`；它不会抢占已经在 SDK 内的同步调用。
- `shutdown()` / `recreateGiveUpClient()` 通过 `ApiLease` + 在途计数与业务 API 互斥；
  第二个 `shutdown()` 立即返回，但析构会等待正在执行的 shutdown 完成（T150），
  以及 recreate 对旧 Impl 的收尾（T153）。
- 看门狗的空闲轮询与健康探针使用 `UA_Client_run_iterate(client, 0)`；恢复分支
  （`disconnect()`/`connect()`）才会阻塞驱动握手。这样空闲期不再每 100ms 持有
  `m_sdkMutex` 最长 50ms。2026-10-27 复跑（WSL2 8 vCPU、batch=16、10 万样本、
  5 次中位数）：1T p99.9 从修复前的 50.191ms 降到 0.514ms，1T 吞吐
  1326.9→6707.1 calls/s（旧归档 8000 ops/3 次，新数据 100000 ops/5 次，
  不同样本量与构建批次，只作台阶量级对照）。**但这不等于 8T SLO 通过**：8T p99=20.956ms、
  calls_s=3361.5（约为 1T 的 0.50）。共享同一实例的同步服务调用本来就被
  `m_sdkMutex` 与 open62541 的 clientMutex 串行化；8 线程饱和 + 主机过订阅，
  不能拿 8T p99 当线性扩展承诺。完整数据与 PASS/FAIL 判据见
  [docs/benchmark.md](docs/benchmark.md)。


## 错误模型

所有公开 API 返回 `Result<T, RichError>`；`RichError::ErrorCode` 是稳定枚举
（`NOT_CONNECTED`、`THREAD_BUSY`、`RECREATING`、`ALREADY_TERMINATED`、
`CONNECT_TIMEOUT`、`DISCONNECT_TIMEOUT`、`SERVICE_RETRY_EXHAUSTED`、
`BUDGET_EXCEEDED`、`GIVENUP_ING` 等），`what()` 只用于日志，不参与分支判断。

## 构建

```sh
# 需要 open62541 v1.4.14 已构建，且 Conan 依赖已就绪
conan install . -of conan_debug_build -s build_type=Debug --build=missing
cmake -B build-asan -DENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_COMPILER=clang-17 -DCMAKE_CXX_COMPILER=clang++-17 \
      -DCMAKE_TOOLCHAIN_FILE=conan_debug_build/conan_toolchain.cmake \
      -DOPEN62541_ROOT=/path/to/open62541-v1.4.14
cmake --build build-asan -j
```

## 测试与门禁

```sh
ctest --test-dir build-asan
./scripts/run_asan.sh    # ASan + UBSan
./scripts/run_lsan.sh    # ASan + LSan（leak-only）
./scripts/run_tsan.sh    # TSan + 并发过滤器完整性自查
```

- `tests/`：41 个文件、159 个用例；`MockSdk` 提供虚拟时钟、状态脚本、响应形状注入，
  以及空数组哨兵（`UA_EMPTY_ARRAY_SENTINEL`）、空 STRING（`length==0 && data!=NULL`）
  与 `hasValue==true` 但 `variant.type==nullptr` 三种边界注入。
- `ClientTestFixture`：每个用例起一个真实 open62541 server（端口基址按 PID 偏移，
  `ctest -j` 并行不撞端口），迭代线程用
  `UA_Server_run_iterate(server_, true)`（事件驱动，`UA_MAXTIMEOUT=500ms` 与 1s
  housekeeping 兜底唤醒），不再忙等一个 CPU 核。
- `tests/compile/EncapsulationProbe.cpp`：只带 `include/` 编译，逐个包含公共头，
  证明 `ua.h` 及其依赖不泄漏 SDK，且每个公共头自包含。
- `tests/compile/PublicHeaderLinkProbe.cpp`：只链接 `OPCUAPacking` 并调用
  `default_value_for()`，把“公共头声明的符号由本库提供”变成链接期门禁。
- `tests/compile/RuleOfFiveAsserts.cpp`：编译期钉死不可拷贝/不可移动。
- `tests/test41.cpp`：补 `default_value_for` 全类型、S7 名称表往返、
  `GOOD_CLAMPED` 严重级（读/写）、以及 `getState` 旧快照覆盖新 `stateCallback`
  的并发回归（T155~T159）。
- `tests/test38.cpp`：异常安全用例（SDK 接缝抛异常时守卫配平、写路径内嵌 NUL NodeId
  在进入 SDK 前被拒等）。
- CI（`.github/workflows/sanitizers.yml`）：`build-open62541` 预热缓存后并行跑
  ASan+UBSan / ASan+LSan / TSan / `--gtest_repeat=50 --gtest_shuffle` 抖动四个 job
  （Ubuntu 22.04、Clang 17、libc++、Conan 2）。

## 性能与耐久（benchmark）

`bench/` 下有四个可执行文件，默认自起一个 in-process open62541 server，
所以每条命令都能单机复现。它们不注册 ctest：benchmark 结果是观测值，不是门禁。
长时 RSS soak 是例外：`run_bench.sh` 会把 server 放到独立进程
（`bench_server`），这样被测进程的 `/proc/self/statm` 只包含客户端，曲线才能
作为客户端泄漏的证据。

```sh
# 一键构建并跑完延迟 / 恢复 / RSS（长时 soak 默认 24h）
OPEN62541_ROOT=/path/to/open62541-v1.4.14 ./scripts/run_bench.sh
SKIP_LONG=1 ./scripts/run_bench.sh          # 只跑延迟与恢复分布
SOAK_HOURS=0.01 ./scripts/run_bench.sh      # 36s soak 冒烟
LATENCY_REPEATS=5 ./scripts/run_bench.sh    # 延迟跑 5 次并输出中位数

# 或分别运行（构建产物在 build-*/bench-build/）
./build-bench/bench-build/bench_latency  --threads 8 --ops 8000 --batch 16 --repeats 5
./build-bench/bench-build/bench_latency  --threads 8 --ops 8000 --batch 16 --write --repeats 5
./build-bench/bench-build/bench_recovery --api-cycles 30 --restart-cycles 20
./build-bench/bench-build/bench_server   --port 4892 &   # soak 用的独立 server
./build-bench/bench-build/bench_rss --endpoint opc.tcp://127.0.0.1:4892 \
    --hours 24 --interval 60 --hz 1 > rss_24h.csv
```

- `bench_latency`：多线程共享同一 `OPC_UA_Client` 的 batchRead/batchWrite
  P50/P90/P99/P99.9/max 与吞吐；`--repeats N` 输出每次运行的结果并附一行
  中位数（单次 p99 受主机调度影响可差一个数量级，报告请引用中位数）。
- `bench_recovery`：显式 `disconnect()+connect()` 往返耗时分布，以及
  “服务端宕机 → 看门狗恢复健康”的耗时分布。
- `bench_rss`：长跑时按间隔输出 `elapsed_s,rss_kb,calls,errors` CSV，用于画 RSS 曲线。
- `bench_server`：独立进程的 server，供 soak 使用，避免 server 内存混入客户端曲线。

当前参考机（WSL2 8 vCPU、batch=16、10 万样本、5 次中位数）的判据：

| 判据 | 结果 | 判定 |
| --- | ---: | --- |
| 1T p99.9 ≤ 1ms（50ms 台阶回归） | 0.514ms | PASS |
| 8T p99 ≤ 3×1T p99 + RTT（≈1ms） | 20.956ms | **FAIL** |
| 8T calls/s ≥ 1T calls/s | 0.50× | **FAIL** |
| 所有线程 errors = 0 | 0 | PASS |

结论：**50ms 台阶修复已被证明；8T 严格 SLO 没有通过。** 8T 数字是共享
client 饱和 + server/看门狗未绑核的排队尾部；要测 8T SLO，必须固定到达率、
把 server 放到独立进程并绑核。方法与建议阈值见
[docs/benchmark.md](docs/benchmark.md)。

```sh
# 对 tmp/result.txt 直接判：strict 会 FAIL（这是正确的结果），
# capacity 是按现场容量定的下限，不是线性扩展承诺。
python3 scripts/check_bench_slo.py tmp/result.txt --profile strict
python3 scripts/check_bench_slo.py tmp/result.txt --profile capacity
```

RSS 曲线用脚本判读（errors / 时长 / 增量 / 稳态斜率四项阈值）：

```sh
python3 scripts/check_rss.py bench-results/rss_curve.csv --min-hours 23.5
```

`run_bench.sh` 的 soak 段是**前台进程**，会话/终端退出会触发它的 `trap` 把独立
server 一起杀掉，曲线只留下几十分钟；24h 长跑必须**脱离终端**（或用下面的手动
命令），并用 `check_rss.py --min-hours` 拒绝被截断的曲线。`run_bench.sh` 现在会
自动传入 `--min-hours`（默认 = `SOAK_HOURS` 的 98%），并把判据输出归档到
`bench-results/rss_check.txt`；判据失败时脚本以非零码退出。

```sh
# 手动长跑（脱离终端；产物与 run_bench.sh 的 soak 段一致）
BIN=./build-bench/bench-build
setsid nohup "$BIN/bench_server" --port 4892 \
    > bench-results/rss_server.log 2>&1 < /dev/null &
setsid nohup "$BIN/bench_rss" --endpoint opc.tcp://127.0.0.1:4892 \
    --hours 24 --interval 60 --hz 1 \
    > bench-results/rss_curve.csv 2> bench-results/rss.err < /dev/null &
# 24h 后：
python3 scripts/check_rss.py bench-results/rss_curve.csv --min-hours 23.5
```

高延迟 / 丢包 / 半开连接用 Linux `tc netem` 注入（需要 root）：

```sh
sudo ./scripts/netem_faults.sh delay 200ms          # 200ms 延迟
sudo ./scripts/netem_faults.sh loss 5%              # 5% 丢包
sudo ./scripts/netem_faults.sh jitter 50ms 20ms    # 延迟 + 抖动
sudo ./scripts/netem_faults.sh halfopen 4890        # iptables 黑洞，制造半开
sudo ./scripts/netem_faults.sh clear                # 清除（含 halfopen 注入的端口）

# 一次跑完 delay/loss/jitter/halfopen：注入 → benchmark → 归档 → 判定
sudo bash ./scripts/run_netem_matrix.sh   # 该文件已 chmod +x，也可直接 ./scripts/...

# 注入故障 → 跑 benchmark → 自动恢复
sudo ./scripts/netem_faults.sh with loss 5% -- \
     ./build-bench/bench-build/bench_latency --threads 8
```

矩阵把逐项 PASS/FAIL 归档到 `bench-results/netem/summary.txt`（已归档的一份是
50ms 延迟 / 5% 丢包 / 50ms±20ms 抖动 / 半开，全部 PASS）。200ms 单程需要把
`timeoutMs` 从 `PROFILE_LOCAL` 的 5s 调大，否则 `create()` 会以
`CONNECT_TIMEOUT` 失败；原因与应对见
[docs/limitations.md](docs/limitations.md) 第 4 节。

## 目录

```
include/OPCUAPacking/ua.h        公共 API（唯一对外入口）
include/OPCUAPacking/Types.h     ClientConfig / LifeState
src/OPCUAPacking/ua.cpp          实现
src/OPCUAPacking/detail/         ISdk / RealSdk / 状态 / 类型转换（内部）
tests/                           GoogleTest 用例与 MockSdk
bench/                           延迟 / 恢复 / RSS benchmark（不注册 ctest）
scripts/                         sanitizer、benchmark、网络故障注入脚本
docs/                            线程模型、已知限制、benchmark 判据
```

## 已知限制

见 [docs/limitations.md](docs/limitations.md)：无最大批量上限、写不自动重试、
只支持 STRING 型 NodeId、`include/PLC/*` 仍是内部转换层等。
