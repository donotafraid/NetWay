#pragma once

#include <optional>

// IStringLengthProbe.h (业务逻辑层，不依赖Qt/网络)
class IStringLengthProbe {
public:
    virtual ~IStringLengthProbe() = default;
    
    // 仅用于构建期，输入变量ID，输出字符串在PLC中的最大长度
    virtual std::optional<int> probeStringLength(int varByteOffset) = 0;
};