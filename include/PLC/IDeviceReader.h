#pragma once

#include <vector>
#include "Rust_error_deal/error_deal.h"
#include <variant>
#include "PLC/WriteRequestAddres.h"

class IDeviceReader {
public:
  virtual Result<std::unordered_map<std::string, ValueType>, RichError>
  batchRead(std::vector<std::string> &requestVec) = 0;
  virtual Result<bool, RichError>
  batchWrite(std::vector<WriteRequest> &requestVec) = 0;

  virtual Result<bool, RichError> connect() = 0;
  virtual Result<bool, RichError> reconnect(int maxRetries,
                                            int retryDelayMs) = 0;
  virtual Result<bool, RichError> isConnected() = 0;
  virtual Result<bool, RichError> disconnect() = 0;
  virtual void
  setAddressMap(std::unordered_map<std::string, PhysicalAddress> &map) = 0;
};