#include "PLC/MapperStrategy.h"
#include <cmath>
#include <cctype>
#include "PLC/OpcUaDataNode.h"
//===============opcUaMapper===================
std::vector<std::shared_ptr<IDataNode>>
OpcUaMapper::map(const RawDataTable &rawTable) const {
    std::vector<std::shared_ptr<IDataNode>> result;
    calculateBlock calStruct{};
    result.reserve(rawTable.size());

    for (const auto &field : rawTable) {
        OPCUAModernDataStruct var;

        // ========== 1. 映射通用基础字段 ==========
        var.variable_name = field.name;
        if (var.variable_name.empty()) {
            var.variable_name = getMeta(field, "DisplayName");
            if (var.variable_name.empty()) {
                var.variable_name = getMeta(field, "BrowseName");
            }
        }

        var.data_type = field.data_type;
        var.raw_data_type = field.data_type;

        var.description = getMeta(field, "Description");
        if (var.description.empty()) {
            var.description = getMeta(field, "Comment");
        }

        // ========== 2. 映射 OPC UA 特有字段 ==========
        std::string fullNodeId = getMeta(field, "NodeId");
        if (!fullNodeId.empty()) {
            var.variable_nodeID = fullNodeId;
            var.variable_full_path = fullNodeId;
            parseNodeId(fullNodeId, var.namespace_index, var.variable_nodeID);
        } else {
            var.namespace_index = 3;
            var.variable_nodeID = "s=" + var.variable_name;
        }

        var.browse_name = getMeta(field, "BrowseName");
        if (var.browse_name.empty() && !var.variable_name.empty()) {
            var.browse_name = var.variable_name;
        }

        var.parent_nodeID = getMeta(field, "ParentNodeId");
        var.parent_path = var.parent_nodeID;
        if (var.parent_nodeID.empty()) {
            var.parent_nodeID = "Objects";
        }

        // ========== 3. 数组处理 ==========
        std::string valueRankStr = getMeta(field, "ValueRank");
        std::string arrayDimsStr = getMeta(field, "ArrayDimensions");

        if (!valueRankStr.empty()) {
          try {
            int valueRank = std::stoi(valueRankStr);
            var.is_array = (valueRank >= 0);
          } catch (...) {
            var.is_array = false;
          }
        } else {
          var.is_array = !arrayDimsStr.empty();
        }

        if (!arrayDimsStr.empty()) {
          var.arrayDimensions = arrayDimsStr;
        }

        var.data_type = getMeta(field, "DataType");
        shouldKeepVariable(var.variable_name, var.variable_nodeID,
                           var.data_type, var.browse_name, var.filter_reason,
                           var.is_array, valueRankStr);
        // 注意：这里原来有 typeMap 查找逻辑，需要确保 typeMap 已定义
        // 如果 typeMap 在类外部定义，需要在此处使用
        auto it = typeMap.find(var.data_type);
        if (it != typeMap.end() && var.filter_reason == "") {
            var.data_type_enum = it->second;
            calculate_variable_offset(var, calStruct );
            calStruct.last_used_var_byte_offset = var.bytes_offset;
        }
        else
        {
            var.data_type_enum = S7DataType::UNKNOWN;
        }

        std::string accessStr = getMeta(field, "AccessLevel");
        if (!accessStr.empty()) {
            try {
                var.access_level = std::stoi(accessStr);
            } catch (...) {
                var.access_level = 0;
            }
        } else {
            var.access_level = 3;
        }

        // ========== 4. 初始化其他字段 ==========
        var.dataValue = 0;
        var.isOPCUAType = false;

        // ========== 5. 过滤并添加节点 ==========
        if (var.filter_reason == "Array" &&
                var.parent_nodeID.find(";s=") != std::string::npos ||
            var.filter_reason == "") {
          result.push_back(std::make_shared<OpcUaDataNode>(std::move(var)));
        } else {
          continue;
        }
    }

    return {result};
}

// ==================== 辅助工具函数实现 ====================

std::string OpcUaMapper::getMeta(const RawDataField &field, const std::string &key) const {
    // 先尝试精确匹配
    auto it = field.metadata.find(key);
    if (it != field.metadata.end()) {
        return it->second;
    }

    // 再尝试忽略大小写匹配
    std::string lowerKey = key;
    std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), ::tolower);

    for (const auto &[k, v] : field.metadata) {
        std::string lowerK = k;
        std::transform(lowerK.begin(), lowerK.end(), lowerK.begin(), ::tolower);
        if (lowerK == lowerKey) {
            return v;
        }
    }
    return "";
}

void OpcUaMapper::parseNodeId(const std::string &fullNodeId, int &outNamespace,
                              std::string &outIdentifier) const {
    outNamespace = 0;
    outIdentifier = fullNodeId;

    std::regex nsRegex(R"(ns=(\d+);[si]=(.+))");
    std::smatch match;
    if (std::regex_search(fullNodeId, match, nsRegex) && match.size() == 3) {
        try {
            outNamespace = std::stoi(match[1].str());
            outIdentifier = match[2].str();
        } catch (...) {
            // 解析失败，保持原样
        }
    }
}

std::string OpcUaMapper::extractLastPartWithoutIndex(const std::string &input) const {
    std::regex pattern("\"([^\"]+)\"$");
    std::smatch match;
    if (std::regex_search(input, match, pattern)) {
        return match[1].str();
    }
    return "";
}

bool OpcUaMapper::shouldKeepVariable(const std::string &var_name,
                                     const std::string &node_id,
                                     const std::string &type_id,
                                     const std::string &browse_name,
                                     std::string &filter_reason,bool isArray,std::string &valueRankStr) const {
    // 1. 空名称
    if (var_name.empty()) {
        filter_reason = "empty_name";
        return false;
    }

    // 2. 纯数字节点ID过滤
    bool is_pure_digit = std::all_of(node_id.begin(), node_id.end(), ::isdigit);
    if (is_pure_digit) {
        filter_reason = "pure_digit";
        return false;
    }

    // 3. 黑名单过滤
    if (SYSTEM_VARIABLES_BLACKLIST.find(node_id) != SYSTEM_VARIABLES_BLACKLIST.end()) {
        filter_reason = "black_list";
        return false;
    }

    // 3. 黑名单过滤
    if (SYSTEM_VARIABLES_BLACKLIST.find(browse_name) != SYSTEM_VARIABLES_BLACKLIST.end()) {
        filter_reason = "black_list";
        return false;
    }

    // // 4. 类型ID特征过滤
    // if (type_id.find("V_") != std::string::npos ||
    //     type_id.find("VT_") != std::string::npos ||
    //     type_id.find("TE_") != std::string::npos ||
    //     type_id.find("DT_") != std::string::npos) {
    //     filter_reason = "type_id";
    //     return false;
    // }

    // 5. 节点ID特征过滤
    if (node_id.find("DataType") != std::string::npos ||
        node_id.find("VariableType") != std::string::npos ||
        node_id.find("V_") != std::string::npos ||
        node_id.find("VT_") != std::string::npos ||
        node_id.find("TE_") != std::string::npos ||
        node_id.find("DT_") != std::string::npos ) {
      filter_reason = "node_id";
      return false;
    }

    if(isArray)
    {
        filter_reason="Array";
        return false;
    }

    filter_reason = "";
    return true;
}

Result<bool, RichError> OpcUaMapper::calculate_variable_offset(
    OPCUAModernDataStruct &var, calculateBlock &calStruct) const {
    
    switch (var.data_type_enum) {
    case S7DataType::BOOL: {
        if (calStruct.last_data_S7_type == S7DataType::BOOL) {
            int used_decimal_part = static_cast<int>(
                calStruct.last_used_var_byte_offset * 10 - 
                std::floor(calStruct.last_used_var_byte_offset) * 10
            );
            if (used_decimal_part < 7) {
                var.bytes_offset = calStruct.last_used_var_byte_offset + 0.1f;
                var.bit_offset = used_decimal_part + 1;
            } else {
                var.bytes_offset = std::floor(calStruct.last_used_var_byte_offset) + 1;
                var.bit_offset = 0;
            }
            calStruct.last_free_byte_offset = (std::floor(var.bytes_offset) + 1);
        } else {
            var.bytes_offset = calStruct.last_free_byte_offset;
            var.bit_offset = 0;
            calStruct.last_free_byte_offset = var.bytes_offset + 1;
        }
        calStruct.last_data_S7_type = S7DataType::BOOL;
        return Result<bool, RichError>(true);
    }
    
    case S7DataType::BYTE:
        calStruct.last_data_S7_type = S7DataType::BYTE;
        var.bytes_offset = calStruct.last_free_byte_offset;
        calStruct.last_free_byte_offset = var.bytes_offset + 1;
        break;
        
    case S7DataType::INT:
    case S7DataType::WORD:
        calStruct.last_data_S7_type = var.data_type_enum;
        if (calStruct.last_free_byte_offset % 2 == 0) {
            var.bytes_offset = calStruct.last_free_byte_offset;
        } else {
            var.bytes_offset = calStruct.last_free_byte_offset + 1;
        }
        calStruct.last_free_byte_offset = var.bytes_offset + 2;
        break;
        
    case S7DataType::DWORD:
    case S7DataType::UDINT:
    case S7DataType::DINT:
    case S7DataType::REAL:
        calStruct.last_data_S7_type = var.data_type_enum;
        if (calStruct.last_free_byte_offset % 2 == 0) {
            var.bytes_offset = calStruct.last_free_byte_offset;
        } else {
            var.bytes_offset = calStruct.last_free_byte_offset + 1;
        }
        calStruct.last_free_byte_offset = var.bytes_offset + 4;
        break;
        
    case S7DataType::STRING: {
        calStruct.last_data_S7_type = S7DataType::STRING;
        var.bytes_offset = calStruct.last_free_byte_offset;
        if(!m_probe)
        {
            break;
        }

        auto length = m_probe->probeStringLength(var.bytes_offset);
        if(length<=0)
        {
          var.s7_data_type_length = 2;
        }
        else
        {
          var.s7_data_type_length = length.value() + 2;
        }
       
        calStruct.last_free_byte_offset += 
            (var.s7_data_type_length % 2) ? (var.s7_data_type_length / 2 + 1) * 2
                                          : var.s7_data_type_length;
        break;
    }
    
    default:
        return Result<bool, RichError>(RichError("unknown data type"));
    }
    
    return Result<bool, RichError>(true);
}

void OpcUaMapper::setProbe(std::shared_ptr<IStringLengthProbe> probe) {
    if(probe)
    {
        m_probe = probe;
    }
}

std::unordered_map<std::string, PhysicalAddress>
OpcUaMapper::buildAddressMap(std::vector<std::shared_ptr<IDataNode>> &dataVec) {
  std::unordered_map<std::string, PhysicalAddress> map;
  for (const auto &field : dataVec) {
    PhysicalAddress addr;
    auto transformField = dynamic_cast<INodeManager *>(field.get());
    addr.protocolType = "OPCUA";
    addr.nodeId = transformField->getNodeId(); // OPC UA 专属提取
    addr.dataType = transformField->getDataType();
    addr.fullPath = transformField->getFullPath();
    addr.nameSpace = transformField->getNameSpace();
    map[addr.nodeId] = addr;
  }
  return map;
}


//================S7Mappper===============================
std::vector<std::shared_ptr<IDataNode>>
S7Mapper::map(const RawDataTable &rawTable) const {
    std::vector<std::shared_ptr<IDataNode>> result;
    calculateBlock calStruct{};
    result.reserve(rawTable.size());

    for (const auto &field : rawTable) {
        OPCUAModernDataStruct var;

        // ========== 1. 映射通用基础字段 ==========
        var.variable_name = field.name;
        if (var.variable_name.empty()) {
            var.variable_name = getMeta(field, "DisplayName");
            if (var.variable_name.empty()) {
                var.variable_name = getMeta(field, "BrowseName");
            }
        }

        var.data_type = field.data_type;
        var.raw_data_type = field.data_type;

        var.description = getMeta(field, "Description");
        if (var.description.empty()) {
            var.description = getMeta(field, "Comment");
        }

        // ========== 2. 映射 OPC UA 特有字段 ==========
        std::string fullNodeId = getMeta(field, "NodeId");
        if (!fullNodeId.empty()) {
            var.variable_nodeID = fullNodeId;
            var.variable_full_path = fullNodeId;
            parseNodeId(fullNodeId, var.namespace_index, var.variable_nodeID);
        } else {
            var.namespace_index = 3;
            var.variable_nodeID = "s=" + var.variable_name;
        }

        var.browse_name = getMeta(field, "BrowseName");
        if (var.browse_name.empty() && !var.variable_name.empty()) {
            var.browse_name = var.variable_name;
        }

        var.parent_nodeID = getMeta(field, "ParentNodeId");
        var.parent_path = var.parent_nodeID;
        if (var.parent_nodeID.empty()) {
            var.parent_nodeID = "Objects";
        }

        // ========== 3. 数组处理 ==========
        std::string valueRankStr = getMeta(field, "ValueRank");
        std::string arrayDimsStr = getMeta(field, "ArrayDimensions");

        if (!valueRankStr.empty()) {
          try {
            int valueRank = std::stoi(valueRankStr);
            var.is_array = (valueRank >= 0);
          } catch (...) {
            var.is_array = false;
          }
        } else {
          var.is_array = !arrayDimsStr.empty();
        }

        if (!arrayDimsStr.empty()) {
          var.arrayDimensions = arrayDimsStr;
        }

        var.data_type = getMeta(field, "DataType");
        shouldKeepVariable(var.variable_name, var.variable_nodeID,
                           var.data_type, var.browse_name, var.filter_reason,
                           var.is_array, valueRankStr);
        // 注意：这里原来有 typeMap 查找逻辑，需要确保 typeMap 已定义
        // 如果 typeMap 在类外部定义，需要在此处使用
        auto it = typeMap.find(var.data_type);
        if (it != typeMap.end() && var.filter_reason == "") {
            var.data_type_enum = it->second;
            calculate_variable_offset(var, calStruct );
            calStruct.last_used_var_byte_offset = var.bytes_offset;
        }
        else
        {
            var.data_type_enum = S7DataType::UNKNOWN;
        }

        std::string accessStr = getMeta(field, "AccessLevel");
        if (!accessStr.empty()) {
            try {
                var.access_level = std::stoi(accessStr);
            } catch (...) {
                var.access_level = 0;
            }
        } else {
            var.access_level = 3;
        }

        // ========== 4. 初始化其他字段 ==========
        var.dataValue = 0;
        var.isOPCUAType = false;

        // ========== 5. 过滤并添加节点 ==========
        if (var.filter_reason == "Array" &&
                var.parent_nodeID.find(";s=") != std::string::npos ||
            var.filter_reason == "") {
          result.push_back(std::make_shared<OpcUaDataNode>(std::move(var)));
        } else {
          continue;
        }
    }

    return {result};
}

// ==================== 辅助工具函数实现 ====================

std::string S7Mapper::getMeta(const RawDataField &field, const std::string &key) const {
    // 先尝试精确匹配
    auto it = field.metadata.find(key);
    if (it != field.metadata.end()) {
        return it->second;
    }

    // 再尝试忽略大小写匹配
    std::string lowerKey = key;
    std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), ::tolower);

    for (const auto &[k, v] : field.metadata) {
        std::string lowerK = k;
        std::transform(lowerK.begin(), lowerK.end(), lowerK.begin(), ::tolower);
        if (lowerK == lowerKey) {
            return v;
        }
    }
    return "";
}

void S7Mapper::parseNodeId(const std::string &fullNodeId, int &outNamespace,
                              std::string &outIdentifier) const {
    outNamespace = 0;
    outIdentifier = fullNodeId;

    std::regex nsRegex(R"(ns=(\d+);[si]=(.+))");
    std::smatch match;
    if (std::regex_search(fullNodeId, match, nsRegex) && match.size() == 3) {
        try {
            outNamespace = std::stoi(match[1].str());
            outIdentifier = match[2].str();
        } catch (...) {
            // 解析失败，保持原样
        }
    }
}

std::string S7Mapper::extractLastPartWithoutIndex(const std::string &input) const {
    std::regex pattern("\"([^\"]+)\"$");
    std::smatch match;
    if (std::regex_search(input, match, pattern)) {
        return match[1].str();
    }
    return "";
}

bool S7Mapper::shouldKeepVariable(const std::string &var_name,
                                     const std::string &node_id,
                                     const std::string &type_id,
                                     const std::string &browse_name,
                                     std::string &filter_reason,bool isArray,std::string &valueRankStr) const {
    // 1. 空名称
    if (var_name.empty()) {
        filter_reason = "empty_name";
        return false;
    }

    // 2. 纯数字节点ID过滤
    bool is_pure_digit = std::all_of(node_id.begin(), node_id.end(), ::isdigit);
    if (is_pure_digit) {
        filter_reason = "pure_digit";
        return false;
    }

    // 3. 黑名单过滤
    if (SYSTEM_VARIABLES_BLACKLIST.find(node_id) != SYSTEM_VARIABLES_BLACKLIST.end()) {
        filter_reason = "black_list";
        return false;
    }

    // 3. 黑名单过滤
    if (SYSTEM_VARIABLES_BLACKLIST.find(browse_name) != SYSTEM_VARIABLES_BLACKLIST.end()) {
        filter_reason = "black_list";
        return false;
    }

    // // 4. 类型ID特征过滤
    // if (type_id.find("V_") != std::string::npos ||
    //     type_id.find("VT_") != std::string::npos ||
    //     type_id.find("TE_") != std::string::npos ||
    //     type_id.find("DT_") != std::string::npos) {
    //     filter_reason = "type_id";
    //     return false;
    // }

    // 5. 节点ID特征过滤
    if (node_id.find("DataType") != std::string::npos ||
        node_id.find("VariableType") != std::string::npos ||
        node_id.find("V_") != std::string::npos ||
        node_id.find("VT_") != std::string::npos ||
        node_id.find("TE_") != std::string::npos ||
        node_id.find("DT_") != std::string::npos ) {
      filter_reason = "node_id";
      return false;
    }

    if(isArray)
    {
        filter_reason="Array";
        return false;
    }

    filter_reason = "";
    return true;
}

Result<bool, RichError> S7Mapper::calculate_variable_offset(
    OPCUAModernDataStruct &var, calculateBlock &calStruct) const {
    
    switch (var.data_type_enum) {
    case S7DataType::BOOL: {
        if (calStruct.last_data_S7_type == S7DataType::BOOL) {
            int used_decimal_part = static_cast<int>(
                calStruct.last_used_var_byte_offset * 10 - 
                std::floor(calStruct.last_used_var_byte_offset) * 10
            );
            if (used_decimal_part < 7) {
                var.bytes_offset = calStruct.last_used_var_byte_offset + 0.1f;
                var.bit_offset = used_decimal_part + 1;
            } else {
                var.bytes_offset = std::floor(calStruct.last_used_var_byte_offset) + 1;
                var.bit_offset = 0;
            }
            calStruct.last_free_byte_offset = (std::floor(var.bytes_offset) + 1);
        } else {
            var.bytes_offset = calStruct.last_free_byte_offset;
            var.bit_offset = 0;
            calStruct.last_free_byte_offset = var.bytes_offset + 1;
        }
        calStruct.last_data_S7_type = S7DataType::BOOL;

        var.s7_data_type_length = 1;
        return Result<bool, RichError>(true);
    }
    
    case S7DataType::BYTE:
        calStruct.last_data_S7_type = S7DataType::BYTE;
        var.bytes_offset = calStruct.last_free_byte_offset;
        calStruct.last_free_byte_offset = var.bytes_offset + 1;

        var.s7_data_type_length = 1;
        break;
        
    case S7DataType::INT:
    case S7DataType::WORD:
        calStruct.last_data_S7_type = var.data_type_enum;
        if (calStruct.last_free_byte_offset % 2 == 0) {
            var.bytes_offset = calStruct.last_free_byte_offset;
        } else {
            var.bytes_offset = calStruct.last_free_byte_offset + 1;
        }
        calStruct.last_free_byte_offset = var.bytes_offset + 2;

        var.s7_data_type_length = 2;
        break;
        
    case S7DataType::DWORD:
    case S7DataType::UDINT:
    case S7DataType::DINT:
    case S7DataType::REAL:
        calStruct.last_data_S7_type = var.data_type_enum;
        if (calStruct.last_free_byte_offset % 2 == 0) {
            var.bytes_offset = calStruct.last_free_byte_offset;
        } else {
            var.bytes_offset = calStruct.last_free_byte_offset + 1;
        }
        calStruct.last_free_byte_offset = var.bytes_offset + 4;
        var.s7_data_type_length = 4;
        break;
        
    case S7DataType::STRING: {
        calStruct.last_data_S7_type = S7DataType::STRING;
        var.bytes_offset = calStruct.last_free_byte_offset;

        auto length = m_probe->probeStringLength(var.bytes_offset);
        if(length<=0)
        {
          var.s7_data_type_length = 2;
        }
        else
        {
          var.s7_data_type_length = length.value() + 2;
        }
       
        calStruct.last_free_byte_offset += 
            (var.s7_data_type_length % 2) ? (var.s7_data_type_length / 2 + 1) * 2
                                          : var.s7_data_type_length;
        break;
    }
    
    default:
        return Result<bool, RichError>(RichError("unknown data type"));
    }
    
    return Result<bool, RichError>(true);
}

void S7Mapper::setProbe(std::shared_ptr<IStringLengthProbe> probe) {
  m_probe = probe;
}

std::unordered_map<std::string, PhysicalAddress>
S7Mapper::buildAddressMap(std::vector<std::shared_ptr<IDataNode>> &dataVec) {
  std::unordered_map<std::string, PhysicalAddress> map;
  for (const auto &field : dataVec) {
    PhysicalAddress addr;
    auto transformField = dynamic_cast<INodeManager *>(field.get());
    addr.protocolType = "S7";
    addr.nodeId = transformField->getNodeId(); // OPC UA 专属提取
    addr.byteOffset = transformField->getDataByte(); 
    addr.bitOffset = transformField->getDataBit(); 
    addr.data_type_length = transformField->getDataTypeLength();
    addr.dataType = transformField->getDataType();
    addr.nameSpace = transformField->getNameSpace();
    map[addr.nodeId] = addr;
  }
  return map;
}