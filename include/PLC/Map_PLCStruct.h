#ifndef Single_Data_Block_H
#define Single_Data_Block_H 

#include "PLC/PLC.h"

struct Single_Data_Variable {
    std::string variable_name;
    std::string variable_full_path;
    S7DataType data_type_enum;
    int data_block_number = 0;
    int bytes_offset = 0;
    int array_size = 0;
    int bit_offset = 0;
    int string_length = 0;
};

class IGateway {
public:
    virtual ~IGateway() = default;
    virtual Result<Single_Data_Variable, RichError> readVariable(const std::string& variablePath) = 0;
    virtual Result<bool, RichError> writeVariable(const std::string& variablePath, const NormalDataType& value) = 0;
    virtual Result<std::vector<std::string>, RichError> getVariableList() = 0;
};

class Single_Data_Block : public IGateway {
public:
    Single_Data_Block()=default;
    Result<Single_Data_Variable,RichError> readVariable(const std::string& variablePath);
    Result<bool,RichError> writeVariable(const std::string& variablePath, const NormalDataType& value);
    Result<std::vector<std::string>,RichError> getVariableList();

    Result<bool,RichError> update_member_variable();
    Result<bool,RichError> add_datablock_from_DataBlockDefinition(DataBlockDefinition& DataBlockDefinition);
    Result<bool,RichError> add_variable_from_VariableDefinition(VariableDefinition& variable_definition,int data_block_number,const std::string& prefix = "");

private:
    std::unordered_map<std::string, Single_Data_Variable> m_variable_map;
    std::vector<uint8_t> m_data_block_buffer;
    int whole_data_block_length = 0;

};

class PLC_Device {
public:
    PLC_Device()=default;
    Result<bool,RichError> add_datablock_from_DataBlockDefinition(DataBlockDefinition& data_block_definition);
private:
    std::unordered_map<std::string, Single_Data_Block> m_data_block_map;
    std::string deviceID;
    std::string ipAdrress;
    PS7Client m_client;
};

class MultiPLCGateway {
public:
    MultiPLCGateway()=default;
private:
    std::unordered_map<std::string, PLC_Device> m_plc_device_map;
    Result<bool,RichError> add_device_from_provided(DataBlockDefinition& data_block_definition,const std::string& plc_name);
};

#endif