#include "PLC/ParsingStrategy.h"
#include "PLC/Struct.h"
#include <spdlog/spdlog.h>
// ============ XmlParsingStrategy 实现 ============

Result<RawDataTable, RichError>
XmlParsingStrategy::parse(const std::string &filePath) const {
    // 读取XML文件
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return Result<RawDataTable, RichError>(RawDataTable{});
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();
    std::string xml_content{content};
    RawDataTable resultRow;

    // 按顺序解析各部分
    parse_all_uavariables(xml_content, resultRow);
    if (resultRow.size()) {
        return Result<RawDataTable, RichError>(std::move(resultRow));
    } else {
        return Result<RawDataTable, RichError>(RichError{"errer parse: RawDataTable is Empty"});
    }
}

RawDataField XmlParsingStrategy::parse_uavariable(const std::string &var_tag) const {
    RawDataField field;

    // ========== 1. 放入"固定通用字段"（所有协议共有的） ==========
    field.name = get_attribute(var_tag, "DisplayName");
    field.data_type = resolve_data_type(get_attribute(var_tag, "DataType"));
    field.value = std::string{0};

    // ========== 2. 放入"元数据字典"（协议专有、非通用的） ==========
    field.metadata["NodeId"] = get_attribute(var_tag, "NodeId");
    field.metadata["BrowseName"] = get_attribute(var_tag, "BrowseName");
    field.metadata["ParentNodeId"] = get_attribute(var_tag, "ParentNodeId");
    field.metadata["DataType"] = get_attribute(var_tag, "DataType");
    field.metadata["AccessLevel"] = get_attribute(var_tag, "AccessLevel");
    field.metadata["ValueRank"] = get_attribute(var_tag, "ValueRank");
    field.metadata["ArrayDimensions"] = get_attribute(var_tag, "ArrayDimensions");
    field.metadata["DisplayName"] = get_tag_content(var_tag, "DisplayName");
    field.metadata["Description"] = get_tag_content(var_tag, "Description");

    return field;
}

void XmlParsingStrategy::parse_all_uavariables(std::string &xml_content,
                                               RawDataTable &result) const {
  std::regex var_regex("<UAVariable[^>]*>([\\s\\S]*?)</UAVariable>",
                       std::regex::icase);
  std::smatch match;
  std::string::const_iterator search_start(xml_content.cbegin());

  while (
      std::regex_search(search_start, xml_content.cend(), match, var_regex)) {
    std::string var_tag = match[0].str();

    // 先移动搜索位置，避免死循环
    search_start = match[0].second;

    if (var_tag.find(";s=") != std::string::npos) {
      result.push_back(parse_uavariable(var_tag));
    }
    // 不包含 ";s=" 的则跳过
  }
}

std::string XmlParsingStrategy::resolve_data_type(const std::string &data_type_attr) const {
    if (data_type_attr.empty())
        return "Unknown";

    if (data_type_attr.find("i=") == 0) {
        return "OPC_BaseType_" + data_type_attr.substr(2);
    }

    if (data_type_attr.find("ns=") != std::string::npos) {
        return data_type_attr;
    }

    return data_type_attr;
}

std::string XmlParsingStrategy::get_attribute(const std::string &tag_content, const std::string &attr_name) const {
    std::regex attr_regex(attr_name + "=\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(tag_content, match, attr_regex)) {
        return decode_xml_entities(match[1].str());
    }
    return "";
}

std::string XmlParsingStrategy::decode_xml_entities(const std::string &input) const {
    std::string result = input;

    static const std::map<std::string, std::string> entities = {
        {"&quot;", "\""}, {"&apos;", "'"}, {"&amp;", "&"},
        {"&lt;", "<"},    {"&gt;", ">"},   {"&#34;", "\""},
        {"&#39;", "'"}, {"&#38;", "&"}, {"&#60;", "<"}, {"&#62;", ">"}
    };

    for (const auto &[entity, replacement] : entities) {
        size_t pos = 0;
        while ((pos = result.find(entity, pos)) != std::string::npos) {
            result.replace(pos, entity.length(), replacement);
            pos += replacement.length();
        }
    }

    return result;
}

std::string XmlParsingStrategy::get_tag_content(const std::string &tag, const std::string &tag_name) const {
    std::string open_tag = "<" + tag_name + ">";
    std::string close_tag = "</" + tag_name + ">";
    return extract_between(tag, 0, open_tag, close_tag);
}

std::string XmlParsingStrategy::extract_between(const std::string &text, size_t start_pos,
                                                const std::string &open_tag, const std::string &close_tag) const {
    size_t open_start = text.find(open_tag, start_pos);
    if (open_start == std::string::npos)
        return "";

    size_t content_start = open_start + open_tag.length();
    size_t content_end = text.find(close_tag, content_start);
    if (content_end == std::string::npos)
        return "";

    return text.substr(content_start, content_end - content_start);
}

std::string XmlParsingStrategy::extractLastPartWithoutIndex(const std::string &input) const {
    std::regex pattern(R"(\.("[^"]+")(?:\[\d+\])?$)");
    std::smatch match;

    if (std::regex_search(input, match, pattern)) {
        std::string result = match[1];
        if (result.size() >= 2 && result.front() == '"' && result.back() == '"') {
            result = result.substr(1, result.size() - 2);
        }
        return result;
    }
    return "";
}

bool XmlParsingStrategy::parse_node_id(const std::string& node_id_str, int& ns_index, std::string& identifier) const {
    std::regex ns_regex("ns=([0-9]+);");
    std::smatch ns_match;
    
    if (std::regex_search(node_id_str, ns_match, ns_regex)) {
        ns_index = std::stoi(ns_match[1].str());
    } else {
        ns_index = 0;
    }
    
    std::regex id_regex(";[si]=([^;]*)$");
    std::smatch id_match;
    
    if (std::regex_search(node_id_str, id_match, id_regex)) {
        identifier = id_match[1].str();
        return true;
    }
    
    return false;
}

// ============ CsvParsingStrategy 实现 ============

Result<RawDataTable, RichError>
CsvParsingStrategy::parse(const std::string &filePath) const {
    std::vector<std::string> lines = readCSVFile(filePath);

    if (lines.empty()) {
        return Result<RawDataTable, RichError>(
            RichError{"source csv file is empty in getBlockVecFromFile"});
    }

    auto parsedData = parseAllData(lines, true);
    if (parsedData.is_fail()) {
        return Result<RawDataTable, RichError>(parsedData.unwrap_err());
    } else {
        return Result<RawDataTable, RichError>(std::move(parsedData.unwrap_returnLeftValue()));
    }
}

std::vector<std::string> CsvParsingStrategy::readCSVFile(const std::string &filePath) const {
    std::vector<std::string> lines;
    std::ifstream file(filePath);

    if (!file.is_open()) {
        std::cerr << "无法打开文件: " << filePath << std::endl;
        return lines;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty())
            continue;

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        lines.push_back(line);
    }

    file.close();
    std::cout << "成功读取 " << lines.size() << " 行数据" << std::endl;
    return lines;
}

Result<RawDataTable, RichError>
CsvParsingStrategy::parseAllData(const std::vector<std::string> &lines, bool hasHeader) const {
  RawDataTable dataVec;
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

      RawDataField item;

      // 解析并转义双引号
      item.data_type = cleanField(fields[4]);
      item.name = escapeCSVQuotesToJson(cleanField(fields[3]));

      item.metadata["NodeId"] = escapeCSVQuotesToJson(cleanField(fields[0]));
      item.metadata["BrowseName"] = escapeCSVQuotesToJson(cleanField(fields[1]));
      item.metadata["ParentNodeId"] = escapeCSVQuotesToJson(cleanField(fields[2]));
      item.metadata["DisplayName"] = escapeCSVQuotesToJson(cleanField(fields[3]));
      item.metadata["DataType"] = cleanField(fields[4]);
      item.metadata["AccessLevel"] = cleanField(fields[5]);
      item.metadata["ValueRank"] = cleanField(fields[6]);
      item.metadata["ArrayDimensions"] = cleanField(fields[7]);
      item.metadata["Description"] = escapeCSVQuotesToJson(cleanField(fields[8]));

      dataVec.push_back(std::move(item));

    } catch (const std::exception &e) {
      std::cerr << "解析第 " << (i + 1) << " 行时发生错误: " << e.what()
                << std::endl;
      std::cerr << "问题行: " << line << std::endl;
    }
  }

  dataVec.shrink_to_fit();
  return dataVec;
}

std::vector<std::string> CsvParsingStrategy::parseCSVLine(const std::string &line) const {
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

std::string CsvParsingStrategy::parseNodeID(const std::string &str) const {
    size_t lastDot = str.rfind('.');
    if (lastDot != std::string::npos) {
        std::string varName = str.substr(lastDot + 1);
        varName = cleanField(varName);
        return varName;
    }
    return cleanField(str);
}

std::string CsvParsingStrategy::cleanField(const std::string &field) const {
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

std::string CsvParsingStrategy::escapeCSVQuotesToJson(const std::string &field) const {
    std::string result = field;
    size_t pos = 0;

    const char TARGET[] = "\"\"";
    const char REPLACE[] = "\"";

    while ((pos = result.find(TARGET, pos)) != std::string::npos) {
        result.replace(pos, 2, REPLACE);
        pos += 1;
    }

    return result;
}

// ============ StrategyFactory 实现 ============

StrategyFactory::StrategyFactory() {
    registerStrategy(".xml", []() { return std::make_unique<XmlParsingStrategy>(); });
    registerStrategy(".XML", []() { return std::make_unique<XmlParsingStrategy>(); });
    registerStrategy(".csv", []() { return std::make_unique<CsvParsingStrategy>(); });
    registerStrategy(".CSV", []() { return std::make_unique<CsvParsingStrategy>(); });
}

void StrategyFactory::registerStrategy(const std::string& extension, 
                                       std::function<std::unique_ptr<ParsingStrategy>()> creator) {
    creators_[extension] = std::move(creator);
}

std::unique_ptr<ParsingStrategy> StrategyFactory::createStrategy(const std::string& filePath) const {
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos == std::string::npos) {
        spdlog::error("No file extension found!");
    }
    std::string ext = filePath.substr(dotPos);
    
    auto it = creators_.find(ext);
    if (it != creators_.end()) {
        return it->second();
    }
    spdlog::error("Unsupported file type: " + ext);
}

// ============ MultiFormatParser 实现 ============

MultiFormatParser::MultiFormatParser(std::unique_ptr<StrategyFactory> factory) 
    : factory_(std::move(factory)) {}

Result<RawDataTable, RichError>
MultiFormatParser::parseFile(const std::string &path) const {
    auto strategy = factory_->createStrategy(path);
    return strategy->parse(path);
}
