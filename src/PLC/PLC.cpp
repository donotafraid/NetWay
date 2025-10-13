#include "PLC/PLC.h"

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

Result<DataBlockDefinition,RichError> SCL_Parser::parse(const std::string& file_path){
    auto result = read_file_content(file_path)
    .transform_func([this]( std::string& file_content){return parse_data_block_header(file_content);})
    .transform_func([this]( DataBlockDefinition& db_def){return parse_variable(db_def);})
    .transform_func([this]( DataBlockDefinition& db_def){return calculate_data_block_size(db_def);})
    ;

    return result;
}

Result<DataBlockDefinition,RichError> SCL_Parser::parse_data_block_header( std::string& file_content){
    DataBlockDefinition db_def;
    std::regex db_grex (R"(DATA_BLOCK\s+\"([^\"]+)\")");
    std::smatch match ;

    std::regex num_regex(R"(DB(\d+))");
    if(std::regex_search(file_content,match,db_grex))
    {
        db_def.data_block_name = match[1];

        //  extract DB number in DB name
        std::regex num_regex(R"(DB(\d+))");
        if(std::regex_search(db_def.data_block_name,match,num_regex))
        {
            db_def.block_number = std::stoi(match[1]);
        }

        db_def.optimized_access = file_content.find("S7_Optimized_Access := 'TRUE'") != std::string::npos;
        return Result<DataBlockDefinition,RichError>(db_def);
    }
    else {
        return Result<DataBlockDefinition,RichError>(RichError("DB header search failed"));
    }
}

Result<DataBlockDefinition,RichError> SCL_Parser::parse_variable( DataBlockDefinition& data_block_definition){ 
    size_t var_start = m_file_content.find("VAR"); 
    if(var_start == std::string::npos)
    {
        return Result<DataBlockDefinition,RichError>(RichError("var start position not found"));
    }

    size_t var_end = m_file_content.find("END_VAR");
    if(var_end == std::string::npos)
    {
        return Result<DataBlockDefinition,RichError>(RichError("var position end not found"));
    }

    std::string var_content = m_file_content.substr(var_start+3,var_end-var_start-3);
    std::istringstream stream(var_content);
    std::string line;
    return extract_variable_from_content(stream,line,data_block_definition);
}

Result<DataBlockDefinition,RichError> SCL_Parser::extract_variable_from_content(std::istringstream& file_content_stream,std::string& line,DataBlockDefinition& data_block_var)
{
    VariableDefinition* struct_variable_pointer = nullptr;   //  used for indicate the struct pointer that store struct member variable these variable level in n+1
    std::vector<VariableDefinition*> struct_pointer_stack_vector ;  //  used for store the struct pointer for rebuild the parent-child struct
    int current_byte_offset = 0;
    int struct_depth = 0;   //  used for indicate the struct level n  where the current structure is located 
    bool is_effective_extract = false;
    int struct_depth_backup = 0;

       while(std::getline(file_content_stream, line))
    {
        //  check the existence of variable
        if(trim(line).is_success())
        {
            line = trim(line).unwrap();
            //  if find the struct in file_content
            if (line.find("Struct") != std::string::npos)
            {
                //  try store the struct variable (e.g Motor:Struct) 
                VariableDefinition struct_variable;
                if(extract_variable_name(line).is_success()&&
                extract_variable_comment(line).is_success())
                {
                    struct_variable.variable_name = extract_variable_name(line).unwrap();
                    struct_variable.data_type_enum = S7DataType::STRUCT;
                    struct_variable.comment = extract_variable_comment(line).unwrap();

                    data_block_var.variable_definitions_vector.push_back(struct_variable);
                    struct_variable_pointer = &data_block_var.variable_definitions_vector.back();
                    struct_pointer_stack_vector.push_back(struct_variable_pointer);
                    struct_depth++;
                    struct_depth_backup = struct_depth; //  backup the struct depth , when the struct level is add , means there is new struct build ,so need update struct_depth_backup
                    is_effective_extract = true;
                    continue;
                }
            }

            //  if find the contraint of struct
            if(line.find("END_STRUCT") != std::string::npos)
            {
                if(struct_depth > 0)
                {
                    //  when find "END_STRUCT" , mean the struct is end, so current struct level is decrease by 1 
                    struct_depth--;
                    if(struct_depth == 0)
                    {
                        struct_variable_pointer = nullptr;
                    }
                    else {
                        //  destory the lasest struct pointer , and update the struct_variable_pointer
                        struct_pointer_stack_vector.pop_back();
                        struct_variable_pointer = struct_pointer_stack_vector.back();
                    }
                }
                continue;
            }

            //  if find the variable or struct member variable in file_content 
            if(line.find(":") != std::string::npos && line.find("END_STRUCT") == std::string::npos)
            {
                if(parse_variable_declaration(line).is_success())
                {
                    VariableDefinition var_def = parse_variable_declaration(line).unwrap();
                    //  if find the struct member variable  (struct_depth != struct_depth_backup mean child struct had done now)
                    if(struct_variable_pointer != nullptr && struct_depth == struct_depth_backup)
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
        return Result<DataBlockDefinition,RichError>(data_block_var);
    }
    else {
        return Result<DataBlockDefinition,RichError>(RichError("variable extract failed"));
    }
}

Result<bool,RichError> SCL_Parser::add_array_member(VariableDefinition& var)
{ 
    for(size_t i = 0;i < var.array_size;i++)
    {
        VariableDefinition arrary_member;
        arrary_member.variable_name = std::to_string(i);
        arrary_member.data_type_enum = var.array_type_enum;

        var.struct_member_vector.push_back(arrary_member);
    }
    return Result<bool,RichError>(true);
}

Result<VariableDefinition,RichError> SCL_Parser::parse_variable_declaration(std::string& line)
{ 
    VariableDefinition var_def;
    //  extract position syboml : (e.g. "Motor:Struct")  
    size_t colon_pos = line.find(":");
    if(colon_pos != std::string::npos)
    {
        //  extract variable name , declaration of type , comment
       if(trim(line.substr(0,colon_pos)).is_success() && trim(line.substr(colon_pos+1)).is_success()) 
       {
            // (e.g. "Motor:Struct") variable_name = Motor , type_occupies_byte_count = Struct 
            var_def.variable_name = trim(line.substr(0,colon_pos)).unwrap();
            std::string type_decl =trim(line.substr(colon_pos+1)).unwrap(); 

            // Status : Bool;   // 电机的运行状态 
            size_t comment_pos = type_decl.find("//");
            if(comment_pos != std::string::npos)
            {
                var_def.comment = trim(type_decl.substr(comment_pos+2)).unwrap();
            }

            //  parse type
            if (parse_data_type(type_decl,var_def).is_success())
            {
                return Result<VariableDefinition,RichError>(var_def);
            }
            else {
                return Result<VariableDefinition,RichError>(RichError("parse variable type failed"));
            }
        }
        return Result<VariableDefinition,RichError>(RichError("parse variable name and type failed"));
    }
    return Result<VariableDefinition,RichError>(RichError("extract position symbol failed"));
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
}

Result<bool,RichError> SCL_Parser::parse_data_type(std::string& type_decl,VariableDefinition& var)
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
            var.array_size = end - start + 1;

            var.array_type_enum = transform_string_to_S7DataType(match[3]).unwrap();
        }
    }
    else if(type_decl.find("Bool") != std::string::npos)
    {
        var.data_type_enum = S7DataType::BOOL;
    }
    else if(type_decl.find("Byte") != std::string::npos)
    {
        var.data_type_enum = S7DataType::BYTE;
    }
    else if(type_decl.find("Word") != std::string::npos)
    {
        var.data_type_enum = S7DataType::WORD;
    }
    else if(type_decl.find("Real") != std::string::npos)
    {
        var.data_type_enum = S7DataType::REAL;
    }
    else if(type_decl.find("Int")!= std::string::npos)
    {
        var.data_type_enum = S7DataType::INT;
    }
    else if(type_decl.find("DInt") != std::string::npos)
    {
        var.data_type_enum = S7DataType::DINT;
    }
    else if(type_decl.find("String") != std::string::npos)
    {
        var.data_type_enum = S7DataType::STRING;
        //  extract length of string
        std::regex str_regex(R"(String\[(\d+)\])");
        std::smatch match;
        if(std::regex_search(type_decl,match,str_regex))
        {
            var.string_length = std::stoi(match[1]);
        }
    }
    else{
        //  type:STRUCT corresponding assignment in extract_variable_from_content function()
        return Result<bool,RichError>(RichError("unknown data type"));    
    }
    return Result<bool,RichError>(true);
}

Result<DataBlockDefinition,RichError> SCL_Parser::calculate_data_block_size(DataBlockDefinition& data_block_definition)
{ 
    int current_byte_offset = 0;
    int current_bit_quality = 0;
    int last_underfill_byte_offset = -1;
    bool is_effective_calculate = true;
    for (auto& var : data_block_definition.variable_definitions_vector)
    {
        is_effective_calculate &= calculate_variable_offset(var,current_byte_offset,current_bit_quality,last_underfill_byte_offset).is_success();
    }
    //  update total bytes size
    data_block_definition.total_bytes_size +=current_byte_offset; 

    if(is_effective_calculate)
    {
        data_block_definition.total_bytes_size = current_byte_offset;
        return Result<DataBlockDefinition,RichError>(data_block_definition);
    }
    else {
        return Result<DataBlockDefinition,RichError>(RichError("calculate data block size failed"));
    }
}


Result<bool,RichError> SCL_Parser::calculate_variable_offset(VariableDefinition& var,int& current_byte_offset,int& current_bit_quality,int& last_underfill_byte_offset)
{
    //  Allocate ownership of the next byte or multiple bytes  
    var.bytes_offset= current_byte_offset;
    
    // update the latest byte position to be allocated 
    switch(var.data_type_enum)
    {
        case S7DataType::BOOL:
            current_bit_quality += 1;
            if((current_bit_quality - 1)%8 == 0)
            {
                //  this is next byte for bit , so initialize last underfill byte offset
                last_underfill_byte_offset = current_byte_offset;
                //  because this byte is underfill ,so next var start position is next byte
                current_byte_offset += 1;
            }
            // when the byte is underfill , the byte offset is the last underfill byte offset 
            var.bytes_offset = last_underfill_byte_offset;
            var.bit_offset = (current_bit_quality - 1)%8;
            break;
        case S7DataType::BYTE:
            current_byte_offset += 1;
            break;
        case S7DataType::INT:
            current_byte_offset += 2;
            break;
        case S7DataType::WORD:
            current_byte_offset += 2;
            break;
        case S7DataType::DINT:
            current_byte_offset += 4;
            break; 
        case S7DataType::REAL:
            current_byte_offset += 4;
            break;
        case S7DataType::STRING:
            current_byte_offset += var.string_length + 2;
            break;
        case S7DataType::ARRAY:
            if(var.array_type_enum == S7DataType::REAL )
            {
                for(auto &member : var.struct_member_vector)
                {
                    member.bytes_offset = current_byte_offset;
                    current_byte_offset += 4;
                }
            }
            break;
        case S7DataType::STRUCT:
            for (auto& member : var.struct_member_vector)
            {
                calculate_variable_offset(member,current_byte_offset,current_bit_quality,last_underfill_byte_offset);
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

