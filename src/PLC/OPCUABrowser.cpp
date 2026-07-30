#include "PLC/OPCUABrowser.h"

// ==================== OPCUANodePathTracer 实现 ====================

bool OPCUANodePathTracer::loadFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        spdlog::error("无法打开文件: {}", filename);
        return false;
    }
    
    fileLines.clear();
    std::string line;
    while (std::getline(file, line)) {
        fileLines.push_back(line);
    }
    file.close();
    
    spdlog::info("成功加载文件，共 {} 行", fileLines.size());
    return true;
}

void OPCUANodePathTracer::parseAllNodes() {
    allNodes.clear();
    
    for (const auto& line : fileLines) {
        if (!isNodeLine(line)) {
            continue;
        }
        
        std::string nodeId = extractAttribute(line, "NodeId");
        std::string browseName = extractAttribute(line, "BrowseName");
        std::string parentNodeId = extractAttribute(line, "ParentNodeId");
        std::string accessLevel = extractAttribute(line, "AccessLevel");
        
        if (nodeId.empty()) {
            continue;
        }
        
        // 如果 ParentNodeId 为空，说明是根节点
        if (parentNodeId.empty()) {
            parentNodeId = "ROOT";
        }
        
        LoopNodeInfo node(nodeId, browseName, parentNodeId, accessLevel);
        allNodes.push_back(node);
    }
    
    spdlog::info("共解析 {} 个节点", allNodes.size());
}

std::vector<LoopNodeInfo> OPCUANodePathTracer::findNodesWithAccessLevel() {
    std::vector<LoopNodeInfo> result;
    
    for (const auto& node : allNodes) {
        if (!node.accessLevel.empty()) {
            result.push_back(node);
        }
    }
    
    spdlog::info("找到 {} 个包含 AccessLevel 的节点", result.size());
    return result;
}

bool OPCUANodePathTracer::tracePathToRoot(const std::string& startNodeId, 
                                         const std::string& startBrowseName) {
    pathNodes.clear();
    pathNodeDetails.clear();
    
    std::string currentNodeId = startNodeId;
    std::set<std::string> visited;  // 防止循环
    
    spdlog::info("开始追溯路径");
    spdlog::info("起始节点: {} ({})", startNodeId, startBrowseName);
    if (!startBrowseName.empty()) {
        spdlog::debug("startBrowseName {} is empty ", startBrowseName);  // 修正：这里逻辑应该是非空时才输出，但原逻辑是输出"is empty"，保留原意改为debug
    }
    spdlog::info("========================================" );
    
    int step = 1;
    while (true) {
        // 保存当前节点
        pathNodes.push_back(currentNodeId);
        
        // 查找当前节点的详细信息
        LoopNodeInfo* nodeInfo = findNodeByNodeId(currentNodeId);
        
        if (nodeInfo) {
            pathNodeDetails.push_back(*nodeInfo);
            spdlog::info("step {} . 节点: {} ", step, startBrowseName);
            spdlog::info("   BrowseName: {}", nodeInfo->browseName);
            if (!nodeInfo->accessLevel.empty()) {
                spdlog::info("   AccessLevel: {}", nodeInfo->accessLevel);
            }
            spdlog::info("   ParentNodeId: {}", nodeInfo->parentNodeId);
        } else {
            // 节点定义不在 XML 中（可能是标准节点）
            pathNodeDetails.push_back(LoopNodeInfo(currentNodeId, "", "", ""));
            spdlog::info("step {} . 节点: {}", step, currentNodeId);
            spdlog::info("   (节点定义不在 XML 中，可能是标准 OPC UA 节点)");
        }
        
        // 检查循环
        if (visited.find(currentNodeId) != visited.end()) {
            spdlog::warn("⚠️ 检测到循环引用，停止追溯");
            break;
        }
        visited.insert(currentNodeId);
        
        // 获取父节点
        std::string parentId;
        if (nodeInfo) {
            parentId = nodeInfo->parentNodeId;
        } else {
            // 无法获取父节点，停止
            spdlog::warn("无法获取父节点信息，停止追溯");
            break;
        }
        
        // 如果父节点是 ROOT 或为空，停止
        if (parentId == "ROOT" || parentId.empty()) {
            spdlog::info("✓ 已到达根节点");
            break;
        }
        
        // 继续追溯父节点
        currentNodeId = parentId;
        step++;
        
        // 防止无限循环（最大深度 30）
        if (step > 30) {
            spdlog::warn("达到最大深度限制(30)，停止追溯");
            break;
        }
    }
    
    spdlog::info("========================================");
    spdlog::info("追溯完成，共 {} 个节点", pathNodes.size());
    
    return !pathNodes.empty();
}

void OPCUANodePathTracer::processAllNodesWithAccessLevel() {
    std::vector<LoopNodeInfo> accessNodes = findNodesWithAccessLevel();
    
    if (accessNodes.empty()) {
        spdlog::warn("未找到包含 AccessLevel 的节点");
        return;
    }
    
    spdlog::info("");
    spdlog::info("{}", std::string(60, '='));
    spdlog::info("开始处理所有包含 AccessLevel 的节点");
    spdlog::info("{}", std::string(60, '='));
    
    for (size_t idx = 0; idx < accessNodes.size(); idx++) {
        const auto& node = accessNodes[idx];
        
        spdlog::info("");
        spdlog::info("{}", std::string(40, '-'));
        spdlog::info("处理节点 [{}/{}]", idx + 1, accessNodes.size());
        spdlog::info("NodeId: {}", node.nodeId);
        spdlog::info("BrowseName: {}", node.browseName);
        spdlog::info("AccessLevel: {}", node.accessLevel);
        spdlog::info("{}", std::string(40, '-'));
        
        // 追溯路径
        if (tracePathToRoot(node.nodeId, node.browseName)) {
            printPath();
            printSavedNodes();
        }
        
        spdlog::info("");
    }
}

bool OPCUANodePathTracer::processFirstNodeWithAccessLevel() {
    std::vector<LoopNodeInfo> accessNodes = findNodesWithAccessLevel();
    
    if (accessNodes.empty()) {
        spdlog::warn("未找到包含 AccessLevel 的节点");
        return false;
    }
    
    const auto& firstNode = accessNodes[0];
    spdlog::info("");
    spdlog::info("使用第一个找到的节点:");
    spdlog::info("  NodeId: {}", firstNode.nodeId);
    spdlog::info("  BrowseName: {}", firstNode.browseName);
    spdlog::info("  AccessLevel: {}", firstNode.accessLevel);

    return tracePathToRoot(firstNode.nodeId, firstNode.browseName);
}

void OPCUANodePathTracer::printPath() const {
    if (pathNodes.empty()) {
        spdlog::info("无路径信息");
        return;
    }
    
    spdlog::info("");
    spdlog::info("📁 完整路径（从叶子到根）:");
    spdlog::info("----------------------------------------");
    
    for (size_t i = 0; i < pathNodes.size(); i++) {
        std::string indent(i * 2, ' ');
        spdlog::info("{}{}", indent, "└── " + pathNodes[i]);
        
        if (i < pathNodeDetails.size()) {
            const auto& detail = pathNodeDetails[i];
            if (!detail.browseName.empty()) {
                std::string msg = indent + "     (" + detail.browseName + ")";
                if (!detail.accessLevel.empty()) {
                    msg += " [AccessLevel=" + detail.accessLevel + "]";
                }
                spdlog::info("{}", msg);
            }
        }
    }
    spdlog::info("----------------------------------------");
}

void OPCUANodePathTracer::printSavedNodes() const {
    spdlog::info("");
    spdlog::info("💾 保存的节点 NodeId 列表:");
    spdlog::info("----------------------------------------");
    for (size_t i = 0; i < pathNodes.size(); i++) {
        std::string msg = "  [" + std::to_string(i) + "] " + pathNodes[i];
        if (i < pathNodeDetails.size() && !pathNodeDetails[i].browseName.empty()) {
            msg += "  // " + pathNodeDetails[i].browseName;
        }
        spdlog::info("{}", msg);
    }
    spdlog::info("----------------------------------------");
}

std::vector<std::string> OPCUANodePathTracer::getPathNodes() const {
    return pathNodes;
}

std::vector<LoopNodeInfo> OPCUANodePathTracer::getPathNodeDetails() const {
    return pathNodeDetails;
}

std::vector<std::string> OPCUANodePathTracer::getRootToLeafPath() const {
    std::vector<std::string> reversed = pathNodes;
    std::reverse(reversed.begin(), reversed.end());
    return reversed;
}

//=================trim functino========================================
std::string OPCUANodePathTracer::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

std::string OPCUANodePathTracer::extractAttribute(const std::string& line, const std::string& attrName) {
    std::string pattern = attrName + "=\"([^\"]*)\"";
    std::regex regex(pattern);
    std::smatch match;
    
    if (std::regex_search(line, match, regex) && match.size() > 1) {
        return trim(match[1].str());
    }
    return "";
}

bool OPCUANodePathTracer::isNodeLine(const std::string& line) {
    return (line.find("<UAObject") != std::string::npos ||
            line.find("<UAVariable") != std::string::npos ||
            line.find("<UAObjectType") != std::string::npos ||
            line.find("<UAVariableType") != std::string::npos ||
            line.find("<UADataType") != std::string::npos) &&
           line.find("NodeId=") != std::string::npos;
}

bool OPCUANodePathTracer::hasAccessLevel(const std::string& line) {
    return line.find("AccessLevel=") != std::string::npos;
}

std::string OPCUANodePathTracer::normalizeNodeId(const std::string& nodeId) {
    std::string result = nodeId;
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    return result;
}

LoopNodeInfo* OPCUANodePathTracer::findNodeByNodeId(const std::string& nodeId) {
    std::string normalizedTarget = normalizeNodeId(nodeId);
    
    for (auto& node : allNodes) {
        std::string normalizedNode = normalizeNodeId(node.nodeId);
        if (normalizedNode == normalizedTarget) {
            return &node;
        }
    }
    return nullptr;
}