#include "load_config/Qt_library.h"
#include "PLC/Struct.h"
#include <spdlog/spdlog.h>
#include <unordered_set>

#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>

class OPCUANodePathTracer;

struct UAStruct {
    std::string nodeID;
    std::string displayName;
    std::string parentNodeID;
    int arrayDimensions = -1;
    int nameSpaceIndex;
    int access_level = -1;

    S7DataType data_type_enum = S7DataType::UNKNOWN;
    std::unique_ptr<Dynamic_Value> data_pointer = std::make_unique<Dynamic_Value>();

    // 默认构造函数
    UAStruct() = default;

    // 禁用拷贝构造函数
    UAStruct(const UAStruct&) = delete;

    // 禁用拷贝赋值运算符
    UAStruct& operator=(const UAStruct&) = delete;

    // 移动构造函数
    UAStruct(UAStruct&& other) noexcept
        : nodeID(std::move(other.nodeID))
        , displayName(std::move(other.displayName))
        , parentNodeID(std::move(other.parentNodeID))
        , arrayDimensions(other.arrayDimensions)
        , nameSpaceIndex(other.nameSpaceIndex)
        , access_level(other.access_level)
        , data_type_enum(other.data_type_enum)
        , data_pointer(std::move(other.data_pointer)) {
        // 将源对象的基础类型成员重置为有效但未指定的状态
        other.arrayDimensions = -1;
        other.nameSpaceIndex = 0;
        other.access_level = -1;
        other.data_type_enum = S7DataType::UNKNOWN;
    }

    // 移动赋值运算符
    UAStruct& operator=(UAStruct&& other) noexcept {
        if (this != &other) {
            nodeID = std::move(other.nodeID);
            displayName = std::move(other.displayName);
            parentNodeID = std::move(other.parentNodeID);
            arrayDimensions = other.arrayDimensions;
            nameSpaceIndex = other.nameSpaceIndex;
            access_level = other.access_level;
            data_type_enum = other.data_type_enum;
            data_pointer = std::move(other.data_pointer);

            // 将源对象的基础类型成员重置
            other.arrayDimensions = -1;
            other.nameSpaceIndex = 0;
            other.access_level = -1;
            other.data_type_enum = S7DataType::UNKNOWN;
        }
        return *this;
    }
};


// ==================== checkVariableAccess 实现 ====================

static void checkVariableAccess(UA_Client* client, const UA_NodeId& nodeId, int& access_level) {
    UA_Byte accessLevel = 0;
    UA_StatusCode status = UA_Client_readAccessLevelAttribute(client, nodeId, &accessLevel);
    
    if (status == UA_STATUSCODE_GOOD) {
        spdlog::info("AccessLevel: 0x{:02X}", accessLevel);
        
        if (accessLevel & UA_ACCESSLEVELMASK_READ) {
            spdlog::info("  - 可读");
            access_level = 1;
        }
        if (accessLevel & UA_ACCESSLEVELMASK_WRITE) {
            spdlog::info("  - 可写");
            if(access_level == 1) {
                access_level = 3;
            }
        }
    } else {
        spdlog::error("读取 AccessLevel 失败: {}", UA_StatusCode_name(status));
        access_level = 0;
    }
}

// ==================== readAndPrintVariable 实现 ====================
static bool readAndPrintVariable(UA_Client *client, const UA_NodeId &nodeId,
                          const std::string &indent, OPCUAModernDataStruct &item) {
    // 第一步：读取 ValueRank 判断是否为数组
    UA_Int32 valueRank;
    UA_StatusCode status = UA_Client_readValueRankAttribute(client, nodeId, &valueRank);

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
            item.arrayDimensions = std::to_string((arrayDimensionsSize > 0) ? arrayDimensions[0] : 0);
            
            // 打印数组信息
            if (dimensions.size() == 1) {
                std::string identifier = uaStringToString(nodeId.identifier.string);
                spdlog::debug("{}{} 是一维数组，长度: {}", indent, identifier, dimensions[0]);
            } else if (dimensions.size() > 1) {
                std::string dimStr;
                for (auto d : dimensions) {
                    dimStr += std::to_string(d) + "x";
                }
                dimStr.pop_back();
                spdlog::debug("{}{} 是多维数组，维度: {}", indent, "Info", dimStr);
            }
        }
        
        // 关键修复：使用 UA_Array_delete 清理数组（与 UA_Client_readArrayDimensionsAttribute 分配方式匹配）
        if (arrayDimensions != nullptr) {
            UA_Array_delete(arrayDimensions, arrayDimensionsSize, &UA_TYPES[UA_TYPES_UINT32]);
            arrayDimensions = nullptr;
        }
        
        // return true;
    }
    
    // 第二步：读取值属性
    UA_Variant value;
    UA_Variant_init(&value);
    
    status = UA_Client_readValueAttribute(client, nodeId, &value);
    
    if (status != UA_STATUSCODE_GOOD) {
        spdlog::debug("{}{} = [读取失败: {}]", indent, "Value", UA_StatusCode_name(status));
        UA_Variant_clear(&value);
        return false;
    }
    
    // 第三步：根据类型打印值
    bool success = true;
    
    if (value.type == &UA_TYPES[UA_TYPES_BOOLEAN]) {
        UA_Boolean boolValue = *(UA_Boolean*)value.data;
        spdlog::info("{}{} = {} {} = {}", indent, "Value",
                     boolValue ? "true" : "false", "DataType",
                     value.type->typeName);
        item.data_type_enum = S7DataType::BOOL;
    } else if (value.type == &UA_TYPES[UA_TYPES_SBYTE]) {
        UA_SByte sbyteValue = *(UA_SByte*)value.data;
        spdlog::info("{}{} = {} {} = {}", indent, "Value",
                     (int)sbyteValue, "DataType", value.type->typeName);
        item.data_type_enum = S7DataType::BYTE;
    } else if (value.type == &UA_TYPES[UA_TYPES_BYTE]) {
        UA_Byte byteValue = *(UA_Byte*)value.data;
        spdlog::info("{}{} = {} {} = {}", indent, "Value",
                     byteValue, "DataType", value.type->typeName);
        item.data_type_enum = S7DataType::BYTE;
    } else if (value.type == &UA_TYPES[UA_TYPES_INT16]) {
        UA_Int16 int16Value = *(UA_Int16*)value.data;
        spdlog::info("{}{} = {} {} = {}", indent, "Value",
                     int16Value, "DataType", value.type->typeName);
        item.data_type_enum = S7DataType::INT;
    } else if (value.type == &UA_TYPES[UA_TYPES_UINT16]) {
        UA_UInt16 uint16Value = *(UA_UInt16*)value.data;
        spdlog::info("{}{} = {} {} = {}", indent, "Value",
                     uint16Value, "DataType", value.type->typeName);
        item.data_type_enum = S7DataType::WORD;
    } else if (value.type == &UA_TYPES[UA_TYPES_INT32]) {
        UA_Int32 int32Value = *(UA_Int32*)value.data;
        spdlog::info("{}{} = {} {} = {}", indent, "Value",
                     int32Value, "DataType", value.type->typeName);
        item.data_type_enum = S7DataType::DINT;
    } else if (value.type == &UA_TYPES[UA_TYPES_UINT32]) {
        UA_UInt32 uint32Value = *(UA_UInt32*)value.data;
        spdlog::info("{}{} = {} {} = {}", indent, "Value",
                     uint32Value, "DataType", value.type->typeName);
        item.data_type_enum = S7DataType::UDINT;
    } else if (value.type == &UA_TYPES[UA_TYPES_FLOAT]) {
        UA_Float floatValue = *(UA_Float*)value.data;
        spdlog::info("{}{} = {} {} = {}", indent, "Value",
                     floatValue, "DataType", value.type->typeName);
        item.data_type_enum = S7DataType::REAL;
    } else if (value.type == &UA_TYPES[UA_TYPES_STRING]) {
        spdlog::info("{}{} = {}", indent, "DataType", value.type->typeName);
        item.data_type_enum = S7DataType::STRING;
    }

    // }
    //  else if (value.type == &UA_TYPES[UA_TYPES_DATETIME]) {
    //     // DateTime 类型处理
    //     UA_DateTime dateTime = *(UA_DateTime*)value.data;
    //     spdlog::info("{}{} = {} {} = {}", indent, "Value",
    //                  dateTime, "DataType", value.type->typeName);
    //     item.data_type_enum = S7DataType::UNKNOWN;

    // } else if (value.type == &UA_TYPES[UA_TYPES_GUID]) {
    //     // GUID 类型处理
    //     UA_Guid* guid = (UA_Guid*)value.data;
    //     spdlog::info("{}{} =
    //     {:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}
    //     {} = {}",
    //                  indent, "Value",
    //                  guid->data1, guid->data2, guid->data3,
    //                  guid->data4[0], guid->data4[1], guid->data4[2],
    //                  guid->data4[3], guid->data4[4], guid->data4[5],
    //                  guid->data4[6], guid->data4[7], "DataType",
    //                  value.type->typeName);
    //     item.data_type_enum = S7DataType::UNKNOWN;
    // }

    else {
      // 未知类型
      spdlog::warn("{}{} = [其他类型: {}]", indent, "Value",
                   value.type->typeName);
      item.data_type_enum = S7DataType::UNKNOWN;
    }


    item.dataVar = 0;
    // 统一使用 _clear 清理
    UA_Variant_clear(&value);
    
    return success;
}
// ==================== browseNodeChildren 实现 ====================

static void browseNodeChildren(UA_Client* client, const UA_NodeId& nodeId, 
                        int depth, int maxDepth, 
                        const std::string& indent = "") {
    if (depth > maxDepth) {
        return;
    }
    
    // 构建浏览请求
    UA_BrowseRequest bReq;
    UA_BrowseRequest_init(&bReq);
    bReq.requestedMaxReferencesPerNode = 0;
    bReq.nodesToBrowseSize = 1;
    bReq.nodesToBrowse = UA_BrowseDescription_new();
    UA_NodeId_copy(&nodeId, &bReq.nodesToBrowse[0].nodeId);
    bReq.nodesToBrowse[0].resultMask = UA_BROWSERESULTMASK_ALL;
    bReq.nodesToBrowse[0].browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bReq.nodesToBrowse[0].referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
    bReq.nodesToBrowse[0].includeSubtypes = true;
    bReq.nodesToBrowse[0].nodeClassMask = 0;
    
    // 调用浏览服务
    UA_BrowseResponse bResp = UA_Client_Service_browse(client, bReq);
    
    std::vector<UAStruct> ua_vector;
    if (bResp.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
        for (size_t i = 0; i < bResp.resultsSize; i++) {
            UA_BrowseResult result = bResp.results[i];
            if (result.statusCode == UA_STATUSCODE_GOOD) {
                for (size_t j = 0; j < result.referencesSize; j++) {
                    UA_ReferenceDescription* ref = &result.references[j];
                    
                    // 只处理对象和变量
                    if (ref->nodeClass == UA_NODECLASS_VARIABLE ||
                        ref->nodeClass == UA_NODECLASS_OBJECT) {
                      OPCUAModernDataStruct item;
                      // 获取节点信息
                      std::string childNodeId = nodeIdToString(&ref->nodeId.nodeId);
                      std::string childBrowseName = uaStringToString(ref->browseName.name);

                      std::string newIndent = indent + "  ";

                      // 打印节点信息
                      spdlog::info(
                          "{}{} [{}] - {} = {}", indent, childBrowseName,
                          ref->nodeClass == UA_NODECLASS_VARIABLE ? "Variable" : "Object",
                          "NodeId", childNodeId);
                      UA_NodeId_copy(&ref->nodeId.nodeId,
                                     &item.nodeID);
                      item.variable_nodeID = childNodeId;
                      item.variable_name = childBrowseName;
                      item.namespace_index = 0;
                      item.parent_nodeID = "";

                      // 如果是变量，读取并打印值
                      if (ref->nodeClass == UA_NODECLASS_VARIABLE) {
                        int t;
                        checkVariableAccess(client, ref->nodeId.nodeId, item.access_level);
                        readAndPrintVariable(client, ref->nodeId.nodeId,
                                             newIndent, item);
                      }
                      
                      // 递归浏览子节点
                      if (depth + 1 <= maxDepth) {
                        browseNodeChildren(client, ref->nodeId.nodeId, 
                                          depth + 1, maxDepth, newIndent);
                      }
                    }
                }
            }
        }
    }
    
    UA_BrowseRequest_clear(&bReq);
    UA_BrowseResponse_clear(&bResp);
}

// ==================== browseDBAndVariables 实现 ====================

static void browseDBAndVariables(UA_Client* client, OPCUANodePathTracer& tracer) {
    spdlog::info("========================================");
    spdlog::info("开始查找并遍历 DB 节点...");
    spdlog::info("========================================");

    // 第一步：通过路径找到 DB 节点
    UA_TranslateBrowsePathsToNodeIdsRequest req;
    UA_TranslateBrowsePathsToNodeIdsRequest_init(&req);

    req.browsePathsSize = 1;
    req.browsePaths = (UA_BrowsePath *)UA_Array_new(1, &UA_TYPES[UA_TYPES_BROWSEPATH]);
    if (!req.browsePaths) {
        spdlog::error("分配 BrowsePath 失败");
        UA_TranslateBrowsePathsToNodeIdsRequest_clear(&req);
        return;
    }

    req.browsePaths[0].startingNode = UA_NODEID_STRING_ALLOC(3, "PLC");

    req.browsePaths[0].relativePath.elementsSize = 2;
    req.browsePaths[0].relativePath.elements = (UA_RelativePathElement *)UA_Array_new(
            2, &UA_TYPES[UA_TYPES_RELATIVEPATHELEMENT]);

    if (!req.browsePaths[0].relativePath.elements) {
        spdlog::error("分配 RelativePath 元素数组失败");
        UA_TranslateBrowsePathsToNodeIdsRequest_clear(&req);
        return;
    }

    // 第一层：DataBlocksGlobal
    req.browsePaths[0].relativePath.elements[0].referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
    req.browsePaths[0].relativePath.elements[0].isInverse = false;
    req.browsePaths[0].relativePath.elements[0].includeSubtypes = true;
    req.browsePaths[0].relativePath.elements[0].targetName = UA_QUALIFIEDNAME_ALLOC(3, "DataBlocksGlobal");

    // 第二层：DB111_EdgeGatewayTest
    req.browsePaths[0].relativePath.elements[1].referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
    req.browsePaths[0].relativePath.elements[1].isInverse = false;
    req.browsePaths[0].relativePath.elements[1].includeSubtypes = true;
    req.browsePaths[0].relativePath.elements[1].targetName = UA_QUALIFIEDNAME_ALLOC(3, "DB111_EdgeGatewayTest");

    // 调用服务
    UA_TranslateBrowsePathsToNodeIdsResponse resp = UA_Client_Service_translateBrowsePathsToNodeIds(client, req);

    UA_NodeId foundNode;
    UA_NodeId_init(&foundNode);
    bool nodeFound = false;

    if (resp.responseHeader.serviceResult == UA_STATUSCODE_GOOD &&
        resp.resultsSize > 0 &&
        resp.results[0].statusCode == UA_STATUSCODE_GOOD &&
        resp.results[0].targetsSize > 0) {

        UA_NodeId_copy(&resp.results[0].targets[0].targetId.nodeId, &foundNode);
        UA_String nodeIdStr;
        UA_NodeId_print(&foundNode, &nodeIdStr);
        spdlog::info("成功找到 DB 节点: {}", uaStringToString(nodeIdStr));
        UA_String_clear(&nodeIdStr);
        nodeFound = true;
    } else {
        spdlog::error("未找到 DB 节点");
    }

    // 清理请求资源
    UA_TranslateBrowsePathsToNodeIdsResponse_clear(&resp);
    UA_TranslateBrowsePathsToNodeIdsRequest_clear(&req);

    if (!nodeFound) {
        return;
    }

    // 第二步：获取节点名称（BrowseName）
    spdlog::info("========================================");
    spdlog::info("DB 节点结构:");
    spdlog::info("========================================");

    // 读取节点的 BrowseName
    UA_QualifiedName browseName;
    UA_QualifiedName_init(&browseName);
    UA_StatusCode status = UA_Client_readBrowseNameAttribute(client, foundNode, &browseName);
    if (status == UA_STATUSCODE_GOOD) {
        spdlog::info("DB 名称: {}:{}", browseName.namespaceIndex,
                     uaStringToString(browseName.name));
    }
    UA_QualifiedName_clear(&browseName);

    // 第三步：遍历 DB 下的所有子节点（变量）
    spdlog::info("----------------------------------------");
    spdlog::info("DB 下的变量列表:");
    spdlog::info("----------------------------------------");

    browseNodeChildren(client, foundNode, 0, 3, "");

    // 清理
    UA_NodeId_clear(&foundNode);

    spdlog::info("========================================");
    spdlog::info("DB 遍历完成");
    spdlog::info("========================================");
}

// ==================== 浏览配置 ====================

struct BrowseConfig {
    int maxDepth = 10;           // 最大深度
    int maxNodes = 1000;         // 最多浏览节点数
    int timeoutSeconds = 10;     // 超时时间（秒）
    bool browseObjects = true;   // 是否浏览 Object
    bool browseVariables = true; // 是否浏览 Variable
    bool browseObjectTypes = false; // 是否浏览 ObjectType（通常不需要）
    bool browseVariableTypes = false; // 是否浏览 VariableType（通常不需要）
};

// ==================== 节点信息结构 ====================

struct XMLNodeInfo {
    UA_NodeId nodeId;
    int depth;
    std::string displayName;
    std::string browseName;
    int namespaceIndex;
    UA_NodeClass nodeClass;
    
    XMLNodeInfo() : nodeId(UA_NODEID_NULL), depth(0), namespaceIndex(0), nodeClass(UA_NODECLASS_UNSPECIFIED) {}
};

struct LoopNodeInfo {
    std::string nodeId;      // 节点的 NodeId
    std::string browseName;  // 节点的 BrowseName
    std::string parentNodeId;// 父节点的 NodeId
    std::string accessLevel; // AccessLevel（如果有）
    
    LoopNodeInfo() = default;
    LoopNodeInfo(const std::string& nid, const std::string& bname, 
             const std::string& pid, const std::string& access = "")
        : nodeId(nid), browseName(bname), parentNodeId(pid), accessLevel(access) {}
};

class OPCUANodePathTracer {
public:
  // 构造函数
  OPCUANodePathTracer() = default;

  // 加载 XML 文件
  bool loadFile(const std::string &filename);

  // 解析所有节点（构建查找表）
  void parseAllNodes();

  // 找到所有包含 AccessLevel 的变量节点
  std::vector<LoopNodeInfo> findNodesWithAccessLevel();

  // 从指定节点开始追溯父节点链
  bool tracePathToRoot(const std::string &startNodeId,
                       const std::string &startBrowseName = "");

  // 处理所有包含 AccessLevel 的节点
  void processAllNodesWithAccessLevel();

  // 处理第一个包含 AccessLevel 的节点（简单模式）
  bool processFirstNodeWithAccessLevel();

  // 打印完整路径（树形结构）
  void printPath() const;

  // 打印保存的节点列表
  void printSavedNodes() const;

  // 获取保存的路径节点列表
  std::vector<std::string> getPathNodes() const;

  // 获取保存的路径节点详细信息
  std::vector<LoopNodeInfo> getPathNodeDetails() const;

  // 获取从根到叶子的正序路径
  std::vector<std::string> getRootToLeafPath() const;

private:
  std::vector<std::string> fileLines;
  std::vector<LoopNodeInfo> allNodes;
  std::vector<std::string> pathNodes;        // 存储路径上的节点 NodeId
  std::vector<LoopNodeInfo> pathNodeDetails; // 存储路径上节点的详细信息

  // 清理字符串（去除首尾空格和引号）
  std::string trim(const std::string &str);

  // 提取属性值
  std::string extractAttribute(const std::string &line,
                               const std::string &attrName);

  // 判断是否是节点行
  bool isNodeLine(const std::string &line);

  // 检查行是否包含 AccessLevel 属性
  bool hasAccessLevel(const std::string &line);

  // 规范化 NodeId 格式（用于比较）
  std::string normalizeNodeId(const std::string &nodeId);

  // 根据 NodeId 查找节点信息
  LoopNodeInfo *findNodeByNodeId(const std::string &nodeId);
};

struct OPCUAInlineBrowse {
  // 处理单个节点的所有引用（包括分页）
  static void
  test_processNodeReferences(UA_Client *client, const UA_NodeId &nodeId,
                             int depth, int maxDepth, const std::string &indent,
                             std::vector<OPCUAModernDataStruct> &vec,
                             std::unordered_set<std::string> &visitedNodes) {
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
      test_processReferencePage(client, result, depth, maxDepth, indent,vec,visitedNodes);

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
          spdlog::error(
              "BrowseNext 请求失败: {}",
              UA_StatusCode_name(bnResp.responseHeader.serviceResult));
          UA_BrowseNextRequest_clear(&bnReq);
          UA_BrowseNextResponse_clear(&bnResp);
          break;
        }

        // 处理下一页的结果
        if (bnResp.resultsSize > 0) {
          UA_BrowseResult *nextResult = &bnResp.results[0];

          if (nextResult->statusCode == UA_STATUSCODE_GOOD) {
            test_processReferencePage(client, nextResult, depth, maxDepth,
                                      indent,vec,visitedNodes);

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
  static std::vector<OPCUAModernDataStruct> test_browseNodeChildren(UA_Client *client,
                                      const UA_NodeId &nodeId, int depth,
                                      int maxDepth, const std::string &indent) {
    std::vector<OPCUAModernDataStruct> UAStruct_vec;
    std::unordered_set<std::string> visitedNodes;
    test_processNodeReferences(client, nodeId, depth, maxDepth, indent,UAStruct_vec,visitedNodes);
    return UAStruct_vec;
  }

  // 辅助函数：处理单页引用
  static void
  test_processReferencePage(UA_Client *client, UA_BrowseResult *result,
                            int depth, int maxDepth, const std::string &indent,
                            std::vector<OPCUAModernDataStruct> &vec,
                            std::unordered_set<std::string> &visitedNodes) {
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
      std::string nodeType =
          (ref->nodeClass == UA_NODECLASS_VARIABLE) ? "Variable" : "Object";

      // 打印节点信息
      std::string newIndent = indent + "  ";
      spdlog::info("{}{} [{}] - NodeId = {}", indent, childBrowseName, nodeType,
                   childNodeId);


      // 如果是变量，读取访问级别和值
      if (ref->nodeClass == UA_NODECLASS_VARIABLE) {
        // 读取 AccessLevel
        OPCUAModernDataStruct item;
        UA_NodeId_copy(&ref->nodeId.nodeId, &item.nodeID);
        item.variable_name = childBrowseName;
        item.variable_nodeID = childNodeId;
        item.namespace_index = ref->nodeId.nodeId.namespaceIndex;
        item.parent_nodeID = extractParentNodeId(childNodeId);

        UA_Byte accessLevel = 0;
        UA_StatusCode status = UA_Client_readAccessLevelAttribute(
            client, ref->nodeId.nodeId, &accessLevel);
        if (status == UA_STATUSCODE_GOOD) {
          item.access_level = 0;
          if (accessLevel & UA_ACCESSLEVELMASK_READ) {
            item.access_level |= 0x01;
          }
          if (accessLevel & UA_ACCESSLEVELMASK_WRITE) {
            item.access_level |= 0x02;
          }
          spdlog::debug("  AccessLevel: 0x{:02X} (读:{}, 写:{})", accessLevel,
                        (item.access_level & 0x01) ? "是" : "否",
                        (item.access_level & 0x02) ? "是" : "否");
        }

        // 读取变量值（需要实现 readAndPrintVariable 函数）
        bool result = readAndPrintVariable(client, ref->nodeId.nodeId, newIndent, item);
        if(result && item.parent_nodeID != "")
        {
          vec.push_back(std::move(item));
        }
      }

      // 递归处理子节点
      if (depth + 1 <= maxDepth) {
        test_processNodeReferences(client, ref->nodeId.nodeId, depth + 1,
                                   maxDepth, newIndent,vec,visitedNodes);
      }
    }
  }

  static std::string extractParentNodeId(const std::string &nodeId) {
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

  static std::string eliminateSpareSymbol(const std::string &nodeId) {
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
 
};
