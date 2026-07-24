#include "PLC/OPCUACSV.h"

// 从文件读取CSV内容
std::vector<std::string> OPCUACSVParser::readCSVFile(const std::string& filePath) {
    std::vector<std::string> lines;
    std::ifstream file(filePath);
    
    if (!file.is_open()) {
        std::cerr << "无法打开文件: " << filePath << std::endl;
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

std::vector<std::string> OPCUACSVParser::parseCSVLine(const std::string &line) {
  std::vector<std::string> fields;
  std::string current;
  bool insideQuotes = false;

  int fieldIndex = 0;
  for (size_t i = 0; i < line.length(); ++i) {
    char c = line[i];

    if (c == '"') {
      // 检查是否是转义的引号
      if (insideQuotes && i + 1 < line.length() &&
          (line[i + 1] == '"')) {
        std::cout << "  检测到转义引号 \"\" 在位置 " << i << std::endl;
        current += c;
        current += line[++i];
        //  achieve add ""
        continue;
      }

      //    non transform symbol mean the current string start or end
      insideQuotes = !insideQuotes;
      current += c;
      std::cout << "  引号切换: insideQuotes = "
                << (insideQuotes ? "true" : "false") << " 位置 " << i
                << std::endl;
    } else if (c == ',' && !insideQuotes) {
      // 只有在引号外部的逗号才是分隔符
      std::cout << "  字段 " << fieldIndex << " 结束: \"" << current << "\""
                << std::endl;
      fields.push_back(current);
      current.clear();
      fieldIndex++;
    } else {
      current += c;
    }
  }

  fields.push_back(current);

  while (fields.size() < 10) {
    fields.push_back("");
  }

  return fields;
}

// 解析node ID，提取变量名
std::string OPCUACSVParser::parseNodeID(const std::string& str) {
    // 查找最后一个点号的位置
    size_t lastDot = str.rfind('.');
    if (lastDot != std::string::npos) {
        // 提取最后一部分作为变量名，并移除引号
        std::string varName = str.substr(lastDot + 1);
        varName = cleanField(varName);
        return varName;
    }
    return cleanField(str);
}

std::string OPCUACSVParser::cleanField(const std::string &field) {
  std::string result = field;

  size_t start = result.find_first_not_of(" \t");
  if (start != std::string::npos) {
    result = result.substr(start);
  }

  size_t end = result.find_last_not_of(" \t");
  if (end != std::string::npos) {
    result = result.substr(0, end + 1);
  }

  if (result.length() >= 2 && result.front() == '"' && result.back() == '"') {
    result = result.substr(1, result.length() - 2);
  }

  return result;
}


// 解析所有数据
std::vector<OPCUAModernDataStructFromCSV>
OPCUACSVParser::parseAllData(const std::vector<std::string> &lines,
                             bool hasHeader) {
  std::vector<OPCUAModernDataStructFromCSV> result;
  result.reserve(lines.size());

  for (size_t i = 0; i < lines.size(); ++i) {
    const std::string &line = lines[i];

    if (line.empty()) {
      continue;
    }

    if (hasHeader && i == 0) {
      std::cout << "跳过标题行" << std::endl;
      continue;
    }

    try {
      std::vector<std::string> fields = parseCSVLine(line);

      // 9 mean csv header size
      if (fields.size() < 9) {
        std::cerr << "警告: 第 " << (i + 1)
                  << " 行字段数量不足: " << fields.size() << ", 期望至少 9"
                  << std::endl;
        continue;
      }

      OPCUAModernDataStructFromCSV item;

      // 解析并转义双引号
      item.nodeId = escapeCSVQuotesToJson(cleanField(fields[0]));
      item.browseName = escapeCSVQuotesToJson(cleanField(fields[1]));
      item.parentNodeId = escapeCSVQuotesToJson(cleanField(fields[2]));
      item.displayName = escapeCSVQuotesToJson(cleanField(fields[3]));
      item.dataType = cleanField(fields[4]);
      item.accessLevel = cleanField(fields[5]);
      item.valueRank = cleanField(fields[6]);
      item.arrayDimensions = cleanField(fields[7]);
      item.description = escapeCSVQuotesToJson(cleanField(fields[8]));

      result.push_back(std::move(item));

    } catch (const std::exception &e) {
      std::cerr << "解析第 " << (i + 1) << " 行时发生错误: " << e.what()
                << std::endl;
      std::cerr << "问题行: " << line << std::endl;
    }
  }

  result.shrink_to_fit();
  return result;
}

// 打印解析结果
void OPCUACSVParser::printData(
    const std::vector<OPCUAModernDataStructFromCSV> &data) {
 
}

// 生成统计信息
void OPCUACSVParser::printStatistics(const std::vector<OPCUAModernDataStructFromCSV>& data) {
   
}

// 辅助函数：去除字符串首尾空格
std::string OPCUACSVParser::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

// 辅助函数：将 CSV 中的 "" 转义为 \"
std::string OPCUACSVParser::escapeCSVQuotesToJson(const std::string& field) {
  std::string result = field;
    size_t pos = 0;
    
     // 使用字符数组，不涉及字符串字面量解析问题
    const char TARGET[] = "\"\"";      // 两个双引号
    const char REPLACE[] = "\\\"";     // 反斜杠 + 双引号

    while ((pos = result.find(TARGET, pos)) != std::string::npos) {
      result.replace(pos, 2, REPLACE);
      pos += 2;
    }

    return result; 
}