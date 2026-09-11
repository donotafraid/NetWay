#pragma once

#include "PLC/TypeTraits.h"
#include "PLC/WriteRequestAddres.h"

namespace opcua::readConversion {
template <S7DataType S7type>
inline Result<Unit, RichError>
setNormalScalar(const UA_Variant &ReadVariant,
                std::optional<ValueType> &dataVar) {
  using Traits = typename opcua::readTraits::S7TypeToUATraits<S7type>::type;
  auto val = Traits::convert(ReadVariant);
  if (val.is_fail()) {
    dataVar = std::nullopt;
    return Result<Unit, RichError>::error(std::move(*val.get_error()));
  }
  dataVar = val.value_or({}); 
  return Result<Unit, RichError>::success(Unit{});
};

// 定义转换器类型
using ConverterFunc = Result<Unit, RichError> (*)(const UA_Variant &,
                                                  std::optional<ValueType> &);

// 创建 unordered_map
inline const std::unordered_map<S7DataType, ConverterFunc> converters = {
    {S7DataType::BOOL,   setNormalScalar<S7DataType::BOOL>},
    {S7DataType::SINT,   setNormalScalar<S7DataType::SINT>},
    {S7DataType::BYTE,   setNormalScalar<S7DataType::BYTE>},
    {S7DataType::INT,    setNormalScalar<S7DataType::INT>},
    {S7DataType::WORD,   setNormalScalar<S7DataType::WORD>},
    {S7DataType::DINT,   setNormalScalar<S7DataType::DINT>},
    {S7DataType::UDINT,  setNormalScalar<S7DataType::UDINT>},
    {S7DataType::LINT,   setNormalScalar<S7DataType::LINT>},
    {S7DataType::ULINT,  setNormalScalar<S7DataType::ULINT>},
    {S7DataType::REAL,   setNormalScalar<S7DataType::REAL>},
    {S7DataType::LREAL,  setNormalScalar<S7DataType::LREAL>},
    {S7DataType::STRING, setNormalScalar<S7DataType::STRING>}
};

inline Result<Unit, RichError> dispatch(S7DataType type,
                                        const UA_Variant &ReadVariant,
                                        std::optional<ValueType> &value) {
  auto it = converters.find(type);
  if (it != converters.end()) {
    return it->second(ReadVariant, value);
  } else {
    return Result<Unit, RichError>::error(RichError{"Unsupported S7DataType"});
  }
};
}; // namespace opcua::readConversion

namespace opcua::writeConversion {
template <S7DataType S7type>
inline Result<Unit, RichError>
setUaNormalScalar(int nameSpace,const std::string &nodeID, const S7DataType &dataType,
                  UA_WriteValue &destValue, const ValueType &VariableItem) {
  using Traits = typename opcua::writeTraits::S7TypeToUATraits<S7type>::type;
  auto val = Traits::convert(nameSpace,nodeID, dataType, destValue, VariableItem);
  if (val.is_fail()) {
    return Result<Unit, RichError>::error(std::move(*val.get_error()));
  }
  return Result<Unit, RichError>::success(Unit{});
};

// 定义转换器类型
using ConverterFunc = Result<Unit, RichError> (*)(int,const std::string &,
                                                  const S7DataType &,
                                                  UA_WriteValue &,
                                                  const ValueType &);

// 创建 unordered_map
inline const std::unordered_map<S7DataType, ConverterFunc> converters = {
    {S7DataType::BOOL, setUaNormalScalar<S7DataType::BOOL>},
    {S7DataType::SINT, setUaNormalScalar<S7DataType::SINT>},
    {S7DataType::BYTE, setUaNormalScalar<S7DataType::BYTE>},
    {S7DataType::INT, setUaNormalScalar<S7DataType::INT>},
    {S7DataType::WORD, setUaNormalScalar<S7DataType::WORD>},
    {S7DataType::DINT, setUaNormalScalar<S7DataType::DINT>},
    {S7DataType::UDINT, setUaNormalScalar<S7DataType::UDINT>},
    {S7DataType::LINT, setUaNormalScalar<S7DataType::LINT>},
    {S7DataType::ULINT, setUaNormalScalar<S7DataType::ULINT>},
    {S7DataType::REAL, setUaNormalScalar<S7DataType::REAL>},
    {S7DataType::LREAL, setUaNormalScalar<S7DataType::LREAL>},
    {S7DataType::STRING, setUaNormalScalar<S7DataType::STRING>}};

inline Result<Unit, RichError> dispatch(int nameSpace,const std::string &nodeID,
                                        const S7DataType &dataType,
                                        UA_WriteValue &destValue,
                                        const ValueType &srcVariant) {
  auto it = converters.find(dataType);
  if (it != converters.end()) {
    return it->second(nameSpace,nodeID, dataType, destValue, srcVariant);
  } else {
    return Result<Unit, RichError>::error(RichError{"Unsupported S7DataType"});
  }
};
}; // namespace opcua::writeConversion