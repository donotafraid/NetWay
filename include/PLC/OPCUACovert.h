#pragma once

#include <open62541/nodeids.h>
#include <open62541/client.h>
#include <open62541/config.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_config_default.h>

#include <fstream>
#include <string>
#include <regex>
#include <iostream>
#include "Rust_error_deal/error_deal.h"
#include "PLC/Struct.h"

class OPCUADataCovert {
public:
  OPCUADataCovert() = default;
  ~OPCUADataCovert() = default;
    // extract function
  std::pair<std::string, std::string>
  extractPureNodeIdRobust(const std::string &input) {
    {
      // 正则表达式结构：
      // 匹配部分: ^ns=    [0-9]+    ;s=
      // 读取部分:         ([0-9]+)       (.*)
      //         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
      //         描述特征             描述内容
      std::regex pattern("^ns=([0-9]+);s=(.*)");
      std::smatch match;
      if (std::regex_search(input, match, pattern)) {
        // match[1] = "3"
        // match[2] = "DB111_EdgeGatewayTest" (不包含引号)
        return {match[1].str(), match[2].str()};
      }
    }
    return {"", input};
  }

  // OPCUA
  // UA->Normal
  Result<bool, RichError>
  Set_UA_To_Read_Normal_Scalar(const S7DataType &S7_type, QVariant &dataVar,
                               int i,
                               const std::vector<UA_Variant> &batchReadVariant);
  template <typename T>
  void Covert_UA_Scalar_To_Specific(T &value, int i,
                                    const std::vector<UA_Variant> &batchReadVariant);

  // Normal->UA
  Result<bool, RichError> batchSet_Normal_To_Write_UA_Scalar(
      int nameSpace, OPCUAModernDataStruct &var, int index,
      std::vector<UA_WriteValue> &batchWriteNodes);
  template <typename T>
  Result<bool, RichError> batchSet_UA_Scalar_StatusCode(
      int nameSpace, OPCUAModernDataStruct &SourceData_var, T &source_var,
      int index, std::vector<UA_WriteValue> &batchWriteNodes);

  // S7
  //  DynamicValue->Uint8_t
  void updateBufferFromS7ModernStructByLSB(
      std::vector<OPCUAModernDataStruct> &vector,
      std::vector<uint8_t> &dataBUffer);
  void updateBufferFromS7ModernStructByMSB(
      std::vector<OPCUAModernDataStruct> &vector,
      std::vector<uint8_t> &dataBUffer);

  // Uint8_t->DynamicValue
  Result<bool, RichError>
  batchSet_Uint8_t_To_Dynamic(std::vector<OPCUAModernDataStruct> &vector,
      std::vector<uint8_t> &dataBuffer);
  // DynamicValue->Uint8_t
  Result<bool, RichError>
  batchSet_DynamicValue_To_Uint8_t(std::vector<OPCUAModernDataStruct> &vector,
      std::vector<uint8_t> &dataBuffer);

  template <typename T>
  void ByteDeserialization_memcpy(T &value, const std::vector<uint8_t> &src,
                                  int data_offset, int data_length);

private:
};