#pragma once

#include <string>

#include "Rust_error_deal/error_deal.h"
#include <open62541/client_highlevel.h>
#include "PLC/S7TypeStruct.h"
#include "PLC/WriteRequestAddres.h"

namespace opcua::readTraits {
template <typename T, int TYPE_ENUM> struct UATypeTraitsBase {
  static const UA_DataType *uaType() { return &UA_TYPES[TYPE_ENUM]; }
  static Result<T, RichError> convert(const UA_Variant &var) {
    if (!var.data || var.type != uaType()) {
      return Result<T, RichError>::error(RichError{"type mismatch"});
    }
    return Result<T, RichError>::success(*static_cast<const T *>(var.data));
  }
};

template <typename S7Type>
struct UATypeTraits; // 特化提供 UA_DataType* 和转换函数

// 按 UA_TYPES 索引顺序排列
template<> struct UATypeTraits<bool> : UATypeTraitsBase<bool, UA_TYPES_BOOLEAN> {};        // 0
template<> struct UATypeTraits<int8_t> : UATypeTraitsBase<int8_t, UA_TYPES_SBYTE> {};      // 1 ← 新增
template<> struct UATypeTraits<uint8_t> : UATypeTraitsBase<uint8_t, UA_TYPES_BYTE> {};      // 2
template<> struct UATypeTraits<int16_t> : UATypeTraitsBase<int16_t, UA_TYPES_INT16> {};     // 3
template<> struct UATypeTraits<uint16_t> : UATypeTraitsBase<uint16_t, UA_TYPES_UINT16> {};  // 4
template<> struct UATypeTraits<int32_t> : UATypeTraitsBase<int32_t, UA_TYPES_INT32> {};     // 5
template<> struct UATypeTraits<uint32_t> : UATypeTraitsBase<uint32_t, UA_TYPES_UINT32> {};  // 6
template<> struct UATypeTraits<int64_t> : UATypeTraitsBase<int64_t, UA_TYPES_INT64> {};     // 7 ← 新增
template<> struct UATypeTraits<uint64_t> : UATypeTraitsBase<uint64_t, UA_TYPES_UINT64> {};  // 8 ← 新增
template<> struct UATypeTraits<float> : UATypeTraitsBase<float, UA_TYPES_FLOAT> {};         // 9
template<> struct UATypeTraits<double> : UATypeTraitsBase<double, UA_TYPES_DOUBLE> {};      // 10 ← 新增

template <> struct UATypeTraits<std::string> {
  static const UA_DataType *uaType() { return &UA_TYPES[UA_TYPES_STRING]; }
  static Result<std::string, RichError> convert(const UA_Variant &var) {
    if (!var.data || var.type != uaType()) {
      return Result<std::string, RichError>::error(RichError{"type mismatch"});
    } else {
      const UA_String *src = static_cast<const UA_String *>(var.data);
      if (!src->data) {
        return Result<std::string, RichError>::error(RichError{"src->data is nullptr"});
      }
      return Result<std::string, RichError>::success(
          std::string(reinterpret_cast<const char *>(src->data), src->length));
    }
  }
};

template <S7DataType> struct S7TypeToUATraits;

template <> struct S7TypeToUATraits<S7DataType::BOOL> {
  using type = UATypeTraits<bool>;
};
template <> struct S7TypeToUATraits<S7DataType::SINT> {
  using type = UATypeTraits<int8_t>;
};
template <> struct S7TypeToUATraits<S7DataType::BYTE> {
  using type = UATypeTraits<uint8_t>;
};
template <> struct S7TypeToUATraits<S7DataType::INT> {
  using type = UATypeTraits<int16_t>;
};
template <> struct S7TypeToUATraits<S7DataType::WORD> {
  using type = UATypeTraits<uint16_t>;
};
template <> struct S7TypeToUATraits<S7DataType::DINT> {
  using type = UATypeTraits<int32_t>;
};
template <> struct S7TypeToUATraits<S7DataType::UDINT> {
  using type = UATypeTraits<uint32_t>;
};
template <> struct S7TypeToUATraits<S7DataType::LINT> {
  using type = UATypeTraits<int64_t>;
};
template <> struct S7TypeToUATraits<S7DataType::ULINT> {
  using type = UATypeTraits<uint64_t>;
};
template <> struct S7TypeToUATraits<S7DataType::DWORD> {
  using type = UATypeTraits<uint32_t>;
};
template <> struct S7TypeToUATraits<S7DataType::REAL> {
  using type = UATypeTraits<float>;
};
template <> struct S7TypeToUATraits<S7DataType::LREAL> {
  using type = UATypeTraits<double>;
};
template <> struct S7TypeToUATraits<S7DataType::STRING> {
  using type = UATypeTraits<std::string>;
};

};

namespace opcua::writeTraits {
template <typename T, int TYPE_ENUM> struct UATypeTraitsBase {
  static const UA_DataType *uaType() { return &UA_TYPES[TYPE_ENUM]; }
  static Result<Unit, RichError> convert(int nameSpace,const std::string &nodeID,
                                        const S7DataType &dataType,
                                        UA_WriteValue &destValue,
                                        const ValueType &VariableItem) {
    T value{};
    if (const T *pValue = std::get_if<T>(&VariableItem)) {
      value = *pValue;
    } else {
      return Result<Unit, RichError>::error(RichError{"type not match "});
    }

    //  INIT WRITE UA_VALUE
    if (destValue.attributeId != UA_ATTRIBUTEID_VALUE) {
      {
        destValue.nodeId = UA_NODEID_STRING_ALLOC(nameSpace,
                                                  nodeID.data());
      }
      destValue.attributeId = UA_ATTRIBUTEID_VALUE;
    }

    //  COPY RESOURCE TO UA_VARIANT OF UA_WriteValue
    UA_StatusCode status =
        UA_Variant_setScalarCopy(&destValue.value.value, &value, uaType());
    if (status != UA_STATUSCODE_GOOD) {
      return Result<Unit, RichError>::error(
          RichError("OPCUADataCovert : batchSet_UA_Scalar_StatusCode fail"));
    }
    destValue.value.hasValue = true;
    if (!destValue.value.hasValue) {
      return Result<Unit, RichError>::error(RichError{"hasValue false"});
    }
    if (!destValue.value.value.type) {
      return Result<Unit, RichError>::error(RichError{"value.type null"});
    }
    if (!destValue.value.value.data) {
      return Result<Unit, RichError>::error(RichError{"value.data null"});
    }
    return Result<Unit, RichError>::success(Unit{});
  }
};

template <typename S7Type>
struct UATypeTraits; // 特化提供 UA_DataType* 和转换函数

// 按 UA_TYPES 索引顺序排列
template <>
struct UATypeTraits<bool> : UATypeTraitsBase<bool, UA_TYPES_BOOLEAN> {}; // 0
template <>
struct UATypeTraits<int8_t> : UATypeTraitsBase<int8_t, UA_TYPES_SBYTE> {
}; // 1 ← 新增
template <>
struct UATypeTraits<uint8_t> : UATypeTraitsBase<uint8_t, UA_TYPES_BYTE> {}; // 2
template <>
struct UATypeTraits<int16_t> : UATypeTraitsBase<int16_t, UA_TYPES_INT16> {
}; // 3
template <>
struct UATypeTraits<uint16_t> : UATypeTraitsBase<uint16_t, UA_TYPES_UINT16> {
}; // 4
template <>
struct UATypeTraits<int32_t> : UATypeTraitsBase<int32_t, UA_TYPES_INT32> {
}; // 5
template <>
struct UATypeTraits<uint32_t> : UATypeTraitsBase<uint32_t, UA_TYPES_UINT32> {
}; // 6
template <>
struct UATypeTraits<int64_t> : UATypeTraitsBase<int64_t, UA_TYPES_INT64> {
}; // 7 ← 新增
template <>
struct UATypeTraits<uint64_t> : UATypeTraitsBase<uint64_t, UA_TYPES_UINT64> {
}; // 8 ← 新增
template <>
struct UATypeTraits<float> : UATypeTraitsBase<float, UA_TYPES_FLOAT> {}; // 9
template <>
struct UATypeTraits<double> : UATypeTraitsBase<double, UA_TYPES_DOUBLE> {
}; // 10 ← 新增

template <> struct UATypeTraits<std::string> {
  static const UA_DataType *uaType() { return &UA_TYPES[UA_TYPES_STRING]; }
  static Result<Unit, RichError> convert(int nameSpace,const std::string &nodeID,
                                        const S7DataType &dataType,
                                        UA_WriteValue &destValue,
                                        const ValueType &VariableItem) {
    std::string value{""};
    if (const std::string *pValue = std::get_if<std::string>(&VariableItem)) {
      value = *pValue;
    } else {
      return Result<Unit, RichError>::error(RichError{"type not match "});
    }

    //  INIT WRITE UA_VALUE
    if (destValue.attributeId != UA_ATTRIBUTEID_VALUE) {
      {
        destValue.nodeId = UA_NODEID_STRING_ALLOC(nameSpace, nodeID.data());
      }
      destValue.attributeId = UA_ATTRIBUTEID_VALUE;
    }

    // 一些 OPC UA 库提供辅助宏
    UA_String uaString = UA_STRING(const_cast<char *>(value.c_str()));

    //  COPY RESOURCE TO UA_VARIANT OF UA_WriteValue
    UA_StatusCode status =
        UA_Variant_setScalarCopy(&destValue.value.value, &uaString, uaType());
    if (status != UA_STATUSCODE_GOOD) {
      return Result<Unit, RichError>::error(
          RichError("OPCUADataCovert : batchSet_UA_Scalar_StatusCode fail"));
    }
    destValue.value.hasValue = true;
   
    if (!destValue.value.hasValue) {
      return Result<Unit, RichError>::error(RichError{"hasValue false"});
    }
    if (!destValue.value.value.type) {
      return Result<Unit, RichError>::error(RichError{"value.type null"});
    }
    if (!destValue.value.value.data) {
      return Result<Unit, RichError>::error(RichError{"value.data null"});
    }
    return Result<Unit, RichError>::success(Unit{});
  }
};

template <S7DataType> struct S7TypeToUATraits;

template <> struct S7TypeToUATraits<S7DataType::BOOL> {
  using type = UATypeTraits<bool>;
};
template <> struct S7TypeToUATraits<S7DataType::SINT> {
  using type = UATypeTraits<int8_t>;
};
template <> struct S7TypeToUATraits<S7DataType::BYTE> {
  using type = UATypeTraits<uint8_t>;
};
template <> struct S7TypeToUATraits<S7DataType::INT> {
  using type = UATypeTraits<int16_t>;
};
template <> struct S7TypeToUATraits<S7DataType::WORD> {
  using type = UATypeTraits<uint16_t>;
};
template <> struct S7TypeToUATraits<S7DataType::DINT> {
  using type = UATypeTraits<int32_t>;
};
template <> struct S7TypeToUATraits<S7DataType::UDINT> {
  using type = UATypeTraits<uint32_t>;
};
template <> struct S7TypeToUATraits<S7DataType::LINT> {
  using type = UATypeTraits<int64_t>;
};
template <> struct S7TypeToUATraits<S7DataType::ULINT> {
  using type = UATypeTraits<uint64_t>;
};
template <> struct S7TypeToUATraits<S7DataType::DWORD> {
  using type = UATypeTraits<uint32_t>;
};
template <> struct S7TypeToUATraits<S7DataType::REAL> {
  using type = UATypeTraits<float>;
};
template <> struct S7TypeToUATraits<S7DataType::LREAL> {
  using type = UATypeTraits<double>;
};
template <> struct S7TypeToUATraits<S7DataType::STRING> {
  using type = UATypeTraits<std::string>;
};

}; // namespace opcua::writeTraits