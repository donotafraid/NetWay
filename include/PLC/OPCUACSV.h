#pragma once

#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <regex>      // ✅ 添加：支持 std::regex, std::smatch, std::regex_search
#include "load_config/Qt_library.h"
#include "PLC/Struct.h"


enum class dataType{
    VARIABLE,
    ARRAY,
    STRUCT,
    EMPTY
};

class OPCUACSVParser {
public:
  // 辅助函数：移除字符串中的引号
  std::vector<std::string> parseCSVLine(const std::string &line);

  // 从文件读取CSV内容
  std::vector<std::string> readCSVFile(const std::string &filePath);

  // 解析所有数据
  std::vector<OPCUAModernDataStructFromCSV>
  parseAllData(const std::vector<std::string> &lines, bool hasHeader = true);

  // 打印解析结果
  void printData(const std::vector<OPCUAModernDataStructFromCSV> &data);

  // 生成统计信息
  void printStatistics(const std::vector<OPCUAModernDataStructFromCSV> &data);

private:
  dataType lastCheckVariableType;
  // 辅助函数：去除字符串首尾空格
  std::string trim(const std::string &str);

  // 辅助函数：移除字符串中的引号
  std::string cleanField(const std::string &field);

  // 辅助函数：解析node ID
  std::string parseNodeID(const std::string &str);

  // 辅助函数：移除字符串中的引号
  std::string escapeCSVQuotesToJson(const std::string &field);

  // get parent node id - 提取父路径（去除最后一部分）
  std::string extractVariableNameWithIndex(const std::string &input) {
    if (input.empty()) {
      return std::string{""};
    }

    // 找到最后一个 '.' 的位置
    size_t lastDotPos = input.rfind('.');
    if (lastDotPos == std::string::npos) {
      // 没有 '.'，整个字符串就是变量名
      return input;
    }

    // ✅ 提取最后一个 '.' 之前的所有内容（父路径）
    std::string result{input.substr(0, lastDotPos)};

    // 提取最后一个 '.' 之后的内容（变量名 + 可能的索引）
    std::string remaining = input.substr(lastDotPos + 1);

    // 匹配模式：可选的引号内容 + 可选的数组索引
    std::regex pattern(R"(^\"?([^\"]+)\"?(\[\d+\])?)");
    std::smatch match;

    if (std::regex_search(remaining, match, pattern)) {
      // match[1] 是变量名（可能包含引号）
      std::string varName = match[1];

      // 去除可能存在的引号
      if (varName.size() >= 2 && varName.front() == '"' &&
          varName.back() == '"') {
        varName = varName.substr(1, varName.size() - 2);
      }
    }

    return result;
  }
};
