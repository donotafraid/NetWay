// 编译期探针：detail/RecreateSync.h 必须先声明 OPCUAPacking::internal::ISdk，
// 而不是全局 ::ISdk。错误声明只在 ua.cpp 的“先包含 Isdk.h、后包含
// RecreateSync.h”顺序下被掩盖；单独包含该头时，成员类型会变成另一个类型，
// 进而在跨 TU 使用 RecreateSync 时构成 ODR 违规。
//
// 本 TU 故意先包含 RecreateSync.h，再包含 Isdk.h；若前向声明写错，下面的
// static_assert 会直接失败，无需运行。

#include "src/OPCUAPacking/detail/RecreateSync.h"
#include "src/OPCUAPacking/detail/Isdk.h"

#include <memory>
#include <type_traits>

static_assert(
    std::is_same_v<
        decltype(OPCUAPacking::internal::RecreateSync::m_sdkForRecreate),
        std::shared_ptr<OPCUAPacking::internal::ISdk>>,
    "RecreateSync::m_sdkForRecreate must use OPCUAPacking::internal::ISdk; "
    "a global ::ISdk forward declaration creates an ODR-visible type mismatch");
