# 工业 OPC UA 客户端封装（OPCUAPacking）

面向工业现场的多线程 OPC UA 客户端封装：C++17、open62541 v1.4.14、
pImpl + `ISdk` 接缝、同步服务调用串行化、看门狗自愈、`Result<T, RichError>`
错误模型。公共头 `include/OPCUAPacking/ua.h` 不暴露任何 `UA_*` 类型。

完整文档（分层图、Quick Start、线程模型、已知限制、benchmark 判据）：

- [docs/README.md](docs/README.md)
- [docs/thread-model.md](docs/thread-model.md)
- [docs/limitations.md](docs/limitations.md)
- [docs/benchmark.md](docs/benchmark.md)

```sh
ctest --test-dir build-asan          # 162 个 gtest（161 pass + 1 真实设备 skip）
./scripts/run_asan.sh                # ASan + UBSan
./scripts/run_tsan.sh                # TSan + 过滤器完整性
./scripts/run_slo_bench.sh           # 5 次中位数 + bootstrap CI
```
