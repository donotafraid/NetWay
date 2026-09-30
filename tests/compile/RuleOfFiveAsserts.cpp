// tests/compile/RuleOfFiveAsserts.cpp
//
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

// 领域值类型必须支持拷贝与 noexcept 移动：std::vector<ReadResult>/<WriteResult>
// 扩容时会优先选择 noexcept 移动，否则退化为深拷贝。把这条契约固定为编译期错误。
static_assert(std::is_copy_constructible_v<OPC_UA_Client::NodeId>);
static_assert(std::is_move_constructible_v<OPC_UA_Client::NodeId>);
static_assert(std::is_nothrow_move_constructible_v<OPC_UA_Client::NodeId>);

static_assert(std::is_copy_constructible_v<OPC_UA_Client::ReadValue>);
static_assert(std::is_nothrow_move_constructible_v<OPC_UA_Client::ReadValue>);
static_assert(std::is_copy_constructible_v<OPC_UA_Client::WriteValue>);
static_assert(std::is_nothrow_move_constructible_v<OPC_UA_Client::WriteValue>);

static_assert(std::is_nothrow_move_constructible_v<OPC_UA_Client::ReadResult>);
static_assert(std::is_nothrow_move_assignable_v<OPC_UA_Client::ReadResult>);
static_assert(std::is_nothrow_move_constructible_v<OPC_UA_Client::WriteResult>);
static_assert(std::is_nothrow_move_assignable_v<OPC_UA_Client::WriteResult>);