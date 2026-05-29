#include "PLC/Map_PLCStruct.h"

template<>
void OPCUA_Access::Covert_UA_Scalar_To_Specific(std::string &SourceData_var,int i)
{
   const UA_String *src =
            static_cast<const UA_String *>(m_read_variant.data) + i;
        if (src && src->length>0) {
          SourceData_var.assign(reinterpret_cast<const char*>((src->data)), src->length);
        } else {
          SourceData_var = "";
    }
}

template<>
void OPCUA_Access::ByteDeserialization_memcpy(std::string &dest,const std::vector<uint8_t> &src,int data_offset,int data_length)
{
  // 边界检查（强烈建议加上）
  if (data_offset < 0 || data_length < 0 ||
      static_cast<size_t>(data_offset + data_length) > src.size()) {
    throw std::out_of_range("ByteDeserialization_memcpy: invalid range");
  }

  dest.assign(reinterpret_cast<const char *>(src.data() + data_offset + 2),
              data_length - 2);
}

Single_Data_Block::Single_Data_Block(){
}

Single_Data_Block::~Single_Data_Block()
{
    for(auto &it : m_variable_map)
    {
        if(it.second.data_pointer)
        {
            delete it.second.data_pointer;
        }
    }
}

Result<bool, RichError> Single_Data_Block::resize_data_block_buffer(const bool is_last_success)
{ 
    if (is_last_success)
    {
        this->m_data_block_buffer.reserve(this->whole_data_block_length);
        this->m_data_block_buffer.resize(this->whole_data_block_length);
        return Result<bool, RichError>(true);
    }
    else{
        return Result<bool, RichError>(RichError("resize failed because of last_success is false"));
    }
}

Result<bool, RichError> Single_Data_Block::add_datablock_from_OPCUADataBlockDefinition(OPCUADataBlockDefinition& data_block_definition)
{
    // Add variables of basic types through loop checking 
    bool is_done_successfully = true;
    for(auto& var : data_block_definition.variable_definitions_vector)
    {
        is_done_successfully = add_variable_from_S7XMLVariableDefinition(var,data_block_definition.block_number,"\"" + data_block_definition.data_block_name + "\"").is_success() && is_done_successfully;
    }
    this->whole_data_block_length += data_block_definition.total_bytes_size;
    this->m_data_block_buffer.resize(this->whole_data_block_length); 
    this->data_block_name = data_block_definition.data_block_name;
    if(is_done_successfully)
    {
        return Result<bool, RichError>(true);
    }
    else{
        return Result<bool, RichError>(RichError("add variable failed"));
    }
}

Result<bool, RichError> Single_Data_Block::add_variable_from_S7XMLVariableDefinition( S7XMLVariableDefinition& variable_definition,int data_block_number,const std::string& prefix)
{
    // Add variables of basic types through loop checking 
    bool is_done_successfully = false;
    {
        switch(variable_definition.data_type_enum)
        {
            case S7DataType::ARRAY:
                for(auto& var : variable_definition.struct_member_vector)
                    is_done_successfully =  add_variable_from_S7XMLVariableDefinition(var,data_block_number,prefix + ".\"" + variable_definition.variable_name + "\"").is_success() && is_done_successfully;
                    break;
            case S7DataType::STRUCT:
                for(auto& var : variable_definition.struct_member_vector)
                    is_done_successfully =  add_variable_from_S7XMLVariableDefinition(var,data_block_number,prefix + ".\"" +variable_definition.variable_name + "\"").is_success() && is_done_successfully;
                break;
                default:
                S7ModernDataStruct tmp_variable;
                if(!isNumber(variable_definition.variable_name).unwrap_returnRightValue())
                {
                    //  NON INTEGER FOR NORMAL SUFFIX
                    tmp_variable.variable_full_path =  prefix + ".\"" + variable_definition.variable_name + "\""; 
                    tmp_variable.variable_nodeID = tmp_variable.variable_full_path;
                }
                else
                {
                    //  INTERGER FOR SPECIAL SUFFIX
                    tmp_variable.variable_full_path =  prefix + "[" + variable_definition.variable_name + "]"; 
                    tmp_variable.variable_nodeID = prefix;
                }
                tmp_variable.variable_name = variable_definition.variable_name;
                tmp_variable.data_type_enum = variable_definition.data_type_enum;
                tmp_variable.data_block_number = data_block_number;

                tmp_variable.bytes_offset = variable_definition.bytes_offset;
                tmp_variable.bit_offset = variable_definition.bit_offset;
                tmp_variable.s7_data_type_length = variable_definition.s7_data_type_length; 
                tmp_variable.s7_data_array_length = variable_definition.s7_data_array_length;
                tmp_variable.data_pointer = new Dynamic_Value("Success");
                
                m_variable_map[tmp_variable.variable_full_path] = std::move(tmp_variable);
                is_done_successfully = true;
        }
    }
  
    return Result<bool, RichError>(is_done_successfully);
}

Result<bool,RichError> Single_Data_Block::isNumber(const std::string& s)
{
    if (s.empty()) return false;
    
    char* end = nullptr;
    // 使用 strtod 而不是 atof，因为 atof 无法检测错误
    strtod(s.c_str(), &end);
    
    // end 指向第一个未转换的字符
    return Result<bool,RichError> (end == s.c_str() + s.length());
}

Result<bool,RichError> Single_Data_Block::isArray(S7ModernDataStruct &data_var)
{
  size_t pos = data_var.variable_full_path.find_last_of('[');
  // IF FIND THE SYMBOL  '['
  if (pos != std::string::npos) {
    // CHECK WHETHER EXIST ']'
    if (data_var.variable_full_path.find(']', pos) != std::string::npos) {
      return Result<bool, RichError>(true);
    }
  }
    // IF DO NOT FIND THE SYMBOL '['
    return Result<bool, RichError>(RichError("the element is Scalar"));
}

Result<bool, RichError> Single_Data_Block::ReadS7DataBlock_FromPLC(S7Object& m_client_object)
{
    bool success = true;
    Result<bool, RichError> write_result (success);
    m_data_block_buffer.clear();
    m_data_block_buffer.resize(whole_data_block_length);
    for(auto& var : m_variable_map)
    {
        tmp_buffer.clear();
        tmp_buffer.resize(var.second.s7_data_type_length);
        {
            int result = Cli_DBRead(m_client_object,1, var.second.bytes_offset, var.second.s7_data_type_length, tmp_buffer.data());
            if (result != 0)
            {
                char error_text[256];
                Cli_ErrorText(result, error_text, sizeof(error_text));
                std::cout<<"Read failed: " << error_text << " (Error code: " << result << ")\n";
                success = false;
            }
            else {
                std::cout<<"Read successful\n";
                success = true ;
                write_result = adjust_BigEndian_to_LittleEndian(var.second.variable_full_path);
                success = success && write_result.is_success();
            }
        }
    }
    if(success)
    {
        return Result<bool, RichError>(true);
    }
    else{
        return Result<bool, RichError>(RichError("read variable failed"));
    }
}

Result<bool, RichError> Single_Data_Block::ReadOPCUADataBlock_FromPLC(OPCUA_Access *OPCUA_pointer)
{
    bool success = true;
   
    //  CLEAR ELEMEMT EXISTED BEFORE
    OPCUA_pointer->Clear_HasRead_var_set();
    for(auto& var : m_variable_map)
    {
        if(Set_NodeID_And_read(OPCUA_pointer, var.second).is_success())
        {
            success = true;
        }
        else
        {
            success = false;
        }
           
    }
    if(success)
    {
        return Result<bool, RichError>(true);
    }
    else{
        return Result<bool, RichError>(RichError("read variable failed"));
    }
}

Result<bool,RichError> Single_Data_Block::Set_NodeID_And_read(OPCUA_Access *OPCUA_pointer,S7ModernDataStruct& var)
{
   if(!OPCUA_pointer->m_hasRead_var_set.count(var.variable_nodeID)) 
   {
        //  THE ARRAY DO NOT CHECK 
        OPCUA_pointer->Set_Read_NodeID(var);
        auto read_result = (OPCUA_pointer->Read_UA_Variant_From_PLC(var));
        if(read_result.is_success())
        {
          if (isArray(var).is_success()) {
            return Result<bool, RichError>(
                OPCUA_pointer->Set_Read_UA_Array(this->m_variable_map, var));
          } else {
            return Result<bool, RichError>(
                OPCUA_pointer->Set_UA_To_Read_Normal_Scalar(
                    var.data_type_enum, *var.data_pointer, 0));
          }
        }
        else
        {
            OPCUA_pointer->Clear_Read_Respondse();
            return Result<bool,RichError> (RichError("Read_UA_Variant_From_PLC : fail"));
        }
   }
    return Result<bool,RichError> (true);
}

Result<bool,RichError> Single_Data_Block::adjust_BigEndian_to_LittleEndian(const std::string& variablePath)
{
    if(m_variable_map.find(variablePath) != m_variable_map.end())
    {
        switch(m_variable_map[variablePath].data_type_enum)
        {

            case S7DataType::BOOL:
            {
                if(tmp_buffer[0]&(1 << m_variable_map[variablePath].bit_offset))
                {
                    m_data_block_buffer[m_variable_map[variablePath].bytes_offset] |= 1 << m_variable_map[variablePath].bit_offset;
                }
                else {
                    m_data_block_buffer[m_variable_map[variablePath].bytes_offset] &= ~(1 << m_variable_map[variablePath].bit_offset);
                }
                break;
            }
            case S7DataType::BYTE:
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset] = tmp_buffer[0];
                break;
            case S7DataType::INT:
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+1] = tmp_buffer[0];
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset] = tmp_buffer[1];
                break;
            case S7DataType::WORD:
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+1] = tmp_buffer[0];
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset] = tmp_buffer[1];
                break;
            case S7DataType::DWORD:
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+3] = tmp_buffer[0];
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+2] = tmp_buffer[1];
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+1] = tmp_buffer[2];
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+0] = tmp_buffer[3];
                break;
            case S7DataType::UDINT:
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+3] = tmp_buffer[0];
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+2] = tmp_buffer[1];
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+1] = tmp_buffer[2];
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+0] = tmp_buffer[3];
                break;
            case S7DataType::DINT:
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+3] = tmp_buffer[0];
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+2] = tmp_buffer[1];
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+1] = tmp_buffer[2];
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+0] = tmp_buffer[3];
                break;
            case S7DataType::REAL:
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+3] = tmp_buffer[0];
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+2] = tmp_buffer[1];
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+1] = tmp_buffer[2];
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+0] = tmp_buffer[3];
                break;
            case S7DataType::STRING:
            {
                int effective_string_length = std::min(tmp_buffer[0],tmp_buffer[1]);
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset] = tmp_buffer[0];
                m_data_block_buffer[m_variable_map[variablePath].bytes_offset+1] = tmp_buffer[1];
                memcpy( &m_data_block_buffer[m_variable_map[variablePath].bytes_offset+2], &tmp_buffer[2],effective_string_length);
                break;
            }
            default:
                return Result<bool, RichError>(RichError("variable not found by variablePath"));
        }
        return Result<bool, RichError>(true);
    }
    else {
        return Result<bool, RichError>(RichError("variable not found by variablePath"));
    }
}

Result<bool,RichError> Single_Data_Block::SendBuffer_To_PLC_Offset(S7_Access *m_client_object)
{
    bool success = true;
    std::vector<uint8_t> tmp_data;
    tmp_data.reserve(whole_data_block_length);
    for(auto& var : m_variable_map)
    {
        {
            tmp_data.clear();
            tmp_data.resize(var.second.s7_data_type_length);
            {
                std::move(m_data_block_buffer.begin()+var.second.bytes_offset,
                m_data_block_buffer.begin()+var.second.bytes_offset+var.second.s7_data_type_length,
                tmp_data.begin());
            }
            
            auto result = m_client_object->write(1, var.second.bytes_offset, var.second.s7_data_type_length, tmp_data.data()); 
            // int result = Cli_DBWrite(m_client_object,1, var.second.bytes_offset, var.second.s7_data_type_length, tmp_data.data());
            if (result.is_fail())
            {
                return Result<bool,RichError> (result);
            }
            else{
                success = true;
                std::cout<<"Send successful\n";
            }
        }
    }
    if(success)
    {
        return Result<bool, RichError>(true);
    }
    else{
        return Result<bool, RichError>(RichError("read variable failed"));
    }
}

Result<bool, RichError>
Single_Data_Block::SendBuffer_To_PLC_OPCUA(OPCUA_Access *m_client_object) {
  m_hasWrite_OPCUA_map.clear();
  for (auto &var : m_variable_map) {
    std::string array_member_full_path =
        var.second.variable_nodeID + "[" + std::to_string(0) + "]";
    //  IF THE ELEMENT IS NOT NEW ARRAY ELEMENT OR IS [1,2,....] ARRAY ELEMENT, SKIP IT
    if (m_hasWrite_OPCUA_map.count(var.second.variable_nodeID) ||
        var.second.variable_full_path.find('[') != std::string::npos &&
            var.second.variable_full_path != array_member_full_path) {
      continue;
    }
    //  MATCH ELEMENT OF ARRAY IN VAR_MAP
    auto it = m_variable_map.find(array_member_full_path);
    if (it != m_variable_map.end()) {
      //  MEAN THE ELEMENT IS ARRAY ELEMENT
      m_hasWrite_OPCUA_map.emplace(var.second.variable_nodeID);
      auto result = m_client_object->Set_Normal_To_Write_UA_Vector(
          var.second, m_data_block_buffer);
      if (result.is_fail()) {
        return Result<bool, RichError>(result);
      }
    } else {
      //  MEAN THE ELEMENT IS SCALAR ELEMENT
      m_hasWrite_OPCUA_map.emplace(var.second.variable_nodeID);
      auto result = m_client_object->Set_Normal_To_Write_UA_Scalar(
          var.second, m_data_block_buffer);
      if (result.is_fail()) {
        return Result<bool, RichError>(result);
      }
    }
  }
  return Result<bool, RichError>(true);
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

Result<bool,RichError> Single_Data_Block::display_variable_list()
{
    for(auto& var : m_variable_map)
    {
        auto result = DataTypeMapper::Data_transform_from_bytes(var.second.data_type_enum,m_data_block_buffer,var.second.bytes_offset,var.second.s7_data_type_length,var.second.bit_offset);
        if(result.is_fail())
        {
            return Result<bool, RichError>(RichError("display variable list failed"));
        }
        else
        {
            if(var.second.data_type_enum == S7DataType::BOOL)
            {
                std::cout<<"Variable_full_path: "<<var.first<<",Variable_value: "<<std::get<bool>(result.unwrap_returnLeftValue())<<std::endl;
            }
            else if(var.second.data_type_enum == S7DataType::BYTE)
            {
                std::cout << "Variable_full_path: " << var.first 
                << ", Variable_value: " << static_cast<int>(std::get<uint8_t>(result.unwrap_returnLeftValue())) 
                << " (0x" << std::hex << static_cast<int>((std::get<uint8_t>(result.unwrap_returnLeftValue()))) << std::dec << ")" 
                << std::endl;
            }
            else if(var.second.data_type_enum == S7DataType::INT)
            {
                std::cout<<"Variable_full_path: "<<var.first<<",Variable_value: "<<std::get<int16_t>(result.unwrap_returnLeftValue())<<std::endl;
            }
            else if(var.second.data_type_enum == S7DataType::DINT)
            {
                std::cout<<"Variable_full_path: "<<var.first<<",Variable_value: "<<std::get<int32_t>(result.unwrap_returnLeftValue())<<std::endl;
            }
            else if(var.second.data_type_enum == S7DataType::REAL)
            {
                std::cout<<"Variable_full_path: "<<var.first<<",Variable_value: "<<std::get<float>(result.unwrap_returnLeftValue())<<std::endl;
            }
            else if(var.second.data_type_enum == S7DataType::WORD)
            {
                std::cout<<"Variable_full_path: "<<var.first<<",Variable_value: "<<std::get<uint16_t>(result.unwrap_returnLeftValue())<<std::endl;
            }
            else if(var.second.data_type_enum == S7DataType::DWORD)
            {
                std::cout<<"Variable_full_path: "<<var.first<<",Variable_value: "<<std::get<uint16_t>(result.unwrap_returnLeftValue())<<std::endl;
            }
            else if(var.second.data_type_enum == S7DataType::UDINT)
            {
                std::cout<<"Variable_full_path: "<<var.first<<",Variable_value: "<<std::get<uint16_t>(result.unwrap_returnLeftValue())<<std::endl;
            }
            else if(var.second.data_type_enum == S7DataType::STRING)
            {
                std::cout<<"Variable_full_path: "<<var.first<<",Variable_value: "<<std::get<std::string>(result.unwrap_returnLeftValue())<<std::endl;
            }
        }
    }
    return Result<bool, RichError>(true);
}

Result<std::string, RichError> Single_Data_Block::return_DataBlockName()
{
    return Result<std::string, RichError>(data_block_name);
}

Result<bool,RichError> PLC_Device::parse_dataBlock_To_device(std::string &source_file_content)
{
    Single_Data_Block *single_data_block = new Single_Data_Block ();

    auto parse_result =
        m_scl_parser.parse(source_file_content)
            .and_then([single_data_block](
                          OPCUADataBlockDefinition &data_block_definition) {
              return single_data_block->add_datablock_from_OPCUADataBlockDefinition(
                  data_block_definition);
            })
            .and_then([&single_data_block](const bool is_last_success) {
              return single_data_block->resize_data_block_buffer(
                  is_last_success);
            });

    if (parse_result.is_success()) {
      DataStructeEditor *dataStruct =
          new DataStructeEditor(nullptr, single_data_block);
      std::cout << "Gateway Data Generator is done." << std::endl;
      // this->add_datablock(std::move(single_data_block));
      this->m_dataStructEditor_map.emplace(std::make_pair(
          single_data_block->return_DataBlockName().unwrap_returnRightValue(),
          dataStruct));
    } else {
      std::cout << "Gateway Data Generator is failed." << std::endl;
      delete single_data_block;
      return Result<bool, RichError>(parse_result);
    }
    is_exist_data_block = true;
    return Result<bool,RichError> (true);
}

Result<bool, RichError> PLC_Device::connect() {
  if (m_S7_Access) {
    return m_S7_Access->connect();
  } else {
    return m_UA_Access->connect();
  }
}

void PLC_Device::disconnect()
{
  if (m_S7_Access) {
     m_S7_Access->disconnect();
  } else {
     m_UA_Access->disconnect();
  }
}

PLC_Device::~PLC_Device()
{
    if(m_S7_Access)
    {
        m_S7_Access->disconnect();
        delete m_S7_Access;
    }
    else if(m_UA_Access)
    {
        m_UA_Access->disconnect();
        delete m_UA_Access;
    }
    for(auto &it: m_dataStructEditor_map)
    {
        if(it.second == nullptr)
        {
            continue;
        }
        else
        {
            delete it.second;
        }
    }
    std::cout<<"PLC_Device destroyed\n";
}

Result<std::string,RichError> PLC_Device::return_input_ipAddress()
{
    if(m_ip_Address != "")
    {
        return Result<std::string,RichError> (m_ip_Address);
    }
    else
    {

        return Result<std::string,RichError> (RichError("ip_Address is empty"));
    }
}

Result<bool, RichError>
PLC_Device::is_configFile_existInMap(const QString &file_path) {
  if (this->m_dataStructEditor_map.find(file_path.toStdString()) ==
      this->m_dataStructEditor_map.end()) {
    std::cout << "select DB file is not exist" << std::endl;
    return Result<bool, RichError>(RichError("(select DB file is not exist"));
  } else {
    return Result<bool,RichError> (true);
  }
}

Result<DataStructeEditor *,RichError> PLC_Device::find_related_dataStruct(const std::string &file_path )
{
    for(auto &it : m_dataStructEditor_map)
    {
        if(it.first != file_path)
        {
            continue;
        }
        else
        {
            return Result<DataStructeEditor *,RichError> (it.second);
        }
    }
    return Result<DataStructeEditor *,RichError> (RichError("do not find related dataStruct"));
}

const std::unordered_map<std::string, DataStructeEditor*>& PLC_Device::getEditors() const {
    return m_dataStructEditor_map;
}

bool PLC_Device::parse_file_content( std::string &source_file_content, const std::string &source_file_path)
{
    auto result = m_scl_parser.read_file_content(source_file_path);
    if(result.is_fail())
    {
        source_file_content = "";
        return false; 
    }
    else
    {
        source_file_content = std::move(result.unwrap_returnLeftValue());
        return true;
    }
}

void PLC_Device::add_subTreeWidgetItem(const QString &file_path ,  QTreeWidgetItem *item)
{
    this->m_subTreeWidget_map.emplace(std::make_pair(file_path.toStdString(), item));
}

void PLC_Device::delete_subTreeWidgetItem(const QString &file_path)
{
  std::string key = file_path.toStdString();
  auto it = m_subTreeWidget_map.find(key);
  {
    if (it != m_subTreeWidget_map.end()) {
      delete it->second;
      m_subTreeWidget_map.erase(it);
    }
  }
}

void PLC_Device::delete_subConfig(const QString &file_path)
{
  std::string key = file_path.toStdString();
  auto it = m_dataStructEditor_map.find(key);
  {
    if (it != m_dataStructEditor_map.end()) {
      delete it->second;
      m_dataStructEditor_map.erase(it);
    }
  }
}

Result<bool,RichError> PLC_Device::read_DataBlock(DataStructeEditor *data_struct_editor,PLC_Device *device_pointer)
{
    return Result<bool,RichError>(data_struct_editor->read_DataBlock_from_PLC(device_pointer));
}

Result<bool,RichError> PLC_Device::SendBuffer_ToPLC(DataStructeEditor *data_struct_editor,PLC_Device *device_pointer)
{
    return Result<bool,RichError>(data_struct_editor->SendBuffer_ToPLC(device_pointer));
}

// Result<bool,RichError> PLC_Device::return_DataStructeEditor(DataStructeEditor &data_struct_editor,const QString &file_path)
// {
//     auto it = this->m_dataStructEditor_map.find(file_path.toStdString()) ;
//     if(it == this->m_dataStructEditor_map.end() )
//     {
//         std::cout<<"select DB file is not exist"<<std::endl;
//         return Result<bool,RichError>(RichError("(select DB file is not exist"));
//     }
//     else{
//       data_struct_editor = *it->second;
//       return Result<bool, RichError>(true);
//     }
// }

Result<bool,RichError> OPC_UA_client::extractVariable(const std::string &config_file_path)
{
    std::ifstream file(config_file_path);
    if(file.is_open())
    {
        return Result<bool,RichError> (RichError("OPC_UA XML file Open file "));
    }

    std::string line;

    // const char *nodeIdPart = R"(NodeId\s*=\s*")";
    // const char *dataTypePart = R"(.*DataType\s*=\s*")";
    // 注意：这里不能直接 nodeIdPart + "([^"]*)" + ... 因为 const char* 不支持 +
    // 所以改用 std::string（如果接受运行时构造）
    std::string patternStr = 
    R"(NodeId\s*=\s*")"      // 匹配 NodeId="...
    R"(([^"]*))"            // 第一个捕获组：引号内的内容
    R"(.*DataType\s*=\s*")"  // 中间任意字符，再匹配 DataType="...
    R"(([^"]*))";            // 第二个捕获组
    std::regex pattern(patternStr);
    std::smatch matches;    

    while(std::getline(file,line))
    {
        if(line.find("DB") != std::string::npos && line.find("DataType") != std::string::npos)
        {
            if(std::regex_search(line,pattern))
            {
                std::string nodeID = matches[1].str();
                std::string DataType = matches[2].str();
                this->m_UA_info_vector.push_back({nodeID,DataType});
            }
        }
    }
    return Result<bool,RichError> (true);
}

 Result<bool,RichError> S7_Access::connect() 
{
    bool check_result = isConnected();
    if(check_result)
    {
      return Result<bool, RichError>(check_result);
    }

    int result =
        Cli_ConnectTo(m_client_var, m_ip_Address.data(), m_rack, m_slot);
    if (result != 0) {
        char error_text[256];
        Cli_ErrorText(result, error_text, sizeof(error_text));
        std::cout << "Connection failed: " << error_text
                << " (Error code: " << result << ")\n";
        return Result<bool, RichError>(RichError("connect fail"));
    } else {
        std::cout << "Connection successful\n";
    }
    return Result<bool, RichError>(result);
}

bool S7_Access::isConnected()
{
    int cpu_state;
    int result = Cli_GetPlcStatus(m_client_var, &cpu_state);
    if (result == 0) {
      std::cout << "CPU is in state " << cpu_state << "\n";
      if (cpu_state == S7CpuStatusRun) {
        std::cout << "CPU is running\n";
      }
    }
    // 如果连接断开，Cli_GetPlcStatus 会返回非 0 错误码
    return (result == 0);
}

void S7_Access::disconnect() 
{
    if (isConnected()) {
        Cli_Disconnect(m_client_var);
    }
    Cli_Destroy(&m_client_var);
    std::cout << "PLC_Device disconnct\n";
}

S7Object& S7_Access::return_client()
{
    return this->m_client_var;
}

Result<bool,RichError> S7_Access::read(int DB_Number,int Start_Position,int Read_Size,uint8_t *SourceData_var) 
{
    bool connect_check = this->isConnected();
    if (!connect_check) {
    return Result<bool, RichError>(false);
    }

    int result = Cli_DBRead(m_client_var,DB_Number, Start_Position, Read_Size,SourceData_var);
    if (result != 0)
    {
        char error_text[256];
        Cli_ErrorText(result, error_text, sizeof(error_text));
        std::cout<<"Read failed: " << error_text << " (Error code: " << result << ")\n";
        return Result<bool, RichError>(RichError{error_text});
    }
    return Result<bool,RichError> (true);
}

 Result<bool,RichError> S7_Access::write(int DB_Number,int Start_Position,int Read_Size,uint8_t *SourceData_var) 
{
    int result = Cli_DBWrite(m_client_var,DB_Number, Start_Position, Read_Size,SourceData_var);
    if (result != 0)
    {
        char error_text[256];
        Cli_ErrorText(result, error_text, sizeof(error_text));
        std::cout<<"Read failed: " << error_text << " (Error code: " << result << ")\n";
    }
    return Result<bool,RichError> (true);
}

Result<bool, RichError> OPCUA_Access::connect() {
  bool check_result = isConnected();
  if(check_result)
  {
    return Result<bool, RichError>(check_result);
  }

  UA_StatusCode result = UA_Client_connect(
      m_client_pointer,
      ("opc.tcp://" + m_ip_Address + ":" + std::to_string(m_port)).data());
  if (result != UA_STATUSCODE_GOOD) {
    char error_text[256];
    Cli_ErrorText(result, error_text, sizeof(error_text));
    std::cout << "Connection failed: " << error_text
              << " (Error code: " << result << ")\n";
    return Result<bool, RichError>(RichError("connect fail"));
  }
  return Result<bool, RichError>(true);
}

bool OPCUA_Access::isConnected()
{
  if (!m_client_pointer) {
    return false;
  }

  UA_SecureChannelState channelState;
  UA_SessionState sessionState;
  UA_Client_getState(m_client_pointer, &channelState, &sessionState, nullptr);

  // 只有当 SecureChannel 和 Session 都处于“已建立”状态，才算真正连接成功
  return (channelState == UA_SECURECHANNELSTATE_OPEN &&
          sessionState == UA_SESSIONSTATE_ACTIVATED);
}


template<>
Result<bool,RichError> OPCUA_Access::Set_UA_Scalar_StatusCode(int nameSpace,S7ModernDataStruct &var,std::string &source_var)
{
    //  type->value->request->response
    // CHECK TYPE MATCH
    auto it = s7_to_ua_map.find(var.data_type_enum);
    if(it == s7_to_ua_map.end())
    {
        return Result<bool,RichError> (RichError("OPCUA_Access : write type does not match T "));
    }
    const UA_DataType *type = it->second;

    //  INIT WRITE UA_VALUE
    UA_WriteValue_init(&m_writeValue);
    m_writeValue.nodeId =
        UA_NODEID_STRING_ALLOC(nameSpace, var.variable_nodeID.data());
    m_writeValue.attributeId = UA_ATTRIBUTEID_VALUE;
    UA_String tmp_data = UA_STRING(const_cast<char*>(source_var.c_str()));

    //  COPY RESOURCE TO UA_VARIANT OF UA_WriteValue
    UA_StatusCode status = UA_Variant_setScalarCopy(&m_writeValue.value.value,&tmp_data,type);
    if(status != UA_STATUSCODE_GOOD)
    {
        UA_WriteValue_clear(&m_writeValue);
        return Result<bool,RichError> (RichError("OPCUA_Access : write value fail"));
    }
    m_writeValue.value.hasValue = true;
    return Result<bool,RichError> (true);
}



template<typename T>
Result<bool,RichError> OPCUA_Access::Set_UA_Scalar_StatusCode(int nameSpace,S7ModernDataStruct &var,T &source_var)
{
    //  type->value->request->response
    // CHECK TYPE MATCH
    auto it = s7_to_ua_map.find(var.data_type_enum);
    if(it == s7_to_ua_map.end())
    {
        return Result<bool,RichError> (RichError("OPCUA_Access : write type does not match T "));
    }
    const UA_DataType *type = it->second;

    //  INIT WRITE UA_VALUE
    UA_WriteValue_init(&m_writeValue);
    m_writeValue.nodeId =
        UA_NODEID_STRING_ALLOC(nameSpace, var.variable_nodeID.data());
    m_writeValue.attributeId = UA_ATTRIBUTEID_VALUE;

    //  COPY RESOURCE TO UA_VARIANT OF UA_WriteValue
    UA_StatusCode status = UA_Variant_setScalarCopy(&m_writeValue.value.value,&source_var,type);
    if(status != UA_STATUSCODE_GOOD)
    {
        UA_WriteValue_clear(&m_writeValue);
        return Result<bool,RichError> (RichError("OPCUA_Access : write value fail"));
    }
    m_writeValue.value.hasValue = true;
    return Result<bool,RichError> (true);
}



template<typename T>
Result<bool,RichError> OPCUA_Access::Set_UA_Array_StatusCode(int nameSpace,S7ModernDataStruct &var,std::vector<T> &source_vector)
{
    //  type->value->request->response
    // CHECK TYPE MATCH
    auto it = s7_to_ua_map.find(var.data_type_enum);
    if(it == s7_to_ua_map.end())
    {
        return Result<bool,RichError> (RichError("OPCUA_Access : write type does not match T "));
    }
    const UA_DataType *type = it->second;

    //  INIT WRITE UA_VALUE
    UA_WriteValue_init(&m_writeValue);
    m_writeValue.nodeId =
        UA_NODEID_STRING_ALLOC(nameSpace, var.variable_nodeID.data());
    m_writeValue.attributeId = UA_ATTRIBUTEID_VALUE;

    //  COPY RESOURCE TO UA_VARIANT OF UA_WriteValue
    UA_StatusCode status = UA_Variant_setArrayCopy(&m_writeValue.value.value,source_vector.data(),source_vector.size(),type);
    if(status != UA_STATUSCODE_GOOD)
    {
        UA_WriteValue_clear(&m_writeValue);
        return Result<bool,RichError> (RichError("OPCUA_Access : write value fail"));
    }
    m_writeValue.value.hasValue = true;
    return Result<bool,RichError> (true);
}

void OPCUA_Access::disconnect()
{
  if (m_client_pointer) {
    // Step 1: 断开连接（发送 CloseSecureChannel 和 CloseSession）
    UA_StatusCode status = UA_Client_disconnect(m_client_pointer);
    if (status != UA_STATUSCODE_GOOD) {
      // 可选：记录日志，但即使失败也继续释放资源
      std::cout << "disconnect fail , try force to delete client directly "
                << std::endl;
      return;
    }
  }
  std::cout << "OPCUA connct successfully\n";
}

void OPCUA_Access::Clear_HasRead_var_set()
{
    m_hasRead_var_set.clear();
}

void OPCUA_Access::Clear_Read_Respondse()
{
    //  CLEAR RESOURCE AFTER SUCCESS LOOP
    UA_ReadResponse_clear(&(m_read_response));
    UA_Variant_init(&m_read_variant);
}

UA_Client *OPCUA_Access::return_client()
{
    return this->m_client_pointer;
}

void OPCUA_Access::Set_Read_NodeID(S7ModernDataStruct& data_var)
{
    m_readValueNodeID->nodeId =
        UA_NODEID_STRING(this->m_nameSpace, data_var.variable_nodeID.data());
    m_readValueNodeID->indexRange = UA_STRING_NULL;
    m_readValueNodeID->attributeId = UA_ATTRIBUTEID_VALUE;
}

Result<bool,RichError> OPCUA_Access::Read_UA_Variant_From_PLC(S7ModernDataStruct& single_data_var)
{
    Result<bool,RichError> it = this->read();
    if(it.is_success())
    {
        //  INSERT READED NODE_ID , AVOIDING REDUPLICATE READING 
        m_hasRead_var_set.insert(single_data_var.variable_nodeID);
        //  RECORD RESPONSE_VALUE
        m_read_variant = m_read_response.results[0].value;
        return Result<bool,RichError> (true);
    }
    else
    {
        return Result<bool,RichError> (RichError(it.unwrap_err()));
    }
}

template<typename T>
void OPCUA_Access::Covert_UA_Scalar_To_Specific(T &SourceData_var,int i)
{
  // ELSE UA TPYE MATCH S7 DATA TYPE
    memcpy(&SourceData_var,( (static_cast<T*>(m_read_variant.data))+i ),sizeof(T) );
}

template <>
void OPCUA_Access::Covert_Uint8_Vector_To_Normal_Vector(
    S7ModernDataStruct &var, std::vector<uint8_t> &src_vector,
    std::vector<char> &dest_vector) {
  // ELSE UA TPYE MATCH S7 DATA TYPE
  dest_vector.resize(var.s7_data_array_length);
  memcpy(dest_vector.data(), (src_vector.data() + int(var.bytes_offset)),
         (var.s7_data_array_length + 7) / 8);
}

template <>
void OPCUA_Access::Covert_Uint8_Vector_To_Normal_Vector(
    S7ModernDataStruct &var, std::vector<uint8_t> &src_vector,
    std::vector<uint8_t> &dest_vector) {
  // ELSE UA TPYE MATCH S7 DATA TYPE
  dest_vector.resize(var.s7_data_array_length);
  memcpy(dest_vector.data(), (src_vector.data() + int(var.bytes_offset)),
         var.s7_data_array_length);
}

template <>
void OPCUA_Access::Covert_Uint8_Vector_To_Normal_Vector(
    S7ModernDataStruct &var, std::vector<uint8_t> &src_vector,
    std::vector<std::string> &dest_vector) {
  std::string result(reinterpret_cast<const char *>(src_vector.data() +
                                                    int(var.bytes_offset) + 2),
                     var.s7_data_type_length - 2);
  dest_vector.push_back(std::move(result));
}

template <typename T>
void OPCUA_Access::Covert_Uint8_Vector_To_Normal_Vector(
    S7ModernDataStruct &var, std::vector<uint8_t> &src_vector,
    std::vector<T> &dest_vector) {
  dest_vector.resize(var.s7_data_array_length);
  memcpy(dest_vector.data(),
         reinterpret_cast<T*>((src_vector.data()) + int(var.bytes_offset)),
         var.s7_data_array_length*sizeof(T));
}

template <typename T>
void OPCUA_Access::Covert_Uint8_t_Vector_To_Normal_Scalar(
    S7ModernDataStruct &var, std::vector<uint8_t> &src_vector,
    T &dest_var) {
  memcpy(&dest_var,
         reinterpret_cast<T *>((src_vector.data()) + int(var.bytes_offset)),
         sizeof(T));
}

template <>
void OPCUA_Access::Covert_Uint8_t_Vector_To_Normal_Scalar(
    S7ModernDataStruct &var, std::vector<uint8_t> &src_vector,
    bool &dest_var) {
  uint8_t tmp;
  memcpy(&tmp, ((src_vector.data()) + int(var.bytes_offset)), 1);
  dest_var = (tmp >> var.bit_offset) & 0x01;
}

template <>
void OPCUA_Access::Covert_Uint8_t_Vector_To_Normal_Scalar(
    S7ModernDataStruct &var, std::vector<uint8_t> &src_vector,
    std::string &dest_var) {
  std::string result(reinterpret_cast<const char *>(src_vector.data() +
                                                    int(var.bytes_offset) + 2),
                     var.s7_data_type_length - 2);
  dest_var = std::move(result);
}



template<typename T>
void OPCUA_Access::ByteDeserialization_memcpy(T &dest,const std::vector<uint8_t> &src,int data_offset,int data_length)
{
  // ELSE UA TPYE MATCH S7 DATA TYPE
  memcpy(&dest,src.data()+data_offset,data_length);
}


Result<bool,RichError> OPCUA_Access::Set_UA_To_Read_Normal_Scalar(const S7DataType &s7_type,Dynamic_Value &value,int i)
{
    if( s7_type == S7DataType::BOOL)
    {
        bool tmp;
        {
            Covert_UA_Scalar_To_Specific(tmp,i);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::BYTE)
    {
        uint8_t tmp;
        {
            Covert_UA_Scalar_To_Specific(tmp,i);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::INT)
    {
        int16_t tmp;
        {
            Covert_UA_Scalar_To_Specific(tmp,i);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::WORD)
    {
        uint16_t tmp;
        {
            Covert_UA_Scalar_To_Specific(tmp,i);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::DWORD || s7_type == S7DataType::UDINT)
    {
        uint32_t tmp;
        {
            Covert_UA_Scalar_To_Specific(tmp,i);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::DINT)
    {
        int32_t tmp;
        {
            Covert_UA_Scalar_To_Specific(tmp,i);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::REAL)
    {
        float tmp;
        {
            Covert_UA_Scalar_To_Specific(tmp,i);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::STRING)
    {
        std::string tmp;
        {
            Covert_UA_Scalar_To_Specific(tmp,i);
            value.Reset_Value(std::move(tmp));
        }
    }
    else
    {
        return Result<bool,RichError> (RichError("Read_UA_Variant_From_PLC : s7_type is unknown"));
    }
    this->Clear_Read_Respondse();
    return Result<bool, RichError>(true);
}


Result<bool,RichError> OPCUA_Access::Set_Normal_To_Write_UA_Scalar(
      S7ModernDataStruct &var,
      std::vector<uint8_t> &m_data_block_buffer)
{
  if (var.data_type_enum == S7DataType::BOOL) {
    bool tmp;
    {
        Covert_Uint8_t_Vector_To_Normal_Scalar(
            var, m_data_block_buffer, tmp);
      return this->write_by_scalar(var, tmp);
    }
  } else if (var.data_type_enum == S7DataType::BYTE) {
    uint8_t tmp;
    {
      Covert_Uint8_t_Vector_To_Normal_Scalar(
          var, m_data_block_buffer, tmp);
      return this->write_by_scalar(var, tmp);
    }
  } else if (var.data_type_enum == S7DataType::INT) {
    int16_t tmp;
    {
      Covert_Uint8_t_Vector_To_Normal_Scalar(
          var, m_data_block_buffer, tmp);
      return this->write_by_scalar(var, tmp);
    }
  } else if (var.data_type_enum == S7DataType::WORD) {
    uint16_t tmp;
    {
      Covert_Uint8_t_Vector_To_Normal_Scalar(
          var, m_data_block_buffer, tmp);
      return this->write_by_scalar(var, tmp);
    }
  } else if (var.data_type_enum == S7DataType::DWORD ||
             var.data_type_enum == S7DataType::UDINT) {
    uint32_t tmp;
    {
      Covert_Uint8_t_Vector_To_Normal_Scalar(
          var, m_data_block_buffer, tmp);
      return this->write_by_scalar(var, tmp);
    }
  } else if (var.data_type_enum == S7DataType::DINT) {
    int32_t tmp;
    {
      Covert_Uint8_t_Vector_To_Normal_Scalar(
          var, m_data_block_buffer, tmp);
      return this->write_by_scalar(var, tmp);
    }
  } else if (var.data_type_enum == S7DataType::REAL) {
    float tmp;
    {
      Covert_Uint8_t_Vector_To_Normal_Scalar(
          var, m_data_block_buffer, tmp);
      return this->write_by_scalar(var, tmp);
    }
  } else if (var.data_type_enum == S7DataType::STRING) {
    std::string tmp;
    {
      Covert_Uint8_t_Vector_To_Normal_Scalar(
          var, m_data_block_buffer, tmp);
      return this->write_by_scalar(var, tmp);
    }
  } else {
    return Result<bool, RichError>(
        RichError("Read_UA_Variant_From_PLC : s7_type is unknown"));
  }
}

Result<bool,RichError> OPCUA_Access::Set_Normal_To_Write_UA_Vector(
      S7ModernDataStruct &var,
      std::vector<uint8_t> &m_data_block_buffer)
{
    if( var.data_type_enum == S7DataType::BOOL)
    {
        std::vector<char> tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,
                                       tmp);
          return this->write_by_vector(var, tmp);
        }
    }
    else if( var.data_type_enum == S7DataType::BYTE)
    {
        std::vector<uint8_t> tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,tmp);
          return this->write_by_vector(var, tmp);
        }
    }
    else if( var.data_type_enum == S7DataType::INT)
    {
        std::vector<UA_Int16> tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,
                                        tmp);
          return this->write_by_vector(var, tmp);
        }
    }
    else if( var.data_type_enum == S7DataType::WORD)
    {
        std::vector<UA_UInt16> tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,
                                        tmp);
          return this->write_by_vector(var, tmp);
        }
    }
    else if( var.data_type_enum == S7DataType::DWORD || var.data_type_enum == S7DataType::UDINT)
    {
        std::vector<UA_UInt32> tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,
                                        tmp);
          return this->write_by_vector(var, tmp);
        }
    }
    else if( var.data_type_enum == S7DataType::DINT)
    {
        std::vector<UA_Int32> tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,
                                        tmp);
          return this->write_by_vector(var, tmp);
        }
    }
    else if( var.data_type_enum == S7DataType::REAL)
    {
        std::vector<UA_FLOAT> tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,
                                        tmp);
          return this->write_by_vector(var, tmp);
        }
    }
    else if( var.data_type_enum == S7DataType::STRING)
    {
        std::vector<UA_String > tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,
                                        tmp);
          return this->write_by_vector(var, tmp);
        }
    }
    else
    {
        return Result<bool,RichError> (RichError("Read_UA_Variant_From_PLC : s7_type is unknown"));
    }
}

Result<bool,RichError> OPCUA_Access::Set_Read_UA_Array(std::unordered_map<std::string, S7ModernDataStruct> &&var_map,S7ModernDataStruct &var)
{
    for(int i = 0 ; i<m_read_variant.arrayLength ; ++i)
    {
        std::string array_member_full_path =
            var.variable_nodeID + "[" + std::to_string(i) + "]";
            //  MATCH ELEMENT OF ARRAY IN VAR_MAP
        auto it = var_map.find(array_member_full_path);
        if (it != var_map.end()) {
            //  MEAN THE ELEMENT IS EXIST
            auto result =
                Set_UA_To_Read_Normal_Scalar(it->second.data_type_enum, *it->second.data_pointer,i);
            if (result.is_fail()) {
              return Result<bool, RichError>(result);
            }
        } else {
            //  MEAN THE ELEMENT DO NOT EXIST
            break;
        }
    }
    this->Clear_Read_Respondse();
    return Result<bool,RichError> (true);
}

Result<bool, RichError> OPCUA_Access::Set_Read_UA_Array(
    std::vector<S7ModernDataStruct> &&var_vector,
    S7ModernDataStruct &var)
{
    // 1. 参数验证
    if (var_vector.empty()) {
        return Result<bool, RichError>(
            RichError("var_vector is empty"));
    }
    
    if (m_read_variant.arrayLength <= 0) {
        return Result<bool, RichError>(
            RichError("Invalid array length: " + 
                      std::to_string(m_read_variant.arrayLength)));
    }
    
    // 2. 验证数组长度匹配
    if (static_cast<int>(var_vector.size()) != m_read_variant.arrayLength) {
        return Result<bool, RichError>(
            RichError("var_vector size (" + std::to_string(var_vector.size()) + 
                      ") does not match array length (" + 
                      std::to_string(m_read_variant.arrayLength) + ")"));
    }
    
    // 3. 遍历 var_vector 中的每个变量
    for (size_t i = 0; i < var_vector.size(); ++i) {
        const auto& current_var = var_vector[i];
        
        // 验证数据指针有效性
        if (current_var.data_pointer == nullptr) {
            return Result<bool, RichError>(
                RichError("data_pointer is null for element " + 
                         std::to_string(i) + ": " + current_var.variable_name));
        }
        
        // 构造数组元素路径（用于验证或日志）
        std::string array_member_full_path = 
            var.variable_nodeID + "[" + std::to_string(i) + "]";
        
        // 可选：验证路径是否匹配
        if (current_var.variable_nodeID != array_member_full_path) {
            std::cerr << "Warning: Path mismatch for element " << i 
                      << ": expected " << array_member_full_path
                      << ", got " << current_var.variable_nodeID << std::endl;
        }
        
        // 读取数据并存储到 data_pointer
        auto result = Set_UA_To_Read_Normal_Scalar(
            current_var.data_type_enum, 
            *current_var.data_pointer, 
            static_cast<int>(i));  // 传递索引位置
        
        if (result.is_fail()) {
            return Result<bool, RichError>(
                RichError("Failed to read array element[" + std::to_string(i) + 
                         "]: " + result.unwrap_err().what()));
        }
    }
    
    // 4. 清理响应
    this->Clear_Read_Respondse();
    
    return Result<bool, RichError>(true);
}

Result<bool,RichError> OPCUA_Access::read() 
{
    bool connect_check = this->isConnected(); 
    if(!connect_check)
    {
        return Result<bool,RichError> (false);
    }

    UA_ReadRequest request;
    UA_ReadRequest_init(&request);
    request.nodesToRead = m_readValueNodeID;
    request.nodesToReadSize = 1;
    m_read_response = UA_Client_Service_read(m_client_pointer, request);

    if (m_read_response.responseHeader.serviceResult != UA_STATUSCODE_GOOD || m_read_response.resultsSize != 1 || !m_read_response.results[0].hasValue)
    {
        Clear_Read_Respondse();
        std::cerr<<"read value fail in read(): "<<UA_ReadResponse(m_read_response).results<<std::endl;
        return Result<bool,RichError> (RichError("OPCUA_Access : read status error"));
    }

    return Result<bool,RichError> (true);
}

Result<bool,RichError> OPCUA_Access::read_nameSpace()
{
    bool is_success = true;
    UA_ReadValueId rvi;
    UA_ReadValueId_init(&rvi);
    rvi.nodeId = UA_NODEID_NUMERIC(0, 2225);
    rvi.indexRange = UA_STRING_NULL;
    rvi.attributeId = UA_ATTRIBUTEID_VALUE;

    //  INIT UA_WRITE_REQUEST
    UA_ReadRequest request;
    UA_ReadRequest_init(&request);
    request.nodesToRead = &rvi;
    request.nodesToReadSize = 1;

    // UA_NodeId plcNode = UA_NODEID_STRING_ALLOC(3, const_cast<char*>("\"DB111_EdgeGatewayTest\""));
    // auto it = read_variable_from_device(plcNode,true);
    // return Result<bool,RichError> (it);

    //  TRY WRITE UA_VALUE INTO DEVICE
    UA_ReadResponse read_response =
        UA_Client_Service_read(m_client_pointer, request);
    if (read_response.responseHeader.serviceResult !=
            UA_STATUSCODE_GOOD ||
        read_response.resultsSize != 1 ||
        &read_response.results[0] != nullptr &&
        !read_response.results[0].hasValue 
    ) {
      std::cerr << "read name Space fail :"
                << " read_response.responseHeader.serviceResult : "
                << read_response.responseHeader.serviceResult
                << " read_response.resultsSize : "
                << read_response.resultsSize
                << "  &read_response.results[0] = :"
                << &read_response.results[0]<<std::endl; 
      UA_ReadResponse_clear(&read_response);
      return Result<bool, RichError>(
          RichError("OPCUA_Access : read status error"));
    }
    else
    {
        UA_Variant *value = &read_response.results[0].value;
        if(!UA_Variant_hasArrayType(value, &UA_TYPES[UA_TYPES_STRING]))
        {
            return Result<bool, RichError>(
                RichError("NameSpaceArray is not string array"));
        }

        size_t nsCount = value->arrayLength;
        UA_String *nsArray = (UA_String *)value->data;
        for (size_t i = 0; i < nsCount; ++i) {
          std::string uri((char *)nsArray[i].data, nsArray[i].length);
          std::cout << "NameSpace[" << i << "] = " << uri << std::endl;
       
          auto it = read_variable_from_device(rvi.nodeId,false);
          if (it.is_fail()) {
            is_success = false;
          } else {
            break;
          }
        }
    }
    UA_ReadResponse_clear(&read_response);
    if (is_success) {
      return Result<bool, RichError>(is_success);
    } else {
      return Result<bool, RichError>(
          RichError("read_variable_from_device exist error"));
    }
}

Result<bool,RichError> OPCUA_Access::nodeIdToString(std::string &str,UA_NodeId &nodeId)
{
  UA_String output;
  UA_String_init(&output); // 初始化为空字符串

  UA_StatusCode status = UA_NodeId_print(&nodeId, &output);
  if (status != UA_STATUSCODE_GOOD) {
      return Result<bool,RichError>(RichError("invalid-nodeid"));
  }

  // 注意：output.data 不一定以 '\0' 结尾，必须用 length 构造 std::string
  str.assign(reinterpret_cast<char *>(output.data), output.length);

  // 释放 open62541 分配的内存
  UA_String_clear(
      &output); // 等价于 UA_free(output.data); output = {0, nullptr};

  return Result<bool,RichError>(true);
}

bool OPCUA_Access::isSiemensContainer(const std::string& browseName)
{
  return (browseName == "DataBlocksGlobal" ||
          browseName == "DataBlocksInstance" || browseName == "Inputs" ||
          browseName == "Outputs" || browseName == "Memory");
}

Result<bool,RichError> OPCUA_Access::read_variable_from_device(UA_NodeId &nodeID,bool reverse_direction)
{
    std::string tmp = "";
    auto it = (nodeIdToString(tmp, nodeID));
    if(it.is_fail())
    {
        return Result<bool,RichError>(it);
    }
    else
    {
        if(m_visited_set.count(tmp))
        {
            std::cout<<"check reduplicate nodeID : "<<tmp<<std::endl;
            return Result<bool,RichError> (true);
        }
        m_visited_set.insert(tmp);
    }

    UA_BrowseDescription bd;
    UA_BrowseDescription_init(&bd);
    bd.nodeId = nodeID;
    if(reverse_direction)
    {
        // SON -> PARENT
        bd.browseDirection = UA_BROWSEDIRECTION_INVERSE; 
    }
    else
    {
        //  PARENT -> SON
        bd.browseDirection = UA_BROWSEDIRECTION_FORWARD; 
    }
    bd.includeSubtypes = true ;
    bd.resultMask = UA_BROWSERESULTMASK_ALL;  // 获取所有信息
    bd.referenceTypeId = UA_NODEID_NULL;

    UA_BrowseRequest bReq;
    UA_BrowseRequest_init(&bReq);
    bReq.nodesToBrowse = &bd;
    bReq.nodesToBrowseSize = 1;
    bReq.requestedMaxReferencesPerNode = 100;

    UA_BrowseResponse bResp = UA_Client_Service_browse(m_client_pointer,bReq);
    if(bResp.responseHeader.serviceResult != UA_STATUSCODE_GOOD|| 
        bResp.resultsSize != 1||
        bResp.results[0].statusCode != UA_STATUSCODE_GOOD
    )
    {
      std::cerr << "read variable from device  fail :"
                << " bResp.responseHeader.serviceResult : "
                << bResp.responseHeader.serviceResult
                << " bResp.resultsSize  : " << bResp.resultsSize << std::endl;
      UA_BrowseResponse_clear(&bResp);
      return Result<bool, RichError>(
          RichError("OPCUA_Access : read variable from device error"));
    }

    std::cout << "🔍 DEBUG: Browsing node " << tmp << " in "
              << (reverse_direction ? "INVERSE" : "FORWARD")
              << " direction, got " << bResp.results[0].referencesSize
              << " references." << std::endl;

    for(size_t i =0;i<bResp.results[0].referencesSize;++i)
    {
        // _t j=0;j<bResp.results[0].referencesSize;++j)
        {
            UA_ReferenceDescription *ref = &bResp.results[0].references[i];
            UA_NodeId fullNodeId =
                ref->nodeId.nodeId; // ns=3;s="PLC_1" 或 ns=0;i=2255
            UA_UInt16 namespaceIndex = fullNodeId.namespaceIndex; // 0, 2, 3 等
            UA_NodeIdType idType =
                fullNodeId.identifierType; // NUMERIC, STRING 等
            std::string browseName(
                (char *)ref->browseName.name.data,
                ref->browseName.name.length); // "PLC_1", "Server" 等
            UA_NodeClass NodeClass =  ref->nodeClass;
            std::string fullNodeId_str = "";
            nodeIdToString(fullNodeId_str,fullNodeId).unwrap_returnRightValue();
            std::string refTypeStr;
            nodeIdToString(refTypeStr, ref->referenceTypeId);
            std::cout << "Node: " << fullNodeId_str << " (" << browseName << ")"
                      << " namespaceIndex : " << fullNodeId.namespaceIndex
                      << " NodeClass : " << NodeClass
                      << " refTypeStr : " << refTypeStr
                      << " referenceSize : "<< bResp.results[0].referencesSize;

            // 3. 检查是否是HasTypeDefinition引用（这是类型定义，不是实际数据）
            UA_NodeId hasTypeDef =
                UA_NODEID_NUMERIC(0, UA_NS0ID_HASTYPEDEFINITION);
            if (UA_NodeId_equal(&ref->referenceTypeId, &hasTypeDef)) {
              std::cout << std::endl;
              continue;
            }

            UA_NodeId organizes = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
            UA_NodeId hasComponent =
                UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);
            UA_NodeId hasProperty = UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY);
            if (UA_NodeId_equal(&ref->referenceTypeId, &organizes)) {
                std::cout<<" [organizes]";
            } else if (UA_NodeId_equal(&ref->referenceTypeId, &hasComponent)) {
                std::cout<<" [hasComponent]";
            } else if (UA_NodeId_equal(&ref->referenceTypeId, &hasProperty)) {
                std::cout<<" [hasProperty]";
            }

            if(!ref->isForward)
            {
                std::cout<<" isForward: false"<<std::endl;
            }
            else
            {
                std::cout<<" isForward: true"<<std::endl;
            }

            if (ref->nodeClass == UA_NODECLASS_VARIABLE) {
              std::cout << ">>> FOUND VARIABLE: " << browseName << " at "
                        << fullNodeId_str << std::endl;
            }

            {
            //   std::cout << "nodeID:" << fullNodeId_str
            //             << " checked start inverse direction : " << std::endl;
            //   (read_variable_from_device(ref->nodeId.nodeId, true));
            //   std::cout << "nodeID:" << fullNodeId_str
            //             << " checked end inverse direction : " << std::endl;

              std::cout << "nodeID:" << fullNodeId_str
                        << " checked start forward direction : " << std::endl;
              (read_variable_from_device(ref->nodeId.nodeId, false));
              std::cout << "nodeID:" << fullNodeId_str
                        << " checked end forward direction : " << std::endl;
            }
        }
    }
    return Result<bool,RichError> (true);
}

Result<bool,RichError> OPCUA_Access::expandNodeIdToString( UA_ExpandedNodeId &id)
{
    UA_String tmp;
    UA_String_init(&tmp);
    UA_StatusCode it = UA_NodeId_print(&id.nodeId, &tmp);
    if(it == UA_STATUSCODE_GOOD)
    {
        std::string nodeIdStr((char*)tmp.data,tmp.length);
        std::cout<<"NodeID: "<<nodeIdStr<<std::endl;
        UA_String_clear(&tmp);
        return Result<bool,RichError> (true);
    }
    else
    {
        return Result<bool,RichError> (RichError("NodeID get fail "));
    }
}

template<typename T>
Result<bool,RichError> OPCUA_Access::write_by_vector(S7ModernDataStruct &SourceData_var,std::vector<T> &var)
{
    //  INITIAZLIE UA_WRTITE_VALUE
    if(this->Set_UA_Array_StatusCode(3, SourceData_var,var).is_fail())
    {
        return Result<bool,RichError> (RichError("Set UA Array StatusCode fail"));
    }
    //  INIT UA_WRITE_REQUEST
    UA_WriteRequest request;
    UA_WriteRequest_init(&request);
    request.nodesToWrite = &m_writeValue;
    request.nodesToWriteSize = 1;

    //  TRY WRITE UA_VALUE INTO DEVICE
    UA_WriteResponse write_response_status =
        UA_Client_Service_write(m_client_pointer, request);
    if (write_response_status.responseHeader.serviceResult !=
            UA_STATUSCODE_GOOD ||
        write_response_status.resultsSize != 1 ||
        &write_response_status.results[0] != nullptr &&
            write_response_status.results[0] != UA_STATUSCODE_GOOD) {
      std::cerr << "write vector value fail : "
                << "write_response_status.responseHeader.serviceResult : "
                << write_response_status.responseHeader.serviceResult
                << "write_response_status.resultsSize  : "
                << write_response_status.resultsSize << std::endl;
      UA_WriteValue_clear(&m_writeValue);
      UA_WriteResponse_clear(&write_response_status);
      return Result<bool, RichError>(
          RichError("OPCUA_Access : read status error"));
    }

    UA_WriteValue_clear(&m_writeValue);
    UA_WriteResponse_clear(&write_response_status);
    return Result<bool,RichError> (true);
}

template<typename T>
Result<bool,RichError> OPCUA_Access::write_by_scalar(S7ModernDataStruct &SourceData_var,T &var)
{
    //  INITIAZLIE UA_WRTITE_VALUE
    if (this->Set_UA_Scalar_StatusCode(3, SourceData_var, var).is_fail()) {
      return Result<bool, RichError>(RichError("Set UA Array StatusCode fail"));
    }
    //  INIT UA_WRITE_REQUEST
    UA_WriteRequest request;
    UA_WriteRequest_init(&request);
    request.nodesToWrite = &m_writeValue;
    request.nodesToWriteSize = 1;

    //  TRY WRITE UA_VALUE INTO DEVICE
    UA_WriteResponse write_response_status =
        UA_Client_Service_write(m_client_pointer, request);
    if (write_response_status.responseHeader.serviceResult !=
            UA_STATUSCODE_GOOD ||
        write_response_status.resultsSize != 1 ||
        &write_response_status.results[0] != nullptr &&
        write_response_status.results[0] != UA_STATUSCODE_GOOD
    ) {
      std::cerr << "write scalar value fail : "
                << "write_response_status.responseHeader.serviceResult : "
                << write_response_status.responseHeader.serviceResult
                << "write_response_status.resultsSize  : "
                << write_response_status.resultsSize<< std::endl;
      UA_WriteValue_clear(&m_writeValue);
      UA_WriteResponse_clear(&write_response_status);
      return Result<bool, RichError>(
          RichError("OPCUA_Access : read status error"));
    }

    UA_WriteValue_clear(&m_writeValue);
    UA_WriteResponse_clear(&write_response_status);
    return Result<bool,RichError> (true);
}


Result<bool,RichError> OPCUA_Access::ByteDeserialization_To_SpecialType(
         int data_offset, int data_length, S7DataType &s7_type,
         std::vector<uint8_t> &m_data_block_buffer,Dynamic_Value &value)
{
    if( s7_type == S7DataType::BOOL)
    {
        bool tmp;
        {
            ByteDeserialization_memcpy(tmp, m_data_block_buffer,data_offset,data_length);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::BYTE)
    {
        uint8_t tmp;
        {
            ByteDeserialization_memcpy(tmp, m_data_block_buffer,data_offset,data_length);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::INT)
    {
        int16_t tmp;
        {
            ByteDeserialization_memcpy(tmp, m_data_block_buffer,data_offset,data_length);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::WORD)
    {
        uint16_t tmp;
        {
            ByteDeserialization_memcpy(tmp, m_data_block_buffer,data_offset,data_length);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::DWORD || s7_type == S7DataType::UDINT)
    {
        uint32_t tmp;
        {
            ByteDeserialization_memcpy(tmp, m_data_block_buffer,data_offset,data_length);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::DINT)
    {
        int32_t tmp;
        {
            ByteDeserialization_memcpy(tmp, m_data_block_buffer,data_offset,data_length);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::REAL)
    {
        float tmp;
        {
            ByteDeserialization_memcpy(tmp, m_data_block_buffer,data_offset,data_length);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::STRING)
    {
        std::string tmp;
        {
            ByteDeserialization_memcpy(tmp, m_data_block_buffer,data_offset,data_length);
            value.Reset_Value(std::move(tmp));
        }
    }
    else
    {
        return Result<bool,RichError> (RichError("error in ByteDeserialization"));
    }

    return Result<bool, RichError>(true);
}

void DataStructeEditor::update_RowMapping()
{
    m_valueWidget_map.clear();
    for(int i = 0 ;i < m_tableWidget->rowCount(); ++i )
    {
        QTableWidgetItem* name_Item = m_tableWidget->item(i,0);
        if(name_Item)
        {
            m_valueWidget_map[name_Item->text().toStdString()] = i ;
        }
        else
        {
            std::cerr<<"update_RowMapping : name_Item is nullptr\n";
        }
    }
    return;
}

QWidget* DataStructeEditor::find_RowMapping(const std::string& var_name, int column )
{
    auto it = m_valueWidget_map.find(var_name);
    if(it == m_valueWidget_map.end())
    {
        std::cerr<<"find_RowMapping: can not find the element\n";
        return nullptr;
    }
    else
    {
        return m_tableWidget->cellWidget(it->second,column);
    }
}


Result<bool,RichError> DataStructeEditor::initialize_table()
{
    m_layout_V = new QVBoxLayout(this); 
    m_layout_V->setContentsMargins(0, 0, 0, 0);  // 移除边距
    m_layout_V->setSpacing(0);                   // 移除间距

    m_tableWidget = new QTableWidget();
    {
        m_tableWidget->setColumnCount(5);
        //  允许拖动widget，以扩展控件的可操作空间
        m_tableWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_tableWidget->setHorizontalHeaderLabels({"name","data_type","data_offset","initial_value","comment"});
        //  允许内容超出宽度时，显示滚动条
        m_tableWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_tableWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        // 关键：设置表头拉伸模式
        QHeaderView* horizontalHeader = m_tableWidget->horizontalHeader();
        horizontalHeader->setSectionResizeMode(QHeaderView::Interactive);  // 列方向可交互调整
        QHeaderView* verticalHeader = m_tableWidget->verticalHeader();
        verticalHeader->setSectionResizeMode(QHeaderView::Interactive);     // 行方向可交互调整
        
        m_layout_V->addWidget(m_tableWidget);
    }
  
    //  resize() buffer_vector
    this->m_table_data_buffer_bigEndian.reserve(m_single_data_block->whole_data_block_length);
    this->m_table_data_buffer_bigEndian.resize(m_single_data_block->whole_data_block_length);
    this->m_table_data_buffer_littleEndian.reserve(m_single_data_block->whole_data_block_length);
    this->m_table_data_buffer_littleEndian.resize(m_single_data_block->whole_data_block_length);

    int row_index = 0;
    for(auto& item:m_single_data_block->m_variable_map)
    {
        auto var_type = item.second.data_type_enum;
        if(var_type == S7DataType::BOOL)
        {
            m_tableWidget->insertRow(row_index);
            m_tableWidget->setItem(row_index,0,new QTableWidgetItem(item.first.c_str()));
            m_tableWidget->setItem(row_index,1,new QTableWidgetItem("BOOL"));
            
            QTableWidgetItem* offsetItem = new QTableWidgetItem();
            offsetItem->setData(Qt::DisplayRole,item.second.bytes_offset);
            m_tableWidget->setItem(row_index,2,offsetItem);

            QCheckBox* check_box = new QCheckBox(m_tableWidget);
            check_box->setChecked(false);

            connect(check_box, QOverload<int>::of(&QCheckBox::stateChanged), 
            [item,this,check_box](int value){
            //  update buffer
            if(value>0)
            {
                m_table_data_buffer_bigEndian[item.second.bytes_offset] |= 1 << item.second.bit_offset;
                m_table_data_buffer_littleEndian[item.second.bytes_offset] |= 1 << item.second.bit_offset;
            }
            else
            {
                m_table_data_buffer_bigEndian[item.second.bytes_offset] &= ~(1 << item.second.bit_offset );
                m_table_data_buffer_littleEndian[item.second.bytes_offset] &= ~(1 << item.second.bit_offset );
            }

            qDebug()<<"update_buffer_from_table in bool\n";
            });

            qDebug()<<"bool_box build !\n";
            m_tableWidget->setCellWidget(row_index, 3, check_box);
            m_tableWidget->setItem(row_index,4,new QTableWidgetItem(""));
        }
        else if(var_type == S7DataType::BYTE)
        {
            m_tableWidget->insertRow(row_index);
            m_tableWidget->setItem(row_index,0,new QTableWidgetItem(item.first.c_str()));
            m_tableWidget->setItem(row_index,1,new QTableWidgetItem("BYTE"));
            QTableWidgetItem* offsetItem = new QTableWidgetItem();
            offsetItem->setData(Qt::DisplayRole,item.second.bytes_offset);
            m_tableWidget->setItem(row_index,2,offsetItem);

            QSpinBox* byte_box = new QSpinBox(m_tableWidget);
            byte_box->setRange(0,255);
            byte_box->setValue(0x00);
            byte_box->setDisplayIntegerBase(16);
            byte_box->setPrefix("16#"); 
            byte_box->setSuffix("");

            connect(byte_box, QOverload<int>::of(&QSpinBox::valueChanged), 
            [item,this,byte_box](int value){
            //  update buffer
            ByteOrderCoverter::to_bigEndian(value,&m_table_data_buffer_bigEndian[item.second.bytes_offset],1);
            ByteOrderCoverter::to_littleEndian(value,&m_table_data_buffer_littleEndian[item.second.bytes_offset],1);

            qDebug()<<"update_buffer_from_table in bytes\n";
            });

            qDebug()<<"byte_box build !\n";
            m_tableWidget->setCellWidget(row_index, 3, byte_box);
            m_tableWidget->setItem(row_index,4,new QTableWidgetItem(""));

        }
        else if(var_type == S7DataType::INT)
        {
            m_tableWidget->insertRow(row_index);
            m_tableWidget->setItem(row_index,0,new QTableWidgetItem(item.first.c_str()));
            m_tableWidget->setItem(row_index,1,new QTableWidgetItem("INT"));

            QTableWidgetItem* offsetItem = new QTableWidgetItem();
            offsetItem->setData(Qt::DisplayRole,item.second.bytes_offset);
            m_tableWidget->setItem(row_index,2,offsetItem);

            QSpinBox* int_box = new QSpinBox(m_tableWidget);
            int_box->setRange(-32768,32767);
            int_box->setValue(0);

            connect(int_box, QOverload<int>::of(&QSpinBox::valueChanged), 
            [item,this, row_index,int_box](int value){
            //  update buffer
            ByteOrderCoverter::to_bigEndian(value,&m_table_data_buffer_bigEndian[item.second.bytes_offset],2);
            ByteOrderCoverter::to_littleEndian(value,&m_table_data_buffer_littleEndian[item.second.bytes_offset],2);

            qDebug()<<"update_buffer_from_table in int\n";
            });

            qDebug()<<"int_box build !\n";
            m_tableWidget->setCellWidget(row_index, 3, int_box);
            m_tableWidget->setItem(row_index,4,new QTableWidgetItem(""));
        }
        else if(var_type == S7DataType::DINT)
        {
            m_tableWidget->insertRow(row_index);
            m_tableWidget->setItem(row_index,0,new QTableWidgetItem(item.first.c_str()));
            m_tableWidget->setItem(row_index,1,new QTableWidgetItem("DINT"));

            QTableWidgetItem* offsetItem = new QTableWidgetItem();
            offsetItem->setData(Qt::DisplayRole,item.second.bytes_offset);
            m_tableWidget->setItem(row_index,2,offsetItem);


            QSpinBox* dint_box = new QSpinBox(m_tableWidget);
            dint_box->setRange(-2147483648,2147483647);
            dint_box->setValue(0);

            connect(dint_box, QOverload<int>::of(&QSpinBox::valueChanged), 
            [item,this,dint_box](int value){
            //  update buffer
            ByteOrderCoverter::to_bigEndian(value,&m_table_data_buffer_bigEndian[item.second.bytes_offset],4);
            ByteOrderCoverter::to_littleEndian(value,&m_table_data_buffer_littleEndian[item.second.bytes_offset],4);

            qDebug()<<"update_buffer_from_table in dint\n";
            });

            qDebug()<<"dint_box build !\n";
            m_tableWidget->setCellWidget(row_index, 3, dint_box);
            m_tableWidget->setItem(row_index,4,new QTableWidgetItem(""));
        }
        else if(var_type == S7DataType::REAL)
        {
            m_tableWidget->insertRow(row_index);
            m_tableWidget->setItem(row_index,0,new QTableWidgetItem(item.first.c_str()));
            m_tableWidget->setItem(row_index,1,new QTableWidgetItem("REAL"));

            QTableWidgetItem* offsetItem = new QTableWidgetItem();
            offsetItem->setData(Qt::DisplayRole,item.second.bytes_offset);
            m_tableWidget->setItem(row_index,2,offsetItem);

            QDoubleSpinBox* real_box = new QDoubleSpinBox(m_tableWidget);
            real_box->setValue(0.0);
            real_box->setRange(-3.4e38, 3.4e38);
            real_box->setDecimals(4);

            connect(real_box,QOverload<const QString&>::of(&QDoubleSpinBox::textChanged), 
            [item,this,real_box](const QString& text){
            //  get bytes_offset from table
            bool ok;
            float float_value = text.toFloat(&ok);
            if(!ok)
            {
                std::cout<<"invalid float value \n";
                return;
            }

            //  update buffer
            uint32_t tmp_data;
            memcpy(&tmp_data,&float_value,4);
            ByteOrderCoverter::to_bigEndian(tmp_data,&m_table_data_buffer_bigEndian[item.second.bytes_offset],4);
            ByteOrderCoverter::to_littleEndian(tmp_data,&m_table_data_buffer_littleEndian[item.second.bytes_offset],4);

            qDebug()<<"update_buffer_from_table in real\n";
            });

            qDebug()<<"real_box build !\n";
            m_tableWidget->setCellWidget(row_index, 3,real_box);
            m_tableWidget->setItem(row_index,4,new QTableWidgetItem(""));
        }
        else if(var_type == S7DataType::WORD)
        {
            m_tableWidget->insertRow(row_index);
            m_tableWidget->setItem(row_index,0,new QTableWidgetItem(item.first.c_str()));
            m_tableWidget->setItem(row_index,1,new QTableWidgetItem("WORD"));

            QTableWidgetItem* offsetItem = new QTableWidgetItem();
            offsetItem->setData(Qt::DisplayRole,item.second.bytes_offset);
            m_tableWidget->setItem(row_index,2,offsetItem);

            QSpinBox* word_box = new QSpinBox(m_tableWidget);
            word_box->setRange(0,65535);
            word_box->setValue(0x0000);
            word_box->setDisplayIntegerBase(16);
            word_box->setPrefix("16#");

            connect(word_box, QOverload<int>::of(&QSpinBox::valueChanged), 
            [item,this,word_box](int value){
            //  update buffer
            ByteOrderCoverter::to_bigEndian(value,&m_table_data_buffer_bigEndian[item.second.bytes_offset],2);
            ByteOrderCoverter::to_littleEndian(value,&m_table_data_buffer_littleEndian[item.second.bytes_offset],2);

            qDebug()<<"update_buffer_from_table in dint\n";
            });

            qDebug()<<"word_box build !\n";
            m_tableWidget->setCellWidget(row_index,3,word_box);
            m_tableWidget->setItem(row_index,4,new QTableWidgetItem(""));
        }
        else if(var_type == S7DataType::DWORD )
        {
            m_tableWidget->insertRow(row_index);
            m_tableWidget->setItem(row_index,0,new QTableWidgetItem(item.first.c_str()));
            m_tableWidget->setItem(row_index,1,new QTableWidgetItem("DWORD"));

            QTableWidgetItem* offsetItem = new QTableWidgetItem();
            offsetItem->setData(Qt::DisplayRole,item.second.bytes_offset);
            m_tableWidget->setItem(row_index,2,offsetItem);

            QLineEdit *hexEdit = new QLineEdit(m_tableWidget);
            hexEdit->setText("0x00000000");

            // 限制输入为 0x 后跟 1~8 个十六进制字符（不区分大小写）
            QRegularExpression regExp(R"(^0x[0-9A-Fa-f]{1,8}$)");
            QValidator *validator =
                new QRegularExpressionValidator(regExp, hexEdit);
            hexEdit->setValidator(validator);

            connect(hexEdit, &QLineEdit::textChanged, 
            [item,this,hexEdit](){
            QString text = hexEdit->text();
            if(text.startsWith("0x",Qt::CaseInsensitive))
            {
                bool ok = false;
                uint32_t value = text.toUInt(&ok,16);
                if(ok && value <= UINT32_MAX)
                {
                    ByteOrderCoverter::to_bigEndian(value,&m_table_data_buffer_bigEndian[item.second.bytes_offset],4);
                    ByteOrderCoverter::to_littleEndian(value,&m_table_data_buffer_littleEndian[item.second.bytes_offset],4);

                    std::cout << "update_buffer_from_table in DWORD is "
                              "success\n";
                }
                else
                {
                  std::cout << "update_buffer_from_table in DWORD is "
                               "fail for out of range\n";
                }
            }
            else
            {
              std::cout << "update_buffer_from_table in DWORD is "
                           "fail for error prefix\n";
            }
            });

            qDebug()<<"DWORD_edit build !\n";
            m_tableWidget->setCellWidget(row_index,3,hexEdit);
            m_tableWidget->setItem(row_index,4,new QTableWidgetItem(""));
        }
        else if(var_type == S7DataType::UDINT)
        {
            m_tableWidget->insertRow(row_index);
            m_tableWidget->setItem(row_index,0,new QTableWidgetItem(item.first.c_str()));
            m_tableWidget->setItem(row_index,1,new QTableWidgetItem("UDINT"));

            QTableWidgetItem* offsetItem = new QTableWidgetItem();
            offsetItem->setData(Qt::DisplayRole,item.second.bytes_offset);
            m_tableWidget->setItem(row_index,2,offsetItem);

            QLineEdit *hexEdit = new QLineEdit(m_tableWidget);
            hexEdit->setText("0");

            QRegularExpression regExp(R"(^[0-9]{1,10}$)");
            QValidator *validator =
                new QRegularExpressionValidator(regExp, hexEdit);
            hexEdit->setValidator(validator);

            connect(hexEdit, &QLineEdit::textChanged, 
            [item,this,hexEdit](){
            QString text = hexEdit->text();
            {
                bool ok = false;
                uint32_t value = text.toUInt(&ok,10);
                if(ok && value <= UINT32_MAX)
                {
                    
                    ByteOrderCoverter::to_bigEndian(value,&m_table_data_buffer_bigEndian[item.second.bytes_offset],4);
                    ByteOrderCoverter::to_littleEndian(value,&m_table_data_buffer_littleEndian[item.second.bytes_offset],4);
                    std::cout << "update_buffer_from_table in DWORD||UDINT is "
                              "success\n";
                }
                else
                {
                  std::cout << "update_buffer_from_table in DWORD||UDINT is "
                               "fail\n";
                }
            }
            });

            qDebug()<<"DWORD_edit build !\n";
            m_tableWidget->setCellWidget(row_index,3,hexEdit);
            m_tableWidget->setItem(row_index,4,new QTableWidgetItem(""));
        }
        else if(var_type == S7DataType::STRING)
        {
            m_tableWidget->insertRow(row_index);
            m_tableWidget->setItem(row_index,0,new QTableWidgetItem(item.first.c_str()));
            m_tableWidget->setItem(row_index,1,new QTableWidgetItem("STRING[" + QString::number(item.second.s7_data_type_length - 2) + "]"));

            QTableWidgetItem* offsetItem = new QTableWidgetItem();
            offsetItem->setData(Qt::DisplayRole,item.second.bytes_offset);
            m_tableWidget->setItem(row_index,2,offsetItem);
            
            QLineEdit* string_box = new QLineEdit(m_tableWidget);
            string_box->setText("");
            string_box->setMaxLength(item.second.s7_data_type_length-2);
            
            m_table_data_buffer_bigEndian[item.second.bytes_offset] = item.second.s7_data_type_length - 2 ;
            m_table_data_buffer_littleEndian[item.second.bytes_offset] = item.second.s7_data_type_length - 2 ;

            connect(
                string_box, &QLineEdit::textChanged,
                [this, item,string_box](const QString &value) {
                  std::string string_value = value.toStdString();
                  m_table_data_buffer_bigEndian[item.second.bytes_offset + 1] =
                      string_value.size();
                  std::fill(m_table_data_buffer_bigEndian.begin() +
                                item.second.bytes_offset + 2,
                            m_table_data_buffer_bigEndian.begin() +
                                item.second.bytes_offset +
                                item.second.s7_data_type_length - 2,
                            0);
                  memcpy(
                      &m_table_data_buffer_bigEndian[item.second.bytes_offset +
                                                     2],
                      string_value.c_str(), string_value.size());

                  m_table_data_buffer_littleEndian[item.second.bytes_offset + 1] =
                      string_value.size();
                  std::fill(m_table_data_buffer_littleEndian.begin() +
                                item.second.bytes_offset + 2,
                            m_table_data_buffer_littleEndian.begin() +
                                item.second.bytes_offset +
                                item.second.s7_data_type_length - 2,
                            0);
                  memcpy(&m_table_data_buffer_littleEndian
                             [item.second.bytes_offset + 2],
                         string_value.c_str(), string_value.size());

                  qDebug() << "update_buffer_from_table in dint\n";
                });

            qDebug()<<"string_box build !\n";
            m_tableWidget->setCellWidget(row_index,3,string_box);
            m_tableWidget->setItem(row_index,4,new QTableWidgetItem(""));
        }
        ++row_index;
    }
    //  启用排序
    m_tableWidget->setSortingEnabled(true);
    m_tableWidget->sortByColumn(2, Qt::AscendingOrder);

    not_need_initialize_table = true;
    update_RowMapping();

     // 延迟设置列宽
    QTimer::singleShot(0, this, [this]() {
        if (m_tableWidget && m_tableWidget->horizontalHeader()) {
            int totalWidth = m_tableWidget->viewport()->width();  // 使用表格可视区域宽度
            
            m_tableWidget->setColumnWidth(0, totalWidth * 0.35);  // 名称 35%
            m_tableWidget->setColumnWidth(1, totalWidth * 0.15);  // 类型 15%
            m_tableWidget->setColumnWidth(2, totalWidth * 0.15);  // 偏移 15%
            m_tableWidget->setColumnWidth(3, totalWidth * 0.20);  // 初始值 20%
            m_tableWidget->setColumnWidth(4, totalWidth * 0.15);  // 注释 15%
            m_tableWidget->horizontalHeader()->setStretchLastSection(true);
        }
    });
    return Result<bool,RichError>(true);
}

Result<bool,RichError> DataStructeEditor::update_LittleEndianBuffer_from_table()
{
  this->m_single_data_block->m_data_block_buffer.clear();
  this->m_single_data_block->m_data_block_buffer.assign(
      m_table_data_buffer_littleEndian.begin(),
      m_table_data_buffer_littleEndian.end());
  return Result<bool, RichError>(true);
}

Result<bool,RichError> DataStructeEditor::update_BigEndianBuffer_from_table()
{
  this->m_single_data_block->m_data_block_buffer.clear();
  this->m_single_data_block->m_data_block_buffer.assign(
      m_table_data_buffer_bigEndian.begin(),
      m_table_data_buffer_bigEndian.end());
  return Result<bool, RichError>(true);
}

void DataStructeEditor::adjust_RowHeight()
{ 
    if(!m_tableWidget)
    {
        return ;
    }

    int row_count = m_tableWidget->rowCount();
    if(row_count == 0)
    {
        return ;
    }

    int table_height = m_tableWidget->viewport()->height();
    int header_height = m_tableWidget->horizontalHeader()->height();
    int available_height = table_height - header_height;

    if(row_count < 15)
    {
        int row_height = std::max(30 , available_height / row_count);
        for(int i = 0; i < row_count; i++)
        {
            m_tableWidget->setRowHeight(i,row_height);
        }
    }
    else
    {
        m_tableWidget->verticalHeader()->setDefaultSectionSize(50);
    }
}

void DataStructeEditor::resizeEvent(QResizeEvent *event)
{ 
    QWidget::resizeEvent(event);
    adjust_RowHeight();
}

//  将数据记录到View里面
Result<bool,RichError> DataStructeEditor::write_Table_From_S7_DatabBlockBuffer()
{ 
    if(!not_need_initialize_table)
    {
        std::cout<<"initialize_table()"<<std::endl;
        return Result<bool,RichError>(initialize_table());
    }
    
    for(auto& item: m_single_data_block->m_variable_map)
    {
        auto result = DataTypeMapper::Data_transform_from_bytes(item.second.data_type_enum,m_single_data_block->m_data_block_buffer,item.second.bytes_offset,item.second.s7_data_type_length,item.second.bit_offset);
        if(result.is_success())
        {
            if(item.second.data_type_enum == S7DataType::BOOL)
            {
                auto bool_box = qobject_cast<QCheckBox*>(find_RowMapping(item.first, 3));
                bool_box->setChecked(std::get<bool>(result.unwrap_returnLeftValue()));
            }
            else if(item.second.data_type_enum == S7DataType::BYTE)
            {
                auto byte_box = qobject_cast<QSpinBox*>(find_RowMapping(item.first, 3));
                byte_box->setValue(std::get<uint8_t>(result.unwrap_returnLeftValue()));
            }
            else if(item.second.data_type_enum == S7DataType::INT)
            {
                auto int_box = qobject_cast<QSpinBox*>(find_RowMapping(item.first, 3));
                int_box->setValue(std::get<int16_t>(result.unwrap_returnLeftValue()));
            }
            else if(item.second.data_type_enum == S7DataType::DINT)
            {
                auto dint_box = qobject_cast<QSpinBox*>(find_RowMapping(item.first, 3));
                dint_box->setValue(std::get<int32_t>(result.unwrap_returnLeftValue()));
            }
            else if(item.second.data_type_enum == S7DataType::REAL)
            {
                auto real_box = qobject_cast<QDoubleSpinBox*>(find_RowMapping(item.first, 3));
                real_box->setValue(std::get<float>(result.unwrap_returnLeftValue()));
            }
            else if(item.second.data_type_enum == S7DataType::WORD)
            {
                auto word_box = qobject_cast<QSpinBox*>(find_RowMapping(item.first, 3));
                word_box->setValue(std::get<uint16_t>(result.unwrap_returnLeftValue()));
            }
            else if(item.second.data_type_enum == S7DataType::UDINT)
            {
                auto hex_edit = qobject_cast<QLineEdit*>(find_RowMapping(item.first, 3));
                hex_edit->setText(QString::fromStdString(DataTypeMapper::transform_uint32_to_string(std::get<uint32_t>(result.unwrap_returnLeftValue()))));
            }
            else if(item.second.data_type_enum == S7DataType::DWORD)
            {
                auto hex_edit = qobject_cast<QLineEdit*>(find_RowMapping(item.first, 3));
                hex_edit->setText(QString::fromStdString(DataTypeMapper::transform_uint32_to_hex_string(std::get<uint32_t>(result.unwrap_returnLeftValue()))));
            }
            else if(item.second.data_type_enum == S7DataType::STRING)
            {
                auto string_box = qobject_cast<QLineEdit*>(find_RowMapping(item.first, 3));
                string_box->setText(std::get<std::string>(result.unwrap_returnLeftValue()).c_str());
            }
        }
    }
    return Result<bool,RichError>(true);
}

Result<bool,RichError> DataStructeEditor::write_Table_From_OPCUA_DatabBlockBuffer()
{ 
    if(!not_need_initialize_table)
    {
        std::cout<<"initialize_table()"<<std::endl;
        return Result<bool,RichError>(initialize_table());
    }
    
    for(auto& item: m_single_data_block->m_variable_map)
    {
        if(item.second.data_pointer->has_value())
        {
            if(item.second.data_type_enum == S7DataType::BOOL)
            {
                auto bool_box = qobject_cast<QCheckBox*>(find_RowMapping(item.first, 3));
                bool_box->setChecked(item.second.data_pointer->get<bool>());
            }
            else if(item.second.data_type_enum == S7DataType::BYTE)
            {
                auto byte_box = qobject_cast<QSpinBox*>(find_RowMapping(item.first, 3));
                byte_box->setValue(item.second.data_pointer->get<uint8_t>());
            }
            else if(item.second.data_type_enum == S7DataType::INT)
            {
                auto int_box = qobject_cast<QSpinBox*>(find_RowMapping(item.first, 3));
                int_box->setValue(item.second.data_pointer->get<int16_t>());
            }
            else if(item.second.data_type_enum == S7DataType::DINT)
            {
                auto dint_box = qobject_cast<QSpinBox*>(find_RowMapping(item.first, 3));
                dint_box->setValue(item.second.data_pointer->get<int32_t>());
            }
            else if(item.second.data_type_enum == S7DataType::REAL)
            {
                auto real_box = qobject_cast<QDoubleSpinBox*>(find_RowMapping(item.first, 3));
                real_box->setValue(item.second.data_pointer->get<float>());
            }
            else if(item.second.data_type_enum == S7DataType::WORD)
            {
                auto word_box = qobject_cast<QSpinBox*>(find_RowMapping(item.first, 3));
                word_box->setValue(item.second.data_pointer->get<uint16_t>());
            }
            else if(item.second.data_type_enum == S7DataType::DWORD)
            {
                auto hex_edit = qobject_cast<QLineEdit*>(find_RowMapping(item.first, 3));
                hex_edit->setText(QString::fromStdString(DataTypeMapper::transform_uint32_to_hex_string(item.second.data_pointer->get<uint32_t>())));
            }
            else if(item.second.data_type_enum == S7DataType::UDINT)
            {
                auto hex_edit = qobject_cast<QLineEdit*>(find_RowMapping(item.first, 3));
                hex_edit->setText(QString::fromStdString(DataTypeMapper::transform_uint32_to_string(item.second.data_pointer->get<uint32_t>())));
            }
            else if(item.second.data_type_enum == S7DataType::STRING)
            {
                auto string_box = qobject_cast<QLineEdit*>(find_RowMapping(item.first, 3));
                string_box->setText(item.second.data_pointer->get<std::string>().c_str());
            }
        }
        else
        {
            std::cout<<"the item data_pointer is nullptr"<<std::endl;
        }
    }
    return Result<bool,RichError>(true);
}

Result<bool,RichError> DataStructeEditor::read_DataBlock_from_PLC(PLC_Device *device_pointer)
{
  if (device_pointer->is_exist_data_block == false) {
    return Result<bool, RichError>(
        RichError("the device do not exist data block"));
  }
  if (device_pointer->m_UA_Access == nullptr) {
    if (this->m_single_data_block
            ->ReadS7DataBlock_FromPLC(
                device_pointer->m_S7_Access->return_client())
            .is_success()) {
      return Result<bool, RichError>(write_Table_From_S7_DatabBlockBuffer());
    } else {
      return Result<bool, RichError>(RichError("read_DataBlock_from_PLC fail"));
    }
  } else {
    if (this->m_single_data_block
            ->ReadOPCUADataBlock_FromPLC(device_pointer->m_UA_Access)
            .is_success()) {
      return Result<bool, RichError>(write_Table_From_OPCUA_DatabBlockBuffer());
    } else {
      return Result<bool, RichError>(RichError("read_DataBlock_from_PLC fail"));
    }
  }
}

Result<std::string,RichError> DataStructeEditor::return_DataBlockName()
{
    return Result<std::string,RichError>(m_single_data_block->return_DataBlockName());
}

Result<bool,RichError> DataStructeEditor::SendBuffer_ToPLC(PLC_Device *client_object)
{
    if(client_object->m_S7_Access != nullptr)
    {
        return m_single_data_block->SendBuffer_To_PLC_Offset(client_object->m_S7_Access);
    }
    else if (client_object->m_UA_Access != nullptr)
    {
        return m_single_data_block->SendBuffer_To_PLC_OPCUA(client_object->m_UA_Access);
    }
    else
    {
        return Result<bool,RichError> (RichError("device pointer error"));
    }
}

