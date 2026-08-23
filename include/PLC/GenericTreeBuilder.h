#pragma once

#include <QObject>
#include "PLC_Collector/IDataNode.h"
#include "PLC/TreeNode.h"
#include <memory>
#include <vector>
#include <map>
#include <string>

// 通用树构建器（放在内在层，永远不需要改）
class GenericTreeBuilder {
public:
    // 输入是抽象的 IDataNode，完全不知道 OPC UA 或 Modbus
    std::shared_ptr<TreeNode> build(const std::vector<std::shared_ptr<IDataNode>> &nodes);

private:
    std::shared_ptr<TreeNode> createPlaceholderNode(
        std::shared_ptr<TreeNode> &rootNode, 
        const std::string &parent_path,
        std::map<std::string, std::shared_ptr<TreeNode>> &m_treeNodeIDMap,
        const std::vector<std::shared_ptr<IDataNode>> &nodes);

    std::shared_ptr<IDataNode> findOPCUADataStruct(
        const std::string &parent_path,
        const std::vector<std::shared_ptr<IDataNode>> &nodes);
};