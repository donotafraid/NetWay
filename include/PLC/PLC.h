#ifndef PLC_H
#define PLC_H

#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>
#include <regex>
#include <snap7.h>
#include "Rust_error_deal/error_deal.h"
#include <dlfcn.h>

// 通用数据值类型 - 支持所有PLC数据类型
using NormalDataType = std::variant<
    bool,                    // BOOL
    uint8_t,                 // BYTE
    int16_t,                 // INT
    uint16_t,                // WORD  
    int32_t,                 // DINT
    float,                   // REAL
    double,                  // LREAL
    std::string              // STRING
>;

enum class S7DataType
{
    BOOL,
    BYTE,
    INT,
    WORD,
    DINT,
    REAL,
    STRING,
    ARRAY,
    STRUCT,
    UNKNOWN 
};

struct VariableDefinition
{
    std::string variable_name = "";
    S7DataType data_type_enum ;
    std::string comment;
    S7DataType array_type_enum ;
    int array_size = -1;
    int string_length = -1;

    int bytes_offset = -1;
    int bit_offset = -1; //  in PLC-S7 ， bool variable saved in a bytes , SO THERE IS 8 BOOL VAIRABLE used for A BTYES VARIABLE AT MOST.  bool variable position(0~7)

    std::vector<VariableDefinition> struct_member_vector;  //  used for Type:struct member variable 
};


struct DataBlockDefinition
{
    std::string data_block_name;
    int block_number = 0;
    bool optimized_access = false;
    int total_bytes_size = 0;
    std::vector<VariableDefinition> variable_definitions_vector;   //  used for file member variable and struct member variable
};


class SCL_Parser
{
    public:
    SCL_Parser(std::string& parse_file_path):file_path(parse_file_path){};
    ~SCL_Parser() = default;
    
    Result<std::string,RichError> read_file_content(const std::string& file_path);
    Result<DataBlockDefinition,RichError> parse(const std::string& file_path);
    Result<DataBlockDefinition,RichError> parse_data_block_header( std::string& file_content);
    Result<DataBlockDefinition,RichError> parse_variable( DataBlockDefinition& data_block_definition);
    Result<DataBlockDefinition,RichError> extract_variable_from_content(std::istringstream& file_content,std::string& line , DataBlockDefinition& data_block_definition );
    Result<VariableDefinition,RichError> parse_variable_declaration( std::string& line);
    Result<bool,RichError> parse_data_type( std::string& str,VariableDefinition& variable_definition);
    Result<DataBlockDefinition,RichError> calculate_data_block_size(DataBlockDefinition& data_block_definition);
    Result<bool,RichError> calculate_variable_offset(VariableDefinition& var,int& current_byte_offset,int& current_bit_quality,int& last_underfill_byte_offset);

    //  Remove leading and trailing whitespace characters from a string
    Result<std::string,RichError> trim(const std::string& str);
    Result<std::string,RichError>  extract_variable_name(const std::string& str);
    Result<std::string,RichError> extract_variable_comment(const std::string& str);
    Result<bool,RichError> add_array_member(VariableDefinition& var);
    Result<S7DataType, RichError> transform_string_to_S7DataType(const std::string& str);
      
    private:
    std::string m_file_content;
    std::string& file_path;
    size_t pos;


};

#endif