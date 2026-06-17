#pragma once

#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include "load_config/Qt_library.h"
#include "PLC/Struct.h"

struct OPCUAModernDataStructFromCSV
{
    std::string namespace_uris;      // 对应 Namespace 列
    std::string variable_nodeID;     // 对应 Identifier 列
    std::string displayName;     // 对应 Identifier 列
    std::string udt;                  // 对应 UDT 列 (用户自定义类型)
    std::string dataType;             // 对应 DataType 列
    int array_Length;                 // 对应 ArrayDimensions 列
};

class OPCUAParser {
public:
    // 从文件读取CSV内容
    std::vector<std::string> readCSVFile(const std::string& filename);
    
    // 解析单行CSV数据
    OPCUAModernDataStructFromCSV parseLine(const std::string& line, bool skipEmptyDataType = false);
    
    // 解析所有数据
    std::vector<OPCUAModernDataStructFromCSV> parseAllData(const std::vector<std::string>& lines, bool hasHeader = true);
    
    // 打印解析结果
    void printData(const std::vector<OPCUAModernDataStructFromCSV>& data);
    
    // 生成统计信息
    void printStatistics(const std::vector<OPCUAModernDataStructFromCSV>& data);
    
private:
    // 辅助函数：去除字符串首尾空格
    std::string trim(const std::string& str);
    
    // 辅助函数：移除字符串中的引号
    std::string removeQuotes(const std::string& str);
    
    // 辅助函数：解析node ID
    std::string parseNodeID(const std::string& str);
};
