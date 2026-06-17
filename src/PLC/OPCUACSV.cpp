#include "PLC/OPCUACSV.h"

// 从文件读取CSV内容
std::vector<std::string> OPCUAParser::readCSVFile(const std::string& filename) {
    std::vector<std::string> lines;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "无法打开文件: " << filename << std::endl;
        return lines;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // 跳过空行
        if (line.empty()) continue;
        
        // 去除行尾的回车符
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        lines.push_back(line);
    }
    
    file.close();
    std::cout << "成功读取 " << lines.size() << " 行数据" << std::endl;
    return lines;
}

// 解析node ID，提取变量名
std::string OPCUAParser::parseNodeID(const std::string& str) {
    // 查找最后一个点号的位置
    size_t lastDot = str.rfind('.');
    if (lastDot != std::string::npos) {
        // 提取最后一部分作为变量名，并移除引号
        std::string varName = str.substr(lastDot + 1);
        varName = removeQuotes(varName);
        return varName;
    }
    return removeQuotes(str);
}

// 解析单行CSV数据
OPCUAModernDataStructFromCSV OPCUAParser::parseLine(const std::string& line, bool skipEmptyDataType) {
    OPCUAModernDataStructFromCSV item;
    // 初始化默认值
    item.array_Length = 1;
    item.namespace_uris = "";
    item.variable_nodeID = "";
    item.udt = "";
    item.dataType = "";
    
    if (line.empty()) {
        return item;
    }
    
    std::vector<std::string> parts;
    std::string part;
    bool inQuotes = false;
    
    // 解析CSV行，处理引号内的内容
    for (char c : line) {
        if (c == '"') {
            inQuotes = !inQuotes;
            part += c;
        } else if (c == ';' && !inQuotes) {
            parts.push_back(part);
            part.clear();
        } else {
            part += c;
        }
    }
    parts.push_back(part); // 添加最后一部分
    
    // 清理每个部分
    for (auto& p : parts) {
        p = trim(p);
        p = removeQuotes(p);
    }
    
    // 根据CSV文件的字段映射填充结构体
    // 格式: Namespace;Identifier;UDT;DataType;ArrayDimensions
    if (parts.size() >= 1) {
        item.namespace_uris = parts[0];
    }
    if (parts.size() >= 2) {
        item.variable_nodeID =(parts[1]);
        item.displayName = parseNodeID(parts[1]);
    }
    if (parts.size() >= 3) {
        item.udt = parts[2];
    }
    if (parts.size() >= 4) {
        item.dataType = parts[3];
    }
    
    // 处理数组长度
    if (parts.size() >= 5 && !parts[4].empty()) {
        std::string arrayLenStr = parts[4];
        // 去除可能的空格
        arrayLenStr.erase(0, arrayLenStr.find_first_not_of(" \t"));
        arrayLenStr.erase(arrayLenStr.find_last_not_of(" \t") + 1);
        
        if (!arrayLenStr.empty()) {
            try {
                size_t pos = 0;
                int len = std::stoi(arrayLenStr, &pos);
                if (pos == arrayLenStr.length() && len > 0) {
                    item.array_Length = len;
                } else {
                    std::cerr << "警告: 数组长度格式不正确 '" << parts[4] << "', 使用默认值 1" << std::endl;
                    item.array_Length = 1;
                }
            } catch (const std::invalid_argument& e) {
                std::cerr << "警告: 无效的数组长度参数 '" << parts[4] << "', 使用默认值 1" << std::endl;
                item.array_Length = 1;
            } catch (const std::out_of_range& e) {
                std::cerr << "警告: 数组长度参数超出范围 '" << parts[4] << "', 使用默认值 1" << std::endl;
                item.array_Length = 1;
            }
        }
    }
    
    // 可选：跳过DataType为空的条目
    if (skipEmptyDataType && item.dataType.empty()) {
        // 返回一个标记为无效的item
        item.dataType = "SKIP";
    }
    
    return item;
}

// 解析所有数据
std::vector<OPCUAModernDataStructFromCSV> OPCUAParser::parseAllData(const std::vector<std::string>& lines, bool hasHeader) {
    std::vector<OPCUAModernDataStructFromCSV> result;
    
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];
        if (line.empty()) continue;
        
        // 跳过标题行
        if (hasHeader && i == 0) {
            std::cout << "跳过标题行: " << line << std::endl;
            continue;
        }
        
        try {
            OPCUAModernDataStructFromCSV item = parseLine(line, false);
            // 检查是否为有效数据（非SKIP）
            if (item.dataType != "SKIP" && !item.dataType.empty()) {
                result.push_back(item);
            }
        } catch (const std::exception& e) {
            std::cerr << "解析第 " << (i + 1) << " 行时发生错误: " << e.what() << std::endl;
            std::cerr << "问题行: " << line << std::endl;
            // 继续解析下一行
        }
    }
    
    return result;
}

// 打印解析结果
void OPCUAParser::printData(const std::vector<OPCUAModernDataStructFromCSV>& data) {
    std::cout << "\n解析结果:\n";
    std::cout << std::string(120, '=') << std::endl;
    
    for (size_t i = 0; i < data.size(); ++i) {
        const auto& item = data[i];
        std::cout << "项目 " << (i + 1) << ":\n";
        std::cout << "  namespace_uris: " << item.namespace_uris << "\n";
        std::cout << "  variable_nodeID: " << item.variable_nodeID << "\n";
        std::cout << "  displayName: " << item.displayName << "\n";
        std::cout << "  udt: " << (item.udt.empty() ? "(无)" : item.udt) << "\n";
        std::cout << "  dataType: " << (item.dataType.empty() ? "(未指定)" : item.dataType) << "\n";
        std::cout << "  array_Length: " << item.array_Length << "\n";
        
        // 额外信息
        if (item.array_Length > 1) {
            std::cout << "  类型: 数组，长度为 " << item.array_Length << "\n";
        } else {
            std::cout << "  类型: 标量\n";
        }
        
        if (!item.udt.empty()) {
            std::cout << "  注意: 这是一个UDT类型变量\n";
        }
        
        std::cout << std::string(80, '-') << std::endl;
    }
    
    std::cout << "\n总计解析了 " << data.size() << " 个变量" << std::endl;
}

// 生成统计信息
void OPCUAParser::printStatistics(const std::vector<OPCUAModernDataStructFromCSV>& data) {
    std::cout << "\n统计信息:\n";
    std::cout << std::string(120, '=') << std::endl;
    
    // 统计数据类型分布
    std::map<std::string, int> dataTypeCount;
    std::map<std::string, int> udtCount;
    int arrayCount = 0;
    int udtVariables = 0;
    
    for (const auto& item : data) {
        if (!item.dataType.empty()) {
            dataTypeCount[item.dataType]++;
        }
        if (!item.udt.empty()) {
            udtCount[item.udt]++;
            udtVariables++;
        }
        if (item.array_Length > 1) {
            arrayCount++;
        }
    }
    
    std::cout << "数据类型分布:\n";
    for (const auto& pair : dataTypeCount) {
        std::cout << "  " << pair.first << ": " << pair.second << " 个\n";
    }
    
    if (!udtCount.empty()) {
        std::cout << "\nUDT类型分布:\n";
        for (const auto& pair : udtCount) {
            std::cout << "  " << pair.first << ": " << pair.second << " 个\n";
        }
    }
    
    std::cout << "\n数组变量数量: " << arrayCount << " 个\n";
    std::cout << "标量变量数量: " << (data.size() - arrayCount) << " 个\n";
    std::cout << "UDT变量数量: " << udtVariables << " 个\n";
}

// 辅助函数：去除字符串首尾空格
std::string OPCUAParser::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

// 辅助函数：移除字符串中的引号
std::string OPCUAParser::removeQuotes(const std::string& str) {
    if (str.empty()) return str;
    std::string result;
    result.reserve(str.length());
    for (char c : str) {
        if (c != '"') {
            result += c;
        }
    }
    return result;
}