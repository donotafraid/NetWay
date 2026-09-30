#include "OPCUAPacking/ua.h"
#include <type_traits>
#include <gtest/gtest.h>

// 本用例只验证 OPC_UA_Client 的 Rule-of-Five 与签名，
// 封装完整性由 tests/compile/EncapsulationProbe.cpp 单独锁定：
// 该目标故意不带 open62541 include 路径，只要 ua.h 间接拖入 SDK 就编译失败。
// 真正的"头文件自洽"应由 tests/compile/OnlyUaH.cpp 编译期探针验证（§6.4）。
TEST(Client, CompileTime_RuleOfFive) {
    static_assert(!std::is_copy_constructible_v<OPC_UA_Client>);
    static_assert(!std::is_move_constructible_v<OPC_UA_Client>);
    static_assert(!std::is_copy_assignable_v<OPC_UA_Client>);
    static_assert(!std::is_move_assignable_v<OPC_UA_Client>);
    static_assert(std::is_destructible_v<OPC_UA_Client>);

    // P3-4：证明确实是用户提供的析构（非平凡）
    static_assert(!std::is_trivially_destructible_v<OPC_UA_Client>);

    // // P3-5：若类已标 final，则断言 final（防止继承后 delete 基类指针 UB）
    static_assert(std::is_final_v<OPC_UA_Client>);

    SUCCEED();
}