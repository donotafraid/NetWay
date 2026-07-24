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
#include "PLC/OPCUADataBlock.h"

class OPCUADataCovert {
public:
  OPCUADataCovert() = default;
  ~OPCUADataCovert() = default;
  // OPCUA
  // UA->Normal
  Result<bool, RichError>
  Set_UA_To_Read_Normal_Scalar(const S7DataType &S7_type, Dynamic_Value &value,
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
  batchSet_Uint8_t_To_Dynamic(OPCUADataBlock *data);
  // DynamicValue->Uint8_t
  Result<bool, RichError>
  batchSet_DynamicValue_To_Uint8_t(OPCUADataBlock *data);

  template <typename T>
  void ByteDeserialization_memcpy(T &value, const std::vector<uint8_t> &src,
                                  int data_offset, int data_length);

private:
};