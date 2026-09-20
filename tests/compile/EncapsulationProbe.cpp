// tests/compile/EncapsulationProbe.cpp
//
// 报告 §6.4 / CT-2：封装完整性探针。
// 本 TU 被编入独立目标 only_ua_h，该目标**故意不添加**
// open62541 的 include 路径。
//   · 若 ua.h 自洽 → 本 TU 编译通过 → 封装完整。
//   · 若 ua.h 仍直接/间接拖入 open62541 → 本 TU 编译失败 → 封装泄漏。
//
// 本文件不写任何断言、不提供 main；它唯一的作用就是“能否被编译”。

#include "OPCUAPacking/ua.h"