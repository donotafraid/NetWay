#pragma once

#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <fstream>    // ✅ 添加：支持 std::ofstream, std::ifstream
#include <set>        // ✅ 添加：支持 std::set

#include "load_config/Qt_library.h"
#include "PLC/Struct.h"

struct NodeInfo {
    std::string nodeId;         // 节点ID
    std::string browseName;     // 浏览名称
    std::string parentNodeId;   // 父节点ID
    std::string displayName;    // 显示名称
    std::string dataType;       // 数据类型
    std::string accessLevel;    // 访问级别（可选）
    std::string valueRank;      // 值秩（可选）
    std::string arrayDimensions; // 数组维度（可选）
    std::string description;    // 描述（可选）
    
    // 辅助方法
    bool isArray() const { 
        return !valueRank.empty() && valueRank != "0" && valueRank != "-1";
    }
    
    std::vector<int> getDimensions() const {
        std::vector<int> dims;
        if (!arrayDimensions.empty()) {
            std::stringstream ss(arrayDimensions);
            std::string dim;
            while (std::getline(ss, dim, ',')) {
                dims.push_back(std::stoi(dim));
            }
        }
        return dims;
    }

    static bool hasDotInQuotes(const std::string &nodeId) {
      return nodeId.find("\".\"") != std::string::npos;
      //                ^^^^^^ 表示字符序列：双引号 + 点 + 双引号
    }
};


class OPCUACSVCovert {
public:
    explicit OPCUACSVCovert(const std::string& xmlFilePath);
    ~OPCUACSVCovert() = default;
    
    // 解析XML并提取所有UAVariable节点
    bool parse();
    
    // 获取所有节点信息
    const std::vector<NodeInfo>& getAllNodes() const { return nodes_; }
    
    // 按数据类型过滤
    std::vector<NodeInfo> getNodesByDataType(const std::string& dataType) const;
    
    // 按节点ID查找
    std::optional<NodeInfo> findNodeById(const std::string& nodeId) const;
    
    // 获取统计信息
    size_t getNodeCount() const { return nodes_.size(); }

private:
    std::string xmlFilePath_;
    std::vector<NodeInfo> nodes_;
    
    // 辅助解析方法
    void parseUAVariable(const std::string& xmlContent, size_t& pos);
    std::string extractAttribute(const std::string& xmlContent, size_t pos, const std::string& attrName);
    std::string extractTagContent(const std::string& xmlContent, size_t pos, const std::string& tagName);
    
};

// 辅助函数：转义CSV字段（处理包含逗号、引号、换行符的情况）
static std::string escapeCSVField(const std::string& field) {
    if (field.empty()) {
        return "";
    }
    
    // 检查是否需要转义
    bool needsQuotes = field.find(',') != std::string::npos ||
                       field.find('"') != std::string::npos ||
                       field.find('\n') != std::string::npos ||
                       field.find('\r') != std::string::npos;
    
    if (!needsQuotes) {
        return field;
    }
    
    // 需要转义：用双引号包裹，并将内部的双引号替换为两个双引号
    std::string escaped = field;
    size_t pos = 0;
    while ((pos = escaped.find('"', pos)) != std::string::npos) {
        escaped.insert(pos, "\"");
        pos += 2;
    }
    return "\"" + escaped + "\"";
}

// 主导出函数
static bool exportNodesToCSV(const std::vector<NodeInfo>& nodes, 
                      const std::string& filename,
                      bool includeHeader) {
    if (nodes.empty()) {
        std::cerr << "Warning: No nodes to export" << std::endl;
        return false;
    }
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return false;
    }
    
    // 写入 BOM (UTF-8 with BOM) 方便 Excel 打开
    file << "\xEF\xBB\xBF";
    
    // 写入表头
    if (includeHeader) {
        file << "NodeId,BrowseName,ParentNodeId,DisplayName,DataType,"
             << "AccessLevel,ValueRank,ArrayDimensions,Description,"
             << "IsArray\n";
    }
    
    // 写入数据行
    for (const auto& node : nodes) {
        // 计算节点层级（统计 "." 的数量）
        int dotCount = 0;
        size_t pos = 0;
        while ((pos = node.nodeId.find("\".\"", pos)) != std::string::npos) {
            dotCount++;
            pos += 3;
        }
        
        file << escapeCSVField(node.nodeId) << ","
             << escapeCSVField(node.browseName) << ","
             << escapeCSVField(node.parentNodeId) << ","
             << escapeCSVField(node.displayName) << ","
             << escapeCSVField(node.dataType) << ","
             << escapeCSVField(node.accessLevel) << ","
             << escapeCSVField(node.valueRank) << ","
             << escapeCSVField(node.arrayDimensions) << ","
             << escapeCSVField(node.description) << ","
             << (node.isArray() ? "true" : "false")
             <<"\n";
    }
    
    file.close();
    std::cout << "Exported " << nodes.size() << " nodes to " << filename << std::endl;
    return true;
}

// HTML/XML 实体解码
static std::string decodeXMLEntities(const std::string& input) {
    static const std::map<std::string, std::string> entities = {
        {"&quot;", "\""},
        {"&amp;", "&"},
        {"&lt;", "<"},
        {"&gt;", ">"},
        {"&apos;", "'"},
        {"&#39;", "'"},
        {"&#34;", "\""}
    };
    
    std::string result = input;
    for (const auto& [entity, replacement] : entities) {
        size_t pos = 0;
        while ((pos = result.find(entity, pos)) != std::string::npos) {
            result.replace(pos, entity.length(), replacement);
            pos += replacement.length();
        }
    }
    return result;
}

//  类型定义前缀
static bool isUnnecessaryTypeNode(const std::string &input) {
 // 1. 类型定义节点前缀（这些是模型定义，不是实例）
    static  std::set<std::string> TYPE_NODE_PREFIXES = {
        "VT_", // VariableType
        "V_",  // VariableType instance
        "DT_", // DataType
        "TE_"  // TypeEncoding
    };

  for (const auto element : TYPE_NODE_PREFIXES) {
    size_t pos = 0;
    while ((pos = input.find(element, pos)) != std::string::npos) {
        return true;
    }
  }
  return false;
}
