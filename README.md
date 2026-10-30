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

## 一条命令构建并测试（Ubuntu/Debian）

```sh
./scripts/quickstart.sh
```

脚本会安装缺失的 Clang 17/libc++/Conan，构建锁定的 open62541 v1.4.14，
解析 Conan 依赖，然后执行 Debug 构建和 `ctest`。已有依赖树时可以完全离线复用：

```sh
OPEN62541_ROOT=/path/to/open62541-v1.4.14 \
CONAN_DIR=/path/to/conan_build \
BUILD_DIR=build ./scripts/quickstart.sh
```

```sh
ctest --test-dir build              # 174 个 ctest = 172 gtest（171 pass + 1 skip）+ 2 个编译/链接探针
./scripts/run_asan.sh               # ASan + UBSan
./scripts/run_lsan.sh               # ASan + LSan
./scripts/run_tsan.sh               # TSan + 过滤器完整性
./scripts/run_slo_bench.sh          # 5 次中位数 + bootstrap CI
```

benchmark 证据的可信度取决于“归档已提交 + 二进制不旧于源码 + 构建类型/源码
哈希 + 原始 run + errors=0”；发布前先看 `bench-results/<stamp>_slo/env.txt`
的 `bin vs src`、`--- build ---`、`--- source hash ---` 段。当前归档是
`bench-results/20261030_083718_slo/`（与当前 `ua.cpp` 的 SHA-256 一致）；
`20261029_141357_slo` 只作上一轮基线。PASS/FAIL 与 operating point 分析见
[docs/benchmark.md](docs/benchmark.md)。
