#include "PLC/TransformS7AndOPCUA.h"

NormalDataType default_value_for(S7DataType type) {
    switch (type) {
        case S7DataType::BOOL:   return false;
        case S7DataType::BYTE:   return uint8_t{0};
        case S7DataType::INT:    return int16_t{0};
        case S7DataType::WORD:   return uint16_t{0};
        case S7DataType::DINT:   return int32_t{0};
        case S7DataType::UDINT:
        case S7DataType::DWORD:  return uint32_t{0};
        case S7DataType::REAL:   return 0.0f;
        case S7DataType::STRING: return std::string{};
        default: throw std::invalid_argument("Unsupported type");
    }
}

Result<std::string,RichError> SCL_Parser::read_file_content(const std::string& file_path)
{
    std::ifstream file(file_path);
    if(file)
    {
        auto file_content = std::string(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
        m_file_content = file_content;
        return Result<std::string,RichError>(file_content);
    }
    else {
        return Result<std::string,RichError>(RichError("file not found"));
    }
}

Result<OPCUADataBlockDefinition,RichError> SCL_Parser::parse(const std::string& source_file_content){
    m_file_content = std::move(source_file_content);
    Result<std::string,RichError> result ("");
    auto it = result
    .and_then([this]( std::string& file_content){return parse_data_block_header(file_content);})
    .and_then([this]( OPCUADataBlockDefinition& db_def){return parse_variable(db_def);})
    .and_then([this]( OPCUADataBlockDefinition& db_def){return calculate_data_block_size(db_def);})
    ;

    m_file_content.clear();
    return it;
}

Result<OPCUADataBlockDefinition,RichError> SCL_Parser::parse_data_block_header( std::string& file_content){
    OPCUADataBlockDefinition db_def;
    std::regex db_regex(R"XXX(DATA_BLOCK\s*"([^"]+)")XXX");
    std::smatch match ;
    if(std::regex_search(m_file_content,match,db_regex))
    {
        db_def.data_block_name = match[1];

        //  extract DB number in DB name
        std::regex num_regex(R"(DB(\d+))");
        if(std::regex_search(db_def.data_block_name,match,num_regex))
        {
            db_def.block_number = std::stoi(match[1]);
        }

        db_def.optimized_access = m_file_content.find("S7_Optimized_Access := 'TRUE'") != std::string::npos;
        return Result<OPCUADataBlockDefinition,RichError>(db_def);
    }
    else {
        return Result<OPCUADataBlockDefinition,RichError>(RichError("DB header search failed"));
    }
}

Result<OPCUADataBlockDefinition,RichError> SCL_Parser::parse_variable( OPCUADataBlockDefinition& data_block_definition){ 
    size_t var_start = m_file_content.find("DATA_BLOCK"); 
    if(var_start == std::string::npos)
    {
        return Result<OPCUADataBlockDefinition,RichError>(RichError("content start position not found"));
    }

    size_t var_end = m_file_content.find("END_DATA_BLOCK");
    if(var_end == std::string::npos)
    {
        return Result<OPCUADataBlockDefinition,RichError>(RichError("content position end not found"));
    }

    std::string var_content = m_file_content.substr(var_start+11,var_end-var_start-12);
    std::istringstream stream(var_content);
    std::string line;
    return Result<OPCUADataBlockDefinition,RichError> (extract_variable_from_content(stream,line,data_block_definition));
}

Result<OPCUADataBlockDefinition,RichError> SCL_Parser::extract_variable_from_content(std::istringstream& file_content_stream,std::string& line,OPCUADataBlockDefinition& data_block_var)
{
    S7XMLVariableDefinition* struct_variable_pointer = nullptr;   //  used for indicate the struct pointer that store struct member variable these variable level in n+1
    std::vector<S7XMLVariableDefinition*> struct_pointer_stack_vector ;  //  used for store the struct pointer for rebuild the parent-child struct
    float last_free_byte_offset = 0;
    int struct_depth_current = 0;   //  used for indicate the struct level n  where the current structure is located 
    bool is_effective_extract = false;
    int struct_depth_current_backup = 0;

       while(std::getline(file_content_stream, line))
    {
        //  check the existence of variable
        if(trim(line).is_success())
        {
            line = trim(line).unwrap_returnRightValue();
            //  if find the struct in file_content
            if (line.find("Struct") != std::string::npos)
            {
                //  try store the struct variable (e.g Motor:Struct) 
                S7XMLVariableDefinition struct_variable;
                if(extract_variable_name(line).is_success()&&
                extract_variable_comment(line).is_success())
                {
                    struct_variable.variable_name = extract_variable_name(line).unwrap_returnRightValue();
                    struct_variable.data_type_enum = S7DataType::STRUCT;
                    struct_variable.comment = extract_variable_comment(line).unwrap_returnRightValue();

                    //  该结构体存入variable_definition_vector ，代表作为父级的子元素存在
                    data_block_var.variable_definitions_vector.push_back(struct_variable);
                    //  取出最新的结构体指针，存入结构体的栈容器里，辅助判断当前结构体的赋值是否结束
                    //  该栈容器指针存在N个对象，则表示存在N层结构体的嵌套
                    struct_variable_pointer = &data_block_var.variable_definitions_vector.back();
                    struct_pointer_stack_vector.push_back(struct_variable_pointer);
                    struct_depth_current++;
                    struct_depth_current_backup = struct_depth_current; // when back_up != current , mean the current struct loop is over 
                    is_effective_extract = true;
                    continue;
                }
            }

            //  if find the contraint of struct
            if(line.find("END_STRUCT") != std::string::npos)
            {
                if(struct_depth_current > 0)
                {
                    //  when find "END_STRUCT" , mean the struct is end, so current struct level is decrease by 1 
                    struct_depth_current--;
                    if(struct_depth_current == 0)
                    {
                        struct_variable_pointer = nullptr;
                    }
                    else {
                        //  destory the LATEST struct pointer , and update the struct_variable_pointer
                        struct_pointer_stack_vector.pop_back();
                        struct_variable_pointer = struct_pointer_stack_vector.back();
                    }
                }
                continue;
            }

            //  if find the variable or struct member variable in file_content 
            if(line.find(":") != std::string::npos && line.find("END_STRUCT") == std::string::npos)
            {
                auto result = parse_variable_declaration(line); 
                if(result.is_success())
                {
                    S7XMLVariableDefinition var_def = result.unwrap_returnLeftValue();
                    //  if find the struct member variable  (struct_depth_current != struct_depth_current_backup mean child struct had done now)
                    if(struct_variable_pointer != nullptr && struct_depth_current == struct_depth_current_backup)
                    {
                        struct_variable_pointer->struct_member_vector.push_back(var_def);
                    }
                    //  if find the variable
                    else {
                        //  add array member variable into array member vector
                        if(var_def.data_type_enum == S7DataType::ARRAY )
                        {
                            add_array_member(var_def);
                        }
                        data_block_var.variable_definitions_vector.push_back(var_def);
                    }
                    is_effective_extract = true;
                }
            }
        }
        else {
            continue;
        }
    }
    if(is_effective_extract)
    {
        return Result<OPCUADataBlockDefinition,RichError>(data_block_var);
    }
    else {
        return Result<OPCUADataBlockDefinition,RichError>(RichError("variable extract failed"));
    }
}

Result<bool,RichError> SCL_Parser::add_array_member(S7XMLVariableDefinition& var)
{ 
    for(size_t i = 0;i < var.s7_data_array_length;i++)
    {
        S7XMLVariableDefinition array_member;
        array_member.variable_name = std::to_string(i);
        array_member.data_type_enum = var.array_type_enum;
        //  assign array member variable length
        array_member.s7_data_type_length = return_type_size(var.array_type_enum).unwrap_returnRightValue();
        array_member.s7_data_array_length = var.s7_data_array_length;

        var.struct_member_vector.push_back(array_member);
    }
    auto type_size = return_type_size(var.array_type_enum);
    if(type_size.is_success())
    {
        var.s7_data_type_length = var.s7_data_type_length * type_size.unwrap_returnLeftValue() ;
    }
    else{
        return Result<bool,RichError>(type_size.unwrap_err());
    }
    return Result<bool,RichError>(true);
}

Result<S7XMLVariableDefinition,RichError> SCL_Parser::parse_variable_declaration(std::string& line)
{ 
    S7XMLVariableDefinition var_def;
    //  extract position syboml : (e.g. "Motor:Struct")  
    size_t colon_pos = line.find(":");
    if(colon_pos != std::string::npos)
    {
        //  extract variable name , declaration of type , comment
       if(trim(line.substr(0,colon_pos)).is_success() && trim(line.substr(colon_pos+1)).is_success()) 
       {
            // (e.g. "Motor:Struct") variable_name = Motor , type_occupies_byte_count = Struct 
            var_def.variable_name = trim(line.substr(0,colon_pos)).unwrap_returnRightValue();
            std::string type_decl =trim(line.substr(colon_pos+1)).unwrap_returnRightValue(); 

            // Status : Bool;   // 电机的运行状态 
            size_t comment_pos = type_decl.find("//");
            if(comment_pos != std::string::npos)
            {
                var_def.comment = trim(type_decl.substr(comment_pos+2)).unwrap_returnRightValue();
            }

            //  parse type
            if (parse_data_type(type_decl,var_def).is_success())
            {
                return Result<S7XMLVariableDefinition,RichError>(var_def);
            }
            else {
                return Result<S7XMLVariableDefinition,RichError>(RichError("parse variable type failed"));
            }
        }
        return Result<S7XMLVariableDefinition,RichError>(RichError("parse variable name and type failed"));
    }
    return Result<S7XMLVariableDefinition,RichError>(RichError("extract position symbol failed"));
}

Result<S7DataType,RichError> SCL_Parser::transform_string_to_S7DataType(const std::string& str)
{
    if(str == "Bool")
    {
        return Result<S7DataType,RichError>(S7DataType::BOOL);
    }
    else if(str == "Byte")
    {
        return Result<S7DataType,RichError>(S7DataType::BYTE);
    }
    else if(str == "DInt")
    {
        return Result<S7DataType,RichError>(S7DataType::DINT);
    }
    else if(str == "Int")
    {
        return Result<S7DataType,RichError>(S7DataType::INT);
    }
    else if(str == "Real")
    {
        return Result<S7DataType,RichError>(S7DataType::REAL);
    }
    else if(str == "String")
    {
        return Result<S7DataType,RichError>(S7DataType::STRING);
    }
    else{
        return Result<S7DataType,RichError>(RichError("transform string to S7DataType failed"));
    }
}

Result<int, RichError> SCL_Parser::return_type_size(S7DataType type)
{ 
    if(type == S7DataType::INT)
    {
        return Result<int,RichError>(2);
    }
    else if(type == S7DataType::BYTE)
    {
        return Result<int,RichError>(1);
    }
    else if(type == S7DataType::REAL)
    {
        return Result<int,RichError>(4);
    }
    else if(type == S7DataType::STRING)
    {
        return Result<int,RichError>(1);
    }
    else if(type == S7DataType::DINT)
    {
        return Result<int,RichError>(4);
    }
    else if(type == S7DataType::BOOL)
    {
        return Result<int,RichError>(1);
    }
    else if(type == S7DataType::WORD)
    {
        return Result<int,RichError>(2);
    }
    else if(type == S7DataType::DWORD || type == S7DataType::UDINT)
    {
        return Result<int,RichError>(4);
    }
    else {
        return Result<int,RichError>(RichError("return type size failed "));
    }
}

Result<bool,RichError> SCL_Parser::parse_data_type(std::string& type_decl,S7XMLVariableDefinition& var)
{
    if(type_decl.find("Array") != std::string::npos)
    {
        var.data_type_enum = S7DataType::ARRAY;
        //  extract length of array
        std::regex array_regex(R"(Array\[(\d+)\.\.(\d+)\]\s+of\s+(\w+))", std::regex_constants::icase);
        std::smatch match;
        if(std::regex_search(type_decl,match,array_regex))
        {
            int start = std::stoi(match[1]);
            int end = std::stoi(match[2]);
            var.array_type_enum = transform_string_to_S7DataType(match[3]).unwrap_returnRightValue();
            var.s7_data_array_length = (end - start + 1) ;
        }
    }
    else if(type_decl.find("Bool") != std::string::npos)
    {
        var.s7_data_type_length = 1;
        var.data_type_enum = S7DataType::BOOL;
    }
    else if(type_decl.find("Byte") != std::string::npos)
    {
        var.s7_data_type_length = 1;
        var.data_type_enum = S7DataType::BYTE;
    }
    else if(type_decl.find("DWord") != std::string::npos)
    {
        var.s7_data_type_length = 4;
        var.data_type_enum = S7DataType::DWORD;
    }
    else if(type_decl.find("Word") != std::string::npos)
    {
        var.s7_data_type_length = 2;
        var.data_type_enum = S7DataType::WORD;
    }
    else if(type_decl.find("UDInt") != std::string::npos)
    {
        var.s7_data_type_length = 4;
        var.data_type_enum = S7DataType::UDINT;
    }
    else if(type_decl.find("Real") != std::string::npos)
    {
        var.s7_data_type_length = 4;
        var.data_type_enum = S7DataType::REAL;
    }
    else if(type_decl.find("DInt") != std::string::npos)
    {
        //  DINT should be placed before INT , otherwise DINT will not be retrieved due to their similarity
        var.s7_data_type_length = 4;
        var.data_type_enum = S7DataType::DINT;
    }
    else if(type_decl.find("Int")!= std::string::npos)
    {
        var.s7_data_type_length = 2;
        var.data_type_enum = S7DataType::INT;
    }
    else if(type_decl.find("String") != std::string::npos)
    {
        var.data_type_enum = S7DataType::STRING;
        //  extract length of string
        std::regex str_regex(R"(String\[(\d+)\])");
        std::smatch match;
        if(std::regex_search(type_decl,match,str_regex))
        {
            var.s7_data_type_length = std::stoi(match[1]) + 2;
        }
    }
    else{
        //  type:STRUCT corresponding assignment in extract_variable_from_content function()
        return Result<bool,RichError>(RichError("unknown data type"));    
    }
    return Result<bool,RichError>(true);
}

Result<OPCUADataBlockDefinition,RichError> SCL_Parser::calculate_data_block_size(OPCUADataBlockDefinition& data_block_definition)
{ 
    //  RECORD LAST USEABLE POSITION
    int last_free_byte_offset = 0;
    //  RECORD LAST USED POSITION 
    float last_used_var_byte_offset = -1;
    int current_bit_quality = 0;
    bool is_effective_calculate = true;
    S7DataType last_data_S7_type = S7DataType::UNKNOWN;
    for (auto& var : data_block_definition.variable_definitions_vector)
    {
        is_effective_calculate &= calculate_variable_offset(var,last_free_byte_offset,current_bit_quality,last_used_var_byte_offset,last_data_S7_type).is_success();
        last_used_var_byte_offset = var.bytes_offset;
    }

    if(is_effective_calculate)
    {
        data_block_definition.total_bytes_size = last_free_byte_offset;
        return Result<OPCUADataBlockDefinition,RichError>(data_block_definition);
    }
    else {
        return Result<OPCUADataBlockDefinition,RichError>(RichError("calculate data block size failed"));
    }
}


Result<bool,RichError> SCL_Parser::calculate_variable_offset(S7XMLVariableDefinition& var,int& last_free_byte_offset,int& current_bit_quality,float& last_used_var_byte_offset,S7DataType& last_data_S7_type)
{
    // update the latest byte position to be allocated 
    switch(var.data_type_enum)
    {
        case S7DataType::BOOL:
            // //  set this length for satisfied with read/write data length parameters
            // current_bit_quality += 1;
            // if((current_bit_quality - 1)%8 == 0)
            // {
            //     //  this is next byte for bit , so initialize last underfill byte offset
            //     last_used_var_byte_offset = last_free_byte_offset;
            //     //  because this byte is underfill ,so next var start position is next byte
            //     last_free_byte_offset += 1;
            // }
            // // when the byte is underfill , the byte offset is the last underfill byte offset 
            // var.bytes_offset = last_used_var_byte_offset;
            // var.bit_offset = (current_bit_quality - 1)%8;

            //  CHECK THE EXISTENCE OF CONTINUOUS BOLL VARIABLE
            if(last_data_S7_type == S7DataType::BOOL)
            {
                //  IN S7 , VARIABLE_OFFSET DECIMAL PART IS EQUAL TO OR LESS THAN 7
                int used_decimal_part = last_used_var_byte_offset * 10 - std::floor(last_used_var_byte_offset) * 10 ;
                if(used_decimal_part < 7)
                {
                    var.bytes_offset = last_used_var_byte_offset + 0.1;
                    var.bit_offset = used_decimal_part + 1 ;
                }
                else
                {
                    var.bytes_offset = std::floor(last_used_var_byte_offset) + 1;
                    var.bit_offset = 0 ;
                }
                //  float PART CAN UPDATE BY VAR.BYTES_OFFSET , BECAUSE 
                last_free_byte_offset = (std::floor(var.bytes_offset) + 1);
            }
            else
            {
                var.bytes_offset = last_free_byte_offset;
                var.bit_offset = 0 ;
                last_free_byte_offset = var.bytes_offset + 1 ;
                // if(int(last_free_byte_offset) %2 != 0 || last_free_byte_offset == 0)
                // {
                // }
                // else
                // {
                //     last_free_byte_offset = var.bytes_offset + 2 ;
                // }
            }
            last_data_S7_type = S7DataType::BOOL;
            //  AVOID THE RUNNING OF ASSIGNMENT OF IS_CONTINUOUS_BOOL TO FALSE
            return Result<bool,RichError>(true);
        case S7DataType::BYTE:
            last_data_S7_type = S7DataType::BYTE;
            var.bytes_offset = last_free_byte_offset ;
            last_free_byte_offset = var.bytes_offset +  1 ;
            break;
        case S7DataType::INT:
            last_data_S7_type = S7DataType::INT;
            if(last_free_byte_offset % 2 == 0)
            {
                var.bytes_offset = last_free_byte_offset ;
            }
            else
            {
                var.bytes_offset = last_free_byte_offset + 1;
            }
            last_free_byte_offset = var.bytes_offset + 2;
            break;
        case S7DataType::WORD:
            last_data_S7_type = S7DataType::WORD;
            if(last_free_byte_offset % 2 == 0)
            {
                var.bytes_offset = last_free_byte_offset ;
            }
            else
            {
                var.bytes_offset = last_free_byte_offset + 1;
            }
            last_free_byte_offset = var.bytes_offset + 2;
            break;
        case S7DataType::DWORD:
            last_data_S7_type = S7DataType::DWORD;
            if(last_free_byte_offset % 2 == 0)
            {
                var.bytes_offset = last_free_byte_offset ;
            }
            else
            {
                var.bytes_offset = last_free_byte_offset + 1;
            }
            last_free_byte_offset = var.bytes_offset + 4;
            break;
        case S7DataType::UDINT:
            last_data_S7_type = S7DataType::UDINT;
            if(last_free_byte_offset % 2 == 0)
            {
                var.bytes_offset = last_free_byte_offset ;
            }
            else
            {
                var.bytes_offset = last_free_byte_offset + 1;
            }
            last_free_byte_offset = var.bytes_offset + 4;
            break;
        case S7DataType::DINT:
            last_data_S7_type = S7DataType::DINT;
            if (last_free_byte_offset % 2 == 0) {
              var.bytes_offset = last_free_byte_offset;
            } else {
              var.bytes_offset = last_free_byte_offset + 1;
            }
            last_free_byte_offset = var.bytes_offset + 4;
            break; 
        case S7DataType::REAL:
            last_data_S7_type = S7DataType::REAL;
            if (last_free_byte_offset % 2 == 0) {
              var.bytes_offset = last_free_byte_offset;
            } else {
              var.bytes_offset = last_free_byte_offset + 1;
            }
            last_free_byte_offset = var.bytes_offset + 4;
            break;
        case S7DataType::STRING:
            last_data_S7_type = S7DataType::STRING;
            var.bytes_offset = last_free_byte_offset ;
            last_free_byte_offset += (var.s7_data_type_length)%2 ? (var.s7_data_type_length/2 + 1) *2 : var.s7_data_type_length ;
            break;
        case S7DataType::ARRAY:
            for(auto &member : var.struct_member_vector)
            {
                calculate_variable_offset(member,last_free_byte_offset,current_bit_quality,last_used_var_byte_offset,last_data_S7_type);
                last_used_var_byte_offset = member.bytes_offset;
            }
            break;
        case S7DataType::STRUCT:
            for (auto& member : var.struct_member_vector)
            {
                calculate_variable_offset(member,last_free_byte_offset,current_bit_quality,last_used_var_byte_offset,last_data_S7_type);
                last_used_var_byte_offset = member.bytes_offset;
            }
            break;
        default:
            return Result<bool,RichError>(RichError("unknown data type"));
    }
    return Result<bool,RichError>(true);
}

Result<std::string,RichError> SCL_Parser::trim(const std::string& str)
{
    size_t start = str.find_first_not_of(" \t\n\r");
    size_t end = str.find_last_not_of(" \t\n\r");
    return (start == std::string::npos) ? "" : str.substr(start, end - start + 1);
}

Result<std::string, RichError> SCL_Parser::extract_variable_name(const std::string& str)
{
    size_t colon_pos = str.find(":");
    if (colon_pos != std::string::npos) {
        return trim(str.substr(0,colon_pos));
    }
    return Result<std::string,RichError>(RichError("variable name not found"));
}

Result<std::string,RichError> SCL_Parser::extract_variable_comment(const std::string& str)
{
    size_t comment_pos = str.find("//");
    if (comment_pos != std::string::npos) {
        return trim(str.substr(comment_pos,2));
    }
    return Result<std::string,RichError>(RichError("comment content not found"));
}

