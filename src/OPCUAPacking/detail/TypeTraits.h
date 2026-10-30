#pragma once

#include <string>

#include "Rust_error_deal/error_deal.h"
#include <open62541/client_highlevel.h>
#include "OPCUAPacking/S7TypeStruct.h"
#include "OPCUAPacking/ValueTypes.h"

namespace TypeTraits{
// NodeId 的字符串表示不允许内嵌 '\0'：UA_NODEID_STRING_ALLOC 内部走
// UA_String_fromChars -> strlen，会把 "A\0B" 静默截断成 "A"。
// 这里显式拒绝，避免下游拿到一个与调用方传入不一致的 NodeId。
inline bool containsEmbeddedNul(const std::string &s) {
    return s.find('\0') != std::string::npos;
}
} // namespace

namespace opcua::readTraits {
template <typename T, int TYPE_ENUM> struct UATypeTraitsBase {
  static const UA_DataType *uaType() { return &UA_TYPES[TYPE_ENUM]; }
  static Result<T, RichError> convert(const UA_Variant &var) {
    // open62541 v1.4.14 include/open62541/types.h:715-721 defines a scalar as
    //   arrayLength == 0 && data > UA_EMPTY_ARRAY_SENTINEL
    // An empty array is arrayLength == 0 && data == UA_EMPTY_ARRAY_SENTINEL
    // (0x01). Testing !data || arrayLength > 0 accepts the sentinel and then
    // dereferences address 0x01, so use the SDK predicate UA_Variant_isScalar
    // (types.h:762-765) which encodes the whole invariant.
    if (var.type != uaType() || !UA_Variant_isScalar(&var)) {
      return Result<T, RichError>::error(RichError{RichError::ErrorCode::TYPE_MISMATCH,"type mismatch"});
    }
    return Result<T, RichError>::success(*static_cast<const T *>(var.data));
  }
};

template <> struct UATypeTraitsBase<std::string,UA_TYPES_STRING> {
  static const UA_DataType *uaType() { return &UA_TYPES[UA_TYPES_STRING]; }
  static Result<std::string, RichError> convert(const UA_Variant &var) {
    // Same scalar invariant as UATypeTraitsBase: an empty STRING array is
    // arrayLength == 0 with data == UA_EMPTY_ARRAY_SENTINEL (types.h:715-721).
    if (var.type != uaType() || !UA_Variant_isScalar(&var)) {
      return Result<std::string, RichError>::error(RichError{RichError::ErrorCode::TYPE_MISMATCH,"type mismatch"});
    } else {
      const UA_String *src = static_cast<const UA_String *>(var.data);
      if (!src->data) {
        // 空 / 缺失的 STRING：给出确定错误码，避免落到 RichError 的 UNKNOWN
        // 兜底（同一输入每次都必须映射到同一 ErrorCode）。
        return Result<std::string, RichError>::error(
            RichError{RichError::ErrorCode::TYPE_MISMATCH, "src->data is nullptr"});
      }
      return Result<std::string, RichError>::success(
          std::string(reinterpret_cast<const char *>(src->data), src->length));
    }
  }
};

template <typename S7Type>
struct UATypeTraits; // 特化提供 UA_DataType* 和转换函数

// 按 UA_TYPES 索引顺序排列
template<> struct UATypeTraits<bool> : UATypeTraitsBase<bool, UA_TYPES_BOOLEAN> {};        // 0
template<> struct UATypeTraits<int8_t> : UATypeTraitsBase<int8_t, UA_TYPES_SBYTE> {};      // 1
template<> struct UATypeTraits<uint8_t> : UATypeTraitsBase<uint8_t, UA_TYPES_BYTE> {};      // 2
template<> struct UATypeTraits<int16_t> : UATypeTraitsBase<int16_t, UA_TYPES_INT16> {};     // 3
template<> struct UATypeTraits<uint16_t> : UATypeTraitsBase<uint16_t, UA_TYPES_UINT16> {};  // 4
template<> struct UATypeTraits<int32_t> : UATypeTraitsBase<int32_t, UA_TYPES_INT32> {};     // 5
template<> struct UATypeTraits<uint32_t> : UATypeTraitsBase<uint32_t, UA_TYPES_UINT32> {};  // 6
template<> struct UATypeTraits<int64_t> : UATypeTraitsBase<int64_t, UA_TYPES_INT64> {};     // 7
template<> struct UATypeTraits<uint64_t> : UATypeTraitsBase<uint64_t, UA_TYPES_UINT64> {};  // 8
template<> struct UATypeTraits<float> : UATypeTraitsBase<float, UA_TYPES_FLOAT> {};         // 9
template<> struct UATypeTraits<double> : UATypeTraitsBase<double, UA_TYPES_DOUBLE> {};      // 11
template<> struct UATypeTraits<std::string> : UATypeTraitsBase<std::string, UA_TYPES_STRING> {};      // 11

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
  static Result<Unit, RichError> convert(int nameSpace,const std::variant<std::string,uint32_t> &nodeID,
                                        const S7DataType &dataType,
                                        UA_WriteValue &destValue,
                                        const ValueType &VariableItem) {
    T value{};
    if (const T *pValue = std::get_if<T>(&VariableItem)) {
      value = *pValue;
    } else {
      return Result<Unit, RichError>::error(RichError{
          RichError::ErrorCode::TYPE_MISMATCH,
          "writeTraits: declared S7DataType does not match the ValueType "
          "alternative"});
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

template <> struct UATypeTraitsBase<std::string, UA_TYPES_STRING> {
  static const UA_DataType *uaType() { return &UA_TYPES[UA_TYPES_STRING]; }
  static Result<Unit, RichError>
  convert(int nameSpace, const std::variant<std::string,uint32_t> &nodeID, const S7DataType &dataType,
          UA_WriteValue &destValue, const ValueType &VariableItem) {
    std::string value{""};
    if (const std::string *pValue = std::get_if<std::string>(&VariableItem)) {
      value = *pValue;
    } else {
      return Result<Unit, RichError>::error(RichError{
          RichError::ErrorCode::TYPE_MISMATCH,
          "writeTraits: declared S7DataType does not match the ValueType "
          "alternative"});
    }

    if (value.find('\0') != std::string::npos) {
      return Result<Unit, RichError>::error(
          RichError{RichError::ErrorCode::INVALID_ARGUMENT,
                    "writeTraits: STRING value contains embedded NULL"});
    }
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

template <typename S7Type>
struct UATypeTraits; // 特化提供 UA_DataType* 和转换函数

// 按 UA_TYPES 索引顺序排列
template <>
struct UATypeTraits<bool> : UATypeTraitsBase<bool, UA_TYPES_BOOLEAN> {}; // 0
template <>
struct UATypeTraits<int8_t> : UATypeTraitsBase<int8_t, UA_TYPES_SBYTE> {
}; // 1
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
}; // 7
template <>
struct UATypeTraits<uint64_t> : UATypeTraitsBase<uint64_t, UA_TYPES_UINT64> {
}; // 8
template <>
struct UATypeTraits<float> : UATypeTraitsBase<float, UA_TYPES_FLOAT> {}; // 9
template <>
struct UATypeTraits<double> : UATypeTraitsBase<double, UA_TYPES_DOUBLE> {
}; // 10
template <>
struct UATypeTraits<std::string> : UATypeTraitsBase<std::string, UA_TYPES_STRING> {
}; // 11

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