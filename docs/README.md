# OPC UA Client 封装（OPCUAPacking）

面向工业现场的多线程 OPC UA 客户端封装，锁定的底层 SDK 是 **open62541
v1.4.14**，C++17 编译，C++ 侧通过 pImpl 隐藏全部 `UA_*` 类型。目标不是
“把 C API 包一层”，而是把**连接生命周期、并发调用、资源出口、错误码**
做成可测试、可解释的契约。

同一 `OPC_UA_Client` 实例可以被多个业务线程共享：底层同步服务调用在封装内
串行化，连接/断开的恢复由后台看门狗统一裁决。调用方只需要关心
`Result<T, RichError>`。公开入口全部 `noexcept`。

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

三个关键事实（详见 [线程模型](thread-model.md)）：

1. open62541 客户端没有自己的线程，必须周期调用
   `UA_Client_run_iterate`（`include/open62541/client.h:46-50`）。
2. 同步服务调用发现未连接时会先 `connectSync()` 补连
   （`src/client/ua_client.c:569-578`）；`noReconnect` 不阻止它。
3. 三元组（status/channel/session）已打包成单个 64 位原子快照；读者不会
   看到过渡组合。

## Quick Start

```cpp
#include "OPCUAPacking/ua.h"

ClientConfig cfg;
cfg.applyProfile(ClientConfig::PROFILE_LOCAL); // 本地现场预设，check() 必须通过

auto created = OPC_UA_Client::create("opc.tcp://127.0.0.1:4840", cfg,
                                     /*useDefault=*/false);
if (created.is_fail()) {
  // created.get_error()->code()/what()
  return 1;
}
std::unique_ptr<OPC_UA_Client> client = std::move(*created.get());

// 读：STRING NodeId（ns=1;s=TestInt）
std::vector<OPC_UA_Client::ReadValue> reads{
    {{1, "TestInt", S7DataType::INT}}};
auto rr = client->batchRead(reads);
if (rr.is_success()) {
  for (const auto &item : rr.value_or({})) {
    if (item.value.has_value())
      int16_t v = std::get<int16_t>(*item.value);
  }
}

// 读：NUMERIC NodeId（ns=1;i=20001）
std::vector<OPC_UA_Client::ReadValue> numericReads{
    {{1, uint32_t{20001}, S7DataType::INT}}};
auto nr = client->batchRead(numericReads);

// 写：非幂等，封装层不自动重试
std::vector<OPC_UA_Client::WriteValue> writes{
    {{1, uint32_t{20001}, S7DataType::INT}, int16_t(42)}};
auto wr = client->batchWrite(writes);

client->shutdown(); // 幂等；析构也会兜底
```

`NodeId.id` 是 `std::variant<std::string, uint32_t>`：`std::string` → `s=`，
`uint32_t` → `i=`。GUID/ByteString 不支持，见 [已知限制](limitations.md)。

## 公开契约（摘要）

| 入口 | 契约 |
| --- | --- |
| `create` / `createWithSdk` | 返回即已连接；失败返回 `Result`，不抛 |
| `batchRead` | 可重试码指数退避；`maxTotalWaitMs` 是软预算；逐项 `UA_StatusCode_isGood` |
| `batchWrite` | 只尝试一次；逐项状态原样返回；写非幂等 |
| `connect` / `disconnect` | `ConnState` CAS 单飞；失败出口 `TurnToRecovring()` |
| `checkConnected` | 主动驱动一次 event loop（`timeout=0`）；低频调用 |
| `isHealthy` | 只读缓存，不碰 SDK；高频健康检查用它 |
| `checkLifeState` | `RUNNING` / `RECOVERING` / `GIVEN_UP` |
| `setGiveUpSignal` | 主动进入 `GIVEN_UP`；重复调用返回 `GIVENUP_ING` |
| `recreateGiveUpClient` | 仅 `GIVEN_UP` 可调用；等 in-flight 归零（2s 上限） |
| `shutdown` | 幂等；第二个调用者立即成功，不等待 drain |

`RichError::ErrorCode` 是稳定枚举；`what()` / `rawStatus` 只用于日志，不能用
于分支判断。所有 public 入口 `noexcept`，由 `tests/test43.cpp` 的
`static_assert` 固定。

## 线程模型（摘要，完整版见 [线程模型](thread-model.md)）

- `m_sdkMutex` 把 `UA_Client_*` 同步调用串行化；共享实例的吞吐不会随线程数
  线性扩展，这是单 session 的结构结果。
- 看门狗空闲轮询与健康探针用 `UA_Client_run_iterate(client, 0)`；只有
  `disconnect()`/`connect()` 的恢复分支允许阻塞。
- `maxTotalWaitMs` 是“下一次尝试前”的预算：单次在途调用最坏 ≈
  `service + connectSync ≈ 2×timeoutMs`，超出剩余预算要到下一个重试点才返回
  `BUDGET_EXCEEDED`；它不会抢占已经在 SDK 内的同步调用。
- `shutdown()` / `recreateGiveUpClient()` 通过 `ApiLease` + 在途计数与业务 API
  互斥；第二次 `shutdown()` 立即返回，但析构会等待赢家退出
  `shutdownInternal()`（T150）和 recreate 的旧 Impl 收尾（T153）。
- 三元组已打包成 `std::atomic<uint64_t>`：`[63:48] version、[47:16] status、
  [15:8] channel、[7:0] session`。`stateCallback` 用 CAS 写，
  `isConnected()` 的 `getState()` 结果只在快照未变时发布（T159）。
- 已知窗口：`connectionLost` 与 `TurnToRunning()` 的清除竞争、16 位 version
  回绕；见 [已知限制](limitations.md) 第 2 节。

## 日志

`ClientConfig::logLevel` 是 open62541 `UA_LogLevel` 数值（TRACE=100 …
FATAL=600），默认 `LOG_WARNING`。SDK 以 `UA_LOGLEVEL=100` 编译；如果不在
`customLog()` 里过滤，每次 batchRead 都会把 TCP/解码的 trace 行写到 stderr，
既污染 benchmark 也拖慢现场。排查时显式调到 `LOG_INFO`/`LOG_DEBUG`，不要在
产线默认打开。`bench/` 固定用 `LOG_ERROR`。

## 构建

从干净 checkout 开始，推荐直接跑一键脚本（会补齐工具链、open62541 和
Conan 依赖）：

```sh
./scripts/quickstart.sh
```

已有工具链与依赖树时，也可以复用它们（不联网）：

```sh
OPEN62541_ROOT=/path/to/open62541-v1.4.14 \
CONAN_DIR=/path/to/conan_build \
BUILD_DIR=build ./scripts/quickstart.sh
```

手工等价步骤：

```sh
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

- `tests/`：45 个文件、174 个用例（173 pass + 1 真实设备 skip），加 2 个
  编译/链接探针共 176 个 ctest。
  `MockSdk` 提供虚拟时钟、状态脚本、响应形状注入，以及空数组哨兵
  （`UA_EMPTY_ARRAY_SENTINEL`）、空 STRING（`length==0 && data!=NULL`）与
  `hasValue==true` 但 `variant.type==nullptr` 三种边界注入。
- `ClientTestFixture`：每个用例起一个真实 open62541 server（端口基址按 PID
  偏移，`ctest -j` 并行不撞端口），迭代线程用
  `UA_Server_run_iterate(server_, true)`（事件驱动，`UA_MAXTIMEOUT=500ms` 与
  1s housekeeping 兜底唤醒），不再忙等一个 CPU 核。fixture 同时提供
  STRING `ns=1;s=TestInt` 与 NUMERIC `ns=1;i=20001` 两个 INT16 变量。
- `tests/compile/EncapsulationProbe.cpp`：只带 `include/` 编译，逐个包含公共
  头，证明 `ua.h` 及其依赖不泄漏 SDK，且每个公共头自包含。
- `tests/compile/PublicHeaderLinkProbe.cpp`：只链接 `OPCUAPacking` 并调用
  `default_value_for()`，把“公共头声明的符号由本库提供”变成链接期门禁。
- `tests/compile/RuleOfFiveAsserts.cpp`：编译期钉死不可拷贝/不可移动。
- `tests/test41.cpp`：`default_value_for` 全类型、S7 名称表往返、
  `GOOD_CLAMPED` 严重级、状态快照并发回归（T155~T159）。
- `tests/test42.cpp`：`ClientConfig::logLevel` 到达 SDK logger、空 STRING 写入、
  `logLevel` 边界（T160~T162）。
- `tests/test43.cpp`：数字 NodeId 的 Mock/真实 server 往返（T163~T166）、
  SDK 抛异常时 public API 不穿透 + CAS/recreate 回滚（T167）、
  `noexcept` 编译期契约（T168）。
- `tests/test44.cpp`：recreate 与析构竞态不泄漏候选 Impl（T169）、
  NodeId 缺省 dataType 不得静默为 BOOL（T170）、`Result::map` 四个重载
  （T171）、`Result::map_or/map_or_else` 与 `map_err/and_then/or_else`
  （T172）。
- `tests/test45.cpp`：真实 server 上的 13 种标量类型批量读写往返（T173）、
  空 STRING 往返（T174）；把 `TypeTraits` 的 S7→UA 映射从 Mock 观察推进到
  真实解码/编码路径。
- `tests/compile/RecreateSyncProbe.cpp`：编译期钉死
  `RecreateSync::m_sdkForRecreate` 的类型是
  `std::shared_ptr<OPCUAPacking::internal::ISdk>`，防止全局 `::ISdk` 前向
  声明在跨 TU 时造成 ODR 类型不一致。
- CI（`.github/workflows/sanitizers.yml`）：`build-open62541` 预热缓存后并行跑
  ASan+UBSan / ASan+LSan / TSan / `--gtest_repeat=50 --gtest_shuffle` 抖动四个
  job（Ubuntu 22.04、Clang 17、libc++、Conan 2）。TSan 构建在 Debug 下跳过
  `-O0`，保留 `-O1`；`scripts/check_bench_refs.py` 检查文档引用的归档是否存在
  且已纳入 git。`scripts/quickstart.sh` 是干净 checkout 的构建入口。

## 性能与耐久（benchmark）

`bench/` 下有四个可执行文件，默认自起一个 in-process open62541 server，
所以每条命令都能单机复现。它们不注册 ctest：benchmark 结果是观测值，不是
门禁。长时 RSS soak 是例外：`run_bench.sh` 会把 server 放到独立进程
（`bench_server`），这样被测进程的 `/proc/self/statm` 只包含客户端。

```sh
# 一键构建并跑完延迟 / 恢复 / RSS（长时 soak 默认 24h）
OPEN62541_ROOT=/path/to/open62541-v1.4.14 ./scripts/run_bench.sh
SKIP_LONG=1 ./scripts/run_bench.sh          # 只跑延迟与恢复分布
SOAK_HOURS=0.01 ./scripts/run_bench.sh      # 36s soak 冒烟
LATENCY_REPEATS=5 ./scripts/run_bench.sh    # 延迟跑 5 次并输出中位数

# 或分别运行（构建产物在 build-*/bench-build/）
./build-bench/bench-build/bench_latency  --threads 8 --ops 8000 --batch 16 --repeats 5
./build-bench/bench-build/bench_latency  --threads 8 --ops 8000 --batch 16 --write --repeats 5
# 诊断基线：每线程一个 client（不是默认部署模型；每线程会多一个看门狗）
./build-bench/bench-build/bench_latency  --threads 8 --ops 100000 --batch 16 \
    --endpoint opc.tcp://127.0.0.1:4892 --per-thread-client
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
- `bench_rss`：长跑时按间隔输出 `elapsed_s,rss_kb,calls,errors` CSV。
- `bench_server`：独立进程的 server，供 soak 使用。

**当前 15×6 SLO 归档是 `bench-results/20261030_141909_slo/`（干净 HEAD
`228df04`，源码哈希与当前 `ua.cpp` 一致），判据与 operating point 分析以
[benchmark 判据](benchmark.md) 为准；`20261030_083718_slo` 是 5 次历史归档，
`20261029_141357_slo` 只作补丁前基线。一句话结论：共享实例容量下限 PASS
（8T p99 9.308ms、8376.1 calls/s），严格线性 FAIL；固定 4268 calls/s 下
p99 1.794ms 仍高于严格界；per-thread client 严格判据 PASS（0.669ms、
23483.2 calls/s），不需要再做每线程绑核。1T p99.9 的 15 次中位数是
0.715ms，bootstrap 95% CI 上界 0.763ms，15 个原始 run 全部低于 1ms；这一轮
可按 PASS 引用，但仍不是跨机器硬保证。

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

见 [已知限制](limitations.md)：无 GUID/ByteString NodeId、无 MethodCall/
Subscription、无最大批量上限、写不自动重试、高 RTT 链路需要显式调大
`timeoutMs`；当前只配置 None 安全策略/匿名身份，没有加密、证书或用户名
身份；`shutdown()` 的 join 无独立超时。
