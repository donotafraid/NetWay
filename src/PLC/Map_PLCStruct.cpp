#include "PLC/Map_PLCStruct.h"

Result<bool, RichError> Single_Data_Block::add_datablock_from_DataBlockDefinition(DataBlockDefinition& data_block_definition)
{
    // Add variables of basic types through loop checking 
    bool is_done_successfully = true;
    for(auto& var : data_block_definition.variable_definitions_vector)
    {
        is_done_successfully = add_variable_from_VariableDefinition(var,data_block_definition.block_number,data_block_definition.data_block_name).is_success() && is_done_successfully;
    }
    this->whole_data_block_length += data_block_definition.total_bytes_size;
    if(is_done_successfully)
    {
        return Result<bool, RichError>(is_done_successfully);
    }
    else{
        return Result<bool, RichError>(RichError("add variable failed"));
    }
}

Result<bool, RichError> Single_Data_Block::add_variable_from_VariableDefinition( VariableDefinition& variable_definition,int data_block_number,const std::string& prefix)
{
    // Add variables of basic types through loop checking 
    bool is_done_successfully = false;
    {
        switch(variable_definition.data_type_enum)
        {
            case S7DataType::ARRAY:
                for(auto& var : variable_definition.struct_member_vector)
                    is_done_successfully =  add_variable_from_VariableDefinition(var,data_block_number,prefix + variable_definition.variable_name).is_success() && is_done_successfully;
                    break;
            case S7DataType::STRUCT:
                for(auto& var : variable_definition.struct_member_vector)
                    is_done_successfully =  add_variable_from_VariableDefinition(var,data_block_number,prefix + variable_definition.variable_name).is_success() && is_done_successfully;
                break;
            default:
                Single_Data_Variable tmp_variable;
                tmp_variable.variable_full_path = prefix + "." + variable_definition.variable_name; 
                tmp_variable.variable_name = variable_definition.variable_name;
                tmp_variable.data_type_enum = variable_definition.data_type_enum;
                tmp_variable.data_block_number = data_block_number;

                tmp_variable.bytes_offset = variable_definition.bytes_offset;
                tmp_variable.array_size = variable_definition.array_size;
                tmp_variable.bit_offset = variable_definition.bit_offset;
                tmp_variable.string_length = variable_definition.string_length;
                
                m_variable_map[tmp_variable.variable_full_path] = std::move(tmp_variable);
                is_done_successfully = true;
        }
    }
  
    return Result<bool, RichError>(is_done_successfully);
}

Result<Single_Data_Variable, RichError> Single_Data_Block::readVariable(const std::string& variablePath)
{
    if(m_variable_map.find(variablePath) != m_variable_map.end())
    {
        return Result<Single_Data_Variable, RichError>(m_variable_map[variablePath]);
    }
    else {
        return Result<Single_Data_Variable, RichError>(RichError("variable not found by variablePath"));
    }
}

Result<bool,RichError> Single_Data_Block::writeVariable(const std::string& variablePath, const NormalDataType& value)
{
    if(m_variable_map.find(variablePath) != m_variable_map.end())
    {
        switch(m_variable_map[variablePath].data_type_enum)
        {
            case S7DataType::BOOL:
                if(std::get<bool>(value))
                {
                    m_data_block_buffer[m_variable_map[variablePath].bytes_offset] |= 1 << m_variable_map[variablePath].bit_offset;
                }
                else {
                    m_data_block_buffer[m_variable_map[variablePath].bytes_offset] &= ~(1 << m_variable_map[variablePath].bit_offset);
                }
                break;
            case S7DataType::BYTE:
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset] = std::get<uint8_t>(value);
                break;
            case S7DataType::INT:
            case S7DataType::WORD:
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset] = (std::get<uint16_t>(value) >> 8)& 0xFF;
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+1] = std::get<uint16_t>(value) & 0xFF;
                break;
            case S7DataType::DINT:
            case S7DataType::REAL:
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset] = (std::get<uint16_t>(value) >> 24)& 0xFF;
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+1] = (std::get<uint16_t>(value) >> 16)& 0xFF;
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+2] = (std::get<uint16_t>(value) >> 8)& 0xFF;
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+3] = (std::get<uint16_t>(value))& 0xFF;
                break;
            case S7DataType::STRING:
                if(std::get<std::string>(value).size() < m_variable_map[variablePath].string_length - 2)
                {
                    m_data_block_buffer[m_variable_map[variablePath].bytes_offset] = m_variable_map[variablePath].string_length;
                    m_data_block_buffer[m_variable_map[variablePath].bytes_offset+1] = std::get<std::string>(value).size();
                    memcpy( &m_data_block_buffer[m_variable_map[variablePath].bytes_offset+2], std::get<std::string>(value).c_str(),std::get<std::string>(value).size());
                }
                else {
                    return Result<bool, RichError>(RichError("string length is too long"));
                }
                break;
            default:
                return Result<bool, RichError>(RichError("variable type not supported"));
        }
        return Result<bool, RichError>(true);
    }
    else {
        return Result<bool, RichError>(RichError("variable not found by variablePath"));
    }
}

Result<std::vector<std::string>,RichError> Single_Data_Block::getVariableList()
{
    std::vector<std::string> variable_list;
    for(auto& var : m_variable_map)
    {
        variable_list.push_back(var.first);
    }
    return Result<std::vector<std::string>, RichError>(variable_list);
}
