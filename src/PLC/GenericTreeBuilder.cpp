#include "PLC/GenericTreeBuilder.h"

std::shared_ptr<TreeNode> GenericTreeBuilder::build(
    const std::vector<std::shared_ptr<IDataNode>> &nodes) {
    
    std::shared_ptr<TreeNode> m_rootNode =
        TreeNode::create(nullptr, "Root", nullptr);

    std::map<std::string, std::shared_ptr<TreeNode>> m_treeNodeIDMap; // 路径到节点的映射
    
    for (auto &element : nodes) {
        std::weak_ptr<TreeNode> parent;

        auto it = m_treeNodeIDMap.find(element->getParentPath());
        if (it != m_treeNodeIDMap.end()) {
            // find it!
            parent = it->second;
        } else {
            // can not find it! mean we need build parent Node in Map first
            parent = createPlaceholderNode(m_rootNode, element->getParentPath(),
                                          m_treeNodeIDMap, nodes);
        }

        std::shared_ptr<TreeNode> varNode = TreeNode::create(
            element, QString::fromStdString(element->getName()), parent.lock());

        m_treeNodeIDMap[element->getFullPath()] = varNode;

        // build son-parent relationship
        std::shared_ptr<TreeNode> tmpPointer = parent.lock();
        if (tmpPointer) {
            tmpPointer->addChild(varNode);
        }
    }
    return m_rootNode;
}

std::shared_ptr<TreeNode> GenericTreeBuilder::createPlaceholderNode(
    std::shared_ptr<TreeNode> &rootNode, 
    const std::string &parent_path,
    std::map<std::string, std::shared_ptr<TreeNode>> &m_treeNodeIDMap,
    const std::vector<std::shared_ptr<IDataNode>> &nodes) {
    
    if (m_treeNodeIDMap.find(parent_path) != m_treeNodeIDMap.end()) {
        // mean the parent node has exist
        return nullptr;
    }

    // get parent IDataNode from transformed parentNodeName
    auto parentPointer = findOPCUADataStruct(parent_path, nodes);

    if (parentPointer) {
        // find the parentNode by recursion as far as
        auto gradParentPointer = createPlaceholderNode(
            rootNode, parentPointer->getParentPath(), m_treeNodeIDMap, nodes);
        
        if (!gradParentPointer) {
            // mean the gradparent node has exist, need find by map
            std::shared_ptr<TreeNode> placeholder = TreeNode::create(
                parentPointer, 
                QString::fromStdString(parentPointer->getName()),
                m_treeNodeIDMap[parentPointer->getParentPath()]);
            m_treeNodeIDMap[parent_path] = placeholder;
            
            // build parent-grandParent, grandParent-great grandParent relationship
            m_treeNodeIDMap[parentPointer->getParentPath()]->addChild(placeholder);
            return placeholder;
        } else {
            std::shared_ptr<TreeNode> placeholder = TreeNode::create(
                parentPointer, 
                QString::fromStdString(parentPointer->getName()),
                gradParentPointer);
            m_treeNodeIDMap[parent_path] = placeholder;
            
            // build parent-grandParent, grandParent-great grandParent relationship
            gradParentPointer->addChild(placeholder);
            return placeholder;
        }
    } else {
        // find node result -> nullptr means the parent node is root node (DB......)
        std::shared_ptr<TreeNode> placeholder = TreeNode::create(
            parentPointer, 
            QString::fromStdString(parent_path), 
            rootNode);
        
        // build parent-grandParent, grandParent-great grandParent relationship
        rootNode->addChild(placeholder);
        m_treeNodeIDMap[parent_path] = placeholder;
        return placeholder;
    }
}

std::shared_ptr<IDataNode> GenericTreeBuilder::findOPCUADataStruct(
    const std::string &parent_path,
    const std::vector<std::shared_ptr<IDataNode>> &nodes) {
    
    for (auto &element : nodes) {
        if (element->getFullPath() == parent_path) {
            return element;
        }
    }
    return nullptr;
}