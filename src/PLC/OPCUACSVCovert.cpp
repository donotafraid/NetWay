// XMLParser.cpp
#include "PLC/OPCUACSVCovert.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <iostream>

OPCUACSVCovert::OPCUACSVCovert(const std::string& xmlFilePath) 
    : xmlFilePath_(xmlFilePath) {}

bool OPCUACSVCovert::parse() {
    // 读取文件内容
    std::ifstream file(xmlFilePath_);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << xmlFilePath_ << std::endl;
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();
    
    // 查找所有UAVariable标签
    std::regex varRegex("<UAVariable[^>]*>");
    std::smatch match;
    std::string::const_iterator searchStart(content.cbegin());
    
    while (std::regex_search(searchStart, content.cend(), match, varRegex)) {
        NodeInfo node;
        
        // 提取匹配的完整标签
        size_t startPos = match.position() + (searchStart - content.cbegin());
        size_t endPos = content.find("</UAVariable>", startPos);
        
        if (endPos == std::string::npos) {
            // 可能是自闭合标签
            endPos = content.find("/>", startPos);
            if (endPos == std::string::npos) break;
            endPos += 2;
        } else {
            endPos += std::string("</UAVariable>").length();
        }
        
        std::string varTag = content.substr(startPos, endPos - startPos);
        
        // 提取属性
        node.nodeId = extractAttribute(varTag, 0, "NodeId");
        if(node.hasDotInQuotes(node.nodeId) && !isUnnecessaryTypeNode(node.nodeId))
        {
            node.browseName = extractAttribute(varTag, 0, "BrowseName");
            node.parentNodeId = extractAttribute(varTag, 0, "ParentNodeId");
            node.dataType = extractAttribute(varTag, 0, "DataType");
            node.accessLevel = extractAttribute(varTag, 0, "AccessLevel");
            node.valueRank = extractAttribute(varTag, 0, "ValueRank");
            node.arrayDimensions = extractAttribute(varTag, 0, "ArrayDimensions");
            
            // 提取DisplayName
            node.displayName = extractTagContent(varTag, 0, "DisplayName");
            
            // 提取Description
            node.description = extractTagContent(varTag, 0, "Description");
            
            // 存储节点
            nodes_.push_back(std::move(node));
        }
        // 更新搜索位置
        searchStart += match.position() + match.length();
    }
    
    return true;
}

std::string OPCUACSVCovert::extractAttribute(const std::string& xmlContent, 
                                            size_t pos, 
                                            const std::string& attrName) {
    std::regex attrRegex(attrName + "\\s*=\\s*\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(xmlContent, match, attrRegex)) {
      return decodeXMLEntities(match[1].str());
    }
    return "";
}

std::string OPCUACSVCovert::extractTagContent(const std::string& xmlContent, 
                                             size_t pos, 
                                             const std::string& tagName) {
    std::regex tagRegex("<" + tagName + "[^>]*>([^<]*)</" + tagName + ">");
    std::smatch match;
    if (std::regex_search(xmlContent, match, tagRegex)) {
        return match[1].str();
    }
    return "";
}

std::vector<NodeInfo> OPCUACSVCovert::getNodesByDataType(const std::string& dataType) const {
    std::vector<NodeInfo> result;
    for (const auto& node : nodes_) {
        if (node.dataType == dataType) {
            result.push_back(node);
        }
    }
    return result;
}

std::optional<NodeInfo> OPCUACSVCovert::findNodeById(const std::string& nodeId) const {
    for (const auto& node : nodes_) {
        if (node.nodeId == nodeId) {
            return node;
        }
    }
    return std::nullopt;
}