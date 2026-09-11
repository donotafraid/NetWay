#pragma once

#include "PLC/OPCUABrowser.h"
#include "PLC/MapperStrategy.h"
#include "PLC/OPC_UA.h"

class OPCUADataBlockContext;

// ✅ 新建聚合结构体
struct ServiceRegistry {
  std::shared_ptr<IDeviceReader> m_readers;
  std::shared_ptr<IStringLengthProbe> m_probes;
  std::shared_ptr<MapperpStrategy> m_mappers;
  std::shared_ptr<OPCUABrowser> m_browsers;
};

class IContextCreationStrategy {
public:
    virtual ~IContextCreationStrategy() = default;
    virtual Result<std::shared_ptr<OPCUADataBlockContext>, RichError>
    create(const QString& identifier, const QString& dataBlockName,
           const ServiceRegistry& registry) const = 0;
};

class InlineBrowseStrategy : public IContextCreationStrategy
{
  Result<std::shared_ptr<OPCUADataBlockContext>, RichError>
    create(const QString& identifier, const QString& dataBlockName,
           const ServiceRegistry& registry) const override;
};

class FileContextStrategy : public IContextCreationStrategy
{
   Result<std::shared_ptr<OPCUADataBlockContext>, RichError>
    create(const QString& identifier, const QString& dataBlockName,
           const ServiceRegistry& registry) const override;
};

class ContextStrategyFactory {
private:
  // 存储一个 "标识符匹配器 -> 策略" 的映射
  // 匹配器可以是：前缀匹配、后缀匹配、正则表达式匹配等
  std::vector<
      std::pair<std::function<bool(const QString &)>,
                std::function<std::unique_ptr<IContextCreationStrategy>()>>>
      creators_;

public:
  ContextStrategyFactory();
  // 注册：根据匹配规则返回对应策略
  void registerStrategy(
      std::function<bool(const QString &)> matcher,
      std::function<std::unique_ptr<IContextCreationStrategy>()> creator);

  std::unique_ptr<IContextCreationStrategy>
  createStrategy(const QString &identifier) const;
};