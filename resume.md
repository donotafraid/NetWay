# 黄亚明

18249900561 | 912030570@qq.com | 广州

求职方向：C++ 后端 / 工业物联网 / 边缘计算

## 专业技能

- **C++17**：RAII、pImpl、Rule of Five、移动语义、模板/特化、异常安全；
  熟悉 libc++/Clang、CMake、Conan、GDB/LLDB。
- **并发**：`std::thread/mutex/condition_variable/atomic`、锁序设计、
  生命周期租约、在途计数、TSan/ASan/LSan、死锁与丢失更新排查。
- **协议/工业**：OPC UA（open62541 v1.4.14）、S7/PLC 数据类型、会话与
  安全通道生命周期、断线重连、状态机、批量读写；当前封装使用 None 安全
  策略/匿名身份，加密/证书/用户名身份尚未暴露。
- **工程化**：GoogleTest、Mock/接缝测试、GitHub Actions、benchmark、
  15 次中位数 + bootstrap CI、CI 上界门禁、RSS soak、netem 故障注入、技术文档。

## 项目：工业 OPC UA 客户端封装（C++17 / open62541 v1.4.14）

- 设计 **pImpl + ISdk 接缝**，把 `UA_*` C 类型完全隔离在实现层；公共头
  编译探针证明只带 `include/` 可编译，链接探针保证公共符号由本库提供；
  编译期签名锁固定所有公开入口不出现 `UA_*`。
- 用 RAII 守卫覆盖 `UA_ReadResponse`/`UA_WriteResponse`/`UA_ReadRequest`/
  `UA_WriteRequest` 的每条出口；对 SDK 畸形响应（`null+size>0`、空数组
  哨兵、`diagnosticInfos`）做 size/指针中和；异常注入路径也由守卫配平。
- 实现多线程共享实例：`ApiLease` + 在途计数 + 看门狗生命周期；
  shutdown/recreate 与在途请求、析构并发安全；修复 recreate 析构 UAF
  窗口（T150/T153），并补齐“recreate 命中析构”时候选 Impl 的 watchdog
  泄漏（T169）。
- 支持 STRING（`ns=1;s=...`）与 NUMERIC（`ns=1;i=...`）NodeId 的读、写、
  混合批处理与真实 server 往返（T163~T166）；写路径非幂等，封装层不自动
  重试；STRING 内嵌 `'\0'` 在进入 SDK 前拒绝。
- 修复 **GoodInfo 状态码被当错误**的缺陷（`GOOD_CLAMPED` 读丢值/写误报
  Bad），用 `UA_StatusCode_isGood` 做 severity 判断；新增状态码、公共头
  链接、`getState` 丢失更新等回归用例。
- 公开入口统一为 `noexcept` + `Result<T, RichError>`；`create` 失败不抛，
  SDK 接缝抛异常时由守卫/`CASGuard` 回滚；T167/T168 把运行时和编译期
  契约钉死；T172 补齐 `Result::map_or/map_or_else/map_err/and_then/or_else`
  的编译与运行覆盖。
- 测试/门禁：174 个 gtest（173 pass + 1 真实设备 skip）、45 个测试文件、
  176 个 ctest；ASan+UBSan、ASan+LSan、TSan 并发过滤器、异常注入、真实
  server 集成、24h RSS、netem 矩阵；CI 覆盖 ASan/LSan/TSan/50 次乱序抖动。
  补 T173/T174 后，真实 server 覆盖 13 种 S7 标量类型的批量读写和空 STRING；
  `tests/compile/RecreateSyncProbe.cpp` 用编译期断言钉死内部头文件的
  `internal::ISdk` 前向声明，避免跨 TU 的 ODR 类型不一致。
- 性能（`bench-results/20261030_141909_slo/`，干净 HEAD 228df04、
  RelWithDebInfo/-O2、15 次中位数、90 个 run 全部 `errors=0`）：1T
  11497.6 calls/s、p99 0.173ms、p99.9 0.715ms（95% CI 上界 0.763ms，15 个
  原始 run 均低于 1ms）；8T 共享 8376.1 calls/s、p99 9.308ms（饱和点，
  0.728×1T）；固定 4268 calls/s 时 p99 1.794ms；8T 每线程一个 client
  23483.2 calls/s、p99 0.669ms。共享实例容量下限通过、严格线性未通过，
  per-thread 严格判据通过。
- 文档/复现：README + 线程模型 + 已知限制 + benchmark 判据；锁序、所有权、
  软预算、8T 结论、归档有效性全部可追溯；`scripts/check_bench_slo.py`
  把 1T p99.9 的 CI 上界与样本数变成可执行门禁，
  `scripts/check_bench_refs.py` 拒绝未提交/悬空归档；
  `scripts/quickstart.sh` 是从干净 checkout 一条命令构建并跑测试的入口。

## 工作/实习经历

**广州艾利包装有限公司 | 助理技术员 | 2022.6.1~2025.4.25**

## 教育

**湖北理工学院 | 机械专业 | 本科 | 2017~2020**

## 其他

- [项目仓库]（README 有架构图、线程模型、benchmark 表、limitations）
- [GitHub 链接](https://github.com/donotafraid/NetWay)
