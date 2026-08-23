#pragma once

#include <memory>
#include <QList>
#include <QString>

class IDataNode; // 前置声明，依赖抽象

class TreeNode : public std::enable_shared_from_this<TreeNode> {
private:
    // ========== 核心数据（全部私有） ==========
    std::shared_ptr<IDataNode> m_data;          // 数据载体（只允许构建时注入）
    std::weak_ptr<TreeNode> m_parent;           // 父节点（仅由树构建器维护）
    QList<std::shared_ptr<TreeNode>> m_children; // 子节点（禁止外部直接修改）
    QString m_displayName;
    bool m_isExpanded = false;

    // 构造函数私有，强制通过工厂或构建器创建
    TreeNode(std::shared_ptr<IDataNode> data, const QString &name,
             std::shared_ptr<TreeNode> parentNode);

public:
    // ========== 只读访问接口（供 Model/View 使用） ==========
    std::shared_ptr<const IDataNode> getReadOnlyData() const;
    QString getDisplayName() const;
    bool isExpanded() const;
    QList<std::shared_ptr<TreeNode>> getChildren() const;
    std::shared_ptr<TreeNode> getChild(int row) const;
    int getChildCount() const;
    std::shared_ptr<TreeNode> getParent() const;

    // ========== 受控的修改接口（仅限树构建器或友好类） ==========
    void addChild(std::shared_ptr<TreeNode> child);
    void removeChild(std::shared_ptr<TreeNode> child);
    void setExpanded(bool expanded);
    std::shared_ptr<IDataNode> getReadWriteData();

    // 工厂方法：创建节点的唯一途径
    static std::shared_ptr<TreeNode> create(std::shared_ptr<IDataNode> data, 
                                            const QString& displayName,
                                            std::shared_ptr<TreeNode> parentNode);

    // 禁止拷贝和赋值（防止意外复制树结构）
    TreeNode(const TreeNode&) = delete;
    TreeNode& operator=(const TreeNode&) = delete;
};