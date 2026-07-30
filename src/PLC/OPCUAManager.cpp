#include "PLC/OPCUAManager.h"
#include <spdlog/spdlog.h>

//OPCUADataReader-------------------------------------------------------------
// ==================== 构造函数 ====================

OPCUADeviceReader::OPCUADeviceReader(const std::string& identifier)
    : m_identifier(identifier) {
}

// ==================== 读取数据方法 ====================
Result<bool, RichError> OPCUADeviceReader::ReadDataFromPLC(OPCUADataBlock* data) {
  if (m_identifier.find("OPC_UA") != std::string::npos) {
    return batchReadOPCUADataBlock_FromPLC(data);
  } else {
    return batchReadS7DataBlock_FromPLC(data);
  }
}

Result<bool, RichError>
OPCUADeviceReader::batchReadOPCUADataBlock_FromPLC(OPCUADataBlock *data) {
  auto result =  m_opcUA->batchReadOPCUADataBlock_FromPLC(data); 
  if(result.is_fail())
  {
    return result;
  }
  else
  {
    return  convertOPCUAToDataPointer(data); 
  }
}

Result<bool, RichError>
OPCUADeviceReader::convertOPCUAToDataPointer(OPCUADataBlock *data) {
  //  store data into data_pointer
  bool success = true;
  int index = 0;
  for (auto &var : data->getVariabeDataVector()) {
    {
      if (var.filter_reason != "" || var.is_array ||
          var.data_type_enum == S7DataType::UNKNOWN ||
          !checkDotAndBackslash(
              uaStringToString(var.nodeID.identifier.string))) {
        continue;
      }
      //  deal normal scalar condition
      Result<bool, RichError> result = this->m_covert.Set_UA_To_Read_Normal_Scalar(
          var.data_type_enum, *var.data_pointer, index,m_opcUA->getReadVariant());
      if (result.is_fail()) {
        success = false;
        return result;
      }
    }
    ++index;
  }

  if (success) {
    return Result<bool, RichError>(true);
  } 
  else
  {
    return Result<bool,RichError> (RichError{"convertOPCUAToDataPointer fail"});
  }
}

Result<bool, RichError>
OPCUADeviceReader::convertDataPointerToOPCUA(OPCUADataBlock *data) {
  m_opcUA->PrepareBatchWrite(data->getVariabeDataVector());

  int index = 0;
  for (auto &var : data->getVariabeDataVector()) {
    {
      if (var.filter_reason != "" || var.is_array ||
          var.data_type_enum == S7DataType::UNKNOWN ||
          !checkDotAndBackslash(
              uaStringToString(var.nodeID.identifier.string))) {
        continue;
      }
      auto &writeNode = m_opcUA->getWriteNodes();
      auto result =
          m_covert.batchSet_Normal_To_Write_UA_Scalar(3, var, index, writeNode);
      if (result.is_fail()) {
        return Result<bool, RichError>(result);
      }
    }

    ++index;
  }
  return Result<bool, RichError>(true);
}

Result<bool, RichError>
OPCUADeviceReader::batchReadS7DataBlock_FromPLC(OPCUADataBlock *data) {
  auto result = m_s7Acess->batchReadS7DataBlock_FromPLC(data);
  if(result.is_fail())
  {
    return result;
  }
  else
  {
    return m_covert.batchSet_Uint8_t_To_Dynamic(data);
  }
}

// ==================== 写入数据方法 ====================
Result<bool, RichError>
OPCUADeviceReader::batchWriteOPCUABlock_ToPLC(OPCUADataBlock *data) {
  auto result = convertDataPointerToOPCUA(data);
  if(result.is_fail())
  {
    return result;
  }
  else
  {
    return m_opcUA->batchWriteOPCUABlock_ToPLC(data); 
  }
}

Result<bool, RichError>
OPCUADeviceReader::batchWriteS7DataBlock_ToPLC(OPCUADataBlock *data) {
  m_covert.batchSet_DynamicValue_To_Uint8_t(data);
  return m_s7Acess->batchWriteS7DataBlock_ToPLC(data); 
}


// ==================== get function ====================

Result<std::string, RichError> OPCUADeviceReader::getIdentifier() {
    if (m_identifier.empty()) {
        return RichError("OPCUADeviceReader identifier is null");
    }
    return m_identifier;
}

// ==================== respond function ====================

bool OPCUADeviceReader::onRequestBuildOPCUA(const std::string& ip_Address, 
                                            int nameSpace, int port) {
    m_identifier = ip_Address + "-" + "OPC_UA";
    m_opcUA = std::make_unique<OPCUA_Access>(ip_Address, nameSpace, port);
    
    auto result = m_opcUA->reconnect(1, 1000);
    if (result.is_fail()) {
        spdlog::error("OPCUA Connect fail , reason : {}", result.unwrap_err().what());
        return false;
    } else {
        spdlog::info("OPCUA Connect successfully");
        return true;
    }
}

bool OPCUADeviceReader::onRequestOPCUACheckConnect() const {
    if (m_opcUA) {
        return m_opcUA->isConnected();
    }
    return false;
}


bool OPCUADeviceReader::onRequestS7CheckConnect() const {
    if (m_s7Acess) {
        return m_s7Acess->isConnected();
    }
    return false;
}

bool OPCUADeviceReader::onRequestBuildS7(const std::string &ip_Address,
                                         int rack, int slot, int timeout) {
  m_identifier = ip_Address + "-" + "S7_Offset";
  m_s7Acess = std::make_unique<S7_Access>(ip_Address, rack, slot);
  auto result = m_s7Acess->connect();
  if (result.is_fail()) {
    spdlog::error("{}", result.unwrap_err().what());
    return false;
  } else {
    spdlog::info("S7 Offset Connect successfully");
    return true;
  }
}

// ==================== 重置连接 ====================

void OPCUADeviceReader::resetConnections() {
    if (m_opcUA) {
        m_opcUA->disconnect();
        m_opcUA.reset();
    }
    
    if (m_s7Acess) {
        m_s7Acess->disconnect();
        m_s7Acess.reset();
    }
    
    Sourcebuffer.clear();
    Destbuffer.clear();
    m_identifier.clear();
}

// ==================== 辅助方法 ====================

bool OPCUADeviceReader::validateDataBlock(OPCUADataBlock* data) const {
    if (!data) {
        return false;
    }
    
    // 检查是否有变量
    if (data->getVariableVectorSize() == 0) {
        return false;
    }
    
    return true;
}

void OPCUADeviceReader::logError(const std::string& function, const std::string& error) const {
    spdlog::error("[OPCUADeviceReader::{}] Error: {}", function, error);
}

void OPCUADeviceReader::logInfo(const std::string& message) const {
    spdlog::info("[OPCUADeviceReader] Info: {}", message);
}

Result<bool, RichError> OPCUADeviceReader::convertS7ToOPCUA(const std::vector<uint8_t>& s7_data,
                                                            OPCUADataBlock* data) {
    if (!data) {
        return RichError("Invalid data block");
    }
    
    try {
        auto& variables = data->getVariabeDataVector();
        size_t offset = 0;
        
        for (auto& var : variables) {
            // 根据数据类型从 buffer 中读取值
            switch (var.data_type_enum) {
                case S7DataType::BOOL: {
                    if (offset < s7_data.size()) {
                        // bool value = (s7_data[offset] != 0);
                        // var.data_pointer = std::make_unique<Dynamic_Value>(value);
                        offset += 1;
                    }
                    break;
                }
                case S7DataType::INT: {
                    if (offset + 1 < s7_data.size()) {
                        // int16_t value = (s7_data[offset] << 8) | s7_data[offset + 1];
                        // var.data_pointer = std::make_unique<Dynamic_Value>(value);
                        offset += 2;
                    }
                    break;
                }
                case S7DataType::REAL: {
                    if (offset + 3 < s7_data.size()) {
                        // float value;
                        // memcpy(&value, &s7_data[offset], sizeof(float));
                        // var.data_pointer = std::make_unique<Dynamic_Value>(value);
                        offset += 4;
                    }
                    break;
                }
                default:
                    // 其他类型处理
                    offset += 4; // 默认大小
                    break;
            }
        }
        
        return true;
        
    } catch (const std::exception& e) {
        return RichError(std::string("Conversion failed: ") + e.what());
    }
}

Result<bool, RichError> OPCUADeviceReader::convertOPCUAToS7(OPCUADataBlock* data,
                                                            std::vector<uint8_t>& s7_data) {
    if (!data) {
        return RichError("Invalid data block");
    }
    
    try {
        auto& variables = data->getVariabeDataVector();
        s7_data.clear();
        
        for (const auto& var : variables) {
            if (!var.data_pointer) {
                // 如果没有值，填充默认值
                s7_data.push_back(0);
                continue;
            }
            
            // 根据数据类型写入 buffer
            switch (var.data_type_enum) {
                case S7DataType::BOOL: {
                    // bool value = var.data_pointer->toBool();
                    // s7_data.push_back(value ? 1 : 0);
                    s7_data.push_back(0);
                    break;
                }
                case S7DataType::INT: {
                    // int16_t value = var.data_pointer->toInt();
                    // s7_data.push_back((value >> 8) & 0xFF);
                    // s7_data.push_back(value & 0xFF);
                    s7_data.push_back(0);
                    s7_data.push_back(0);
                    break;
                }
                case S7DataType::REAL: {
                    // float value = var.data_pointer->toFloat();
                    // uint8_t* bytes = reinterpret_cast<uint8_t*>(&value);
                    // s7_data.insert(s7_data.end(), bytes, bytes + sizeof(float));
                    s7_data.push_back(0);
                    s7_data.push_back(0);
                    s7_data.push_back(0);
                    s7_data.push_back(0);
                    break;
                }
                default:
                    // 默认写入 4 字节
                    s7_data.insert(s7_data.end(), 4, 0);
                    break;
            }
        }
        
        return true;
        
    } catch (const std::exception& e) {
        return RichError(std::string("Conversion failed: ") + e.what());
    }
}


//OPCUADataBlockModel----------------------------------------------------------

// ==================== 构造函数 ====================

OPCUADataBlockModel::OPCUADataBlockModel(QObject* parent)
    : QAbstractTableModel(parent)
    , m_OPCUADataBlock(nullptr) {
}

OPCUADataBlockModel::OPCUADataBlockModel(std::shared_ptr<OPCUADataBlock> block, 
                                         QObject* parent)
    : QAbstractTableModel(parent)
    , m_OPCUADataBlock(block) {
}

// ==================== 析构函数 ====================

OPCUADataBlockModel::~OPCUADataBlockModel() {
    // 智能指针会自动释放，无需额外处理
}

// ==================== set internal member function ====================

void OPCUADataBlockModel::setOPCUADataBlock(const std::shared_ptr<OPCUADataBlock> &block) {
    beginResetModel(); // 告诉 View 准备完全重置
    m_OPCUADataBlock = block;
    buildTree();
    rebuildVisualRowMap();
    printTreeNode(m_rootNode.get());
    endResetModel(); // View 会自动重新读取所有数据
}

// ==================== QAbstractTableModel 接口 ====================
QModelIndex OPCUADataBlockModel::index(int row, int column,
                                       const QModelIndex &parent) const {
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    TreeNode *parentNode = parent.isValid()
                               ? static_cast<TreeNode *>(parent.internalPointer())
                               : m_rootNode.get();

    if (!parentNode || row >= parentNode->children.size())
        return QModelIndex();

    // ⚠️ 关键：这里创建索引时传入的指针是否正确？
    TreeNode* childNode = parentNode->children[row];
    QModelIndex result =
        createIndex(row, column, childNode); // ← childNode 应该非空且有数据

    return result;
}

QModelIndex OPCUADataBlockModel::parent(const QModelIndex &child) const {
  TreeNode *childNode = static_cast<TreeNode *>(child.internalPointer());

  TreeNode *parentNode = childNode ? childNode->parent : nullptr;

  if (!parentNode || parentNode == m_rootNode.get()) {
    return QModelIndex();
  }

  TreeNode *grandParent = parentNode->parent;
  int row = grandParent ? grandParent->children.indexOf(parentNode) : -1;

  if (row < 0)
    return QModelIndex();

  return createIndex(row, 0, parentNode);
}

int OPCUADataBlockModel::rowCount(const QModelIndex &parent) const {
  if (!parent.isValid()) {
    // 顶层节点数量
    return m_rootNode->children.size();
  }

  TreeNode *node = static_cast<TreeNode *>(parent.internalPointer());
  int count = node ? node->children.size() : 0;
  return node ? node->children.size() : 0;
}

int OPCUADataBlockModel::columnCount(const QModelIndex &parent) const {
  return 5; // 叶子节点有所有数据列
}

bool OPCUADataBlockModel::hasChildren(const QModelIndex &parent) const {
  // qDebug() << "=== hasChildren() called ===";

  if (!parent.isValid()) {
    bool result = !m_rootNode->children.isEmpty();
    qDebug() << "  top level, children count:" << m_rootNode->children.size();
    qDebug() << "  returning:" << result;
    return result;
  }

  // index node self
  TreeNode *node = static_cast<TreeNode *>(parent.internalPointer());
  if (!node) {
    qDebug() << "  node is null, returning false";
    return false;
  }

  // 容器节点且有子节点才返回 true
  bool hasChild = (node->m_dataBlock == nullptr) && !node->children.isEmpty();
  // qDebug() << "  node:" << node->displayName
  //          << "is container:" << (node->m_dataBlock == nullptr)
  //          << "children count:" << node->children.size()
  //          << "hasChildren:" << hasChild
  //          << " its index : "<<parent;

  return hasChild;
}

QVariant OPCUADataBlockModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid())
    return QVariant();

  TreeNode *node = static_cast<TreeNode *>(index.internalPointer());

  // EditRole - 返回原始数据用于编辑
  if (role == Qt::EditRole) {
    switch (index.column()) {
    case 0: // Name 列
      return (node->displayName);

    case 1: // Data Type 列（只读）
      if (node->m_dataBlock)
        return static_cast<int>(node->m_dataBlock->data_type_enum);

    case 2: // limit of authority 列（只读）
      if (node->m_dataBlock)
        return node->m_dataBlock->access_level;

    case 3: // Value 列 - 根据数据类型返回原始值
      if (node->m_dataBlock)
        switch (node->m_dataBlock->data_type_enum) {
        case S7DataType::BOOL:
          return node->m_dataBlock->data_pointer->get<bool>();
        case S7DataType::BYTE:
          return node->m_dataBlock->data_pointer->get<uint8_t>();
        case S7DataType::INT:
          return node->m_dataBlock->data_pointer->get<int16_t>();
        case S7DataType::DINT:
          return node->m_dataBlock->data_pointer->get<int32_t>();
        case S7DataType::WORD:
          return node->m_dataBlock->data_pointer->get<uint16_t>();
        case S7DataType::DWORD:
          return node->m_dataBlock->data_pointer->get<uint32_t>();
        case S7DataType::UDINT:
          return node->m_dataBlock->data_pointer->get<uint32_t>();
        case S7DataType::REAL:
          return node->m_dataBlock->data_pointer->get<float>();
        case S7DataType::STRING:
          return QString::fromStdString(
              node->m_dataBlock->data_pointer->get<std::string>());
        default:
          return node->m_dataBlock->data_pointer->get<QVariant>();
        }

    case 4: // Comment 列
      if (node->m_dataBlock)
        return QString::fromStdString(node->m_dataBlock->description);

    default:
      return QVariant();
    }
  }

  // DisplayRole - 返回格式化的显示数据
  if (role == Qt::DisplayRole) {
    switch (index.column()) {
    case 0: // Name 列
      return (node->displayName);

    case 1: // Data Type 列
    {
      if(node->m_dataBlock == nullptr)
      {
        return QVariant{};
      }
      auto it = S7DataTypeToString.find(node->m_dataBlock->data_type_enum);
      if (it != S7DataTypeToString.end()) {
        return QString::fromStdString(it->second);
      }
      return QString::fromStdString("UNKNOWN");
    }

    case 2: // limit of authority 列
      if (node->m_dataBlock == nullptr) {
        return QVariant{};
      }
      return node->m_dataBlock->access_level;

    case 3: // Value 列 - 格式化显示
      if (node->m_dataBlock == nullptr) {
        return QVariant{};
      }
      switch (node->m_dataBlock->data_type_enum) {
      case S7DataType::BOOL:
        return node->m_dataBlock->data_pointer->get<bool>() ? "true" : "false";

      case S7DataType::BYTE:
        return QString::number(node->m_dataBlock->data_pointer->get<uint8_t>());

      case S7DataType::INT:
        return QString::number(node->m_dataBlock->data_pointer->get<int16_t>());

      case S7DataType::DINT:
        return QString::number(node->m_dataBlock->data_pointer->get<int32_t>());

      case S7DataType::WORD:
        return QString::number(node->m_dataBlock->data_pointer->get<uint16_t>());

      case S7DataType::DWORD:
        return QString("0x%1").arg(node->m_dataBlock->data_pointer->get<uint32_t>(), 8, 16,
                                   QChar('0'));

      case S7DataType::UDINT:
        return QLocale(QLocale::English)
            .toString(node->m_dataBlock->data_pointer->get<uint32_t>());
        // 结果示例： "1,234,567" 而不是 "1234567"

      case S7DataType::REAL:
        return QString::number(node->m_dataBlock->data_pointer->get<float>(), 'f', 6);

      case S7DataType::STRING:
        return QString::fromStdString(node->m_dataBlock->data_pointer->get<std::string>());

      default:
        return QString::fromStdString(node->m_dataBlock->data_pointer->get<std::string>());
      }

    case 4: // Comment 列
      if (node->m_dataBlock == nullptr) {
        return QVariant{};
      }
      return QString::fromStdString(node->m_dataBlock->description);

    default:
      return QVariant();
    }
  }

  return QVariant();
}

bool OPCUADataBlockModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || role != Qt::EditRole) {
        return false;
    }

    // 1. 从 index 中获取 TreeNode 指针
    TreeNode *node = static_cast<TreeNode *>(index.internalPointer());
    if (!node) {
      return false;
    }

    // 2. 获取对应的 VariableInfo
    OPCUAModernDataStruct *dataBlock = node->m_dataBlock;
    if (!dataBlock) {
      // 这是一个容器节点（如 "Motor"），不可编辑
      return false;
    }

    if (index.column() == 3) { // Value 列
        // 根据数据类型进行验证和转换
        switch (node->m_dataBlock->data_type_enum) {
        case S7DataType::BOOL: {
            bool boolValue = value.toBool();
            node->m_dataBlock->data_pointer->Reset_Value(boolValue);
            break;
        }
        
        case S7DataType::BYTE: {
            bool ok;
            int intValue = value.toInt(&ok);
            if (ok && intValue >= 0 && intValue <= 255) {
                node->m_dataBlock->data_pointer->Reset_Value(static_cast<uint8_t>(intValue));
            } else {
                return false; // 数据无效
            }
            break;
        }
        
        case S7DataType::INT: {
            bool ok;
            int intValue = value.toInt(&ok);
            if (ok && intValue >= -32768 && intValue <= 32767) {
                node->m_dataBlock->data_pointer->Reset_Value(static_cast<int16_t>(intValue));
            } else {
                return false;
            }
            break;
        }
        
        case S7DataType::DINT: {
            bool ok;
            qint64 longValue = value.toLongLong(&ok);
            if (ok && longValue >= -2147483648LL && longValue <= 2147483647LL) {
                node->m_dataBlock->data_pointer->Reset_Value(static_cast<int32_t>(longValue));
            } else {
                return false;
            }
            break;
        }
        
        case S7DataType::WORD: {
            bool ok;
            int intValue = value.toInt(&ok);
            if (ok && intValue >= 0 && intValue <= 65535) {
                node->m_dataBlock->data_pointer->Reset_Value(static_cast<uint16_t>(intValue));
            } else {
                return false;
            }
            break;
        }
        
        case S7DataType::DWORD:
        case S7DataType::UDINT: {
            bool ok;
            uint32_t uintValue = value.toUInt(&ok);
            if (ok) {
                node->m_dataBlock->data_pointer->Reset_Value(uintValue);
            } else {
                return false;
            }
            break;
        }
        
        case S7DataType::REAL: {
            float floatValue = value.toFloat();
            node->m_dataBlock->data_pointer->Reset_Value(floatValue);

            float savedValue = node->m_dataBlock->data_pointer->get<float>();
            spdlog::debug("Saved REAL value: {}, expected: {}", savedValue, floatValue);
            break;
        }
        
        case S7DataType::STRING: {
            QString stringValue = value.toString();
            node->m_dataBlock->data_pointer->Reset_Value(stringValue.toStdString());
            break;
        }
        
        default: {
            // 未知类型，尝试存储为字符串
            node->m_dataBlock->data_pointer->Reset_Value(value.toString());
            break;
        }
        }
        
        // 数据修改成功，发射信号通知视图更新
        emit dataChanged(index, index, {Qt::DisplayRole});
        return true;
    }
    
    // if (index.column() == 4) { // Comment 列
    //     node->m_dataBlock->description = value.toString().toStdString();
    //     emit dataChanged(index, index, {Qt::DisplayRole});
    //     return true;
    // }
    
    // Data Block Number 和 OffReset_Value 列通常只读，不允许编辑
    if (index.column() == 0 || index.column() == 1 || index.column() == 2 || index.column() == 4) {
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
  if (node->m_dataBlock && index.column() == 3 &&
      node->children.isEmpty()) { // 关键：检查是否有子节点
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
  }

  return Qt::ItemIsEnabled | Qt::ItemIsSelectable ;
}

// ==================== batch update function ====================
void OPCUADataBlockModel::batchSetDataForOPCUA() {
if (!m_OPCUADataBlock )
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
        node->displayName.isEmpty() ? "(unnamed)" : node->displayName);

    // 添加类型标识
    if (node->m_dataBlock) {
      // 变量节点
      QString typeStr = QString::fromStdString(node->m_dataBlock->data_type);
      nodeInfo += QString(" [变量: %1]").arg(typeStr);
    } else {
      // 容器节点
      nodeInfo += QString(" [容器, 子节点数: %1]").arg(node->children.size());
    }

    qDebug().noquote() << nodeInfo;

    // 递归打印子节点
    for (int i = 0; i < node->children.size(); i++) {
      bool lastChild = (i == node->children.size() - 1);
      printTreeNode(node->children[i], depth + 1, lastChild);
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

  std::string OPCUADataBlockModel::extractLastPartWithoutIndexForNormal(
      const std::string &input) {
    // 直接查找最后一个双引号包裹的内容（包含双引号）
    // 正则表达式: "([^"]+)"$ 匹配最后一个双引号内容
    std::regex pattern("\"([^\"]+)\"$");
    std::smatch match;

    if (std::regex_search(input, match, pattern)) {
      std::string result = match[1];

      // 去除开头所有的 "\" 
    while (!result.empty() && result.front() == '\\') {
      result.erase(0, 1);
    }

    // 去除结尾所有的 "\""
    while (!result.empty() && result.back() == '\\') {
    result.pop_back();
  }

  return result;
  }

  return "";
  }

  std::string OPCUADataBlockModel::extractLastPartWithoutIndexForNormal(const OPCUAModernDataStruct &data) {
    return data.parentName; 
  }

// ==================== 辅助方法 ====================

QString OPCUADataBlockModel::getDisplayValue(const OPCUAModernDataStruct& var, int column) const {
    switch (column) {
        case VariableName:
            return QString::fromStdString(var.variable_name);
            
        case DataType:
            return getTypeString(var.data_type_enum);
            
        case Limit:
            // 根据数据类型返回限制范围
            // 这里可以根据实际需求实现
            return QVariant().toString();
            
        case Value:
            if (var.data_pointer) {
                // 根据实际 Dynamic_Value 的实现来获取值
                // return var.data_pointer->toQVariant().toString();
                return QString::fromStdString(var.data_type);
            }
            return QString();
            
        case Comment:
            return QString::fromStdString(var.description);
            
        default:
            return QString();
    }
}

QString OPCUADataBlockModel::getTypeString(S7DataType type) const {
    switch (type) {
        case S7DataType::BOOL:
            return "BOOL";
        case S7DataType::BYTE:
            return "BYTE";
        case S7DataType::INT:
            return "INT";
        case S7DataType::WORD:
            return "WORD";
        case S7DataType::DINT:
            return "DINT";
        case S7DataType::UDINT:
            return "UDINT";
        case S7DataType::DWORD:
            return "DWORD";
        case S7DataType::REAL:
            return "REAL";
        case S7DataType::STRING:
            return "STRING";
        case S7DataType::ARRAY:
            return "ARRAY";
        case S7DataType::STRUCT:
            return "STRUCT";
        case S7DataType::UNKNOWN:
        default:
            return "UNKNOWN";
    }
}

bool OPCUADataBlockModel::setVariableValue(OPCUAModernDataStruct& var, 
                                           int column, 
                                           const QVariant& value) {
    if (column != Value) {
        return false;
    }
    
    if (!var.data_pointer) {
        // 如果 data_pointer 为空，可能需要创建
        // var.data_pointer = std::make_unique<Dynamic_Value>();
        return false;
    }
    
    try {
        // 根据实际 Dynamic_Value 的实现来设置值
        // var.data_pointer->fromQVariant(value);
        
        // 临时实现
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to set variable value: {}", e.what());
        return false;
    }
}

QModelIndex OPCUADataBlockModel::indexFromNode(TreeNode *node,
                                               int column ) const {
  if (!node || node == m_rootNode.get())
    return QModelIndex();

  TreeNode *parentNode = node->parent;
  if (!parentNode)
    return QModelIndex();

  int row = parentNode->children.indexOf(const_cast<TreeNode *>(node));
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
  for (TreeNode *child : node->children) {
    buildVisualRowMapRecursive(child, currentRow);
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

    if (m_visualRowMap.value(effectiveRow, nullptr)->children.size() == 0) {
      // normal variable member
      --visualRow;
      ++effectiveRow;
      continue;
    } else {
      //  current member belong to   struct or array variable member
      if(m_visualRowMap.value(effectiveRow, nullptr)->isExpanded)
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

TreeNode *OPCUADataBlockModel::getRootNode()
{
  return m_rootNode.get();
}

void OPCUADataBlockModel::buildTree(TreeNode *parent) {
  if (!m_rootNode) {
    m_rootNode = std::make_shared<TreeNode>();
    m_rootNode->displayName = "Root";
  }

  for (auto &element : m_OPCUADataBlock->getVariabeDataVector()) {
    //  special node skip it
    if (element.filter_reason != "" ||
        m_parentNodeIDMap.find(QString::fromStdString(element.variable_name)) !=
            m_parentNodeIDMap.end()) {
      continue;
    }
    TreeNode *parent = nullptr;

    auto it = m_parentNodeIDMap.find(QString::fromStdString(element.parent_nodeID));
    if (it != m_parentNodeIDMap.end()) {
      //  find it !
      parent = it.value();
    } else {
      //  can not find it ! mean we need build parent Node in Map fisrt
      parent = createPlaceholderNode(element.parent_nodeID);
    }

    TreeNode *varNode = new TreeNode();
    if (element.arrayDimensions == "" && element.data_type_enum != S7DataType::UNKNOWN) {
      varNode->m_dataBlock = &element;
    }
    if(!varNode->m_dataBlock)
    {
      element.data_type_enum = S7DataType::UNKNOWN;
    }

    //  Array_Template do not need set m_dataBlock
    varNode->parent = parent;
    varNode->displayName = QString::fromStdString(element.variable_name);

    //  build parent-son relationship
    parent->children.append(varNode);
    m_parentNodeIDMap[QString::fromStdString(element.variable_nodeID)] = varNode;
    // Resize the varNode parent array according to child sizes.
    if(parent->m_dataBlock != nullptr)
    {
      parent->m_dataBlock->s7_data_array_length = parent->children.size(); 
      parent->m_dataBlock->is_array = true;
    }
   
  }
}

TreeNode *
OPCUADataBlockModel::createPlaceholderNode(const std::string &parent_nodeID) {
  if (m_parentNodeIDMap.find(QString::fromStdString(parent_nodeID)) !=
      m_parentNodeIDMap.end()) {
    //  mean the parent node has exist
    return nullptr;
  }

  TreeNode *placeholder = new TreeNode();
  std::string parentName{extractLastPartWithoutIndexForNormal(parent_nodeID)};
  placeholder->displayName = QString::fromStdString(parentName);
  m_parentNodeIDMap[QString::fromStdString(parent_nodeID)] = placeholder;

  //  get parent data block
  auto parentPointer = findOPCUADataStruct(parent_nodeID);
 
  if (parentPointer) {
    //  generate parent node by parent data block when the parent node do not
    //  exist in parent map
    auto gradParentPointer =
        createPlaceholderNode(parentPointer->parent_nodeID);
    if (!gradParentPointer) {
      //  mean the gradparent node has exist , need find by map
      // placeholder->parent = m_parentNodeIDMap[QString::fromStdString(
      //     getParentName(*parentPointer))];
      placeholder->parent = m_parentNodeIDMap[QString::fromStdString(
          parentPointer->parent_nodeID)];
    } else {
      placeholder->parent = gradParentPointer;
    }
  } else {
    //  find node result -> nullptr means the node is root node (DB......)
    placeholder->parent = m_rootNode.get();
  }

  placeholder->parent->children.append(placeholder);
  return placeholder;
}

OPCUAModernDataStruct * 
OPCUADataBlockModel::findOPCUADataStruct(const std::string &parent_nodeID) {
  for(auto &element : m_OPCUADataBlock->getVariabeDataVector()) 
  {
    if(element.variable_nodeID == parent_nodeID)
    {
      return &element;
    }
  }
  return nullptr;
}

std::string
OPCUADataBlockModel::getParentName(const OPCUAModernDataStruct &element) {
  std::string parentName{};
  
  if(element.isOPCUAType)
  {
    parentName = extractLastPartWithoutIndexForNormal(element.parent_nodeID);
  }
  else
  {
    parentName = extractLastPartWithoutIndexForNormal(element);
  }
  return parentName;
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

  for (TreeNode *child : node->children) {
    collectIndicesByLevel(child, level + 1, levelMap);
  }
}

//OPCUADataBlockBuilder------------------------------------------------------------
Result<bool, RichError>
OPCUADataBlockBuilder::build(const QString  &content,const std::string &ip_Address) {
  //  build DataBlock and package it into Result
  auto result = this->add_OPCUADataBlock_from_OPCUADataBlockDefinition(content,ip_Address,InputFormat::XML);

  if (result.is_success()) {
    emit requestSaveOPCUAParseResult(std::move(result.unwrap_returnLeftValue()));
    return Result<bool, RichError>(true);
  } else {
    return Result<bool, RichError>(RichError{result.unwrap_err()});
  }
}

Result<bool, RichError>
OPCUADataBlockBuilder::build_CSV(const QString  &content,const std::string &ip_Address) {
  //  build DataBlock and package it into Result
  auto result = this->add_OPCUADataBlock_from_OPCUADataBlockDefinition(content,ip_Address,InputFormat::CSV);

  if (result.is_success()) {
    emit requestSaveOPCUAParseResult(std::move(result.unwrap_returnLeftValue()));
    return Result<bool, RichError>(true);
  } else {
    return Result<bool, RichError>(RichError{result.unwrap_err()});
  }
}


Result<bool, RichError>
OPCUADataBlockBuilder::build_InlineBrowse(const std::string &ip_Address) {
  UA_Client *client = UA_Client_new();
  // 设置更长的超时时间（因为要浏览很多节点）
  UA_ClientConfig *ua_config = UA_Client_getConfig(client);
  ua_config->timeout = 30000; // 30秒

  UA_StatusCode retval =
      UA_Client_connect(client, "opc.tcp://192.168.0.2:4840");

  OPCUAInlineBrowse item;
  std::shared_ptr<OPCUAParseResult> parseResult = std::make_shared<OPCUAParseResult>();
  UA_NodeId objectsFolderId = UA_NODEID_NUMERIC(0, 85);
  auto vec = item.test_browseNodeChildren(client, objectsFolderId, 0, 10, "");
  if(vec.empty())
  {
    return Result<bool, RichError>(RichError{"Get Inline Browse Information fail"});
  }
  else
  {
    parseResult->getOPCUAStrcutVec(std::move(vec));
  }

  {
    emit requestSaveOPCUAParseResult(std::move(parseResult));
    return Result<bool, RichError>(true);
  }
}

Result<std::shared_ptr<OPCUAParseResult>, RichError>
OPCUADataBlockBuilder::add_OPCUADataBlock_from_OPCUADataBlockDefinition(
    const QString &file_path, const std::string &ip_Address,const InputFormat &buildType) {
  // 读取XML文件
  std::ifstream file(file_path.toStdString());
  if (!file.is_open()) {
    return Result<std::shared_ptr<OPCUAParseResult>, RichError>(
        RichError{"XML parse fail"});
  }

  std::string xml_content((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
  file.close();

  // 创建解析器并执行解析
  if(buildType==InputFormat::XML)
  {
    OPCUAXMLParser parser(xml_content);
    std::shared_ptr<OPCUAParseResult> result = parser.parse();
    auto updateResult = updateTypeEnum(result);
    if (updateResult.is_fail()) {
      return Result<std::shared_ptr<OPCUAParseResult>, RichError>(
          RichError{"OPCUAXML Parser work fail"});
    } else {
      return Result<std::shared_ptr<OPCUAParseResult>, RichError>((result));
    }
  }
  else if(buildType ==InputFormat::CSV)
  {
    std::shared_ptr<OPCUAParseResult> result = std::make_shared<OPCUAParseResult>();
    OPCUACSVParser parser;

    // 1. 从文件读取内容
    std::vector<std::string> lines = parser.readCSVFile(file_path.toStdString());

    if (lines.empty()) {
      return Result<std::shared_ptr<OPCUAParseResult>, RichError>(
          RichError{"source csv file is empty in add_OPCUADataBlock_from_OPCUADataBlockDefinition"});
    }

  // 3. 解析数据并填充结构体（hasHeader=true表示跳过第一行标题）
    std::vector<OPCUAModernDataStructFromCSV> parsedData =
        parser.parseAllData(lines, true);
    std::vector<OPCUAModernDataStruct> vecData;

    for(auto&element:parsedData)
    {
      OPCUAModernDataStruct data{element};
      vecData.push_back(std::move(data));
    }
    result->getOPCUAStrcutVec(std::move(vecData));
    return Result<std::shared_ptr<OPCUAParseResult>, RichError>((result));
  }
  else
  {
    return Result<std::shared_ptr<OPCUAParseResult>, RichError>(
        RichError{"error build type in "
                  "add_OPCUADataBlock_from_OPCUADataBlockDefinition "});
  }
}

Result<std::shared_ptr<OPCUADataBlock>, RichError>
OPCUADataBlockBuilder::add_OPCUADataBlock_from_OPCUADataBlockDefinition(
    const OPCUADataBlockDefinition &data_block_definition, const std::string &ip_Address) {
  // Add variables of basic types through loop checking
  auto dataBlock = std::make_shared<OPCUADataBlock>();
  std::vector<OPCUAModernDataStruct> m_variable_vector;

  bool is_done_successfully = true;
  for (auto &var : data_block_definition.variable_definitions_vector) {
    is_done_successfully =
        add_variable_from_OPCUADataBlockDefinition(
            var, data_block_definition.block_number,
            "\"" + data_block_definition.data_block_name + "\"",
            m_variable_vector)
            .is_success() &&
        is_done_successfully;
  }

  std::shared_ptr<OPCUAParseResult> m_variable =
      std::make_shared<OPCUAParseResult>();
  dataBlock->setParseResult(m_variable);
  dataBlock->setVariableMap(m_variable_vector);
  dataBlock->calculateDataBlockLength(data_block_definition.total_bytes_size);
  dataBlock->setName(data_block_definition.data_block_name);
  dataBlock->setIpAddres(ip_Address);

  if (is_done_successfully) {
    return Result<std::shared_ptr<OPCUADataBlock>, RichError>(dataBlock);
  } else {
    return Result<std::shared_ptr<OPCUADataBlock>, RichError>(
        RichError("add variable failed"));
  }
}

Result<bool, RichError>
OPCUADataBlockBuilder::add_variable_from_OPCUADataBlockDefinition(
    const S7XMLVariableDefinition &variable_definition, int data_block_number,
    const std::string &prefix,
    std::vector<OPCUAModernDataStruct> &m_variable_vector) {
  {
    // Add variables of basic types through loop checking
    bool is_done_successfully = false;
    {
      switch (variable_definition.data_type_enum) {
      case S7DataType::ARRAY:
        for (auto &var : variable_definition.struct_member_vector)
          is_done_successfully =
              add_variable_from_OPCUADataBlockDefinition(
                  var, data_block_number,
                  prefix + ".\"" + variable_definition.variable_name + "\"",
                  m_variable_vector)
                  .is_success() &&
              is_done_successfully;
        break;
      case S7DataType::STRUCT:
        for (auto &var : variable_definition.struct_member_vector)
          is_done_successfully =
              add_variable_from_OPCUADataBlockDefinition(
                  var, data_block_number,
                  prefix + ".\"" + variable_definition.variable_name + "\"",
                  m_variable_vector)
                  .is_success() &&
              is_done_successfully;
        break;
      default:
        OPCUAModernDataStruct tmp_variable;
        if (!isNumber(variable_definition.variable_name)
                 .unwrap_returnRightValue()) {
          //  NON INTEGER FOR NORMAL SUFFIX
          tmp_variable.variable_full_path =
              prefix + ".\"" + variable_definition.variable_name + "\"";
          tmp_variable.variable_nodeID = tmp_variable.variable_full_path;
        } else {
          //  INTERGER FOR SPECIAL SUFFIX
          tmp_variable.variable_full_path =
              prefix + "[" + variable_definition.variable_name + "]";
          tmp_variable.variable_nodeID = prefix;
        }
        tmp_variable.parentName = prefix;
        tmp_variable.isOPCUAType = false;
        tmp_variable.variable_name = variable_definition.variable_name;
        tmp_variable.data_type_enum = variable_definition.data_type_enum;
        tmp_variable.data_block_number = data_block_number;

        tmp_variable.bytes_offset = variable_definition.bytes_offset;
        tmp_variable.bit_offset = variable_definition.bit_offset;
        tmp_variable.s7_data_type_length =
            variable_definition.s7_data_type_length;
        tmp_variable.s7_data_array_length =
            variable_definition.s7_data_array_length;

        // initialize data_pointer
        switch (variable_definition.data_type_enum) {
        case S7DataType::BOOL:
          tmp_variable.data_pointer = std::make_unique<Dynamic_Value>(false);
          break;
        case S7DataType::BYTE:
          tmp_variable.data_pointer =
              std::make_unique<Dynamic_Value>(uint8_t{0});
          break;
        case S7DataType::INT:
          tmp_variable.data_pointer = std::make_unique<Dynamic_Value>(int{0});
          break;
        case S7DataType::DINT:
          tmp_variable.data_pointer =
              std::make_unique<Dynamic_Value>(int32_t{0});
          break;
        case S7DataType::REAL:
          tmp_variable.data_pointer = std::make_unique<Dynamic_Value>(float{0});
          break;
        case S7DataType::WORD:
          tmp_variable.data_pointer =
              std::make_unique<Dynamic_Value>(uint16_t{0});
          break;
        case S7DataType::DWORD:
          tmp_variable.data_pointer =
              std::make_unique<Dynamic_Value>(uint32_t{0});
          // 如需显示十六进制，在 data() 函数中转换，而不是存字符串
          break;
        case S7DataType::UDINT:
          tmp_variable.data_pointer =
              std::make_unique<Dynamic_Value>(uint32_t{0});
          break;
        case S7DataType::STRING:
          tmp_variable.data_pointer =
              std::make_unique<Dynamic_Value>(std::string{"Null"});
          break;
        default:
          break;
        }

        m_variable_vector.push_back(std::move(tmp_variable));
        is_done_successfully = true;
      }
    }

    return Result<bool, RichError>(is_done_successfully);
  }
}

Result<bool,RichError> OPCUADataBlockBuilder::updateTypeEnum(std::shared_ptr<OPCUAParseResult> &data)
{
  if(!data->variables.size())
  {
    return Result<bool, RichError> (RichError{"variables size = 0 "});
  }
  for(auto &item : data->variables)
  {
    auto it = typeMap.find(item.raw_data_type);
    if (it != typeMap.end()) {
      item.data_type_enum = it->second;
      resetValueByTypeEnum(item);
    } else {
      item.data_type_enum = S7DataType::UNKNOWN;
    }
  }
  return Result<bool, RichError>(true);
}

void OPCUADataBlockBuilder::resetValueByTypeEnum(OPCUAModernDataStruct &data) {
  auto &s7_type = data.data_type_enum;
  if (s7_type == S7DataType::BOOL) {
    { data.data_pointer->Reset_Value(bool{0}); }
  } else if (s7_type == S7DataType::BYTE) {
    uint8_t tmp;
    { data.data_pointer->Reset_Value(uint8_t{0}); }
  } else if (s7_type == S7DataType::INT) {
    { data.data_pointer->Reset_Value(int16_t{0}); }
  } else if (s7_type == S7DataType::WORD) {
    { data.data_pointer->Reset_Value(uint16_t{0}); }
  } else if (s7_type == S7DataType::DWORD || s7_type == S7DataType::UDINT) {
    { data.data_pointer->Reset_Value(uint32_t{0}); }
  } else if (s7_type == S7DataType::DINT) {
    { data.data_pointer->Reset_Value(int32_t{0}); }
  } else if (s7_type == S7DataType::REAL) {
    { data.data_pointer->Reset_Value(float{0}); }
  } else if (s7_type == S7DataType::STRING) {
    { data.data_pointer->Reset_Value(std::string{0}); }
  } else {
    { data.data_pointer->Reset_Value(std::string{0}); }
  }
}

//OPCUADelegate-----------------------------------------------------------
QWidget *OPCUADataDelegate::createEditor(QWidget *parent,
                                         const QStyleOptionViewItem &option,
                                         const QModelIndex &index) const {
  if (index.column() == 3) { // Value 列
    // 获取数据类型
    TreeNode *node = static_cast<TreeNode *>(index.internalPointer());
    if (!node || !node->m_dataBlock) {
      qDebug() << "Node or dataBlock is null";
      return QStyledItemDelegate::createEditor(parent, option, index);
    }
    S7DataType dataType = node->m_dataBlock->data_type_enum;

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
        S7DataType dataType = node->m_dataBlock->data_type_enum;

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
        S7DataType dataType = node->m_dataBlock->data_type_enum;
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

void OPCUADataBlockView::validateTreeStructure() {
  qDebug() << "========================================";
  qDebug() << "=== 开始验证树结构（增强版） ===";
  qDebug() << "========================================";

  int nodeCount = 0;
  int leafCount = 0;
  int errorCount = 0;
  int depthLevels = 0;

  std::function<void(const QModelIndex &, int)> traverse =
      [&](const QModelIndex &parent, int depth) {
        depthLevels = std::max(depthLevels, depth);
        int rows = m_model->rowCount(parent);

        for (int row = 0; row < rows; row++) {
          QModelIndex idx = m_model->index(row, 0, parent);
          if (!idx.isValid()) {
            qDebug() << "❌ 错误：无效索引 at row:" << row << "depth:" << depth;
            errorCount++;
            continue;
          }

          nodeCount++;

          TreeNode *node = static_cast<TreeNode *>(idx.internalPointer());
          QString indent = QString(" ").repeated(depth * 2);

          // 获取节点信息
          QString nodeName = idx.data().toString();
          bool hasData = (node && node->m_dataBlock != nullptr);
          bool hasChildren = m_model->hasChildren(idx);
          int childCount = m_model->rowCount(idx);

          // 获取内部指针信息
          quint64 internalPtr =
              reinterpret_cast<quint64>(idx.internalPointer());
          quint64 parentPtr = 0;
          if (node && node->parent) {
            parentPtr = reinterpret_cast<quint64>(node->parent);
          }

          // 获取节点类型（如果有 dataBlock）
          QString dataType = "无数据";
          quint64 dataPtr = 0;
          if (hasData && node->m_dataBlock) {
            dataType = QString::fromStdString(node->m_dataBlock->data_type);
            dataPtr = reinterpret_cast<quint64>(node->m_dataBlock);
          }

          // 特殊标记：如果节点有数据但不是叶子节点（有子节点），这是一个严重问题
          bool invalidState = (hasData && hasChildren);
          if (invalidState) {
            qDebug() << "";
            qDebug().noquote()
                << indent + "⚠️⚠️⚠️ 警告：节点同时有数据和孩子！⚠️⚠️⚠️";
            errorCount++;
          }

          // 打印节点信息
          QString nodeInfo = QString("%1[%2] row:%3 col:0 name:\"%4\" %5")
                                 .arg(indent)
                                 .arg(nodeCount)
                                 .arg(row)
                                 .arg(nodeName)
                                 .arg(invalidState ? "❌【异常节点】" : "");

          qDebug().noquote() << nodeInfo;
          qDebug().noquote() << indent + "  ├─ internalPtr : 0x" +
                                    QString::number(internalPtr, 16);
          qDebug().noquote() << indent + "  ├─ parentPtr   : 0x" +
                                    QString::number(parentPtr, 16);
          qDebug().noquote() << indent + "  ├─ hasData     : " +
                                    QString(hasData ? "true" : "false");
          qDebug().noquote() << indent + "  ├─ dataType    : " + dataType;
          qDebug().noquote() << indent + "  ├─ dataPtr     : 0x" +
                                    QString::number(dataPtr, 16);
          qDebug().noquote() << indent + "  ├─ hasChildren : " +
                                    QString(hasChildren ? "true" : "false");
          qDebug().noquote()
              << indent + "  └─ childCount  : " + QString::number(childCount);

          // 统计叶子节点（应该有数据的节点）
          if (!hasChildren && hasData) {
            leafCount++;
          }

          // 打印所有列的数据
          for (int col = 0; col < m_model->columnCount(parent); col++) {
            QModelIndex colIdx = m_model->index(row, col, parent);
            if (colIdx.isValid()) {
              QVariant data = colIdx.data(Qt::DisplayRole);
              qDebug().noquote()
                  << indent + "     col" + QString::number(col) + " : "
                  << data.toString();
            } else {
              qDebug().noquote()
                  << indent + "     ❌ 列" + QString::number(col) + "无效";
              errorCount++;
            }
          }

          qDebug() << "";

          // 递归子节点
          if (hasChildren) {
            traverse(idx, depth + 1);
          }
        }
      };

  traverse(QModelIndex(), 0);

  qDebug() << "========================================";
  qDebug() << "=== 验证结果统计 ===";
  qDebug() << "总节点数        : " << nodeCount;
  qDebug() << "数据叶子节点数  : " << leafCount;
  qDebug() << "最大深度        : " << depthLevels;
  qDebug() << "错误/警告数     : " << errorCount;
  qDebug() << "========================================";

  // 额外：打印 manualIdx 和 viewIdx 的对应关系提示
  qDebug() << "";
  qDebug() << "=== 提示 ===";
  qDebug() << "注意：viewIdx 应该只包含数据叶子节点（无子节点）";
  qDebug() << "如果发现节点同时有数据和子节点，说明树结构有问题";
  qDebug() << "数据叶节点的 dataPtr 应该与 manualIdx 中的 internalPtr 一致";
  qDebug() << "========================================";
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
    if (node == nullptr || node->m_dataBlock == nullptr) {
      qDebug() << "node:" << node << "node->m_dataBlock:";
      return QWidget::eventFilter(obj, event);
    }

    // ✅ 关键验证：检查 column>0 时，parent() 是否能正确返回
    // if (idx.column() > 0) {
    //   // 测试：通过这个索引获取父节点
    //   QModelIndex parentCheck = idx.parent();
    //   qDebug() << "=== index() final validation ===";
    //   qDebug() << "  idx.isValid()=" << idx.isValid();
    //   qDebug() << "  idx.row()=" << idx.row();
    //   qDebug() << "  idx.column()=" << idx.column();
    //   qDebug() << "  parentCheck.isValid()=" << parentCheck.isValid();
    //   if (parentCheck.isValid()) {
    //     qDebug() << "  parentCheck.row()=" << parentCheck.row();
    //     qDebug() << "  parentCheck.column()=" << parentCheck.column();
    //     qDebug() << "  parentCheck.data()=" << parentCheck.data().toString();
    //   }

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
      if (!idx.isValid() || node == nullptr || node->m_dataBlock == nullptr) {
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
  if (!node || node == m_model->getRootNode()) {
    return QModelIndex();
  }

  // 获取父节点
  TreeNode *parentNode = node->parent;
  if (!parentNode) {
    return QModelIndex();
  }

  // 获取 node 在父节点中的行号
  int row = parentNode->children.indexOf(node);
  if (row < 0) {
    return QModelIndex();
  }

  // 需要获取父节点的 QModelIndex
  // 如果父节点是根节点，parentIdx 应该无效
  QModelIndex parentIdx;

  if (parentNode != m_model->getRootNode()) {
    // 递归获取父节点的索引
    parentIdx = findIndexByNode(parentNode, 0); // 获取父节点的第0列
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
            if (!Node || Node->children.size() == 0) {
              return;
            }
            Node->isExpanded = true;
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
            if (!Node || Node->children.size() == 0) {
              return;
            }

            Node->isExpanded = false;
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
void OPCUADataBlockController::initialize(Scope *scope) {
  if (!scope) {
    return;
  } else {
    std::shared_ptr<OPCUADataBlockModel> model = scope->getShared<OPCUADataBlockModel>();
    if (!model) {
      spdlog::error("OPCUADataBlockModel getSharedPtr is fail");
    } else {
      m_model = std::move(model);
    }

    std::shared_ptr<OPCUADeviceReader> reader = scope->getShared<OPCUADeviceReader>();
    if (!reader) {
      spdlog::error("OPCUADeviceReader getSharedPtr is fail");
    } else {
      m_reader = std::move(reader);
    }

    m_OPCUADataBlockBuild = std::make_shared<OPCUADataBlockBuilder>();
  }

  
};

void OPCUADataBlockController::initializeView(OPCUADataBlockView *view)
{
  if(!view)
  {
    spdlog::error("initialize view fail : view is nullptr");
    return ;
  }
  else
  {
    // 打印实际类型
    qDebug() << "=== VIEW TYPE DEBUG ===";
    qDebug() << "View class name:" << view->metaObject()->className();
    qDebug() << "View inheritance:";
    const QMetaObject *meta = view->metaObject();
    const QMetaObject *meta_treeView= view->getTableView()->metaObject();
    while (meta) {
      qDebug() <<"view class Name : "<< "  -" << meta->className();
      qDebug() <<"treeView class Name " <<"  -" << meta_treeView->className();
      meta = meta->superClass();
    }
    m_view = view;
  }
}

void OPCUADataBlockController::buildConnection() {
  connect(m_OPCUADataBlockBuild.get(),
          &OPCUADataBlockBuilder::requestSaveOPCUAParseResult, this,
          &OPCUADataBlockController::onSaveOPCUAParseResult);
  connect(m_OPCUADataBlockBuild.get(),
          &OPCUADataBlockBuilder::requestSaveOPCUADataBlock, this,
          &OPCUADataBlockController::onSaveOPCUADataBlock);
  // connect(m_view, &OPCUADataBlockView::requestFile, this,
  //         &OPCUADataBlockController::onbuildDataBlockFromDBFile);
  connect(m_view, &OPCUADataBlockView::requestRefresh, this,
          &OPCUADataBlockController::onViewReadRequested);
  connect(m_view, &OPCUADataBlockView::requestWrite, this,
          &OPCUADataBlockController::onViewWriteRequested);
}

void OPCUADataBlockController::onViewReadRequested() {
  //  update buffer from data_pointer in littleEndian
  auto result_test = m_reader->getIdentifier();
  if(result_test.is_fail())
  {
    return;
  }

  if(result_test.unwrap_returnLeftValue().find("OPC_UA") != std::string::npos)
  {
    auto result = m_reader->batchReadOPCUADataBlock_FromPLC(m_OPCUADataBlock.get());
    if (result.is_fail()) {
      spdlog::error("onViewReadRequested fail : {}", result.unwrap_err().what());
    }
  }
  else
  {
    auto result = m_reader->batchReadS7DataBlock_FromPLC(m_OPCUADataBlock.get());
    if (result.is_fail()) {
      spdlog::error("onViewReadRequested fail : {}", result.unwrap_err().what());
    }
  }

  m_model->batchSetDataForOPCUA();
};

void OPCUADataBlockController::onViewWriteRequested() {
  //  update buffer from data_pointer in littleEndian
  auto result_test = m_reader->getIdentifier();
  if(result_test.is_fail())
  {
    return;
  }

  if(result_test.unwrap_returnLeftValue().find("OPC_UA") != std::string::npos )
  {
    auto result = m_reader->batchWriteOPCUABlock_ToPLC(m_OPCUADataBlock.get());
    if (result.is_fail()) {
      spdlog::error("onViewWriteRequested fail : {}", result.unwrap_err().what());
    }
  }
  else
  {
    auto result = m_reader->batchWriteS7DataBlock_ToPLC(m_OPCUADataBlock.get());
    if (result.is_fail()) {
      spdlog::error("onViewWriteRequested fail : {}", result.unwrap_err().what());
    }
  }
  
};

void OPCUADataBlockController::onSaveOPCUAParseResult(const std::shared_ptr<OPCUAParseResult> &parseResult) {
  if (parseResult) {
    //  update lastest data block
    m_OPCUADataBlock = std::make_shared<OPCUADataBlock>();
    m_OPCUADataBlock->setParseResult(parseResult);
    m_model->setOPCUADataBlock(m_OPCUADataBlock);
    m_view->setModel();
    m_view->setDelegate();
    if (m_reader->getIdentifier().is_fail()) {

    } else if (m_reader->getIdentifier().unwrap_returnRightValue().find(
                   "OPC_UA") != std::string::npos) {
    } else {
      calculate_data_block_size(m_OPCUADataBlock->getVariabeDataVector());
    }
  } else {
    spdlog::error("onSaveOPCUAParseResult : dataBlock is nullptr");
  }
}

void OPCUADataBlockController::onSaveOPCUADataBlock(const std::shared_ptr<OPCUADataBlock> &dataBlock) {
  if (dataBlock) {
    //  update lastest data block
    m_OPCUADataBlock = dataBlock;
    m_model->setOPCUADataBlock(dataBlock);
    m_view->setModel();
    m_view->setDelegate();
    if(m_reader->getIdentifier().is_fail())
    {

    }
    else if(m_reader->getIdentifier().unwrap_returnRightValue() .find("OPC_UA") != std::string::npos)
    {
    }
    else
    {
      calculate_data_block_size(dataBlock->getVariabeDataVector());
    }
  } else {
    spdlog::error("onSaveOPCUADataBlock : dataBlock is nullptr");
  }
}

//OPCUADataBlockManager-------------------------------------------------------------------------
bool
OPCUADataBlockManager::createTableView(const QString &ipAddress,
                                       const QString &filePath) {
  QString object;
  if (!filePath.contains("-")) {
    QFileInfo info{filePath};
    object = info.fileName();
  } else {
    object = filePath;
  }
  
  OPCUADataBlockKey key(ipAddress, object);
  // 创建新的 DataBlock
  OPCUADataBlockContext *context =
      createOPCUADataBlockContext(ipAddress, object);
  if (!context) {
    emit errorOccurred(ipAddress, filePath, "Failed to create DataBlock");
    return false;
  }

  // 存储
  m_OPCUADataBlocks[key] = context;

  return true;
}

QTreeView* OPCUADataBlockManager::getTableView(const QString& ipAddress, 
                                          const QString& dataBlockName)
{
    OPCUADataBlockKey key(ipAddress, dataBlockName);
    if (m_OPCUADataBlocks.contains(key)) {
        return m_OPCUADataBlocks[key]->view->getTableView();
    }
    return nullptr;
}

QWidget* OPCUADataBlockManager::getView(const QString& ipAddress, 
                                          const QString& dataBlockName)
{
    OPCUADataBlockKey key(ipAddress, dataBlockName);
    if (m_OPCUADataBlocks.contains(key)) {
        return m_OPCUADataBlocks[key]->view->getView();
    }
    return nullptr;
}


// ========== 创建 DataBlock 上下文 ==========
OPCUADataBlockContext* OPCUADataBlockManager::createOPCUADataBlockContext(const QString& ipAddress,
                                                          const QString& dataBlockName)
{
    OPCUADataBlockContext* context = new OPCUADataBlockContext();
    context->key = OPCUADataBlockKey(ipAddress, dataBlockName);
    context->createTime = QDateTime::currentDateTime();
    context->lastAccessTime = context->createTime;
    
    // 创建独立的 MVC 组件
    context->view = createView();
    context->model = createModel(ipAddress, dataBlockName);
    context->delegate = createDelegate(dataBlockName);
    context->controller = std::make_shared<OPCUADataBlockController>();
    context->view->setParent(&context->m_controllWidget);
    
    Scope tmpScope;
    tmpScope.registerService(context->model);
    tmpScope.registerService(m_builder);
    //  register special OPCUADeviceReader
    for (auto it = m_readerVector.begin(); it != m_readerVector.end();) {
      if ((*it)->getIdentifier().is_success()) {
        if ((*it)->getIdentifier().unwrap_returnRightValue() ==
            ipAddress.toStdString()) {
          tmpScope.registerService(*it);
          it = m_readerVector.erase(it); // erase 返回下一个有效迭代器
          break;
        } else {
          ++it;
        }
      } else {
        ++it;
      }
    }

    // 初始化Controller,View
    context->controller->initialize(&tmpScope);
    context->controller->initializeView(context->view);
    context->controller->buildConnection();

    if (!context->view || !context->model || !context->delegate) {
        delete context;
        return nullptr;
    }
    
    // 组装 MVC
    context->view->getModel(context->model.get());
    context->view->getDelegate(context->delegate.get());
    
    // 设置连接
    setupOPCUADataBlockConnections(context);
    
    return context;
}


OPCUADataBlockView *OPCUADataBlockManager::createView()
{
  auto view =  new OPCUADataBlockView();
  // 配置 View 属性
  return view;
}

std::shared_ptr<OPCUADataBlockModel> OPCUADataBlockManager::createModel(const QString& ipAddress,
                                                              const QString& dataBlockName)
{
    auto model = std::make_shared<OPCUADataBlockModel>();
    return model;
}

std::shared_ptr<OPCUADataDelegate> OPCUADataBlockManager::createDelegate(const QString& dataBlockName)
{
    auto delegate = std::make_shared<OPCUADataDelegate>();
    return delegate;
}

void OPCUADataBlockManager::setupOPCUADataBlockConnections(OPCUADataBlockContext* context)
{
    if (!context) return;
    
    // 连接 Model 的数据变化信号
    connect(context->model.get(), &OPCUADataBlockModel::dataChanged,
            [this, context](const QModelIndex& topLeft, const QModelIndex& bottomRight) {
                Q_UNUSED(topLeft);
                Q_UNUSED(bottomRight);
                markAsModified(context->key.ipAddress, 
                              context->key.OPCUADataBlockName, 
                              true);
            });
    
    // 可以添加其他信号连接
}

// ========== 删除接口 ==========
bool OPCUADataBlockManager::removeOPCUADataBlock(const QString& ipAddress, 
                                      const QString& dataBlockName)
{
    OPCUADataBlockKey key(ipAddress, dataBlockName);
    
    if (!m_OPCUADataBlocks.contains(key)) {
        return false;
    }

    // 清理资源
    OPCUADataBlockContext* context = m_OPCUADataBlocks[key];
    cleanupOPCUADataBlockContext(context);
    
    // 从映射中移除
    m_OPCUADataBlocks.remove(key);
    
    emit OPCUADataBlockRemoved(ipAddress, dataBlockName);
    
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

// ========== 设备连接管理 ==========
std::shared_ptr<OPCUADeviceReader> OPCUADataBlockManager::getClient(const QString& ipAddress,const std::string& connectWay)
{
    if (m_clients.contains(ipAddress)) {
        return m_clients[ipAddress];
    }
    return nullptr;
}

// ========== 业务接口实现 ==========
bool OPCUADataBlockManager::buildS7Connect(
    const QString &ipAddress, int rack, int slot,
    const std::string &connectWay) // S7参数
{
  auto context = getSpecialReader(ipAddress.toStdString());

  if (!context) {
    emit errorOccurred(ipAddress, "", " not found");
    auto reader = std::make_shared<OPCUADeviceReader>();
    bool result =
        reader->onRequestBuildS7(ipAddress.toStdString(), rack, slot,1000);
    if (result) {
      m_readerVector.push_back(std::move(reader));
    } else {
      return false;
    }

    return true;
  }

  spdlog::info("{} has exist", ipAddress.toStdString());
  return context->onRequestOPCUACheckConnect();
}

bool OPCUADataBlockManager::buildOPCUAConnect(const QString &ipAddress, int nameSpace,
                                      int port,const std::string &connectWay) {
  auto context = getSpecialReader(ipAddress.toStdString());

  if (!context) {
    emit errorOccurred(ipAddress, "", " not found");
    auto reader = std::make_shared<OPCUADeviceReader>();
    bool result =
        reader->onRequestBuildOPCUA(ipAddress.toStdString(), nameSpace, port);
    m_readerVector.push_back(std::move(reader));

    return true;
  }

  spdlog::info("{} has exist", ipAddress.toStdString());
  return context->onRequestOPCUACheckConnect();
}

bool OPCUADataBlockManager::checkConnectToDevice(const QString& ipAddress,
                                      const std::string& connectWay)
{
    auto client = getClient(ipAddress,connectWay);
    if(!client)
    {
      return false;
    } else {
      {
        if(connectWay == "OPC_UA")
        {
          return client->onRequestOPCUACheckConnect();
        }
        else
        {
          return client->onRequestS7CheckConnect();
        }
      }
    }
}

// ========== 辅助函数 ==========
OPCUADataBlockContext *
OPCUADataBlockManager::getOPCUADataBlockContext(const QString &ipAddress,
                                                const QString &filePath) {
  QString object;
  if (!filePath.contains("-")) {
    //  expect behaviours : xxx/xxxx/xxx/
    QFileInfo info{filePath};
    object = info.fileName();
  } else {
    //  expect behaviours : xxxx-xxxx-xxx-xxx
    object = filePath;
  }

  OPCUADataBlockKey key(ipAddress, object);
  if (m_OPCUADataBlocks.contains(key)) {
    return m_OPCUADataBlocks[key];
  }
  return nullptr;
}

std::shared_ptr<OPCUADeviceReader> OPCUADataBlockManager::getSpecialReader(const std::string &ip_Address) const {
 for(auto &item:m_readerVector) 
 {
  if(item->getIdentifier().is_success())
  {
    if(item->getIdentifier().unwrap_returnRightValue() == ip_Address)
    {
      return item;
    }
  }
 }
 return nullptr;
}

void OPCUADataBlockManager::markAsModified(const QString& ipAddress, 
                                     const QString& dataBlockName, 
                                     bool modified)
{
    OPCUADataBlockContext* context = getOPCUADataBlockContext(ipAddress, dataBlockName);
    if (context && context->isModified != modified) {
        context->isModified = modified;
        emit OPCUADataBlockModified(ipAddress, dataBlockName, modified);
    }
}

bool OPCUADataBlockManager::buildDataFromFile(const QString &ip_Address,const QString &filePath)
{
  auto dataPtr = getOPCUADataBlockContext(ip_Address, filePath);
  if(!dataPtr)
  {
    // create new tableView and dataBlockContext
    bool result = createTableView(ip_Address, filePath);
    if(!result)
    {
      return false;
    }
    else
    {
      OPCUADataBlockContext *context = getOPCUADataBlockContext(ip_Address, filePath);
      if(context)
      {
        if(ip_Address.contains("OPC_UA"))
        {
          //  create dataBlock for filling contextt into model
          return context->controller->onbuildOPCUADataBlockFromXMLFile(
              filePath, ip_Address.toStdString());
        }
        else
        {
          //  create dataBlock for filling contextt into model
          return context->controller->onbuildOPCUADataBlockFromXMLFile(
              filePath, ip_Address.toStdString());
        }
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

bool OPCUADataBlockManager::buildOPCUAInlineBrowse(const QString &ipAddress, int nameSpace, int port,
                           const std::string &connectWay) 
{
  QString filePath{ipAddress + "-" + QString::number(nameSpace) + "-" + QString::number(port)};

  auto dataPtr = getOPCUADataBlockContext(ipAddress,filePath);
  if(!dataPtr)
  {
    // create new tableView and dataBlockContext
    bool result = createTableView(ipAddress, filePath);
    if(!result)
    {
      return false;
    }
    else
    {
      OPCUADataBlockContext *context = getOPCUADataBlockContext(ipAddress, filePath);
      if(context)
      {
        //  create dataBlock for filling contextt into model
        return context->controller->onbuildOPCUAInlineBrowse(
            ipAddress.toStdString());
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
  if (!item.node || item.node == m_model->getRootNode()) {
    return Result<QModelIndex, RichError>(QModelIndex());
  }

  // 获取父节点
  TreeNode *parentNode = item.node->parent;
  if (!parentNode) {
    return Result<QModelIndex, RichError>(QModelIndex());
  }

  // 获取 node 在父节点中的行号
  int row = parentNode->children.indexOf(item.node);
  if (row < 0) {
    return Result<QModelIndex, RichError>(QModelIndex());
  }

  // 需要获取父节点的 QModelIndex
  // 如果父节点是根节点，parentIdx 应该无效
  QModelIndex parentIdx;

  if (parentNode != m_model->getRootNode()) {
    // 递归获取父节点的索引
    auto result = findIndexByNode(
        FindRelativeIndex{parentNode, 0}); // 获取父节点的第0列
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
  qDebug() << "select item :" << node->displayName
           << " and its parent name :" << node->parent->displayName;
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