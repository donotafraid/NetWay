// tests/compile/EncapsulationProbe.cpp
//
// 本 TU 被编入独立目标 only_ua_h，该目标**故意不添加**
// open62541 的 include 路径。
//   · 若每个公共头都自洽 → 本 TU 编译通过 → 封装完整。
//   · 若某个头直接/间接拖入 open62541，或漏掉自己用到的标准库头 →
//     本 TU 编译失败 → 封装/自包含被破坏。
//
// 逐个包含（而不是只包含 ua.h）是刻意的：ua.h 先包含了 error_deal.h，
// 会顺带提供 <variant>，从而掩盖 ValueTypes.h / S7TypeStruct.h
// 自身漏 include 的问题。
//
// 本文件不写任何断言、不提供 main；它唯一的作用就是“能否被编译”。

#include "OPCUAPacking/S7TypeStruct.h"
#include "OPCUAPacking/ValueTypes.h"
#include "OPCUAPacking/Types.h"
#include "OPCUAPacking/ua.h"