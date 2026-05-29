#ifndef PLC_H
#define PLC_H

#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>
#include <regex>
#include <snap7.h>
#include <dlfcn.h>
#include <cmath>
#include <list>
#include <typeindex>   // defines std::type_index
#include <typeinfo>    // defines typeid and std::type_info

#include "Rust_error_deal/error_deal.h"
#include "MainWindows/Struct.h"

// 通用数据值类型 - 支持所有PLC数据类型
using NormalDataType = std::variant<
    bool,                    // BOOL
    uint8_t,                 // BYTE
    int16_t,                 // INT
    uint16_t,                // WORD  
    int32_t,                 // DINT
    uint32_t,                // UDINT,DWORD
    float,                   // REAL
    std::string              // STRING
>;

enum class S7DataType
{
    BOOL,  //QCheckBox 
    BYTE,   //QSpinBox
    INT,    //QSpinBox
    WORD,   //QSpinBox
    DINT,   //QSpinBox
    UDINT,  //QLineEdit
    DWORD,  //QLineEdit
    REAL,   //QDoubleSpinBox
    STRING, //QLineEdit
    ARRAY, 
    STRUCT,
    UNKNOWN 
};

static const std::map<S7DataType, std::string> S7DataTypeToString = {
    {S7DataType::BOOL, "BOOL"},     {S7DataType::BYTE, "BYTE"},
    {S7DataType::INT, "INT"},       {S7DataType::WORD, "WORD"},
    {S7DataType::DINT, "DINT"},     {S7DataType::UDINT, "UDINT"},
    {S7DataType::DWORD, "DWORD"},   {S7DataType::REAL, "REAL"},
    {S7DataType::STRING, "STRING"}, {S7DataType::ARRAY, "ARRAY"},
    {S7DataType::STRUCT, "STRUCT"}, {S7DataType::UNKNOWN, "UNKNOWN"}};

// 建立字符串到枚举的映射表
static const std::unordered_map<std::string, S7DataType> typeMap = {
    {"BOOL", S7DataType::BOOL},     {"BYTE", S7DataType::BYTE},
    {"INT", S7DataType::INT},       {"WORD", S7DataType::WORD},
    {"DINT", S7DataType::DINT},     {"UDINT", S7DataType::UDINT},
    {"DWORD", S7DataType::DWORD},   {"REAL", S7DataType::REAL},
    {"STRING", S7DataType::STRING}, {"ARRAY", S7DataType::ARRAY},
    {"STRUCT", S7DataType::STRUCT},
};

NormalDataType default_value_for(S7DataType type); 

struct S7XMLVariableDefinition
{
    std::string variable_name = "";
    S7DataType data_type_enum = S7DataType::UNKNOWN ;
    S7DataType array_type_enum = S7DataType::UNKNOWN ;
    std::string comment;
    // in string , mean the length of string object + 2
    int s7_data_type_length = 0;
    int s7_data_array_length = 0;

    float bytes_offset = 0;
    int bit_offset = 0; //  in PLC-S7 ， bool variable saved in a bytes , SO THERE IS 8 BOOL VAIRABLE used for A BTYES VARIABLE AT MOST.  bool variable position(0~7)

    std::vector<S7XMLVariableDefinition> struct_member_vector;  //  used for Type:struct member variable 
};

struct OPCUADataBlockDefinition
{
    std::string data_block_name;
    int block_number = 0;
    bool optimized_access = false;
    int total_bytes_size = 0;
    std::vector<S7XMLVariableDefinition> variable_definitions_vector;   //  used for file member variable and struct member variable
};

class Dynamic_Value{
    public:
    void *ptr = nullptr;
    std::type_index m_cpp_initalize_type = typeid(void);
    std::type_index m_cpp_introduced_type = typeid(void);
    void(*deleter)(void*) = nullptr;

    //  AVOIDING float DELETE BY SHALLOW COPY
    Dynamic_Value (const Dynamic_Value&) = delete;
    Dynamic_Value& operator= (const Dynamic_Value&) =delete; 
    
    //  RIGHT VALUE CONSTRUCT AND DELETE NULL CONSTRUCT
    template<typename T>
    Dynamic_Value (T&& val){Reset_Value(std::forward<T>(val));}
    Dynamic_Value () = default;

    bool has_value() const { return ptr != nullptr; }

    template<typename T>
    const T& get() const{
        return *(static_cast<T*>(ptr));
    }

    Dynamic_Value(Dynamic_Value&& otehr) noexcept:
    ptr(otehr.ptr),deleter(otehr.deleter)
    {
        otehr.ptr = nullptr;
        otehr.deleter = nullptr;
    }

    Dynamic_Value& operator= (Dynamic_Value &&other) noexcept{
        if(this == &other)
        {
            return *this;
        }
        if(ptr && deleter)
        {
            ptr = other.ptr;
            deleter = other.deleter;
            other.ptr = nullptr;
            other.deleter = nullptr;
        }
        return *this;
    }

    template <typename T> void Reset_Value(T &&val) {
      // 1. 获取实际要存储的类型（去除引用和cv限定符）
      using StoredType = std::decay_t<T>;

      // 2. 记录新类型的 typeid
      m_cpp_introduced_type = typeid(StoredType);

      // 3. 情况1：已有数据且类型匹配
      if (deleter && ptr && m_cpp_introduced_type == m_cpp_initalize_type) {
        // 直接赋值到现有内存
        *static_cast<StoredType *>(ptr) = std::forward<T>(val);
        return;
      }

      // 4. 情况2：首次分配
      if (deleter == nullptr && ptr == nullptr) {
        auto *new_ptr = new StoredType(std::forward<T>(val));
        if (new_ptr != nullptr) {
          ptr = new_ptr;
          m_cpp_initalize_type = typeid(StoredType);
          deleter = [](void *pointer) {
            delete static_cast<StoredType *>(pointer);
          };
        }
        return;
      }

      // 5. 情况3：类型不匹配，需要重新分配
      if (m_cpp_initalize_type != m_cpp_introduced_type) {
        // 清理旧资源
        if (deleter) {
          deleter(ptr);
        }
        ptr = nullptr;
        deleter = nullptr;

        // 分配新资源
        auto *new_ptr = new StoredType(std::forward<T>(val));
        if (new_ptr != nullptr) {
          ptr = new_ptr;
          m_cpp_initalize_type = typeid(StoredType);
          deleter = [](void *pointer) {
            delete static_cast<StoredType *>(pointer);
          };
        }
        return;
      }
    }

    Result<bool, RichError> Reset_Value_by_uint8_t(
        int data_offset, int data_length, S7DataType &data_type_enum,
        std::vector<uint8_t> &m_data_block_buffer);

    //  SHOULD DO DELETE
    ~Dynamic_Value(){
      if (deleter && ptr) {
        //  HOW TO DELETE 
        deleter(ptr);
      }
    }
};

class SCL_Parser
{
    public:
    explicit SCL_Parser( ) = default;
    ~SCL_Parser() = default;
    
    Result<std::string,RichError> read_file_content(const std::string& file_path);
    Result<OPCUADataBlockDefinition,RichError> parse(const std::string& file_path);
    Result<OPCUADataBlockDefinition,RichError> parse_data_block_header( std::string& file_content);
    Result<OPCUADataBlockDefinition,RichError> parse_variable( OPCUADataBlockDefinition& data_block_definition);
    Result<OPCUADataBlockDefinition,RichError> extract_variable_from_content(std::istringstream& file_content,std::string& line , OPCUADataBlockDefinition& data_block_definition );
    Result<S7XMLVariableDefinition,RichError> parse_variable_declaration( std::string& line);
    Result<bool,RichError> parse_data_type( std::string& str,S7XMLVariableDefinition& variable_definition);
    Result<OPCUADataBlockDefinition,RichError> calculate_data_block_size(OPCUADataBlockDefinition& data_block_definition);
    Result<bool,RichError> calculate_variable_offset(S7XMLVariableDefinition& var,int& last_free_byte_offset,int& current_bit_quality,float& last_var_bit_offset,S7DataType& last_data_S7_type);

    //  Remove leading and trailing whitespace characters from a string
    Result<std::string,RichError> trim(const std::string& str);
    Result<std::string,RichError>  extract_variable_name(const std::string& str);
    Result<std::string,RichError> extract_variable_comment(const std::string& str);
    Result<bool,RichError> add_array_member(S7XMLVariableDefinition& var);
    Result<S7DataType, RichError> transform_string_to_S7DataType(const std::string& str);
    Result<int,RichError> return_type_size(S7DataType type);
      
    Result<bool,RichError> is_odd_or_even();
    private:
    std::string m_file_content;
    size_t pos;
};

class DataTypeMapper{
    public:
    static Result<NormalDataType,RichError> Data_transform_from_bytes(const S7DataType& type,const std::vector<uint8_t>& bytes,int offset,int string_length,int bit_offset){
        switch(type)
        {
            //  the data order is little endian
            case S7DataType::BOOL:
            {
                auto tmp_byte = bytes[offset];
                if((tmp_byte & (1 << bit_offset)))
                {
                    return Result<NormalDataType,RichError>(bool{true});
                }
                else {
                    return Result<NormalDataType,RichError>(bool{false});
                }
            }
            case S7DataType::BYTE:
                return Result<NormalDataType,RichError>(uint8_t{bytes[offset]});
            case S7DataType::INT:
            {
                int16_t value = 0;
                std::memcpy(&value, bytes.data() + offset, sizeof(int16_t));
                return Result<NormalDataType,RichError>(int16_t{value});
            }
            case S7DataType::DINT:
            { 
                int32_t value = 0;
                std::memcpy(&value, bytes.data() + offset, sizeof(int32_t));
                return Result<NormalDataType,RichError>(int32_t{value});
            }
            case S7DataType::REAL:
            {
                float value = 0;
                std::memcpy(&value, bytes.data() + offset, sizeof(float));
                return Result<NormalDataType,RichError>(float{value});
            }
            case S7DataType::WORD:
            {
                uint16_t value = 0;
                std::memcpy(&value, bytes.data() + offset, sizeof(uint16_t));
                return Result<NormalDataType,RichError>(uint16_t{value});
            }
            case S7DataType::UDINT:
            {
                uint32_t value = 0;
                std::memcpy(&value, bytes.data() + offset, sizeof(uint32_t));
                return Result<NormalDataType, RichError>(uint32_t{value});
            }
            case S7DataType::DWORD:
            {
                uint32_t value = 0;
                std::memcpy(&value, bytes.data() + offset, sizeof(uint32_t));
                return Result<NormalDataType, RichError>(uint32_t{value});
            }
            case S7DataType::STRING:
            {
                std::string value(
                    reinterpret_cast<const char *>(&bytes[offset + 2]),
                    bytes[offset+1]);
                return Result<NormalDataType, RichError>(
                    NormalDataType{std::move(value)} // move 避免拷贝
                );
            }
            default:
                return Result<NormalDataType,RichError>(RichError("Unsupported data type"));
        }
    }

    static std::string transform_uint32_to_string(uint32_t value)
    {
         return std::to_string(value);
    }

    static std::string transform_uint32_to_hex_string(uint32_t value)
    {
        std::ostringstream oss;
        oss<<"0x"<<std::hex<<value;
        return oss.str();
    }


};

#endif