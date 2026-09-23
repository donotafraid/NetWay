// include/OPCUAPacking/internal/ErrorMapping.h
#pragma once
#include <string>
#include "OPCUAPacking/internal/State.h"
#include "Rust_error_deal/error_deal.h"

namespace OPCUAPacking::internal {

std::string connectErrorStateToString(ConnectErrorState state);
std::string disconnectErrorStateToString(DisconnectErrorState state);
RichError::ErrorCode toRichErrorCode(ConnectErrorState s);
RichError::ErrorCode toRichErrorCode(DisconnectErrorState s);

} // namespace OPCUAPacking::internal