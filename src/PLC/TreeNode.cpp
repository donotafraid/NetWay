#include "PLC/TreeNode.h"
#include "PLC_Collector/IDataNode.h"

// ============ 构造函数 ============
TreeNode::TreeNode(std::shared_ptr<IDataNode> data, const QString &name,
                   std::shared_ptr<TreeNode> parentNode)
    : m_data(std::move(data)), m_displayName(name), m_parent(parentNode) {
    }

// ============ 只读访问接口实现 ============

std::shared_ptr<const IDataNode> TreeNode::getReadOnlyData() const {
    return m_data;
}

std::shared_ptr<IDataNode> TreeNode::getReadWriteData() { return m_data; }

QString TreeNode::getDisplayName() const {
    return m_displayName;
}

bool TreeNode::isExpanded() const {
    return m_isExpanded;
}

QList<std::shared_ptr<TreeNode>> TreeNode::getChildren() const {
    return m_children; // 返回副本，安全
}

std::shared_ptr<TreeNode> TreeNode::getChild(int row) const {
    if (row < 0 || row >= m_children.size()) {
        return nullptr;
    }
    return m_children[row];
}

int TreeNode::getChildCount() const {
    return m_children.size();
}

std::shared_ptr<TreeNode> TreeNode::getParent() const {
    return m_parent.lock();
}

// ============ 受控的修改接口实现 ============

void TreeNode::addChild(std::shared_ptr<TreeNode> child) {
    if (!child) return;
    
    child->m_parent = shared_from_this(); // 建立弱引用
    m_children.append(child);
}

void TreeNode::removeChild(std::shared_ptr<TreeNode> child) {
    int idx = m_children.indexOf(child);
    if (idx != -1) {
        child->m_parent.reset(); // 断开父链接
        m_children.removeAt(idx);
    }
}

void TreeNode::setExpanded(bool expanded) {
    // 如果没有子节点，不允许展开
    if (expanded && m_children.isEmpty()) {
        return; // 可以改为记录警告日志
    }
    m_isExpanded = expanded;
}

// ============ 工厂方法 ============

std::shared_ptr<TreeNode> TreeNode::create(std::shared_ptr<IDataNode> data, 
                                           const QString& displayName,
                                           std::shared_ptr<TreeNode> parentNode) {
    return std::shared_ptr<TreeNode>(
        new TreeNode(std::move(data), displayName, parentNode));
}