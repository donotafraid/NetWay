#include "PLC/XMLParser.h"

// 构造函数
OPCUAXMLParser::OPCUAXMLParser(const std::string& content) 
    : xml_content(content) {
}

// XML 转义字符解码
std::string OPCUAXMLParser::decode_xml_entities(const std::string& input) {
    std::string result = input;
    
    // 定义转义映射表
    static const std::map<std::string, std::string> entities = {
        {"&quot;", "\""}, {"&apos;", "'"}, {"&amp;", "&"},
        {"&lt;", "<"},    {"&gt;", ">"},   {"&#34;", "\""}, // 数字形式双引号
        {"&#39;", "'"},   // 数字形式单引号
        {"&#38;", "&"},   // 数字形式 &
        {"&#60;", "<"},   // 数字形式 <
        {"&#62;", ">"}    // 数字形式 >
    };
    
    for (const auto& [entity, replacement] : entities) {
        size_t pos = 0;
        while ((pos = result.find(entity, pos)) != std::string::npos) {
            result.replace(pos, entity.length(), replacement);
            pos += replacement.length();
        }
    }
    
    return result;
}

// 辅助函数：提取两个标签之间的内容
std::string OPCUAXMLParser::extract_between(const std::string& text, size_t start_pos,
                                            const std::string& open_tag,
                                            const std::string& close_tag) {
    size_t open_start = text.find(open_tag, start_pos);
    if (open_start == std::string::npos)
        return "";
    
    size_t content_start = open_start + open_tag.length();
    size_t content_end = text.find(close_tag, content_start);
    if (content_end == std::string::npos)
        return "";
    
    return text.substr(content_start, content_end - content_start);
}

// 辅助函数：提取XML属性值
std::string OPCUAXMLParser::get_attribute(const std::string& tag_content,
                                          const std::string& attr_name) {
    std::regex attr_regex(attr_name + "=\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(tag_content, match, attr_regex)) {
        return decode_xml_entities(match[1].str());
    }
    return "";
}

// 辅助函数：提取标签内的内容
std::string OPCUAXMLParser::get_tag_content(const std::string& tag,
                                            const std::string& tag_name) {
    std::string open_tag = "<" + tag_name + ">";
    std::string close_tag = "</" + tag_name + ">";
    return extract_between(tag, 0, open_tag, close_tag);
}

// 解析 NodeId 属性，格式如：ns=3;s="DB111_EdgeGatewayTest"."Bool_Switch"
bool OPCUAXMLParser::parse_node_id(const std::string& node_id_str, int& ns_index,
                                   std::string& identifier) {
    std::regex ns_regex("ns=([0-9]+);");
    std::smatch ns_match;
    
    if (std::regex_search(node_id_str, ns_match, ns_regex)) {
        ns_index = std::stoi(ns_match[1].str());
    } else {
        ns_index = 0; // 默认命名空间
    }
    
    // 提取标识符部分（;s= 或 ;i= 后面的内容）
    std::regex id_regex(";[si]=([^;]*)$");
    std::smatch id_match;
    
    if (std::regex_search(node_id_str, id_match, id_regex)) {
        identifier = id_match[1].str();
        return true;
    }
    
    return false;
}

// 解析类型别名（Aliases部分）
void OPCUAXMLParser::parse_aliases() {
    size_t aliases_start = xml_content.find("<Aliases>");
    size_t aliases_end = xml_content.find("</Aliases>");
    
    if (aliases_start == std::string::npos || aliases_end == std::string::npos)
        return;
    
    std::string aliases_section = 
        xml_content.substr(aliases_start, aliases_end - aliases_start);
    
    std::regex alias_regex("<Alias Alias=\"([^\"]+)\">([^<]+)</Alias>");
    std::smatch match;
    std::string::const_iterator search_start(aliases_section.cbegin());
    
    while (std::regex_search(search_start, aliases_section.cend(), match, 
                             alias_regex)) {
        std::string alias_name = match[1].str();
        std::string alias_value = match[2].str();
        result.type_aliases[alias_name] = alias_value;
        search_start = match[0].second;
    }
}

// 解析命名空间URI
void OPCUAXMLParser::parse_namespace_uris() {
    size_t ns_start = xml_content.find("<NamespaceUris>");
    size_t ns_end = xml_content.find("</NamespaceUris>");
    
    if (ns_start == std::string::npos || ns_end == std::string::npos)
        return;
    
    std::string ns_section = xml_content.substr(ns_start, ns_end - ns_start);
    
    std::regex uri_regex("<Uri>([^<]+)</Uri>");
    std::smatch match;
    std::string::const_iterator search_start(ns_section.cbegin());
    
    while (std::regex_search(search_start, ns_section.cend(), match, uri_regex)) {
        result.namespace_uris.push_back(match[1].str());
        search_start = match[0].second;
    }
}

// 解析Extension（生成器信息）
void OPCUAXMLParser::parse_extensions() {
    size_t ext_start = xml_content.find("<Extensions>");
    size_t ext_end = xml_content.find("</Extensions>");
    
    if (ext_start == std::string::npos || ext_end == std::string::npos)
        return;
    
    std::string ext_section = 
        xml_content.substr(ext_start, ext_end - ext_start);
    
    std::regex generator_regex(
        "<si:Generator[^>]*Product=\"([^\"]*)\"[^>]*Edition=\"([^\"]*)\"[^>]*"
        "Version=\"([^\"]*)\"[^>]*/>");
    std::smatch match;
    
    if (std::regex_search(ext_section, match, generator_regex)) {
        result.generator_info = 
            match[1].str() + " " + match[2].str() + " " + match[3].str();
    }
}

// 解析数据类型的实际名称（通过Alias转换）
std::string OPCUAXMLParser::resolve_data_type(const std::string& data_type_attr) {
    if (data_type_attr.empty())
        return "Unknown";
    
    // 如果已经是标准类型（以i=开头）
    if (data_type_attr.find("i=") == 0) {
        return "OPC_BaseType_" + data_type_attr.substr(2);
    }
    
    // 如果包含命名空间前缀（如ns=3;i=3001），先尝试从别名解析，否则保留原始
    if (data_type_attr.find("ns=") != std::string::npos) {
        // 尝试从别名反向查找
        for (const auto& alias : result.type_aliases) {
            if (alias.second == data_type_attr) {
                return resolve_data_type(alias.first);
            }
        }
        return data_type_attr; // 返回原始值
    }
    
    // 直接通过别名映射
    auto it = result.type_aliases.find(data_type_attr);
    if (it != result.type_aliases.end()) {
        return data_type_attr + "-" + (it->second);
    }
    
    return data_type_attr;
}

// 解析单个UAVariable节点
void OPCUAXMLParser::parse_uavariable(const std::string& var_tag) {
    OPCUAModernDataStruct var;
    
    // 提取基本属性
    var.variable_nodeID = get_attribute(var_tag, "NodeId");
    var.browse_name = get_attribute(var_tag, "BrowseName");
    var.data_type = get_attribute(var_tag, "DataType");
    var.raw_data_type = var.data_type;
    
    // 解析访问级别
    std::string access_level_str = get_attribute(var_tag, "AccessLevel");
    if (!access_level_str.empty()) {
        var.access_level = std::stoi(access_level_str);
    }
    
    // 解析数组相关信息
    std::string value_rank = get_attribute(var_tag, "ValueRank");
    if (value_rank == "1") {
        var.is_array = true;
        std::string array_dim = get_attribute(var_tag, "ArrayDimensions");
        if (!array_dim.empty()) {
            var.array_dimension = std::stoi(array_dim);
        }
    }
    
    // 解析父节点关系
    std::string parent_nodeID = get_attribute(var_tag, "ParentNodeId");
    if (!parent_nodeID.empty()) {
        var.parent_nodeID = parent_nodeID;
    }
    
    // 提取标签内的内容
    var.variable_name = get_tag_content(var_tag, "DisplayName");
    var.description = get_tag_content(var_tag, "Description");
    
    // 如果DisplayName为空，使用BrowseName
    if (var.variable_name.empty()) {
        // BrowseName格式可能为 "3:VariableName"，需要去掉前缀
        size_t colon_pos = var.browse_name.find(':');
        if (colon_pos != std::string::npos) {
            var.variable_name = var.browse_name.substr(colon_pos + 1);
        } else {
            var.variable_name = var.browse_name;
        }
    } else {
      if (var.variable_nodeID.find("[") != std::string::npos) {
        auto parentName = extractLastPartWithoutIndex(var.variable_nodeID);
        if (parentName.empty()) {
          var.variable_name = var.variable_name;
        } else {
          var.variable_name = parentName + "[" + var.variable_name + "]";
        }
      } else {
        var.variable_name = var.variable_name;
      }
    }
    
    // 解析NodeId获取命名空间索引和标识符
    if (!var.variable_nodeID.empty()) {
        parse_node_id(var.variable_nodeID, var.namespace_index,
                      var.variable_nodeID);
    }
    
    // 解析数据类型为可读形式
    var.data_type = resolve_data_type(var.data_type);

    result.variables.push_back(std::move(var));
}

// 查找并解析所有UAVariable节点
void OPCUAXMLParser::parse_all_uavariables() {
    std::regex var_regex("<UAVariable[^>]*>([\\s\\S]*?)</UAVariable>",
                         std::regex::icase);
    std::smatch match;
    std::string::const_iterator search_start(xml_content.cbegin());
    
    while (std::regex_search(search_start, xml_content.cend(), match, var_regex)) {
        std::string var_tag = match[0].str();
        parse_uavariable(var_tag);
        search_start = match[0].second;
    }
}

// 判断是否保留变量
bool OPCUAXMLParser::shouldKeepVariable(const std::string& var_name,
                                        const std::string& node_id,
                                        const std::string& type_id,
                                        const std::string& browse_name,
                                        std::string& filter_reason) {
    // 1. 空名称
    if (var_name.empty()) {
        filter_reason = "empty_name";
        return false;
    }

    // 2.数字精确 - 过滤纯数字或 [数字] 格式
    bool is_pure_digit = std::all_of(node_id.begin(), node_id.end(), ::isdigit);
    if (is_pure_digit) {
      filter_reason = "pure_digit";
      return false; // 需要过滤
    }

    if(SYSTEM_VARIABLES_BLACKLIST.find(node_id) != SYSTEM_VARIABLES_BLACKLIST.end())
    {
        filter_reason = "black_list";
        return false;
    }

    // 5. 节点ID包含类型定义特征
    if (type_id.find("V_") != std::string::npos ||
        type_id.find("VT_") != std::string::npos ||
        type_id.find("TE_") != std::string::npos ||
        type_id.find("DT_") != std::string::npos) {
      filter_reason = "type_id";
      return false;
    }

    // 5. 节点ID包含类型定义特征
    if (node_id.find("DataType") != std::string::npos ||
        node_id.find("VariableType") != std::string::npos ||
        node_id.find("V_") != std::string::npos ||
        node_id.find("VT_") != std::string::npos ||
        node_id.find("TE_") != std::string::npos ||
        node_id.find("DT_") != std::string::npos) {
      filter_reason = "node_id";
      return false;
    }

    filter_reason = "";
    return true;
}

// 主解析函数
std::shared_ptr<OPCUAParseResult> OPCUAXMLParser::parse() {
    // 按顺序解析各部分
    parse_aliases();
    parse_namespace_uris();
    parse_extensions();
    parse_all_uavariables();
    for (auto &var : result.variables) {
      // 过滤掉系统变量（如EnumValues、EngineeringRevision等）
      if (!this->shouldKeepVariable(
              var.variable_name, var.variable_nodeID, var.data_type,
              var.browse_name,
              var.filter_reason)) { // 过滤数组索引如"0","1"等
        continue;
      }
    }

      // 移动result到智能指针
      auto resultPtr = std::make_shared<OPCUAParseResult>(std::move(result));
      return resultPtr;
    }