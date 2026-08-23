#include "PLC/OPCUABrowser.h"
#include <fstream>   // ✅ 添加：std::ifstream, std::ofstream
#include <set>       // ✅ 添加：std::set
#include <regex>     // ✅ 添加：std::regex, std::smatch, std::regex_search

OPCUABrowser::OPCUABrowser(const std::string &endpointUrl,const int &objectId) {
  client_ = UA_Client_new();

  // 设置更长的超时时间（因为要浏览很多节点）
  UA_ClientConfig *ua_config = UA_Client_getConfig(client_);
  ua_config->timeout = 30000; // 30秒

  UA_StatusCode retval = UA_Client_connect(client_, endpointUrl.data());
  if (retval != UA_STATUSCODE_GOOD) {
    spdlog::error("connect fail: {}", UA_StatusCode_name(retval));
    UA_Client_delete(client_);
    spdlog::shutdown();
    return;
  }
  spdlog::info("connect success: {}", UA_StatusCode_name(retval));

  retval = UA_ClientConfig_setDefault(ua_config);
  if (retval != UA_STATUSCODE_GOOD) {
    spdlog::error("Failed to set default client config");
    return;
  }
  spdlog::info("set defalut success: {}", UA_StatusCode_name(retval));

  nodeId = UA_NODEID_NUMERIC(0, objectId);
}

OPCUABrowser::~OPCUABrowser() {
  if (client_) {
    UA_Client_delete(client_);
    UA_NodeId_delete(&nodeId);
  }
  client_ = nullptr;
}

// ==================== OPCUABrowser 实现 ====================
void OPCUABrowser::processNodeReferences(
    UA_Client *client, const UA_NodeId &nodeId, int depth, int maxDepth,
    const std::string &indent, RawDataTable &vec,
    std::unordered_set<std::string> &visitedNodes) const {
  if (depth > maxDepth)
    return;

  {
    // 将当前节点转为字符串用于去重
    std::string nodeKey = nodeIdToString(&nodeId);

    // 如果已经访问过，直接返回
    if (visitedNodes.find(nodeKey) != visitedNodes.end()) {
      spdlog::debug("跳过已访问节点: {}", nodeKey);
      return;
    }

    // 标记为已访问
    visitedNodes.insert(nodeKey);
  }

  // 第一步：初始浏览请求
  UA_BrowseRequest bReq;
  UA_BrowseRequest_init(&bReq);

  bReq.requestedMaxReferencesPerNode = 0; // 0 表示使用服务器默认值
  bReq.nodesToBrowseSize = 1;

  // 使用 UA_Array_new 分配数组
  bReq.nodesToBrowse = (UA_BrowseDescription *)UA_Array_new(
      1, &UA_TYPES[UA_TYPES_BROWSEDESCRIPTION]);
  if (!bReq.nodesToBrowse) {
    spdlog::error("分配 BrowseDescription 数组失败");
    UA_BrowseRequest_clear(&bReq);
    return;
  }

  UA_NodeId_copy(&nodeId, &bReq.nodesToBrowse[0].nodeId);
  bReq.nodesToBrowse[0].resultMask = UA_BROWSERESULTMASK_ALL;
  bReq.nodesToBrowse[0].browseDirection = UA_BROWSEDIRECTION_FORWARD;
  bReq.nodesToBrowse[0].referenceTypeId =
      UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
  bReq.nodesToBrowse[0].includeSubtypes = true;
  bReq.nodesToBrowse[0].nodeClassMask = 0;

  UA_BrowseResponse bResp = UA_Client_Service_browse(client, bReq);

  if (bResp.responseHeader.serviceResult != UA_STATUSCODE_GOOD) {
    spdlog::error("Browse 请求失败: {}",
                  UA_StatusCode_name(bResp.responseHeader.serviceResult));
    UA_BrowseRequest_clear(&bReq);
    UA_BrowseResponse_clear(&bResp);
    return;
  }

  // 第二步：处理响应结果（可能需要多次调用 BrowseNext）
  for (size_t resultIdx = 0; resultIdx < bResp.resultsSize; resultIdx++) {
    UA_BrowseResult *result = &bResp.results[resultIdx];

    if (result->statusCode != UA_STATUSCODE_GOOD) {
      spdlog::warn("BrowseResult[{}] 状态码: {}", resultIdx,
                   UA_StatusCode_name(result->statusCode));
      continue;
    }

    // 处理当前页的引用
    processReferencePage(client, result, depth, maxDepth, indent, vec,
                         visitedNodes);

    // 关键：处理可能存在的分页（ContinuationPoint）
    // 注意：不要直接赋值，需要先初始化并复制
    UA_ByteString continuationPoint;
    UA_ByteString_init(&continuationPoint);

    if (!UA_String_isEmpty(&result->continuationPoint)) {
      UA_ByteString_copy(&result->continuationPoint, &continuationPoint);
    }

    while (!UA_String_isEmpty(&continuationPoint)) {
      // 发起 BrowseNext 请求获取下一页
      UA_BrowseNextRequest bnReq;
      UA_BrowseNextRequest_init(&bnReq);
      bnReq.releaseContinuationPoints = false;
      bnReq.continuationPointsSize = 1;

      // 使用 UA_Array_new 分配 continuationPoints 数组
      bnReq.continuationPoints =
          (UA_ByteString *)UA_Array_new(1, &UA_TYPES[UA_TYPES_BYTESTRING]);
      if (!bnReq.continuationPoints) {
        spdlog::error("分配 continuationPoints 数组失败");
        UA_BrowseNextRequest_clear(&bnReq);
        break;
      }

      // 初始化数组元素并复制内容
      UA_ByteString_init(&bnReq.continuationPoints[0]);
      UA_ByteString_copy(&continuationPoint, &bnReq.continuationPoints[0]);

      UA_BrowseNextResponse bnResp =
          UA_Client_Service_browseNext(client, bnReq);

      if (bnResp.responseHeader.serviceResult != UA_STATUSCODE_GOOD) {
        spdlog::error("BrowseNext 请求失败: {}",
                      UA_StatusCode_name(bnResp.responseHeader.serviceResult));
        UA_BrowseNextRequest_clear(&bnReq);
        UA_BrowseNextResponse_clear(&bnResp);
        break;
      }

      // 处理下一页的结果
      if (bnResp.resultsSize > 0) {
        UA_BrowseResult *nextResult = &bnResp.results[0];

        if (nextResult->statusCode == UA_STATUSCODE_GOOD) {
          processReferencePage(client, nextResult, depth, maxDepth, indent, vec,
                               visitedNodes);

          // 更新 continuation point 用于下一轮循环
          UA_ByteString_clear(&continuationPoint);
          if (!UA_String_isEmpty(&nextResult->continuationPoint)) {
            UA_ByteString_copy(&nextResult->continuationPoint,
                               &continuationPoint);
          }
        } else {
          spdlog::warn("BrowseNext result 状态码: {}",
                       UA_StatusCode_name(nextResult->statusCode));
          UA_BrowseNextRequest_clear(&bnReq);
          UA_BrowseNextResponse_clear(&bnResp);
          break;
        }
      }

      UA_BrowseNextRequest_clear(&bnReq);
      UA_BrowseNextResponse_clear(&bnResp);
    }

    // 清理 continuation point
    UA_ByteString_clear(&continuationPoint);
  }

  UA_BrowseRequest_clear(&bReq);
  UA_BrowseResponse_clear(&bResp);
}

// 对外暴露的简单接口
Result<RawDataTable, RichError>
OPCUABrowser::browseNodeChildren(int depth, int maxDepth,
                                 const std::string &indent) const {
  RawDataTable UAStruct_vec;
  std::unordered_set<std::string> visitedNodes;
  processNodeReferences(client_, nodeId, depth, maxDepth, indent, UAStruct_vec,
                        visitedNodes);
  return Result<RawDataTable, RichError>{std::move(UAStruct_vec)};
}

// 辅助函数：处理单页引用
void OPCUABrowser::processReferencePage(
    UA_Client *client, UA_BrowseResult *result, int depth, int maxDepth,
    const std::string &indent, RawDataTable &vec,
    std::unordered_set<std::string> &visitedNodes) const  {
  if (!result || result->referencesSize == 0)
    return;

  for (size_t j = 0; j < result->referencesSize; j++) {
    UA_ReferenceDescription *ref = &result->references[j];

    // 只处理对象和变量
    if (ref->nodeClass != UA_NODECLASS_VARIABLE &&
        ref->nodeClass != UA_NODECLASS_OBJECT) {
      continue;
    }

    std::string childNodeId = nodeIdToString(&ref->nodeId.nodeId);
    std::string childBrowseName = uaStringToString(ref->browseName.name);
    std::string childDisplayName = uaStringToString(ref->displayName.text);
    std::string nodeType =
        (ref->nodeClass == UA_NODECLASS_VARIABLE) ? "Variable" : "Object";

    // 打印节点信息
    std::string newIndent = indent + "  ";
    spdlog::info("{}{} [{}] - NodeId = {}", indent, childBrowseName, nodeType,
                 childNodeId);

    // 如果是变量，读取访问级别和值
    if (ref->nodeClass == UA_NODECLASS_VARIABLE) {
      // 读取 AccessLevel
      RawDataField item;
      item.name = childBrowseName;
      item.value = "0";

      UA_Byte accessLevel = 0;
      UA_StatusCode status = UA_Client_readAccessLevelAttribute(
          client, ref->nodeId.nodeId, &accessLevel);
      if (status == UA_STATUSCODE_GOOD) {
        uint8_t value = 0; // 使用整数类型
        if (accessLevel & UA_ACCESSLEVELMASK_READ) {
          value |= 0x01; // 设置读权限位
        }
        if (accessLevel & UA_ACCESSLEVELMASK_WRITE) {
          value |= 0x02; // 设置写权限位
        }
        item.metadata["AccessLevel"] = std::to_string(value);

        spdlog::debug("  AccessLevel: 0x{:02X} (读:{}, 写:{})", accessLevel,
                      (value & 0x01) ? "是" : "否",
                      (value & 0x02) ? "是" : "否");
      }

      item.metadata["NodeId"] = childNodeId;
      item.metadata["BrowseName"] = childBrowseName;
      item.metadata["ParentNodeId"] = extractParentNodeId(childNodeId);
      item.metadata["DisplayName"] = childDisplayName;

      // 读取变量值（需要实现 readAndPrintVariable 函数）
      bool result =
          readAndPrintVariable(client, ref->nodeId.nodeId, newIndent, item);
      if (result && item.metadata["ParentNodeId"] != "") {
        vec.push_back(std::move(item));
      }
    }

    // 递归处理子节点
    if (depth + 1 <= maxDepth) {
      processNodeReferences(client, ref->nodeId.nodeId, depth + 1, maxDepth,
                            newIndent, vec, visitedNodes);
    }
  }
}

std::string OPCUABrowser::extractParentNodeId(const std::string &nodeId) const {
  if (nodeId.empty()) {
    return "";
  }

  std::string parentId;

  // size_t bracketPos = nodeId.rfind('[');
  // if (bracketPos != std::string::npos) {
  //   // 检查是否是数组元素（不是字符串内容的一部分）
  //   // 方法1：检查 [ 前面是否是引号（说明是字符串内的内容，不是数组索引）
  //   // 方法2：直接认为是数组索引，因为 OPC UA 节点名中不包含 [ 字符

  //   // 推荐：直接认为是数组索引（OPC UA 节点名不会包含 '[' 字符）
  //   parentId = nodeId.substr(0, bracketPos);
  //   return parentId;
  // }

  // 2. 如果不是数组元素，查找最后一个 '.' 的位置
  if (parentId.empty()) {
    size_t lastDotPos = nodeId.rfind('.');
    if (lastDotPos == std::string::npos) {
      return ""; // 没有父节点
    }

    size_t lastDoubleQuotationPos = nodeId.rfind('\"');
    size_t lastSquareBracketPos = nodeId.rfind('[');
    if (lastSquareBracketPos - lastDoubleQuotationPos == 1 &&
        lastSquareBracketPos != std::string::npos) {
      //  mean \"xxxx.xxxx.xxxx.xxx\"[y]
      parentId = nodeId.substr(0, lastSquareBracketPos);
    } else {
      // do not find square or the square exist in front of .
      //  mean \"xxx.xxx\"
      parentId = nodeId.substr(0, lastDotPos);
    }
  }

  // parentId = eliminateSpareSymbol(parentId);

  //  do not exist "ns=3;s=" part , return parentId directly
  return parentId;
}

std::string OPCUABrowser::eliminateSpareSymbol(const std::string &nodeId) {
  if (nodeId.empty()) {
    return "";
  }

  std::string parentId = nodeId;

  // 3. 去除 "ns=3;s=" 前缀，只保留后面的内容
  size_t sPos = parentId.find(";s=");
  if (sPos != std::string::npos) {
    // 找到 "s=" 后面的内容
    std::string result = parentId.substr(sPos + 3); // +3 跳过 ";s="

    // // 去除可能的开头的引号
    // if (!result.empty() && result.front() == '"') {
    //   result = result.substr(1);
    // }
    // // 去除可能的结尾的引号（但保留结尾的点？）
    // if (!result.empty() && result.back() == '"') {
    //   result = result.substr(0, result.size() - 1);
    // }

    // 预期结果: "DB111_EdgeGatewayTest"."Motor"
    return result;
  }

  //  do not exist "ns=3;s=" part , return parentId directly
  return parentId;
}

bool OPCUABrowser::readAndPrintVariable(UA_Client *client, const UA_NodeId &nodeId,
                            const std::string &indent,
                            RawDataField &item) const {
  // 第一步：读取 ValueRank 判断是否为数组
  UA_Int32 valueRank;
  UA_StatusCode status =
      UA_Client_readValueRankAttribute(client, nodeId, &valueRank);

  if (status == UA_STATUSCODE_GOOD && valueRank > 0) {
    // 读取 ArrayDimensions 获取数组长度
    UA_UInt32 *arrayDimensions = nullptr;
    size_t arrayDimensionsSize = 0;
    status = UA_Client_readArrayDimensionsAttribute(
        client, nodeId, &arrayDimensionsSize, &arrayDimensions);

    if (status == UA_STATUSCODE_GOOD && arrayDimensionsSize > 0) {
      std::vector<UA_UInt32> dimensions;
      for (size_t i = 0; i < arrayDimensionsSize; i++) {
        dimensions.push_back(arrayDimensions[i]);
      }
      item.metadata["ArrayDimensions"] =
          std::to_string((arrayDimensionsSize > 0) ? arrayDimensions[0] : 0);
      item.metadata["ValueRank"] = std::to_string(valueRank);

      // 打印数组信息
      if (dimensions.size() == 1) {
        std::string identifier = uaStringToString(nodeId.identifier.string);
        spdlog::debug("{}{} 是一维数组，长度: {}", indent, identifier,
                      dimensions[0]);
      } else if (dimensions.size() > 1) {
        std::string dimStr;
        for (auto d : dimensions) {
          dimStr += std::to_string(d) + "x";
        }
        dimStr.pop_back();
        spdlog::debug("{}{} 是多维数组，维度: {}", indent, "Info", dimStr);
      }
    }

    // 关键修复：使用 UA_Array_delete 清理数组（与
    // UA_Client_readArrayDimensionsAttribute 分配方式匹配）
    if (arrayDimensions != nullptr) {
      UA_Array_delete(arrayDimensions, arrayDimensionsSize,
                      &UA_TYPES[UA_TYPES_UINT32]);
      arrayDimensions = nullptr;
    }

    // return true;
  }

  // 第二步：读取值属性
  UA_Variant value;
  UA_Variant_init(&value);

  status = UA_Client_readValueAttribute(client, nodeId, &value);

  if (status != UA_STATUSCODE_GOOD) {
    spdlog::debug("{}{} = [读取失败: {}]", indent, "Value",
                  UA_StatusCode_name(status));
    UA_Variant_clear(&value);
    return false;
  }

  // 第三步：根据类型打印值
  bool success = true;

  if (value.type == &UA_TYPES[UA_TYPES_BOOLEAN]) {
    UA_Boolean boolValue = *(UA_Boolean *)value.data;
    spdlog::info("{}{} = {} {} = {}", indent, "Value",
                 boolValue ? "true" : "false", "DataType",
                 value.type->typeName);
    item.metadata["DataType"] = "BOOL";
  } else if (value.type == &UA_TYPES[UA_TYPES_SBYTE]) {
    UA_SByte sbyteValue = *(UA_SByte *)value.data;
    spdlog::info("{}{} = {} {} = {}", indent, "Value", (int)sbyteValue,
                 "DataType", value.type->typeName);
    item.metadata["DataType"] = "INT";
  } else if (value.type == &UA_TYPES[UA_TYPES_BYTE]) {
    UA_Byte byteValue = *(UA_Byte *)value.data;
    spdlog::info("{}{} = {} {} = {}", indent, "Value", byteValue, "DataType",
                 value.type->typeName);
    item.metadata["DataType"] = "BYTE";
  } else if (value.type == &UA_TYPES[UA_TYPES_INT16]) {
    UA_Int16 int16Value = *(UA_Int16 *)value.data;
    spdlog::info("{}{} = {} {} = {}", indent, "Value", int16Value, "DataType",
                 value.type->typeName);
    item.metadata["DataType"] = "INT";
  } else if (value.type == &UA_TYPES[UA_TYPES_UINT16]) {
    UA_UInt16 uint16Value = *(UA_UInt16 *)value.data;
    spdlog::info("{}{} = {} {} = {}", indent, "Value", uint16Value, "DataType",
                 value.type->typeName);
    item.metadata["DataType"] = "WORD";
  } else if (value.type == &UA_TYPES[UA_TYPES_INT32]) {
    UA_Int32 int32Value = *(UA_Int32 *)value.data;
    spdlog::info("{}{} = {} {} = {}", indent, "Value", int32Value, "DataType",
                 value.type->typeName);
    item.metadata["DataType"] = "DINT";
  } else if (value.type == &UA_TYPES[UA_TYPES_UINT32]) {
    UA_UInt32 uint32Value = *(UA_UInt32 *)value.data;
    spdlog::info("{}{} = {} {} = {}", indent, "Value", uint32Value, "DataType",
                 value.type->typeName);
    item.metadata["DataType"] = "UDINT";
  } else if (value.type == &UA_TYPES[UA_TYPES_FLOAT]) {
    UA_Float floatValue = *(UA_Float *)value.data;
    spdlog::info("{}{} = {} {} = {}", indent, "Value", floatValue, "DataType",
                 value.type->typeName);
    item.metadata["DataType"] = "REAL";
  } else if (value.type == &UA_TYPES[UA_TYPES_STRING]) {
    spdlog::info("{}{} = {}", indent, "DataType", value.type->typeName);
    item.metadata["DataType"] = "STRING";
  }
  else {
    // 未知类型
    spdlog::warn("{}{} = [其他类型: {}]", indent, "Value",
                 value.type->typeName);
    item.metadata["DataType"] = "UNKNOWN";
  }

  // 统一使用 _clear 清理
  UA_Variant_clear(&value);

  return success;
}