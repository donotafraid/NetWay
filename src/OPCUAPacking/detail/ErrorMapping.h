// include/OPCUAPacking/internal/ErrorMapping.h
#pragma once
#include <string>
#include "Rust_error_deal/error_deal.h"
#include "src/OPCUAPacking/detail/State.h"
 #include <cstdio>
 #include <cstdlib>

namespace OPCUAPacking::internal {

std::string connectErrorStateToString(ConnectErrorState state);
std::string disconnectErrorStateToString(DisconnectErrorState state);
RichError::ErrorCode toRichErrorCode(ConnectErrorState s);
RichError::ErrorCode toRichErrorCode(DisconnectErrorState s);

// 走到这里说明 switch 没有覆盖全部枚举值，属于代码缺陷。
// 用 abort() 而不是静默返回 UNKNOWN：新增枚举漏处理时必须立即暴露，
// 否则调用方会拿到含糊的 "unhandled connection error" 或 UNKNOWN，
// 比崩溃更难排查。
// 不用 __builtin_unreachable()：那是 UB，会掩盖缺陷而不是暴露缺陷。
[[noreturn]] inline void assertNever(const char *where) noexcept {
  std::fprintf(stderr, "[OPCUAPacking] unhandled enum value in %s\n", where);
  std::abort();
}


} // namespace OPCUAPacking::internal