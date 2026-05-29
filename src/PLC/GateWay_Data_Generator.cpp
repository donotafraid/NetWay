#include "PLC/GateWay_Data_Generator.h"

Result<std::string,RichError> GatewayDataGenerator::generateData_cpp_class(const OPCUADataBlockDefinition& data_block_definition)
{ 
    std::ostringstream ss;

    ss<<"// auto generated class of gateway data -"<<data_block_definition.data_block_name<<std::endl;
    ss<<" class "<<data_block_definition.data_block_name<<"_GateWay {\n";
    ss<<"private:\n";
    ss<<"   PSnap7Client client_;\n";
    ss<<"   int db_number_ = "<<data_block_definition.block_number<<";\n\n";

    ss<<"public:\n";
    ss<<"   "<<data_block_definition.data_block_name<<"(PSnap7Client client)  : client(client) {}\n\n";

    //  generate read-write function for every variable 
    for (const auto& variable_definition: data_block_definition.variable_definitions_vector)
    {
        generate_accessor(ss,variable_definition,"");
    }

    ss<<"};\n";
    return Result<std::string,RichError>(ss.str());
}

Result<bool,RichError> GatewayDataGenerator::generate_accessor(std::ostringstream& ss,const S7XMLVariableDefinition& variable_definition,const std::string& prefix)
{ 
    std::string full_name = prefix.empty() ? variable_definition.variable_name : prefix + "." + variable_definition.variable_name;  //  distinguish between struct member and variable

    switch(variable_definition.data_type_enum)
    { 
        case S7DataType::BOOL:
            generate_bool_accessors(ss,variable_definition, full_name); 
            break;
        case S7DataType::WORD:
        case S7DataType::INT:
            generate_int_accessors(ss, variable_definition,full_name);
            break;
        case S7DataType::DINT:
            generate_dint_accessors(ss,variable_definition,full_name);
            break;
        case S7DataType::REAL:
            generate_real_accessors(ss,variable_definition,full_name);
            break;
        case S7DataType::STRING:
            generate_string_accessors(ss,variable_definition,full_name);
            break;
        case S7DataType::BYTE:
            generate_byte_accessors(ss,variable_definition,full_name);
            break;
        case S7DataType::ARRAY:
            generate_array_accessors(ss, variable_definition,full_name);
            break;
        case S7DataType::STRUCT:
            //  generate function for every struct member recursively
            for(const auto& member: variable_definition.struct_member_vector)
            {
                generate_accessor(ss,member,full_name);
            }
        default:
            return Result<bool,RichError>(RichError("unsupported data type"));
    }
    return Result<bool,RichError>(true);
}

void GatewayDataGenerator::generate_bool_accessors(std::ostringstream& ss,const S7XMLVariableDefinition& variable_definition,const std::string& full_name)
{ 
        ss<<"   bool get_"<<full_name<<"() {\n";
        ss<<"       byte buffer[1];\n";
        ss<<"       client_->DBRead (db_number_, "<<variable_definition.bytes_offset<< ", 1, buffer);\n";    //  read one byte
        ss<<"       return (buffer[0] & 0x01) != 0;\n"; //  check NO.0 bit
        ss<<"   }\n\n";

        ss<<"   void set_"<<full_name<<"(bool value) {\n";
        ss<<"       byte buffer[1] = {value ? 0x01 : 0x00};\n"; //  set NO.0 bit
        ss<<"       client_->DBWrite (db_number_, "<<variable_definition.bytes_offset<< ", 1, buffer);\n";
        ss<<"   }\n\n";
}

void GatewayDataGenerator::generate_int_accessors(std::ostringstream& ss,const S7XMLVariableDefinition& variable_definition,const std::string& full_name)
{ 
    ss<<"   int get_"<<full_name<<"() {\n";
    ss<<"       byte buffer[2];\n";
    ss<<"       client_->DBRead (db_number_, "<<variable_definition.bytes_offset<< ", 2, buffer);\n";
    ss<<"       return (buffer[0]<<8) | buffer[1];\n";
    ss<<"   }\n\n";

    ss<<"   void set_"<<full_name<<"(int value) {\n";
    ss<<"       byte buffer[2] = {\n";
    ss<<"          static_cast<byte>((value >> 8) & 0xFF),\n"; 
    ss<<"          static_cast<byte>(value & 0xFF)\n";
    ss<<"       }\n";
    ss<<"       client_->DBWrite (db_number_, "<<variable_definition.bytes_offset<< ", 2, buffer);\n";
    ss<<"  }\n\n";
}

void GatewayDataGenerator::generate_dint_accessors(std::ostringstream& ss,const S7XMLVariableDefinition& variable_definition,const std::string& full_name)
{ 
    ss<<"   int32_t get_"<<full_name<<"() {\n";
    ss<<"       byte buffer[4];\n";
    ss<<"       client_->DBRead (db_number_, "<<variable_definition.bytes_offset<< ", 4, buffer);\n";
    ss<<"       return (buffer[0]<<24) | (buffer[1]<<16) | (buffer[2]<<8) | buffer[3];\n";
    ss<<"  }\n\n";

    ss<<"   void set_"<<full_name<<"(int32_t value) {\n";
    ss<<"       byte buffer[4] = {\n";
    ss<<"          static_cast<byte>((value >> 24) & 0xFF),";
    ss<<"          static_cast<byte>((value >> 16) & 0xFF),";
    ss<<"          static_cast<byte>((value >> 8) & 0xFF),";
    ss<<"          static_cast<byte>(value & 0xFF)\n";
    ss<<"       }\n";
    ss<<"       client_->DBWrite (db_number_, "<<variable_definition.bytes_offset<< ", 4, buffer);\n";
    ss<<"  }\n\n";
}

void GatewayDataGenerator::generate_real_accessors(std::ostringstream& ss,const S7XMLVariableDefinition& variable_definition,const std::string& full_name)
{ 
    ss<<"   float get_"<<full_name<<"() {\n";
    ss<<"       byte buffer[4];\n";
    ss<<"       client_->DBRead (db_number_, "<<variable_definition.bytes_offset<< ", 4, buffer);\n";
    ss<<"       return parse_s7_real(buffer).unwrap_returnLeftValue();\n";
    ss<<"  }\n\n";

    ss<<"  void set_"<<full_name<<"(float value) {\n";
    ss<<"       byte buffer[4];\n";
    ss<<"       set_s7_real(buffer,value);\n";
    ss<<"      client_->DBWrite (db_number_, "<<variable_definition.bytes_offset<< ", 4, buffer);\n";
    ss<<"  }\n\n";
}

void GatewayDataGenerator::generate_string_accessors(std::ostringstream& ss,const S7XMLVariableDefinition& variable_definition,const std::string& full_name)
{ 
    ss<<"   std::string get_"<<full_name<<"() {\n";
    ss<<"       byte buffer["<<variable_definition.string_length + 2<<"];\n";
    ss<<"       client_->DBRead (db_number_, "<<variable_definition.bytes_offset<< ", "<<variable_definition.string_length + 2<<",buffer);\n";
    ss<<"       // S7字符串格式:[字符串的最大长度][当前字符串的长度][字符串的实际数据](e.g string[20]只是代表实际有效数据的长度)\n";
    ss<<"       int actual_string_length = buffer[1];\n";
    ss<<"        return std::string(reinterpret_cast<char*>(buffer + 2),actual_string_length);\n";
    ss<<"  }\n\n";

    ss<<"   void set_"<<full_name<<"(std::string value) {\n";
    ss<<"   byte buffer["<<variable_definition.string_length + 2<<"] = {0};";
    ss<<"   // buffer[0] 代表储存了该字符串的传输数据最大长度(e.g string[20]),buffer[1] 代表了当前字符串的长度,该长度可能在运行中进行变化\n";
    ss<<"   buffer[0] = static_cast<byte>(variable_definition.string_length);\n";
    ss<<"   buffer[1] = static_cast<byte>(std::min(value.size(),size_t ("<<variable_definition.string_length<<") ));\n";
    ss<<"   memcpy(buffer + 2, value.c_str(), buffer[1]);\n";
    ss<<"  client_->DBWrite (db_number_, "<<variable_definition.bytes_offset<< ", "<<variable_definition.string_length + 2<<",buffer);\n";
    ss<<"  }\n\n";
}

void GatewayDataGenerator::generate_byte_accessors(std::ostringstream& ss,const S7XMLVariableDefinition& variable_definition,const std::string& full_name)
{ 
    ss<<"   byte get_"<<full_name<<"() {\n";
    ss<<"       byte buffer[1];\n";
    ss<<"       client_->DBRead (db_number_, "<<variable_definition.bytes_offset<< ", 1, buffer);\n";
    ss<<"       return buffer[0];\n";
    ss<<"  }\n\n";

    ss<<"   void set_"<<full_name<<"(byte value) {\n";
    ss<<"       byte buffer[1] = {value};\n";
    ss<<"       client_->DBWrite (db_number_, "<<variable_definition.bytes_offset<< ", 1, buffer);\n";
    ss<<"  }\n\n";
}

void GatewayDataGenerator::generate_array_accessors(std::ostringstream& ss,const S7XMLVariableDefinition& variable_definition,const std::string& full_name)
{ 
    ss<<"   //数组访问器 - "<< variable_definition.variable_name <<"["<<variable_definition.array_size<<"]\n";
    if (variable_definition.data_type_enum == S7DataType::REAL)
    {
        for(int i = 0; i < variable_definition.array_size; i++)
        {
            ss<<"   float get_"<<full_name<<"() {\n";
            ss<<"       byte buffer[4];\n";
            ss<<"       client_->DBRead (db_number_, "<<variable_definition.bytes_offset + i*4 << ", 4, buffer);\n";
            ss<<"       return parse_s7_real(buffer).unwrap_returnLeftValue();\n";
            ss<<"  }\n\n";
            
            ss<<"  void set_"<<full_name<<"(,float value) {\n";
            ss<<"       byte buffer[4];\n";
            ss<<"       set_s7_real(buffer,value);\n";
            ss<<"      client_->DBWrite (db_number_, "<<variable_definition.bytes_offset + i*4 << " , 4, buffer);\n";
            ss<<"  }\n\n";
        }
    }
}

Result<float,RichError> GatewayDataGenerator::parse_s7_real(const byte* buffer)
{ 
    auto value = (buffer[0]<<24) | (buffer[1]<<16) | (buffer[2]<<8) | buffer[3];
    return Result<float,RichError>(static_cast<float>(value)); 
}

Result<bool,RichError> GatewayDataGenerator::set_s7_real(byte* buffer,float value)
{ 
    auto int_value = static_cast<int>(value);
    buffer[0] = static_cast<byte>((int_value >> 24) & 0xFF);
    buffer[1] = static_cast<byte>((int_value >> 16) & 0xFF);
    buffer[2] = static_cast<byte>((int_value >> 8)& 0xFF);
    buffer[3] = static_cast<byte>(int_value & 0xFF);
    return Result<bool,RichError>(true);
}
