#include "OPCUAPacking/ua.h"
#include <type_traits>
#include <gtest/gtest.h>

// 报告 §3.13 CT-1：原注释"只 include ua.h，不含 open62541 头"与事实相反。
// ua.h 自身会用 UA_Client（shared_ptr 包装），必然牵入 SDK 头；
// 因此本测试只断言 Rule of Five 与封装约束，不再声称"不含 SDK 头"。
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