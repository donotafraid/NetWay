#pragma once

#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <regex>
#include <dlfcn.h>
#include <cmath>

#include "load_config/Qt_library.h"
#include "MainWindows/Struct.h"
#include "PLC/Struct.h"

class OPCUAXMLParser {
private:
    std::string xml_content;
    OPCUAParseResult result;
    
    const std::set<std::string> SYSTEM_VARIABLES_BLACKLIST = {
        // 设备信息类
        "DeviceRevision", "HardwareRevision", "SoftwareRevision", "Manufacturer",
        "Model", "SerialNumber", "OrderNumber", "RevisionCounter",
        "EngineeringRevision", "DeviceManual", "OperatingMode",
        
        // OPC UA 模型类
        "SimaticStructuresType", "SimaticOperatingState",
        
        // 容器节点类
        "DataBlocksGlobal", "DataBlocksInstance", "Counters", "Timers", "Inputs",
        "Outputs", "Memory",  "DataBlocksGlobal",
        
        // 枚举定义类
        "EnumValues"
    };
    
    // 2. 类型定义节点前缀（这些是模型定义，不是实例）
    const std::set<std::string> TYPE_NODE_PREFIXES = {
        "VT_", // VariableType
        "V_",  // VariableType instance
        "DT_", // DataType
        "TE_"  // TypeEncoding
    };

public:
    // 构造函数
    explicit OPCUAXMLParser(const std::string& content);
    
    // 主解析函数
    std::shared_ptr<OPCUAParseResult> parse();
    
    // XML 转义字符解码
    std::string decode_xml_entities(const std::string& input);
    
    // 辅助函数：提取两个标签之间的内容
    std::string extract_between(const std::string& text, size_t start_pos,
                                const std::string& open_tag,
                                const std::string& close_tag);
    
    // 辅助函数：提取XML属性值
    std::string get_attribute(const std::string& tag_content,
                              const std::string& attr_name);
    
    // 辅助函数：提取标签内的内容
    std::string get_tag_content(const std::string& tag,
                                const std::string& tag_name);

    // 辅助函数: 提取 . 后面的部分
    std::string extractLastPartWithoutIndex(const std::string &input) {
      // 修正后的正则表达式
      // 匹配：点 + 双引号内容 + 可选的数组索引，直到字符串结束
      std::regex pattern(R"(\.("[^"]+")(?:\[\d+\])?$)");
      std::smatch match;

      if (std::regex_search(input, match, pattern)) {
        std::string result =
            match[1]; // 得到带引号的字符串，如 "Array_Template"
        // 删除首尾的双引号
        if (result.size() >= 2 && result.front() == '"' &&
            result.back() == '"') {
          result = result.substr(1, result.size() - 2);
        }
        return result;

        // // match[1] 是第一个捕获组，即带引号的部分
        // return match[1];
      }

      return "";
    }

    // 解析 NodeId 属性，格式如：ns=3;s="DB111_EdgeGatewayTest"."Bool_Switch"
    bool parse_node_id(const std::string& node_id_str, int& ns_index,
                       std::string& identifier);
    
    // 解析类型别名（Aliases部分）
    void parse_aliases();
    
    // 解析命名空间URI
    void parse_namespace_uris();
    
    // 解析Extension（生成器信息）
    void parse_extensions();
    
    // 解析数据类型的实际名称（通过Alias转换）
    std::string resolve_data_type(const std::string& data_type_attr);
    
    // 解析单个UAVariable节点
    void parse_uavariable(const std::string& var_tag);
    
    // 查找并解析所有UAVariable节点
    void parse_all_uavariables();
    
    // 判断是否保留变量
    bool shouldKeepVariable(const std::string& var_name,
                            const std::string& node_id,
                            const std::string& type_id,
                            const std::string& browse_name,
                            std::string& filter_reason);
};
