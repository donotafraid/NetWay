// tests/compile/RuleOfFiveAsserts.cpp
//
// 报告 §3.13 CT-2 / §4.4 P3-4/P3-5：
// 编译期断言 OPC_UA_Client 满足 Rule of Five、final、非平凡析构。
// 本 TU 编入 tests 目标，跟随 gtest 的 include 环境；
// “ua.h 是否泄漏 open62541”由独立目标 only_ua_h 验证。
//
// 注意：本文件不提供 main；断言在编译期生效，不需要运行期入口。

#include "OPCUAPacking/ua.h"
#include <type_traits>

static_assert(!std::is_copy_constructible_v<OPC_UA_Client>);
static_assert(!std::is_move_constructible_v<OPC_UA_Client>);
static_assert(!std::is_copy_assignable_v<OPC_UA_Client>);
static_assert(!std::is_move_assignable_v<OPC_UA_Client>);
static_assert(std::is_destructible_v<OPC_UA_Client>);
static_assert(!std::is_trivially_destructible_v<OPC_UA_Client>);
static_assert(std::is_final_v<OPC_UA_Client>);