#ifndef GATEWAY_DATA_GENERATOR_H
#define GATEWAY_DATA_GENERATOR_H

#include <iostream>
#include <ostream>
#include <string>
#include "Rust_error_deal/error_deal.h"
#include "PLC/PLC.h"

class IGateway {
public:
    virtual ~IGateway() = default;
    virtual Result<NormalDataType, RichError> readVariable(const std::string& variablePath) = 0;
    virtual Result<bool, RichError> writeVariable(const std::string& variablePath, const NormalDataType& value) = 0;
    virtual std::vector<std::string> getVariableList() = 0;
};

class GatewayDataGenerator
{
public:
    GatewayDataGenerator() = default;
    ~GatewayDataGenerator() = default;

    Result<std::string,RichError> generateData_cpp_class(const DataBlockDefinition& data_block_definition);
    Result<bool,RichError> generate_accessor(std::ostringstream& s,const VariableDefinition& variable_definition,const std::string& prefix);
    void generate_bool_accessors(std::ostringstream& s,const VariableDefinition& data_block_definition,const std::string& prefix);
    void generate_byte_accessors(std::ostringstream& s,const VariableDefinition& data_block_definition,const std::string& prefix);
    void generate_dint_accessors(std::ostringstream& s,const VariableDefinition& data_block_definition,const std::string& prefix);
    void generate_int_accessors(std::ostringstream& s,const VariableDefinition& data_block_definition,const std::string& prefix);
    void generate_real_accessors(std::ostringstream& s,const VariableDefinition& data_block_definition,const std::string& prefix);
    void generate_string_accessors(std::ostringstream& s,const VariableDefinition& data_block_definition,const std::string& prefix);
    void generate_array_accessors(std::ostringstream& s,const VariableDefinition& data_block_definition,const std::string& prefix);
    void generate_struct_accessors(std::ostringstream& s,const VariableDefinition& data_block_definition,const std::string& prefix);

    Result<float,RichError> parse_s7_real(const byte* buffer);
    Result<bool,RichError> set_s7_real(byte* buffer,float value);
};

#endif