#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include "PLC/IRawData.h"
#include "PLC/CreateContextStrategy.h"
#include "PLC/OPCUABrowser.h"

#include "Rust_error_deal/error_deal.h"

// 前向声明
class ParsingStrategy;
class XmlParsingStrategy;
class CsvParsingStrategy;
class StrategyFactory;
class MultiFormatParser;

// 1. 定义解析策略接口（抽象基类）
class ParsingStrategy {
public:
    virtual ~ParsingStrategy() = default;
    virtual Result<RawDataTable, RichError> parse(const std::string& filePath) const = 0;
};

// 2. XML解析策略类
class XmlParsingStrategy : public ParsingStrategy {
public:
    Result<RawDataTable, RichError> parse(const std::string &filePath) const override;

private:
    RawDataField parse_uavariable(const std::string &var_tag) const;
    void parse_all_uavariables(std::string &xml_content, RawDataTable &result) const;
    std::string resolve_data_type(const std::string &data_type_attr) const;
    std::string get_attribute(const std::string &tag_content, const std::string &attr_name) const;
    std::string decode_xml_entities(const std::string &input) const;
    std::string get_tag_content(const std::string &tag, const std::string &tag_name) const;
    std::string extract_between(const std::string &text, size_t start_pos,
                                const std::string &open_tag, const std::string &close_tag) const;
    std::string extractLastPartWithoutIndex(const std::string &input) const;
    bool parse_node_id(const std::string& node_id_str, int& ns_index, std::string& identifier) const;
};

// 3. CSV解析策略类
class CsvParsingStrategy : public ParsingStrategy {
public:
    Result<RawDataTable, RichError> parse(const std::string &filePath) const override;

private:
    std::vector<std::string> readCSVFile(const std::string &filePath) const;
    Result<RawDataTable, RichError> parseAllData(const std::vector<std::string> &lines, bool hasHeader) const;
    std::vector<std::string> parseCSVLine(const std::string &line) const;
    std::string parseNodeID(const std::string &str) const;
    std::string cleanField(const std::string &field) const;
    std::string escapeCSVQuotesToJson(const std::string &field) const;
};

// 4. 策略工厂类
class StrategyFactory {
private:
    std::unordered_map<std::string, std::function<std::unique_ptr<ParsingStrategy>()>> creators_;
    
public:
    StrategyFactory();
    void registerStrategy(const std::string& extension, 
                          std::function<std::unique_ptr<ParsingStrategy>()> creator);
    std::unique_ptr<ParsingStrategy> createStrategy(const std::string& filePath) const;
};

// 5. 多格式解析器类
class MultiFormatParser {
private:
    std::unique_ptr<StrategyFactory> factory_;
    
public:
    explicit MultiFormatParser(std::unique_ptr<StrategyFactory> factory);
    Result<RawDataTable, RichError> parseFile(const std::string &path) const;
};

// DataLoader.h - 统一外观
class DataLoader {
public:
  // ✅ 唯一入口：根据 source 类型自动选择解析方式
  Result<RawDataTable, RichError>
  load(const QString &source,
    const QString &identifier,
       const std::unordered_map<QString, ServiceRegistry> &m_serviceSuites)
      const {
    if (isEndpointUrl(source.toStdString())) {
        auto it = m_serviceSuites.find(identifier);
        if(it != m_serviceSuites.end())
        {
            return loadFromEndpoint(source.toStdString(),it->second.m_browsers);
        }
    } else {
      return loadFromFile(source.toStdString());
    }
  }

private:
    // ✅ 内部技术路由（属于 How 层）
    bool isEndpointUrl(const std::string& source) const {
        return source.rfind("opc.tcp://", 0) == 0;
    }

    Result<RawDataTable, RichError>
    loadFromEndpoint(const std::string &endpoint,
                     const std::shared_ptr<OPCUABrowser> &browser) const {
      return browser->browseNodeChildren(0, 10, "");
    }

    Result<RawDataTable, RichError> loadFromFile(const std::string& filePath) const {
      auto factory = std::make_unique<StrategyFactory>();
      auto m_parser = std::make_unique<MultiFormatParser>((std::move(factory)));
      return m_parser->parseFile(filePath);
    }
};
