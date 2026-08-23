#pragma once

#include "load_config/Qt_library.h"
#include "PLC/Struct.h"
#include <spdlog/spdlog.h>
#include <unordered_set>
#include "PLC/IRawData.h"
#include "Rust_error_deal/error_deal.h"

#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>

// OPCUABrowser.h
class OPCUABrowser {
public:
  // 依赖注入：可传入配置（如超时、最大节点数）
  explicit OPCUABrowser(const std::string &endpointUrl,const int &objectId);
  ~OPCUABrowser();

  // 高层业务接口：浏览指定节点的所有子节点（递归可配置）
  Result<RawDataTable, RichError>
  browseNodeChildren(int depth, int maxDepth, const std::string &indent) const;

private:
  UA_Client *client_;
  UA_NodeId nodeId;
  // 内部辅助方法（非静态，可访问成员变量）
  void processReferences(UA_BrowseResult &result, int depth, int maxDepth,
                         RawDataTable &out) const ;
  // ... 其他辅助
  // 处理单个节点的所有引用（包括分页）
  void processNodeReferences(UA_Client *client, const UA_NodeId &nodeId,
                             int depth, int maxDepth, const std::string &indent,
                             RawDataTable &vec,
                             std::unordered_set<std::string> &visitedNodes) const;

  // 辅助函数：处理单页引用
  void processReferencePage(UA_Client *client, UA_BrowseResult *result,
                            int depth, int maxDepth, const std::string &indent,
                            RawDataTable &vec,
                            std::unordered_set<std::string> &visitedNodes) const ;

  std::string extractParentNodeId(const std::string &nodeId) const ;

  std::string eliminateSpareSymbol(const std::string &nodeId);

  bool readAndPrintVariable(UA_Client *client, const UA_NodeId &nodeId,
                            const std::string &indent,
                            RawDataField &item) const;
};