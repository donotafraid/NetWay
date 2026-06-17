#include "PLC/OPCUABrowser.h"

// ==================== OPCUANodePathTracer 实现 ====================

bool OPCUANodePathTracer::loadFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "无法打开文件: " << filename << std::endl;
        return false;
    }
    
    fileLines.clear();
    std::string line;
    while (std::getline(file, line)) {
        fileLines.push_back(line);
    }
    file.close();
    
    std::cout << "成功加载文件，共 " << fileLines.size() << " 行" << std::endl;
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
    
    std::cout << "共解析 " << allNodes.size() << " 个节点" << std::endl;
}

std::vector<LoopNodeInfo> OPCUANodePathTracer::findNodesWithAccessLevel() {
    std::vector<LoopNodeInfo> result;
    
    for (const auto& node : allNodes) {
        if (!node.accessLevel.empty()) {
            result.push_back(node);
        }
    }
    
    std::cout << "找到 " << result.size() << " 个包含 AccessLevel 的节点" << std::endl;
    return result;
}

bool OPCUANodePathTracer::tracePathToRoot(const std::string& startNodeId, 
                                         const std::string& startBrowseName) {
    pathNodes.clear();
    pathNodeDetails.clear();
    
    std::string currentNodeId = startNodeId;
    std::set<std::string> visited;  // 防止循环
    
    std::cout << "\n开始追溯路径" << std::endl;
    std::cout << "起始节点: " << startNodeId;
    if (!startBrowseName.empty()) {
        std::cout << " (" << startBrowseName << ")";
    }
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    
    int step = 1;
    while (true) {
        // 保存当前节点
        pathNodes.push_back(currentNodeId);
        
        // 查找当前节点的详细信息
        LoopNodeInfo* nodeInfo = findNodeByNodeId(currentNodeId);
        
        if (nodeInfo) {
            pathNodeDetails.push_back(*nodeInfo);
            std::cout << step << ". 节点: " << currentNodeId << std::endl;
            std::cout << "   BrowseName: " << nodeInfo->browseName << std::endl;
            if (!nodeInfo->accessLevel.empty()) {
                std::cout << "   AccessLevel: " << nodeInfo->accessLevel << std::endl;
            }
            std::cout << "   ParentNodeId: " << nodeInfo->parentNodeId << std::endl;
        } else {
            // 节点定义不在 XML 中（可能是标准节点）
            pathNodeDetails.push_back(LoopNodeInfo(currentNodeId, "", "", ""));
            std::cout << step << ". 节点: " << currentNodeId << std::endl;
            std::cout << "   (节点定义不在 XML 中，可能是标准 OPC UA 节点)" << std::endl;
        }
        
        // 检查循环
        if (visited.find(currentNodeId) != visited.end()) {
            std::cout << "⚠️ 检测到循环引用，停止追溯" << std::endl;
            break;
        }
        visited.insert(currentNodeId);
        
        // 获取父节点
        std::string parentId;
        if (nodeInfo) {
            parentId = nodeInfo->parentNodeId;
        } else {
            // 无法获取父节点，停止
            std::cout << "无法获取父节点信息，停止追溯" << std::endl;
            break;
        }
        
        // 如果父节点是 ROOT 或为空，停止
        if (parentId == "ROOT" || parentId.empty()) {
            std::cout << "✓ 已到达根节点" << std::endl;
            break;
        }
        
        // 继续追溯父节点
        currentNodeId = parentId;
        step++;
        
        // 防止无限循环（最大深度 30）
        if (step > 30) {
            std::cout << "达到最大深度限制(30)，停止追溯" << std::endl;
            break;
        }
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "追溯完成，共 " << pathNodes.size() << " 个节点" << std::endl;
    
    return !pathNodes.empty();
}

void OPCUANodePathTracer::processAllNodesWithAccessLevel() {
    std::vector<LoopNodeInfo> accessNodes = findNodesWithAccessLevel();
    
    if (accessNodes.empty()) {
        std::cout << "未找到包含 AccessLevel 的节点" << std::endl;
        return;
    }
    
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "开始处理所有包含 AccessLevel 的节点" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    
    for (size_t idx = 0; idx < accessNodes.size(); idx++) {
        const auto& node = accessNodes[idx];
        
        std::cout << "\n" << std::string(40, '-') << std::endl;
        std::cout << "处理节点 [" << idx + 1 << "/" << accessNodes.size() << "]" << std::endl;
        std::cout << "NodeId: " << node.nodeId << std::endl;
        std::cout << "BrowseName: " << node.browseName << std::endl;
        std::cout << "AccessLevel: " << node.accessLevel << std::endl;
        std::cout << std::string(40, '-') << std::endl;
        
        // 追溯路径
        if (tracePathToRoot(node.nodeId, node.browseName)) {
            printPath();
            printSavedNodes();
        }
        
        std::cout << std::endl;
    }
}

bool OPCUANodePathTracer::processFirstNodeWithAccessLevel() {
    std::vector<LoopNodeInfo> accessNodes = findNodesWithAccessLevel();
    
    if (accessNodes.empty()) {
        std::cout << "未找到包含 AccessLevel 的节点" << std::endl;
        return false;
    }
    
    const auto& firstNode = accessNodes[0];
    std::cout << "\n使用第一个找到的节点:" << std::endl;
    std::cout << "  NodeId: " << firstNode.nodeId << std::endl;
    std::cout << "  BrowseName: " << firstNode.browseName << std::endl;
    std::cout << "  AccessLevel: " << firstNode.accessLevel << std::endl;

    return tracePathToRoot(firstNode.nodeId, firstNode.browseName);
}

void OPCUANodePathTracer::printPath() const {
    if (pathNodes.empty()) {
        std::cout << "无路径信息" << std::endl;
        return;
    }
    
    std::cout << "\n📁 完整路径（从叶子到根）:" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    
    for (size_t i = 0; i < pathNodes.size(); i++) {
        std::string indent(i * 2, ' ');
        std::cout << indent << "└── " << pathNodes[i] << std::endl;
        
        if (i < pathNodeDetails.size()) {
            const auto& detail = pathNodeDetails[i];
            if (!detail.browseName.empty()) {
                std::cout << indent << "     (" << detail.browseName << ")";
                if (!detail.accessLevel.empty()) {
                    std::cout << " [AccessLevel=" << detail.accessLevel << "]";
                }
                std::cout << std::endl;
            }
        }
    }
    std::cout << "----------------------------------------" << std::endl;
}

void OPCUANodePathTracer::printSavedNodes() const {
    std::cout << "\n💾 保存的节点 NodeId 列表:" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    for (size_t i = 0; i < pathNodes.size(); i++) {
        std::cout << "  [" << i << "] " << pathNodes[i];
        if (i < pathNodeDetails.size() && !pathNodeDetails[i].browseName.empty()) {
            std::cout << "  // " << pathNodeDetails[i].browseName;
        }
        std::cout << std::endl;
    }
    std::cout << "----------------------------------------" << std::endl;
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