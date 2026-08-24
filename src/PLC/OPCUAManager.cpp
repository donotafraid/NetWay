#include "PLC/OPCUAManager.h"
#include <spdlog/spdlog.h>
#include "PLC/OpcUaDataNode.h"
#include "PLC/XMLParser.h"
#include "PLC/OPCUACSV.h"
#include "PLC/Struct.h"

//OPCUADataBlockModel----------------------------------------------------------

// ==================== 构造函数 ====================

OPCUADataBlockModel::OPCUADataBlockModel(QObject* parent)
    : QAbstractTableModel(parent)
    {
}

// ==================== 析构函数 ====================

OPCUADataBlockModel::~OPCUADataBlockModel() {
    // 智能指针会自动释放，无需额外处理
}

// ==================== set internal member function ====================

// ==================== QAbstractTableModel 接口 ====================
QModelIndex OPCUADataBlockModel::index(int row, int column,
                                       const QModelIndex &parent) const {
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    TreeNode *parentNode = parent.isValid()
                               ? static_cast<TreeNode *>(parent.internalPointer())
                               : m_rootNode.get();

    if (!parentNode || row >= parentNode->getChildCount())
        return QModelIndex();

    // ⚠️ 关键：这里创建索引时传入的指针是否正确？
    TreeNode* childNode = parentNode->getChild(row).get();
    QModelIndex result =
        createIndex(row, column, childNode); // ← childNode 应该非空且有数据

    return result;
}

QModelIndex OPCUADataBlockModel::parent(const QModelIndex &child) const {
    if (!child.isValid())
        return QModelIndex();

    // 1. 获取子节点指针（不增加引用计数）
    TreeNode *childNode = static_cast<TreeNode *>(child.internalPointer());
    if (!childNode)
        return QModelIndex();

    // 2. 获取父节点（使用弱引用或原始指针，避免增加引用计数）
    std::shared_ptr<TreeNode> parentNode = childNode->getParent(); // 如果使用 weak_ptr
    
    if (!parentNode)
        return QModelIndex();

    // 3. 如果父节点是根节点，返回无效索引
    if (parentNode == m_rootNode)
        return QModelIndex();

    // 4. 获取祖父节点以计算行号
    std::shared_ptr<TreeNode> grandParent = parentNode->getParent();
    if (!grandParent)
        return QModelIndex();

    // 5. 查找父节点在祖父节点中的位置
    int row = findChildIndex(grandParent, parentNode);
    if (row < 0)
        return QModelIndex();

    return createIndex(row, 0, parentNode.get());
}



int OPCUADataBlockModel::rowCount(const QModelIndex &parent) const {
  if (!parent.isValid()) {
    // 顶层节点数量
    return m_rootNode->getChildCount();
  }

  TreeNode *node = static_cast<TreeNode *>(parent.internalPointer());
  int count = node ? node->getChildCount(): 0;
  return node ? node->getChildCount() : 0;
}

int OPCUADataBlockModel::columnCount(const QModelIndex &parent) const {
  return 5; // 叶子节点有所有数据列
}

bool OPCUADataBlockModel::hasChildren(const QModelIndex &parent) const {
  // qDebug() << "=== hasChildren() called ===";

  if (!parent.isValid()) {
    bool result = !m_rootNode->getChildren().empty();
    qDebug() << "  top level, children count:" << m_rootNode->getChildCount();
    qDebug() << "  returning:" << result;
    return result;
  }

  // index node self
  TreeNode *node = static_cast<TreeNode *>(parent.internalPointer());
  if (!node) {
    qDebug() << "  node is null, returning false";
    return false;
  }

  return node->getChildCount();
}

QVariant OPCUADataBlockModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid())
    return QVariant();

  TreeNode *node = static_cast<TreeNode *>(index.internalPointer());

  // EditRole - 返回原始数据用于编辑
  if (role == Qt::EditRole) {
    switch (index.column()) {
    case 0: // Name 列
      return (node->getDisplayName());

    case 1: // Data Type 列（只读）
      if (node->getReadOnlyData())
        return static_cast<int>(node->getReadOnlyData()->getDataType());

    case 2: // limit of authority 列（只读）
      if (node->getReadOnlyData())
        return node->getReadOnlyData()->getAccessLevel();

    case 3: // Value 列 - 根据数据类型返回原始值
    {
      if (node->getReadOnlyData()) {
        ValueType val = node->getReadOnlyData()->readValue();
        return std::visit(
            [](auto &&arg) -> QVariant {
              using T = std::decay_t<decltype(arg)>;

              if constexpr (std::is_same_v<T, bool>) {
                return QVariant(arg);
              } else if constexpr (std::is_same_v<T, uint8_t>) {
                return QVariant(static_cast<uint>(
                    arg)); // QVariant 不支持 uint8_t，转为 uint
              } else if constexpr (std::is_same_v<T, int16_t>) {
                return QVariant(arg);
              } else if constexpr (std::is_same_v<T, uint16_t>) {
                return QVariant(arg);
              } else if constexpr (std::is_same_v<T, int32_t>) {
                return QVariant(arg);
              } else if constexpr (std::is_same_v<T, uint32_t>) {
                return QVariant(arg);
              } else if constexpr (std::is_same_v<T, float>) {
                return QVariant(arg);
              } else if constexpr (std::is_same_v<T, std::string>) {
                return QString::fromStdString(arg);
              } else {
                // 未知类型，返回空 QVariant
                return QVariant();
              }
            },
            val);
      }
    }

    case 4: // Comment 列
      if (node->getReadOnlyData())
        return QString::fromStdString(node->getReadOnlyData()->getDescrition());

    default:
      return QVariant();
    }
  }

  // DisplayRole - 返回格式化的显示数据
  if (role == Qt::DisplayRole) {
    switch (index.column()) {
    case 0: // Name 列
      return (node->getDisplayName());

    case 1: // Data Type 列
    {
      if (node->getReadOnlyData() == nullptr) {
        return QVariant{};
      }
      auto it = S7DataTypeToString.find(node->getReadOnlyData()->getDataType());
      if (it != S7DataTypeToString.end()) {
        return QString::fromStdString(it->second);
      }
      return QString::fromStdString("UNKNOWN");
    }

    case 2: // limit of authority 列
      if (node->getReadOnlyData() == nullptr) {
        return QVariant{};
      }
      return node->getReadOnlyData()->getAccessLevel();

    case 3: // Value 列 - 格式化显示
      if (node->getReadOnlyData() == nullptr) {
        return QVariant{};
      } else {
        ValueType val = node->getReadOnlyData()->readValue();
        return std::visit(
            [](auto &&arg) -> QVariant {
              using T = std::decay_t<decltype(arg)>;

              if constexpr (std::is_same_v<T, bool>) {
                return QVariant(arg);
              } else if constexpr (std::is_same_v<T, uint8_t>) {
                return QVariant(static_cast<uint>(
                    arg)); // QVariant 不支持 uint8_t，转为 uint
              } else if constexpr (std::is_same_v<T, int16_t>) {
                return QVariant(arg);
              } else if constexpr (std::is_same_v<T, uint16_t>) {
                return QVariant(arg);
              } else if constexpr (std::is_same_v<T, int32_t>) {
                return QVariant(arg);
              } else if constexpr (std::is_same_v<T, uint32_t>) {
                return QVariant(arg);
              } else if constexpr (std::is_same_v<T, float>) {
                return QVariant(arg);
              } else if constexpr (std::is_same_v<T, std::string>) {
                return QString::fromStdString(arg);
              } else {
                // 未知类型，返回空 QVariant
                return QVariant();
              }
            },
            val);
      }

    case 4: // Comment 列
      if (node->getReadOnlyData() == nullptr) {
        return QVariant{};
      }
      return QString::fromStdString(node->getReadOnlyData()->getDescrition());

    default:
      return QVariant();
    }
  }

  return QVariant();
}

bool OPCUADataBlockModel::setData(const QModelIndex &index,
                                  const QVariant &value, int role) {
  if (!index.isValid() || role != Qt::EditRole) {
    return false;
  }

  // 1. 从 index 中获取 TreeNode 指针
  TreeNode *node = static_cast<TreeNode *>(index.internalPointer());
  if (!node) {
    return false;
  }

  // 2. 获取对应的 VariableInfo
  std::shared_ptr<IDataNode> dataBlock = node->getReadWriteData();
  if (!dataBlock) {
    // 这是一个容器节点（如 "Motor"），不可编辑
    return false;
  }

  if (index.column() == 3) { // Value 列
    // 根据数据类型进行验证和转换
    // 检查是否已存在

    std::weak_ptr<IDataNode> dataNode = node->getReadWriteData();
    S7DataType type = dataNode.lock()->getDataType();

    // 2. 【核心转换层】：根据业务类型，将 QVariant 解包为对应的 C++ 标准类型
    std::optional<ValueType> coreValue; // ValueType = std::variant<int, float,
                                        // bool, std::string, ...>
    switch (type) {
    case S7DataType::BOOL: {
      coreValue = value.toBool();
      break;
    }

    case S7DataType::INT: {
      // 使用辅助函数，更安全
      convertAndStoreSigned<int16_t>(value, coreValue);
      break;
    }

    case S7DataType::DINT: {
      convertAndStoreSigned<int32_t>(value, coreValue);
      break;
    }

    case S7DataType::REAL: {
      bool ok = false;
      float val = value.toFloat(&ok);
      if (!ok)
        return false;
      coreValue = val;
      break;
    }

    case S7DataType::STRING: {
      coreValue = value.toString().toStdString();
      break;
    }

    // ✅ 修改部分：使用辅助函数精确存储
    case S7DataType::BYTE: {
      convertAndStoreUnsigned<uint8_t>(value, coreValue);
      break;
    }

    case S7DataType::WORD: {
      convertAndStoreUnsigned<uint16_t>(value, coreValue);
      break;
    }

    case S7DataType::DWORD:
    case S7DataType::UDINT: {
      convertAndStoreUnsigned<uint32_t>(value, coreValue);
      break;
    }

    default: {
      // 未知类型，尝试转为字符串兜底
      coreValue = value.toString().toStdString();
      break;
    }
    }

    // 3. 如果转换失败，返回 false（Model 会拒绝更新）
    if (!coreValue.has_value())
      return false;

    // 4. 委托给核心业务逻辑写入（此时已完全剥离 Qt 类型）
    auto writeResult = dataNode.lock()->writeValue(coreValue.value());

    if (writeResult.is_success()) {
      // 5. 通知 View 刷新
      emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
      return true;
    } else {
      return false;
    }
  }

  // if (index.column() == 4) { // Comment 列
  //     node->getReadOnlyData()Block->description = value.toString().toStdString();
  //     emit dataChanged(index, index, {Qt::DisplayRole});
  //     return true;
  // }

  // Data Block Number 和 OffReset_Value 列通常只读，不允许编辑
  if (index.column() == 0 || index.column() == 1 || index.column() == 2 ||
      index.column() == 4) {
    return false; // 只读
  }

  return false;
}

QVariant OPCUADataBlockModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();

    if (orientation == Qt::Horizontal) {
        switch (section) {
        case 0:
          return tr("VaribaleName");
        case 1:
          return tr("DataType");
        case 2:
          return tr("Limit of Authority");
        case 3:
          return tr("Value");
        case 4:
          return tr("Comment");
        }
    } else if (orientation == Qt::Vertical) {
        return section + 1;
    }

    return QVariant();
}

Qt::ItemFlags OPCUADataBlockModel::flags(const QModelIndex &index) const {
  if (!index.isValid())
    return Qt::NoItemFlags;

  TreeNode *node = static_cast<TreeNode *>(index.internalPointer());
  if (node->getReadOnlyData() && index.column() == 3 || index.column() == 0) { // 关键：检查是否有子节点
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
  }

  return Qt::ItemIsEnabled | Qt::ItemIsSelectable ;
}

// ==================== batch update function ====================
void OPCUADataBlockModel::batchSetDataForOPCUA() {
  if (!m_rootNode)
    return;

  // 2. 恢复信号并一次性通知更新
  { updateAllDataByLevel(3); }
}

// ==================== validation function ====================
bool OPCUADataBlockModel::isValidIndex(const QModelIndex &index) const {
  return index.isValid() && index.row() >= 0 && index.row() < rowCount() &&
         index.column() >= 0 && index.column() < columnCount();
}

  void OPCUADataBlockModel::printTreeNode(TreeNode *node, int depth , bool isLast ) {
    if (!node)
      return;

    // 构建缩进
    QString indent;
    for (int i = 0; i < depth; i++) {
      indent += "  ";
    }

    // 添加树形符号
    QString prefix = indent;
    if (depth > 0) {
      prefix += isLast ? "└─ " : "├─ ";
    }

    // 节点信息
    QString nodeInfo = QString("%1%2").arg(prefix).arg(
        node->getDisplayName().isEmpty() ? "(unnamed)" : node->getDisplayName());

    // 添加类型标识
    if (node->getReadOnlyData()) {
      // 变量节点
      auto it = S7DataTypeToString.find(node->getReadOnlyData()->getDataType());
      if (it == S7DataTypeToString.end()) {
        QString typeStr = QString::fromStdString("UNKNOWN");
        }
        QString typeStr = QString::fromStdString(it->second);
        nodeInfo += QString(" [变量: %1]").arg(typeStr);
    } else {
      // 容器节点
      nodeInfo += QString(" [容器, 子节点数: %1]").arg(node->getChildCount());
    }

    qDebug().noquote() << nodeInfo;

    // ✅ 正确：使用范围 for
    int i  = 0 ;
    for (const auto &child : node->getChildren()) {
      // 处理 child
      bool lastChild = (i == node->getChildCount() - 1);
      printTreeNode(child.get(), depth + 1, lastChild);
      ++i;
    }
  }

  std::string OPCUADataBlockModel::extractVariableNameWithoutIndex(const std::string &input) {
    // 修正后的正则表达式
    // 匹配：点 + 双引号内容 + 可选的数组索引，直到字符串结束
    std::regex pattern(R"(\.("[^"]+")(?:\[\d+\])?$)");
    std::smatch match;

    if (std::regex_search(input, match, pattern)) {
      std::string result = match[1]; // 得到带引号的字符串，如 "Array_Template"
      // 删除首尾的双引号
      if (result.size() >= 2 && result.front() == '"' && result.back() == '"') {
        result = result.substr(1, result.size() - 2);
      }
      return result;

      // // match[1] 是第一个捕获组，即带引号的部分
      // return match[1];
    }

    return "";
  }

  std::pair<std::string, std::string> OPCUADataBlockModel::
  extractVariableNameWithIndex(const std::string &input) {
    std::pair<std::string, std::string> result;

    // 更清晰的匹配方式：找到最后一个点
    size_t lastDotPos = input.rfind('.');
    if (lastDotPos == std::string::npos) {
      return result;
    }

    // 提取最后一个点后面的内容
    std::string remaining = input.substr(lastDotPos + 1);

    // 匹配模式：可选的引号内容 + 可选的数组索引
    std::regex pattern(R"(^\"?([^\"]+)\"?(\[\d+\])?)");
    std::smatch match;

    if (std::regex_search(remaining, match, pattern)) {
      // match[1] 是变量名（可能包含引号）
      result.first = match[1];

      // 去除可能存在的引号
      if (result.first.size() >= 2 && result.first.front() == '"' &&
          result.first.back() == '"') {
        result.first = result.first.substr(1, result.first.size() - 2);
      }

      // match[2] 是数组索引（如果存在）
      if (match[2].matched) {
        result.second = match[2];
      }
    }

    return result;
  }

  std::string OPCUADataBlockModel::extractLastPartWithoutIndexForArray(const std::string &input) {
    // 统计点的个数
    int dotCount = 0;
    for (char c : input) {
      if (c == '.')
        dotCount++;
    }

    // 根据点的个数构建不同的正则表达式
    std::smatch match;

    if (dotCount == 1) {
      // 提取唯一的双引号内容
      std::regex pattern(R"(\.("[^"]+"))");
      if (std::regex_search(input, match, pattern)) {
        std::string result = match[1];
        if (result.size() >= 2 && result.front() == '"' &&
            result.back() == '"') {
          result = result.substr(1, result.size() - 2);
        }
        return result;
      }
    } else if (dotCount > 1) {
      // 提取最后一个双引号内容
      // 备选方案：如果没有匹配到带索引的，尝试匹配不带索引的
      std::regex pattern2(R"(\.("[^"]+")[^.]*$)");
      if (std::regex_search(input, match, pattern2)) {
        std::string result = match[1];
        if (result.size() >= 2 && result.front() == '"' &&
            result.back() == '"') {
          result = result.substr(1, result.size() - 2);
        }
        return result;
      }
    }
    return "";
  }

// ==================== 辅助方法 ====================
QModelIndex OPCUADataBlockModel::indexFromNode(TreeNode *node,
                                               int column ) const {
  if (!node || node == m_rootNode.get())
    return QModelIndex();

  std::shared_ptr<TreeNode> parentNode = node->getParent();
  if (!parentNode)
    return QModelIndex();

  // 查找 node 在父节点 children 中的位置
  // 需要遍历查找原始指针对应的 shared_ptr
  int row = -1;
  for (int i = 0; i < parentNode->getChildCount(); ++i) {
    if (parentNode->getChild(i).get() == node) {
      row = i;
      break;
    }
  }
  if (row < 0)
    return QModelIndex();

  //  return son node with location in parent node
  return createIndex(row, column, node); // 存储节点指针作为内部ID
}

void OPCUADataBlockModel::rebuildVisualRowMap() {
  m_visualRowMap.clear();
  int currentRow = 0;
  buildVisualRowMapRecursive(m_rootNode.get(), currentRow);
  m_visualRowMapValid = true;

  qDebug() << "Visual row map built with" << m_visualRowMap.size() << "entries";
}

void OPCUADataBlockModel::buildVisualRowMapRecursive(TreeNode *node,
                                                     int &currentRow) {
  if (!node)
    return;

  // 记录当前节点的视觉行号（跳过根节点）
  if (node != m_rootNode.get()) {
    m_visualRowMap[currentRow] = node;
    currentRow++;
  }

  //  root无条件进行递归
  //  其余节点判断：是否处于展开状态 && 是否有子节点
  //  递归处理子节点
  // !node->isExpanded &&
  if(currentRow != 0 && node == m_rootNode.get())
  {
    return ;
  }
  for (auto child : node->getChildren()) {
    buildVisualRowMapRecursive(child.get(), currentRow);
  }
}

TreeNode *OPCUADataBlockModel::getNodeByVisualRow(int visualRow) const {
  if (!m_visualRowMapValid || visualRow > m_visualRowMap.size()) {
    return nullptr;
  }
  if(visualRow==0)
  {
    return m_visualRowMap.value(visualRow, nullptr);
  }

  int effectiveRow=0;
  while(1) 
  {
    if(visualRow == 0)
    {
      return m_visualRowMap.value(effectiveRow, nullptr);
    }

    if (m_visualRowMap.value(effectiveRow, nullptr)->getChildCount() == 0) {
      // normal variable member
      --visualRow;
      ++effectiveRow;
      continue;
    } else {
      //  current member belong to  struct or array variable member
      if(m_visualRowMap.value(effectiveRow, nullptr)->isExpanded())
      {
        // need find the final not expand member
        ++effectiveRow;
      }
      else
      {
        {
        // add now struct or array member children size and then update point to new member
        effectiveRow +=
            getChildSize(m_visualRowMap.value(effectiveRow, nullptr));
        }
      }
        --visualRow;
    }
  }
}

std::shared_ptr<TreeNode>OPCUADataBlockModel::getRootNode()
{
  return m_rootNode;
}

void OPCUADataBlockModel::updateAllDataByLevel(int targetColumn) {
  // 按层级分组，减少 dataChanged 调用次数
  QMap<int, QList<QModelIndex>> levelMap;

  // 收集所有索引并按层级分组
  collectIndicesByLevel(m_rootNode.get(), 0, levelMap);

  // 使用 QMap 的 key 列表反向遍历
  // 按层级从深到浅更新（避免重复刷新）
  QList<int> keys = levelMap.keys();
  // 优化版本：批量更新同一层的同一列
  for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
    int level = *it;
    if (level == 0)
      continue;

    const QList<QModelIndex> &indices = levelMap[level];
    QModelIndex firstValid, lastValid;

    for (const QModelIndex &idx : indices) {
      QModelIndex colIndex = index(idx.row(), targetColumn, idx.parent());
      if (colIndex.isValid()) {
        if (!firstValid.isValid())
        {
          firstValid = colIndex;
        } 
        lastValid = colIndex;
      }
    }

    if (firstValid.isValid()) {
      // 一次性更新整层该列的所有项
      emit dataChanged(firstValid, lastValid, {Qt::DisplayRole, Qt::EditRole});
    }
  }
}

void OPCUADataBlockModel::collectIndicesByLevel(
    TreeNode *node, int level, QMap<int, QList<QModelIndex>> &levelMap) {
  if (node != m_rootNode.get()) {
    QModelIndex idx = indexFromNode(node);
    if (idx.isValid()) {
      levelMap[level].append(idx);
    }
  }

  for (auto child : node->getChildren()) {
    collectIndicesByLevel(child.get(), level + 1, levelMap);
  }
}


//OPCUADelegate-----------------------------------------------------------
QWidget *OPCUADataDelegate::createEditor(QWidget *parent,
                                         const QStyleOptionViewItem &option,
                                         const QModelIndex &index) const {
  if (index.column() == 3) { // Value 列
    // 获取数据类型
    TreeNode *node = static_cast<TreeNode *>(index.internalPointer());
    if (!node || !node->getReadOnlyData()) {
      qDebug() << "Node or dataBlock is null";
      return QStyledItemDelegate::createEditor(parent, option, index);
    }
    S7DataType dataType = node->getReadOnlyData()->getDataType();

    QWidget* editor = nullptr;
    switch (dataType) {
    case S7DataType::BOOL:
      editor = createBoolEditor(parent);
      break;
    case S7DataType::BYTE:
      editor = createNumberEditor(parent, S7DataType::BYTE);
      break;
    case S7DataType::INT:
      editor = createNumberEditor(parent, S7DataType::INT);
      break;
    case S7DataType::DINT:
      editor = createNumberEditor(parent, S7DataType::DINT);
      break;
    case S7DataType::WORD:
      editor = createNumberEditor(parent, S7DataType::WORD);
      break;
    case S7DataType::DWORD:
      editor = createHexEditor(parent, dataType);
      break;
    case S7DataType::UDINT:
      editor = createHexEditor(parent, dataType);
      break;
    case S7DataType::REAL:
      editor = createfloatEditor(parent);
      break;
    case S7DataType::STRING:
      editor = createStringEditor(parent);
      break;
    default:
      editor = createNumberEditor(parent, dataType);
      break;
    }

    return editor;
  }

  //  // 注释列使用文本编辑器
  // if (index.column() == 4) {
  //   QLineEdit *editor = new QLineEdit(parent);
  //   editor->setGeometry(option.rect);
  //   editor->setFixedSize(option.rect.width(), option.rect.height());
  //   return editor;
  // }

  // 其他列使用默认编辑器
  return QStyledItemDelegate::createEditor(parent, option, index);
}


void OPCUADataDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const {
    QVariant value = index.data(Qt::EditRole);
    
    if (index.column() == 3) {
        TreeNode *node = static_cast<TreeNode *>(index.internalPointer());
        S7DataType dataType = node->getReadOnlyData()->getDataType();

        switch (dataType) {
        case S7DataType::BOOL: {
            QCheckBox* checkBox = qobject_cast<QCheckBox*>(editor);
            if (checkBox)
                checkBox->setChecked(value.toBool());
            break;
        }
        case S7DataType::BYTE:
        case S7DataType::INT:
        case S7DataType::WORD:
        case S7DataType::DINT: {
            QSpinBox* spinBox = qobject_cast<QSpinBox*>(editor);
            if (spinBox) 
                spinBox->setValue(value.toInt());
            break;
        }
        case S7DataType::DWORD:
        case S7DataType::UDINT: {
          QLineEdit *lineEdit = qobject_cast<QLineEdit *>(editor);
          if (lineEdit) {
            if (dataType == S7DataType::DWORD) {
              // 显示为十六进制
              lineEdit->setText(
                  QString("0x%1").arg(value.toUInt(), 8, 16, QChar('0')));
            } else {
              // 显示为十进制
              lineEdit->setText(QString::number(value.toUInt()));
            }
          }
          break;
        }
        case S7DataType::REAL: {
            QDoubleSpinBox* spinBox = qobject_cast<QDoubleSpinBox*>(editor);
            if (spinBox)
                spinBox->setValue(value.toFloat());
            break;
        }
        case S7DataType::STRING: {
            QLineEdit* lineEdit = qobject_cast<QLineEdit*>(editor);
            if (lineEdit)
                lineEdit->setText(value.toString());
            break;
        }
        default: {
            QSpinBox* spinBox = qobject_cast<QSpinBox*>(editor);
            if (spinBox)
                spinBox->setValue(value.toInt());
            break;
        }
        }
    } else {
        QStyledItemDelegate::setEditorData(editor, index);
    }
}

void OPCUADataDelegate::setModelData(QWidget* editor, QAbstractItemModel* model,
                                  const QModelIndex& index) const {
    if (index.column() == 3) {
        TreeNode *node = static_cast<TreeNode *>(index.internalPointer());
        S7DataType dataType = node->getReadOnlyData()->getDataType();
        QVariant value;
        
        switch (dataType) {
        case S7DataType::BOOL: {
            QCheckBox* checkBox = qobject_cast<QCheckBox*>(editor);
            if (checkBox)
                value = checkBox->isChecked();
            break;
        }
        case S7DataType::REAL: {
            QDoubleSpinBox* spinBox = qobject_cast<QDoubleSpinBox*>(editor);
            if (spinBox)
                value = spinBox->value();
            break;
        }
        case S7DataType::STRING: {
            QLineEdit* lineEdit = qobject_cast<QLineEdit*>(editor);
            if (lineEdit)
                value = lineEdit->text();
            break;
        }
        case S7DataType::DWORD:
        case S7DataType::UDINT: {
          QLineEdit *lineEdit = qobject_cast<QLineEdit *>(editor);
          if (lineEdit) {
            QString text = lineEdit->text();
            bool ok;
            uint32_t result;

            if (dataType == S7DataType::DWORD && text.startsWith("0x")) {
              // 按十六进制解析
              result = text.mid(2).toUInt(&ok, 16);
            } else {
              // 按十进制解析
              result = text.toUInt(&ok, 10);
            }

            if (ok) {
              model->setData(index, result, Qt::EditRole);
            }
          }
          break;
        }
        case S7DataType::BYTE: {
            QSpinBox* spinBox = qobject_cast<QSpinBox*>(editor);
            if (spinBox) {
                uint8_t byteValue = static_cast<uint8_t>(spinBox->value());
                value = QVariant::fromValue(byteValue);
            }
            break;
        }
        case S7DataType::INT: {
            QSpinBox* spinBox = qobject_cast<QSpinBox*>(editor);
            if (spinBox) {
                int16_t intValue = static_cast<int16_t>(spinBox->value());
                value = QVariant::fromValue(intValue);
            }
            break;
        }
        case S7DataType::DINT: {
            QSpinBox* spinBox = qobject_cast<QSpinBox*>(editor);
            if (spinBox) {
                int32_t dintValue = spinBox->value();
                value = QVariant::fromValue(dintValue);
            }
            break;
        }
        case S7DataType::WORD: {
            QSpinBox* spinBox = qobject_cast<QSpinBox*>(editor);
            if (spinBox) {
                uint16_t wordValue = static_cast<uint16_t>(spinBox->value());
                value = QVariant::fromValue(wordValue);
            }
            break;
        }
        default: {
            QSpinBox* spinBox = qobject_cast<QSpinBox*>(editor);
            if (spinBox)
                value = spinBox->value();
            break;
        }
        }
        
        if (value.isValid())
            model->setData(index, value, Qt::EditRole);
    } else {
        QStyledItemDelegate::setModelData(editor, model, index);
    }
}

QWidget* OPCUADataDelegate::createBoolEditor(QWidget* parent) const {
    QCheckBox* checkBox = new QCheckBox(parent);
    checkBox->setTristate(false);
    return checkBox;
}

QWidget* OPCUADataDelegate::createfloatEditor(QWidget* parent) const {
    QDoubleSpinBox* spinBox = new QDoubleSpinBox(parent);
    spinBox->setRange(-1e10, 1e10);
    spinBox->setDecimals(3);
    return spinBox;
}

QWidget *OPCUADataDelegate::createHexEditor(QWidget *parent,
                                         S7DataType &dataType) const {
  QLineEdit *lineEdit = new QLineEdit(parent);

  // 根据类型设置不同的验证器
  QRegularExpressionValidator *validator = nullptr;

  switch (dataType) {
  case S7DataType::DWORD:
    // 十六进制验证：0x00000000 到 0xFFFFFFFF
    validator = new QRegularExpressionValidator(
        QRegularExpression("0x[0-9A-Fa-f]{1,8}"), lineEdit);
    lineEdit->setPlaceholderText("0x00000000");
    break;

  case S7DataType::UDINT:
    // 十进制验证：0 到 4294967295
    validator = new QRegularExpressionValidator(
        QRegularExpression("[0-9]{1,10}"), lineEdit);
    lineEdit->setPlaceholderText("0 - 4294967295");
    break;

  default:
    break;
  }

  return lineEdit;
}

QWidget* OPCUADataDelegate::createStringEditor(QWidget* parent) const {
    QLineEdit* lineEdit = new QLineEdit(parent);
    return lineEdit;
}

QWidget* OPCUADataDelegate::createNumberEditor(QWidget* parent, S7DataType dataType) const {
    QSpinBox* spinBox = new QSpinBox(parent);
    
    switch (dataType) {
    case S7DataType::BYTE:
        spinBox->setRange(0, 255);
        break;
    case S7DataType::INT:
        spinBox->setRange(-32768, 32767);
        break;
    case S7DataType::DINT:
        spinBox->setRange(-2147483648, 2147483647);
        break;
    case S7DataType::WORD:
        spinBox->setRange(0, 65535);
        break;
    case S7DataType::UDINT:
        spinBox->setRange(0, 4294967295U);
        break;
    default:
        spinBox->setRange(-999999, 999999);
    }
    
    return spinBox;
}


void OPCUADataDelegate::paint(QPainter *painter,
                              const QStyleOptionViewItem &option,
                              const QModelIndex &index) const {

  QStyleOptionViewItem opt = option; // ✅ 从传入的 option 复制
  opt.displayAlignment = Qt::AlignCenter;

  if (index.column() == 3 && index.row() == 0 && !index.parent().isValid()) {
    // qDebug() << "ROOT col3 painted!  WHY?";
    // painter->fillRect(opt.rect, Qt::red); // 红色背景
    return;                               // 不调用父类
  }

  // ✅ 不调用父类的 paint，直接绘制
  // 或者调用父类但传入完全重建的 opt
  QStyledItemDelegate::paint(painter, opt, index);
}

//OPCUAView-----------------------------------------------------------------
OPCUADataBlockView::OPCUADataBlockView(QWidget *parent) {
  setupUI();
  buildConnection();
};

OPCUADataBlockView::~OPCUADataBlockView() {
  spdlog::debug("~OPCUADataBlockView call");
}

void OPCUADataBlockView::getModel(OPCUADataBlockModel *model) {
  m_model = model;
  treeView->getModel(model);
}
void OPCUADataBlockView::getDelegate(OPCUADataDelegate *delegate) {
  m_delegate = delegate;
  treeView->getDelegate(delegate);
}
void OPCUADataBlockView::setModel() {
  treeView->setModel(m_model); // 2. 设置列宽（必须在设置模型后）
}
void OPCUADataBlockView::setDelegate() {
  treeView->setItemDelegate(m_delegate);
}

void OPCUADataBlockView::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    
    // 工具栏
    m_refreshBtn = new QPushButton("Rfresh Table PLC Data", this);
    m_writeBtn = new QPushButton("Write Data InTo PLC", this);
    m_exportBtn = new QPushButton("Expert To CSV", this);
    m_importBtn = new QPushButton("Load In CSV", this);
    
    // 搜索框
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("Search...");
    
    // 类型过滤器
    m_typeFilter = new QComboBox();
    m_typeFilter->addItem("All Type");
    m_typeFilter->addItem("Bool");
    m_typeFilter->addItem("Int");
    m_typeFilter->addItem("Real");
    m_typeFilter->addItem("String");
    
    // 表格视图
    treeView = new SpecialTreeView();
    treeView->setUniformRowHeights(true); // 优化性能
    treeView->setRootIsDecorated(true);   // 显示展开/折叠图标
    treeView->setAlternatingRowColors(true); // 斑马条纹，提升可读性
    treeView->setIndentation(40); // 设置缩进
    treeView->setAutoScroll(false);
    treeView->setSelectionBehavior(QAbstractItemView::SelectItems);
    treeView->setSelectionMode(QAbstractItemView::SingleSelection); // 单选

    treeView->header()->setDefaultAlignment(Qt::AlignCenter);
    treeView->header()->setStretchLastSection(true); // 最后一列填充剩余空间
    QFont font = treeView->font();
    font.setPointSize(25); // 放大字体
    treeView->setFont(font);
    

    // 状态栏
    m_statusBar = new QStatusBar();
    m_progressBar = new QProgressBar();
    m_progressBar->setVisible(false);
    
    // 布局
    auto* topLayout = new QHBoxLayout();
    topLayout->addWidget(m_refreshBtn);
    topLayout->addWidget(m_writeBtn);
    topLayout->addWidget(m_exportBtn);
    topLayout->addWidget(m_importBtn);
    topLayout->addStretch();
    topLayout->addWidget(m_searchEdit);
    topLayout->addWidget(m_typeFilter);
    
    mainLayout->addLayout(topLayout);
    // mainLayout->addWidget(m_tableView);
    mainLayout->addWidget(treeView);
    mainLayout->addWidget(m_statusBar);
    mainLayout->addWidget(m_progressBar);
}

void OPCUADataBlockView::onRefreshClicked() {
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, 0);  // 不确定进度
    updateButtonStates(true);
    
    // 发送请求给外部层
    emit requestRefresh();
}

void OPCUADataBlockView::onWriteClicked() {
    // 确认对话框
    if (QMessageBox::question(this, "确认", "确定要写入PLC吗？") == QMessageBox::Yes) {
        emit requestWrite();
    }
}

void OPCUADataBlockView::importFile() {
    QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("load file"),
        "",
        tr("support file type (*.csv *.xlsx *.json *.txt);")
    );

    if (filePath.isEmpty())
      return;
    else if (filePath.endsWith(".csv", Qt::CaseInsensitive) ||
             filePath.endsWith(".xlsx", Qt::CaseInsensitive) ||
             filePath.endsWith(".json", Qt::CaseInsensitive) ||
             filePath.endsWith(".txt", Qt::CaseInsensitive)) {
      emit requestFile(filePath);
      return;
    } else {
      QMessageBox::warning(this, "error",
                           " importFile : there is file type do not supported");
    }
}

void OPCUADataBlockView::onRowdoubleClicked(const QModelIndex &idx) {
 
}

void OPCUADataBlockView::printCallStack() {
   // 1. 创建堆栈跟踪对象
    backward::StackTrace st;
    st.load_here(32);
    
    // 2. 创建打印器并配置
    backward::Printer p;
    p.object = true;      // 打印对象地址
    p.address = true;     // 打印地址
    
    // 3. 捕获到字符串流
    std::stringstream ss;
    p.print(st, ss);
    
    // 4. 写入 spdlog
    spdlog::info("========== Call Stack ==========\n{}", ss.str());
}

// 添加调试代码验证
void OPCUADataBlockView::selectCell(const QModelIndex &index) {
  static QElapsedTimer lastCall;
  static QModelIndex lastIndex;

  qDebug() << "=== selectCell called at" << QTime::currentTime().toString();

  if (lastIndex.isValid() && lastIndex == index && lastCall.elapsed() < 99) {
    qDebug() << "WARNING: Duplicate selectCell call within 99ms!";
  }
  lastCall.start();
  lastIndex = index;

  // 临时断开所有信号
  auto *selModel = treeView->selectionModel();
  bool blocked = selModel->blockSignals(true);

  QItemSelection selection(index, index);
  selModel->select(selection, QItemSelectionModel::ClearAndSelect);

  selModel->blockSignals(blocked);

  // 验证选择
  auto selected = selModel->selectedIndexes();
  qDebug() << "Selected after (signals blocked):" << selected;

  // 重新发出信号
  selModel->selectionChanged(selection, QItemSelection());
}

bool OPCUADataBlockView::eventFilter(QObject *obj, QEvent *event) {
  // qDebug() << "event->type():" << event->type();
  if (obj == treeView->viewport() &&
      event->type() == QEvent::MouseButtonDblClick) {
    QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
    QModelIndex idx = treeView->indexAt(mouseEvent->pos());
    TreeNode *node = static_cast<TreeNode *>(idx.internalPointer());
    if (node == nullptr || node->getReadOnlyData() == nullptr) {
      qDebug() << "node:" << node << "node->getReadOnlyData():";
      return QWidget::eventFilter(obj, event);
    }

    if (idx.column() == 3 || idx.column() == 4) {
      // 调用 edit() 创建编辑器
      treeView->edit(idx);
      QPoint pos0(10, mouseEvent->pos().y()); // X=10 保证在第0列
      QModelIndex idxCol0 = treeView->indexAt(pos0);

      // 立即修正编辑器位置
      QTimer::singleShot(0, this, [this, idx, idxCol0]() {
        QWidget *editor = treeView->indexWidget(idx);
        if (editor) {
          // 基于第0列计算正确位置
          QRect baseRect = treeView->visualRect(idxCol0);

          if (baseRect.isValid()) {
            int x = treeView->columnViewportPosition(idx.column());
            int w = treeView->columnWidth(idx.column());
            editor->setGeometry(x, baseRect.y(), w, baseRect.height());
            qDebug() << "Fixed editor geometry to:" << editor->geometry();
          }
        }
      });
      return true;
    }

    return true; // 阻止信号继续传播
  }

  if (obj == treeView->viewport() &&
      event->type() == QEvent::MouseButtonPress) {
    {
      if (headerHeight == -1) {
        headerHeight = treeView->header()->height();
        treeView->setHeaderHeight(headerHeight);
      }

      QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
      lastClickLocation = mouseEvent->pos();
      QModelIndex idx = treeView->indexAt(lastClickLocation);
      TreeNode *node = static_cast<TreeNode *>(idx.internalPointer());
      // ✅ 第0列：让 Qt 正常处理（展开/折叠、选择等）
      if (!idx.isValid() || node == nullptr || node->getChildCount() != 0) {
        return QWidget::eventFilter(obj, event); // 交给 Qt 默认处理
      }

      QModelIndex parent = idx.parent(); // 保留 parent 用于子节点

      {
        QModelIndex col0Idx = m_model->index(idx.row(), 0, parent);

        // if(!initializeStatus)
        // {
        //   auto *selModel = treeView->selectionModel();
        //   connect(selModel, &QItemSelectionModel::selectionChanged, this,
        //           [this](const QItemSelection &selected,
        //                  const QItemSelection &deselected) {
        //             qDebug() << "⚠️ selectionChanged SIGNAL!";
        //             qDebug() << "  Selected:" << selected.indexes();
        //             qDebug() << "  Deselected:" << deselected.indexes();
        //             // 打印调用栈找出是谁触发的
        //             this->printCallStack();
        //           });
        //   initializeStatus = true;
        // }

        if (col0Idx.isValid()) {
          treeView->selectionModel()->clearSelection();

          QTimer::singleShot(200, this, [this, col0Idx]() {
            // 只选择第0列
            treeView->selectionModel()->select(col0Idx,
                                               QItemSelectionModel::Select);
            this->treeView->viewport()->update();
          });
        }
      }

      return true;
    }
  }

  return QWidget::eventFilter(obj, event);
}


QModelIndex OPCUADataBlockView::findIndexByNode(TreeNode *node, int column) const {
  if (!node || node == m_model->getRootNode().get()) {
    return QModelIndex();
  }

  // 获取父节点
  std::shared_ptr<TreeNode> parentNode = node->getParent();
  if (!parentNode) {
    return QModelIndex();
  }

  int row = findChildIndex(parentNode, node);
  // 获取 node 在父节点中的行号
  if (row < 0) {
    return QModelIndex();
  }

  // 需要获取父节点的 QModelIndex
  // 如果父节点是根节点，parentIdx 应该无效
  QModelIndex parentIdx;

  if (parentNode.get() != m_model->getRootNode().get()) {
    // 递归获取父节点的索引
    parentIdx = findIndexByNode(parentNode.get(), 0); // 获取父节点的第0列
                                                // 在 manualIndexAt 中添加
    if (!parentIdx.isValid()) {
      return QModelIndex();
    }
  }

  // 返回目标列的索引
  return m_model->index(row, column, parentIdx);
}

QModelIndex OPCUADataBlockView::mapVisualRowToModelIndex(
    const QModelIndex &parent, 
    int targetGlobalRow, 
    int targetCol,
    int &currentVisualRow) {
    
    int rows = m_model->rowCount(parent);
    for (int row = 0; row < rows; row++) {
        QModelIndex idx = m_model->index(row, targetCol, parent);
        
        // 检查当前视觉行是否匹配
        if (currentVisualRow == targetGlobalRow) {
            return idx;
        }
        currentVisualRow++;
        
        // 如果节点展开，递归处理子节点
        if (treeView->isExpanded(idx)) {
            QModelIndex childIdx = mapVisualRowToModelIndex(
                idx, targetGlobalRow, targetCol, currentVisualRow);
            if (childIdx.isValid()) {
                return childIdx;
            }
        }
    }
    
    return QModelIndex();
}

// 重载版本，方便调用
QModelIndex OPCUADataBlockView::mapVisualRowToModelIndex(
    const QModelIndex &parent, 
    int targetGlobalRow, 
    int targetCol) {
    
    int currentVisualRow = 0;
    return mapVisualRowToModelIndex(parent, targetGlobalRow, targetCol, currentVisualRow);
}

QModelIndex OPCUADataBlockView::findIndexByY(const QModelIndex &parent,
                                             int &currentY, int targetY,
                                             int targetX) {
    int rows = m_model->rowCount(parent);
    int depth = getDepth(parent);
    int indentX = depth * treeView->indentation();
    
    // ✅ 如果是根节点，跳过表头高度
    int startY = currentY;
    if (!parent.isValid()) {
        startY = treeView->header()->height();
    }
    
    for (int row = 0; row < rows; row++) {
        QModelIndex idx = m_model->index(row, 0, parent);
        if (!idx.isValid()) continue;
        
        QRect rect = treeView->visualRect(idx);
        int rowHeight = rect.isValid() ? rect.height() : treeView->fontMetrics().height() + 2;
        
        // 计算这一行的实际 Y 坐标
        int rowStartY = (row == 0 && !parent.isValid()) ? startY : currentY;
        
        // 检查目标Y是否在当前行内
        if (targetY >= rowStartY && targetY < rowStartY + rowHeight) {
            int currentX = indentX;
            for (int col = 0; col < m_model->columnCount(parent); col++) {
                int colWidth = treeView->columnWidth(col);
                if (targetX >= currentX && targetX < currentX + colWidth) {
                    return m_model->index(row, col, parent);
                }
                currentX += colWidth;
            }
            return QModelIndex();
        }
        
        currentY = rowStartY + rowHeight;
        
        if (treeView->isExpanded(idx)) {
            QModelIndex childIdx = findIndexByY(idx, currentY, targetY, targetX);
            if (childIdx.isValid()) {
                return childIdx;
            }
        }
    }
    
    return QModelIndex();
}

int OPCUADataBlockView::calculateColumnAtX(int x) const {
  int offset = 0;
  int columnCount = m_model->columnCount();

  for (int col = 0; col < columnCount; col++) {
    int colWidth = treeView->columnWidth(col);
    if (x >= offset && x < offset + colWidth) {
      return col;
    }
    offset += colWidth;
  }

  return -1; // 没有找到对应的列
}

int OPCUADataBlockView::getDepth(const QModelIndex &index) {
    int depth = 0;
    QModelIndex parent = index.parent();
    while (parent.isValid()) {
        depth++;
        parent = parent.parent();
    }
    return depth;
}

void OPCUADataBlockView::buildConnection()
{
  connect(m_refreshBtn, &QPushButton::clicked, this,
          &OPCUADataBlockView::onRefreshClicked);
  connect(m_writeBtn, &QPushButton::clicked, this,
          &OPCUADataBlockView::onWriteClicked);
  connect(m_exportBtn, &QPushButton::clicked, this,
          &OPCUADataBlockView::onExportClicked);
  connect(m_importBtn, &QPushButton::clicked, this,
          &OPCUADataBlockView::onImportClicked);

  connect(m_searchEdit, &QLineEdit::textChanged, this,
          &OPCUADataBlockView::onFilterChanged);

  connect(treeView, &QTreeView::doubleClicked, this,
          &OPCUADataBlockView::onRowdoubleClicked);
  connect(treeView, &QTreeView::expanded, this,
          [this](const QModelIndex &index) {
            auto result = this->treeView->getGlobalRow(lastClickLocation)
                              .and_then([this](int row) {
                                return this->treeView->getNodeByGlobalRow(row);
                              });
            if (result.is_fail()) {
              return;
            }
            TreeNode *Node = result.unwrap_returnLeftValue();
            if (!Node || Node->getChildCount() == 0) {
              return;
            }
            Node->setExpanded(true);
            // this->m_model->rebuildVisualRowMap();
            qDebug() << "节点已展开:" << index.data().toString();
            treeView->expand(index);
          });
  connect(treeView, &QTreeView::collapsed, this,
          [this](const QModelIndex &index) {
            auto result = this->treeView->getGlobalRow(lastClickLocation)
                              .and_then([this](int row) {
                                return this->treeView->getNodeByGlobalRow(row);
                              });
            if (result.is_fail()) {
              return;
            }
            TreeNode *Node = result.unwrap_returnLeftValue();
            if (!Node || Node->getChildCount() == 0) {
              return;
            }

            Node->setExpanded(false);
            // this->m_model->rebuildVisualRowMap();
            qDebug() << "节点已折叠:" << index.data().toString();
            treeView->collapse(index);
          });

  treeView->viewport()->installEventFilter(this);
}

// 外部响应层回调
void OPCUADataBlockView::onRefreshComplete(bool success, const QString& error) {
    m_progressBar->setVisible(false);
    updateButtonStates(false);
    
    if (success) {
        showStatusMessage("数据刷新成功", false);
    } else {
        showStatusMessage("刷新失败: " + error, true);
    }
}

void OPCUADataBlockView::onWriteComplete(bool success, const QString &error) {}

void OPCUADataBlockView::onConnectWithOPCUADataBlockView(QSplitter *splitter) {
  splitter->addWidget(this);
};

QTreeView *OPCUADataBlockView::getTableView() { return treeView; }
QWidget *OPCUADataBlockView::getView() { return this; }

//OPCUAController----------------------------------------------------------
void OPCUADataBlockController::initialize(
    std::shared_ptr<OPCUADataBlockView> view,
    std::shared_ptr<OPCUADataBlockModel> model, const QString &identifier,
    const ServiceRegistry &registry) {
  m_view = view;
  m_model = model;
  m_identifier = identifier;
  m_registry = registry;
};

void OPCUADataBlockController::buildConnection() {
  connect(m_view.lock().get(), &OPCUADataBlockView::requestRefresh, this,
          &OPCUADataBlockController::onViewReadRequested);
  connect(m_view.lock().get(), &OPCUADataBlockView::requestWrite, this,
          &OPCUADataBlockController::onViewWriteRequested);
}

//  on function
bool OPCUADataBlockController::onbuildOPCUADataBlockFromMap(
    const QString &file_path, const QString &identifier,
    const std::unordered_map<QString, ServiceRegistry> &m_serviceSuites) {
  if (!treeBuilder) {
    treeBuilder = std::make_unique<GenericTreeBuilder>();
  }

  // 1. 检查文件路径是否为空
  if (file_path.isEmpty()) {
    // 记录错误日志
    spdlog::error("Error: Empty file path");
    return false;
  }

  auto rawVecResult = m_loader.load(file_path, identifier, m_serviceSuites);
  if(rawVecResult.is_fail())
  {
    spdlog::info("error in load rawVec"); 
    return false;
  }
  auto &dataVec = rawVecResult.unwrap_returnLeftValue();
  std::shared_ptr<TreeNode> rootTreeNode;
  if (rawVecResult.is_fail()) {
    return false;
  } else {
    m_registry.m_mappers->setProbe(m_registry.m_probes);
    auto OpcDataVec = m_registry.m_mappers->map(dataVec);
    dataNodeVec = std::move(OpcDataVec);
    auto addressMapper = m_registry.m_mappers->buildAddressMap(dataNodeVec);
    m_registry.m_readers->setAddressMap(addressMapper);
  }

  rootTreeNode = treeBuilder->build(dataNodeVec);
  if (rootTreeNode) {
    m_model.lock()->setRootNode(rootTreeNode);
    m_view.lock()->setModel();
    m_view.lock()->setDelegate();
  }

  return true;
}

void OPCUADataBlockController::onViewReadRequested() {
  std::vector<std::string> nodeIds; // 注意：只取 ID，不取 Value！
  nodeIds.reserve(dataNodeVec.size());

  for (auto &node : dataNodeVec) {
    if (node->getDataType()!=S7DataType::UNKNOWN) {
      // 协调层只负责提取“逻辑名（ID）”，完全不碰 ValueType
      nodeIds.push_back(node->getNodeId());
    }
  }

  if (nodeIds.empty()) {
    return; // 或者通知 UI “无数据可读”
  }

  {
    auto result = m_registry.m_readers->batchRead(nodeIds);
    if (result.is_fail()) {
      return;
    } else {
      auto resultMap = result.unwrap_returnLeftValue();
      for (auto &nodePtr : dataNodeVec) {
        if (!nodePtr)
          continue;

        // 获取节点 ID
        std::string nodeId = nodePtr->getNodeId();

        auto it = resultMap.find(nodeId);
        if (it != resultMap.end()) {
          nodePtr->setRawValue(it->second);
        }
      }
      m_model.lock()->batchSetDataForOPCUA();
      return;
    }
  }
}

void OPCUADataBlockController::onViewWriteRequested() {
  std::vector<WriteRequest> nodeIds; // 注意：只取 ID，不取 Value！
  nodeIds.reserve(dataNodeVec.size());

  for (auto &node : dataNodeVec) {
    if (node && node->isDirty() && node->getDataType() != S7DataType::UNKNOWN) {
      // 协调层只负责提取“逻辑名（ID）”，完全不碰 ValueType
      nodeIds.push_back(WriteRequest{node->getNodeId(), node->readValue()});
    }
  }

  if (nodeIds.empty()) {
    spdlog::info("there is no dirty node");
    return; // 或者通知 UI “无数据可读”
  }

  auto result = m_registry.m_readers->batchWrite(nodeIds);
  if (result.is_fail()) {
    spdlog::info("batchWrite error:{}", result.unwrap_err().what());
    return;
  }

  for (auto &node : dataNodeVec) {
    node->clearDirty(); // Controller 亲自调用，Model 完全不参与
  }
}

//OPCUADataBlockManager-------------------------------------------------------------------------
bool OPCUADataBlockManager::createTableView(const QString &identifier,
                                            const QString &filePath) {
  QString object;
  if (!filePath.contains("opc.tcp://")) {
    QFileInfo info{filePath};
    object = info.fileName();
  } else {
    object = filePath;
  }
  
  OPCUADataBlockKey key(identifier, filePath);
  auto it = m_serviceSuites.find(identifier);
  if(it == m_serviceSuites.end())
  {
    return false;
  }
  auto createStrategy = m_contextFactory.createStrategy(filePath);
  if (!createStrategy) {
    return false;
  }
  auto createResult = createStrategy->create(identifier, object, it->second);
  if(createResult.is_fail())
  {
    return false;
  }
  else
  {
    m_blockMap[key] = std::move(createResult.unwrap_returnLeftValue());
  }

  return true;
}



QWidget* OPCUADataBlockManager::getView(const QString& ipAddress, 
                                          const QString& dataBlockName)
{
    OPCUADataBlockKey key(ipAddress, dataBlockName);
    if (m_blockMap.contains(key)) {
        return m_blockMap[key]->view->getView();
    }
    return nullptr;
}


// ========== 创建 DataBlock 上下文 ==========

// ========== 删除接口 ==========
bool OPCUADataBlockManager::removeOPCUADataBlock(const QString &ipAddress,
                                                 const QString &dataBlockName) {
  OPCUADataBlockKey key(ipAddress, dataBlockName);

  auto it = m_blockMap.find(key);
  if (it == m_blockMap.end()) {
    return false;
  }

  // 1. 先获取指针
  auto *blockPtr = it.value().get(); // 假设 value 是 shared_ptr 或 unique_ptr

  // 2. 从 Map 中移除
  m_blockMap.erase(it); // 先移除，避免 cleanup 中访问 Map

  // 3. 清理资源（此时对象还在内存中，可以安全清理）
  if (blockPtr) {
    cleanupOPCUADataBlockContext(blockPtr);
  }

  return true;
}

void OPCUADataBlockManager::cleanupOPCUADataBlockContext(OPCUADataBlockContext* context)
{
    if (!context) return;
    
    // 断开所有信号连接
    if (context->model) {
        context->model->disconnect();
    }
    if (context->view) {
        context->view->disconnect();
    }
    
    // View、Model、Delegate 会在 shared_ptr 析构时自动清理
    // 这里只需要清空指针
    // view 对象 由 OPCUADataBlockContext 内部的widget对象管理
    context->model.reset();
    context->delegate.reset();
}


// ========== 业务接口实现 ==========
bool OPCUADataBlockManager::buildS7Connect(
    const QString &ipAddress, int rack, int slot, const QString &identifier,
    const std::string &connectWay) // S7参数
{
  auto context = getSpecialReader(identifier);

  if (!context) {
    auto reader =
        std::make_shared<S7_Access>(ipAddress.toStdString(), rack, slot);
    std::shared_ptr<IStringLengthProbe> probe = reader;
    auto mapper = std::make_shared<S7Mapper>();
    

    m_serviceSuites[identifier] = {reader, probe, mapper,nullptr};
    auto result = reader->connect();
    if(result.is_fail())
    {
      return false;
    }
    else
    {
      return true;
    }
  }
    return true;
}

bool OPCUADataBlockManager::buildOPCUAConnect(const QString &ipAddress,
                                              int nameSpace, int port,
                                              const QString &identifier,
                                              const std::string &connectWay) {
  auto context = getSpecialReader(identifier);

  if (!context) {
    auto reader = std::make_shared<OPCUA_Access>(ipAddress.toStdString(),
                                                 nameSpace, port,identifier);
    std::shared_ptr<IStringLengthProbe> probe = reader;
    auto mapper = std::make_shared<OpcUaMapper>();

    m_serviceSuites[identifier] = {reader, probe, mapper,nullptr};
    auto result = reader->connect();
    if (result.is_fail()) {
      return false;
    } else {
      return true;
    }
  }
  return true;
}

bool OPCUADataBlockManager::buildOPCUAInlineBrowseConnect(
    const QString &ipAddress, int nameSpace, int port,
    const std::string &urlPrefix, const int &objectId,
    const QString &identifier, const std::string &connectWay) {
  auto context = getSpecialReader(identifier);

  if (!context) {
    auto reader = std::make_shared<OPCUA_Access>(ipAddress.toStdString(),
                                                 nameSpace, port);
    std::shared_ptr<IStringLengthProbe> probe = reader;
    auto browser = std::make_shared<OPCUABrowser>(
        urlPrefix + ipAddress.toStdString() + ":" + std::to_string(port),
        objectId);
    auto mapper = std::make_shared<OpcUaMapper>();

    m_serviceSuites[identifier] = {reader, probe, mapper,browser};
    auto result = reader->connect();
    if (result.is_fail()) {
      return false;
    } else {
      return true;
    }
  }

  return true;
}

bool OPCUADataBlockManager::checkConnectToDevice(const QString& ipAddress,
                                      const std::string& connectWay)
{

  QString identifier{ipAddress + "-" + QString::fromStdString(connectWay)};
  auto client = getSpecialReader(identifier);
  if (!client) {
    return false;
    } else 
    {
      auto result = client->connect();
      if(result.is_fail())
      {
        return false;
      }
      else
      {
        return true;
      }
    }
}

// ========== 辅助函数 ==========
OPCUADataBlockContext *
OPCUADataBlockManager::getOPCUADataBlockContext(const QString &identifier,
                                                const QString &filePath) {
  OPCUADataBlockKey key(identifier,filePath);
  if (m_blockMap.contains(key)) {
    return m_blockMap[key].get();
  }
  return nullptr;
}

std::shared_ptr<IDeviceReader> OPCUADataBlockManager::getSpecialReader(const QString &identifier) const {
    auto result = m_serviceSuites.find(identifier);
    if (result != m_serviceSuites.end()) {
      return result->second.m_readers;
    } else {
      return nullptr;
    }
}

// void OPCUADataBlockManager::markAsModified(const QString& ipAddress, 
//                                      const QString& dataBlockName, 
//                                      bool modified)
// {
//     OPCUADataBlockContext* context = getOPCUADataBlockContext(ipAddress, dataBlockName);
//     if (context && context->isModified != modified) {
//         context->isModified = modified;
//         emit OPCUADataBlockModified(ipAddress, dataBlockName, modified);
//     }
// }

bool OPCUADataBlockManager::buildDataFromFile(const QString &identify,const QString &filePath)
{
  auto dataPtr = getOPCUADataBlockContext(identify, filePath);
  if(!dataPtr)
  {
    // create new tableView and dataBlockContext
    bool result = createTableView(identify, filePath);
    if(!result)
    {
      return false;
    }
    else
    {
      OPCUADataBlockContext *context = getOPCUADataBlockContext(identify, filePath);
      if(context)
      {
        //  create dataBlock for filling contextt into model
        return context->controller->onbuildOPCUADataBlockFromMap(
            filePath, identify,m_serviceSuites);

      }
      else
      {
        return false;
      }
    }
  }
  {
    return true;
  }
}



//SpecialTreeView------------------------------------------------
SpecialTreeView::SpecialTreeView(QWidget *parent) {
 
};

SpecialTreeView::~SpecialTreeView() {
  spdlog::debug("~SpecialTreeView call");
}

Result<QModelIndex, RichError>
SpecialTreeView::findIndexByNode(const FindRelativeIndex &item) const {
  if (!item.node || item.node == m_model->getRootNode().get()) {
    return Result<QModelIndex, RichError>(QModelIndex());
  }

  // 获取父节点
  std::shared_ptr<TreeNode> parentNode = item.node->getParent();
  if (!parentNode) {
    return Result<QModelIndex, RichError>(QModelIndex());
  }

  // 获取 node 在父节点中的行号
  int row = findChildIndex(parentNode, item.node);
  if (row < 0) {
    return Result<QModelIndex, RichError>(QModelIndex());
  }

  // 需要获取父节点的 QModelIndex
  // 如果父节点是根节点，parentIdx 应该无效
  QModelIndex parentIdx;

  if (parentNode.get()!= m_model->getRootNode().get()) {
    // 递归获取父节点的索引
    auto result = findIndexByNode(
        FindRelativeIndex{parentNode.get(), 0}); // 获取父节点的第0列
                                                 // 在 manualIndexAt 中添加
    if (result.is_fail()) {
      return Result<QModelIndex, RichError>(result);
    }
    parentIdx = result.unwrap_returnLeftValue();
  }

  // 返回目标列的索引
  return Result<QModelIndex, RichError>(
      QModelIndex(m_model->index(row, item.col, parentIdx)));
}

Result<int, RichError> SpecialTreeView::getColumnAtX(const QPoint &pos) const {
  int col = calculateColumnAtX(pos.x());
  if (col < 0) {
    return Result<int, RichError>(
        RichError("Invalid column: " + std::to_string(col)));
  }
  return Result<int, RichError>(col);
}

Result<TreeNode *, RichError>
SpecialTreeView::getNodeByGlobalRow(int globalRow) const {
  TreeNode *node = m_model->getNodeByVisualRow(globalRow);
  if (!node) {
    return Result<TreeNode *, RichError>(
        RichError("No node found for row: " + std::to_string(globalRow)));
  }
  qDebug() << "select item :" << node->getDisplayName();
          
  return Result<TreeNode *, RichError>(node);
}

Result<int, RichError> SpecialTreeView::getGlobalRow(const QPoint &pos) const {
  int scrollValue = this->verticalScrollBar()->value();
  int rowHeight = this->fontMetrics().height();

  // 计算行号逻辑
  int relativeY = pos.y();
  int visualRow = (relativeY - 0) / rowHeight;
  //  visualRow means the distance between No.0 and the No.X
  if (visualRow < 0) {
    return Result<int, RichError>(RichError("Invalid visual row: negative"));
  }

  int globalRow = scrollValue + visualRow;
  globalRowPassager = globalRow;
  qDebug() << "pos.x():" << pos.x() << "pos.y():" << pos.y()
           << "rowHeight:" << rowHeight << "HeaderHeight:" << headerHeight;
  qDebug() << "scrollValue:" << scrollValue << "visualRow:" << visualRow
           << "globalRow:" << globalRow;
  return Result<int, RichError>(globalRow);
}

QModelIndex SpecialTreeView::indexAt(const QPoint &pos) const {
  // 1. 缓存检查 - 快速路径
  auto cacheResult = checkCache(pos);
  if (cacheResult.is_success()) {
    return cacheResult.unwrap_returnLeftValue();
  }

  // 2. 主流程 - 使用管道式处理
  auto result = getGlobalRow(pos)
                    .and_then([this](int row) { return getNodeByGlobalRow(row); })
                    .and_then([this, pos](TreeNode *node) {
                      return getColumnAtX(pos).transform_func([node](int col) {
                        return FindRelativeIndex(node, col);
                      });
                    })
                    .and_then([this](FindRelativeIndex &item) {
                      return findIndexByNode(item);
                    });

  // 3. 结果处理
  if (result.is_success()) {
    return result.unwrap_returnLeftValue();
  } else {
    spdlog::error("{}", result.unwrap_err().what());
    return QModelIndex();
  }
}

int SpecialTreeView::calculateColumnAtX(int x) const {
  int offset = 0;
  int scrollValue = this->horizontalScrollBar()->value();

  int columnCount = m_model->columnCount();

  for (int col = 0; col < columnCount; col++) {
    int colWidth = this->columnWidth(col);
    if (x + scrollValue >= offset && x + scrollValue < offset + colWidth) {
      return col;
    }
    offset += colWidth;
    qDebug()<<"col:"<<col<<" colWidth:"<<colWidth<<" offset:"<<offset;
  }

  return -1; // 没有找到对应的列
}

void SpecialTreeView::getModel(OPCUADataBlockModel *model) {
  m_model = model;
}

void SpecialTreeView::getDelegate(OPCUADataDelegate *delegate) {
  m_delegate = delegate;
}

void SpecialTreeView::setHeaderHeight(int height) {
  this->headerHeight = height;
}