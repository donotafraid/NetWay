# 工业 OPC UA 客户端封装（OPCUAPacking）

面向工业现场的多线程 OPC UA 客户端封装：C++17、open62541 v1.4.14、
pImpl + `ISdk` 接缝、同步服务调用串行化、看门狗自愈、单 64 位状态快照、
`Result<T, RichError>` 错误模型。公共头 `include/OPCUAPacking/ua.h` 不暴露
任何 `UA_*` 类型；所有公开入口是 `noexcept`，失败通过 `Result` 返回。

支持 STRING（`ns=1;s=...`）与 NUMERIC（`ns=1;i=...`）NodeId，批量读/写
VALUE 属性；写不自动重试。不支持的标识类型与能力边界见
[已知限制](docs/limitations.md)。

完整文档（分层图、Quick Start、线程模型、已知限制、benchmark 判据）：

- [docs/README.md](docs/README.md)
- [docs/thread-model.md](docs/thread-model.md)
- [docs/limitations.md](docs/limitations.md)
- [docs/benchmark.md](docs/benchmark.md)

```sh
ctest --test-dir build-asan          # 168 个 gtest（167 pass + 1 真实设备 skip）
./scripts/run_asan.sh                # ASan + UBSan
./scripts/run_lsan.sh                # ASan + LSan
./scripts/run_tsan.sh                # TSan + 过滤器完整性
./scripts/run_slo_bench.sh           # 5 次中位数 + bootstrap CI
```

benchmark 证据的可信度取决于“二进制不旧于源码 + 原始 run 文件 + errors=0”；
发布前先看 `bench-results/<stamp>_slo/env.txt` 的 `bin vs src` 段。当前源码
的 5 次复跑命令与 PASS/FAIL 表见 [docs/benchmark.md](docs/benchmark.md)。
