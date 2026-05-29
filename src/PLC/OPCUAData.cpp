#include "PLC/OPCUAData.h"
//OPCUADataBlock--------------------------------------------------
OPCUADataBlock::OPCUADataBlock(const std::string& name, const std::string& ip)
    : data_block_name(name)
    , ip_address(ip) {
}

// 基本数据访问实现
const std::string& OPCUADataBlock::getBlocktName() const {
    return data_block_name;
}

std::vector<OPCUAModernDataStruct>& OPCUADataBlock::getVariabeDataVector() {
    return m_variable->variables;
}

bool OPCUADataBlock::hasVariable(const std::string& path) const {
    return findVariableByPath(path) != nullptr;
}

OPCUAParseResult* OPCUADataBlock::getVariable(const std::string& path) {
    auto* var = findVariableByPath(path);
    if (var) {
        // 返回包含该变量的 OPCUAParseResult，这里需要根据实际需求调整
        return m_variable.get();
    }
    return nullptr;
}

const int OPCUADataBlock::getVariableVectorSize() const {
    return static_cast<int>(m_variable->variables.size());
}

// 获取变量映射实现
// Result<bool, RichError> 
// OPCUADataBlock::getVariableMap(std::vector<OPCUAModernDataStruct>& m_variable_vector) {
//     try {
//         m_variable_vector = m_variable->variables;
//         return true;
//     } catch (const std::exception& e) {
//         return RichError(std::string("Failed to get variable map: ") + e.what());
//     }
// }

// set part 实现
void OPCUADataBlock::setName(const std::string& name) {
    data_block_name = name;
}

void OPCUADataBlock::setIpAddres(const std::string& ip_Address) {
    ip_address = ip_Address;
}

void OPCUADataBlock::setParseResult(const std::shared_ptr<OPCUAParseResult> &parseResult)
{
  m_variable = parseResult;
}

// 获取标识符实现
std::string OPCUADataBlock::getIdentifier() {
    return ip_address;
}

// 批量操作实现
Result<bool, RichError> OPCUADataBlock::batchReadValues(
    const std::vector<std::string>& paths,
    std::vector<QVariant>& out_values) const {
    
    out_values.clear();
    out_values.reserve(paths.size());
    
    for (const auto& path : paths) {
        auto* var = findVariableByPath(path);
        if (!var) {
            return RichError("Variable not found: " + path);
        }
        
        if (var->data_pointer) {
            // out_values.push_back(var->data_pointer->toQVariant());
            out_values.push_back(QVariant());  // 临时返回
        } else {
            out_values.push_back(QVariant());
        }
    }
    
    return true;
}

Result<bool, RichError> OPCUADataBlock::batchWriteValues(
    const std::vector<std::string>& paths,
    const std::vector<QVariant>& values) {
    
    if (paths.size() != values.size()) {
        return RichError("Paths and values size mismatch");
    }
    
    for (size_t i = 0; i < paths.size(); ++i) {
        auto* var = findVariableByPath(paths[i]);
        if (!var) {
            return RichError("Variable not found: " + paths[i]);
        }
        
        // 写入值逻辑
        // if (var->data_pointer) {
        //     var->data_pointer->fromQVariant(values[i]);
        // }
    }
    
    return true;
}

// 辅助方法实现
OPCUAModernDataStruct* OPCUADataBlock::findVariableByPath(const std::string& path) {
    auto it = std::find_if(m_variable->variables.begin(), m_variable->variables.end(),
                          [&path](const OPCUAModernDataStruct& var) {
                              return var.variable_name == path || 
                                     var.browse_name == path ||
                                     var.variable_nodeID == path;
                          });
    
    if (it != m_variable->variables.end()) {
        return &(*it);
    }
    return nullptr;
}

const OPCUAModernDataStruct* OPCUADataBlock::findVariableByPath(const std::string& path) const {
    auto it = std::find_if(m_variable->variables.begin(), m_variable->variables.end(),
                          [&path](const OPCUAModernDataStruct& var) {
                              return var.variable_name == path || 
                                     var.browse_name == path ||
                                     var.variable_nodeID == path;
                          });
    
    if (it != m_variable->variables.end()) {
        return &(*it);
    }
    return nullptr;
}

bool OPCUADataBlock::validatePath(const std::string& path, std::string& error_msg) const {
    if (path.empty()) {
        error_msg = "Path is empty";
        return false;
    }
    
    // 可以添加更多的路径验证逻辑
    // 例如：检查路径格式、特殊字符等
    
    error_msg = "";
    return true;
}

// Result<QVariant, RichError> OPCUADataBlock::readValue(QModelIndex index) const {
//     // 1. 边界检查
//     if (index.row() < 0 || index.row() >= static_cast<int>(m_variable->variables.size())) {
//         return Result<QVariant, RichError>(
//             RichError{QString("Index out of range: row=%1, size=%2")
//                       .arg(index.row())
//                       .arg(m_variable->variables.size())
//                       .toStdString()}
//         );
//     }
    
//     // 2. 获取对应行的数据（不需要遍历整个vector）
//     const auto& item = m_variable->variables[index.row()];
    
//     // 3. 根据列索引返回不同的数据
//     switch (index.column()) {
//     case 0:  // 变量名
//         return Result<QVariant, RichError>(
//             QVariant(QString::fromStdString(node->m_dataBlock->variable_name))
//         );
        
//     case 1:  // 数据类型枚举
//         return Result<QVariant, RichError>(
//             QVariant(static_cast<int>(node->m_dataBlock->data_type_enum))
//         );

//     case 2: // 读取权限
//       return Result<QVariant, RichError>(QVariant((node->m_dataBlock->access_level)));

//     case 3: {  // 实际数据值
//         // 检查 data_pointer 是否有效
//         if (!node->m_dataBlock->data_pointer) {
//             return Result<QVariant, RichError>(
//                 RichError{"data_pointer is null for variable: " + node->m_dataBlock->variable_name}
//             );
//         }
        
//         // 根据不同类型返回对应的 QVariant
//         try {
//             switch (node->m_dataBlock->data_type_enum) {
//             case S7DataType::BOOL:
//                 return Result<QVariant, RichError>(
//                     QVariant(node->m_dataBlock->data_pointer->get<bool>())
//                 );
                
//             case S7DataType::BYTE:
//                 return Result<QVariant, RichError>(
//                     QVariant(node->m_dataBlock->data_pointer->get<uint8_t>())
//                 );
                
//             case S7DataType::INT:
//                 return Result<QVariant, RichError>(
//                     QVariant(node->m_dataBlock->data_pointer->get<int16_t>())
//                 );
                
//             case S7DataType::DINT:
//                 return Result<QVariant, RichError>(
//                     QVariant(node->m_dataBlock->data_pointer->get<int32_t>())
//                 );
                
//             case S7DataType::REAL:
//                 return Result<QVariant, RichError>(
//                     QVariant(node->m_dataBlock->data_pointer->get<float>())
//                 );
                
//             case S7DataType::WORD:
//                 return Result<QVariant, RichError>(
//                     QVariant(node->m_dataBlock->data_pointer->get<uint16_t>())
//                 );
                
//             case S7DataType::DWORD:
//                 return Result<QVariant, RichError>(
//                     QVariant(QString::fromStdString(
//                         DataTypeMapper::transform_uint32_to_hex_string(
//                             node->m_dataBlock->data_pointer->get<uint32_t>()
//                         )
//                     ))
//                 );
                
//             case S7DataType::UDINT:
//                 return Result<QVariant, RichError>(
//                     QVariant(QString::fromStdString(
//                         DataTypeMapper::transform_uint32_to_string(
//                             node->m_dataBlock->data_pointer->get<uint32_t>()
//                         )
//                     ))
//                 );
                
//             case S7DataType::STRING:
//                 return Result<QVariant, RichError>(
//                     QVariant(QString::fromStdString(
//                         node->m_dataBlock->data_pointer->get<std::string>()
//                     ))
//                 );
                
//             default:
//                 return Result<QVariant, RichError>(
//                     RichError{"Unsupported data type: " + 
//                               std::to_string(static_cast<int>(node->m_dataBlock->data_type_enum))}
//                 );
//             }
//         } catch (const std::exception& e) {
//             return Result<QVariant, RichError>(
//                 RichError{"Failed to read data: " + std::string(e.what())}
//             );
//         }
//     }

  

//     case 4:  // 注释
//         return Result<QVariant, RichError>(
//             QVariant(QString::fromStdString(node->m_dataBlock->description))
//         );
        
//     default:
//         return Result<QVariant, RichError>(
//             RichError{QString("Column index out of range: column=%1")
//                       .arg(index.column())
//                       .toStdString()}
//         );
//     }
// }

Result<bool, RichError> OPCUADataBlock::isArray(OPCUAModernDataStruct &data_var) {
  if(data_var.is_array)
  {
    return Result<bool, RichError>(data_var.is_array);
  }
  else
  {
    return Result<bool, RichError>(RichError{"the var is not array element"});
  }
}


//OPCUADataReader-------------------------------------------------------------
// ==================== 构造函数 ====================

OPCUADeviceReader::OPCUADeviceReader(const std::string& identifier)
    : m_identifier(identifier) {
}

// ==================== 读取数据方法 ====================

Result<bool, RichError> OPCUADeviceReader::ReadDataFromPLC(OPCUADataBlock* data) {
  return batchReadOPCUAOPCUADataBlock_FromPLC(data);
}

Result<bool, RichError>
OPCUADeviceReader::batchReadOPCUAOPCUADataBlock_FromPLC(OPCUADataBlock *data) {
  bool success = true;
  //  CLEAR ELEMEMT EXISTED BEFORE
  m_opcUA->PrepareBatchRead(data->getVariabeDataVector());

  //  read data from PLC
  auto read_result = (m_opcUA->Read_UA_Variant_From_PLC());
  if (read_result.is_fail()) {
    m_opcUA->Clear_Read_Respondse();
    return Result<bool, RichError>(read_result);
  }

  //  store data into data_pointer
  int index = 0;
  for (auto &var : data->getVariabeDataVector()) {
    std::string array_member_full_path =
        var.variable_nodeID + "[" + std::to_string(0) + "]";
    if (data->isArray(var).is_success() &&
        var.variable_full_path != array_member_full_path) {
      //  meet the condition , mean the element is passed element of array , need skip it
      ++index;
      continue;
    }

    //  check the element if array member or normal scalar
    if (data->isArray(var).is_success()) {
      Result<bool, RichError> result = (m_opcUA->Set_Read_UA_Array(
          data->getVariabeDataVector(), var, index));
      if (result.is_fail()) {
        m_opcUA->Clear_Read_Respondse();
        return result;
      }

    } else {
      //  deal normal scalar condition
      Result<bool, RichError> result = (m_opcUA->Set_UA_To_Read_Normal_Scalar(
          var.data_type_enum, *var.data_pointer, index));
      if (result.is_fail()) {
        m_opcUA->Clear_Read_Respondse();
        return result;
      }
    }

    //  clear resource
    ++index;
  }

  //  clear batch reader variant
  m_opcUA->Clear_Read_Respondse();

  if (success) {
    return Result<bool, RichError>(true);
  } else {
    return Result<bool, RichError>(RichError("read variable failed"));
  }
}

// ==================== 写入数据方法 ====================

Result<bool, RichError>
OPCUADeviceReader::WriteDataToPLC(OPCUADataBlock *data) {
  if (!validateDataBlock(data)) {
    return RichError("Invalid data block pointer");
  }

  { return batchWriteSOPCUABlock_ToPLC(data); }
}

Result<bool, RichError>
OPCUADeviceReader::batchWriteSOPCUABlock_ToPLC(OPCUADataBlock *data) {
  if (!validateDataBlock(data)) {
    return Result<bool,RichError> ("Invalid data block pointer");
  }
  else
  {
    return Result<bool,RichError> (true);
  }

  // {
  //   auto result = m_opcUA->ensureConnection();
  //   if (result.is_fail()) {
  //     return Result<bool, RichError>(result);
  //   }
  //   m_opcUA->PrepareBatchWrite(data->getVariabeDataVector());

  //   int index = 0;
  //   for (auto &var : data->getVariabeDataVector()) {
  //     {
  //       auto result = m_opcUA->batchSet_Normal_To_Write_UA_Scalar(
  //           var,index);
  //       //  MEAN THE ELEMENT IS SCALAR ELEMENT
  //       // auto result = m_opcUA->batchSet_Normal_To_Write_UA_Scalar(
  //       //     var, data->getVariableDataBuffer(), index);
  //       if (result.is_fail()) {
  //         return Result<bool, RichError>(result);
  //       }
  //     }

  //     ++index;
  //     // clear write resource
  //   }

  //   auto writeResult = m_opcUA->batchWrite();
  //   if (writeResult.is_fail()) {
  //     m_opcUA->Clear_Write_Respondse();
  //     return Result<bool, RichError>(writeResult);
  //   }

  //   m_opcUA->Clear_Write_Respondse();
  //   return Result<bool, RichError>(true);
  // }
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
    
    auto result = m_opcUA->reconnect(5, 1000);
    if (result.is_fail()) {
        std::cout << "OPCUA Connect fail , reason : " << result.unwrap_err().what() << std::endl;
        return false;
    } else {
        std::cout << "OPCUA Connect successfully \n";
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
    std::cerr << "[OPCUADeviceReader::" << function << "] Error: " << error << std::endl;
}

void OPCUADeviceReader::logInfo(const std::string& message) const {
    std::cout << "[OPCUADeviceReader] Info: " << message << std::endl;
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

// ==================== respond external request about variable ====================

// std::vector<OPCUAModernDataStruct>& OPCUADataBlockModel::getModelItemVecotr() {
//     if (m_OPCUADataBlock) {
//         return m_OPCUADataBlock->getVariabeDataVector();
//     }
//     // 返回一个静态空向量作为 fallback（需要谨慎使用）
//     static std::vector<OPCUAModernDataStruct> empty_vector;
//     return empty_vector;
// }

// ==================== set internal member function ====================

void OPCUADataBlockModel::setOPCUADataBlock(std::shared_ptr<OPCUADataBlock> &block) {
    beginResetModel(); // 告诉 View 准备完全重置
    m_OPCUADataBlock = block;
    buildTree();
    printTreeNode(m_rootNode.get());
    endResetModel(); // View 会自动重新读取所有数据
    // 在你的代码中调用
    // simulateTreeViewCalls();
}

void OPCUADataBlockModel::simulateTreeViewCalls() {
  qDebug() << "\n=== Simulating TreeView Calls ===";

  // 1. 获取顶层索引
  QModelIndex rootIdx;
  int topRows = this->rowCount(rootIdx);
  qDebug() << "Top rows:" << topRows;

  for (int row = 0; row < topRows; row++) {
    QModelIndex topIdx = this->index(row, 0, rootIdx);
    qDebug() << "\nTop node[" << row << "]:" << this->data(topIdx).toString();

    // 2. 检查是否有子节点（View 会调用这个）
    bool hasKids = this->hasChildren(topIdx);
    qDebug() << "  hasChildren:" << hasKids;

    // 3. 如果有子节点，获取它们
    if (hasKids) {
      int childRows = this->rowCount(topIdx);
      qDebug() << "  child count:" << childRows;

      for (int childRow = 0; childRow < childRows && childRow < 5; childRow++) {
        QModelIndex childIdx = this->index(childRow, 0, topIdx);
        qDebug() << "    child[" << childRow
                 << "]:" << this->data(childIdx).toString()
                 << "hasChildren:" << this->hasChildren(childIdx);
      }
    }
  }
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

  return createIndex(row, column, parentNode->children[row]);
}

QModelIndex OPCUADataBlockModel::parent(const QModelIndex &child) const {
  if (!child.isValid())
    return QModelIndex();

  TreeNode *childNode = static_cast<TreeNode *>(child.internalPointer());
  TreeNode *parentNode = childNode ? childNode->parent : nullptr;

  // 没有父节点，或父节点是根节点 → 返回无效索引
  if (!parentNode || parentNode == m_rootNode.get())
    return QModelIndex();

  // 获取父节点在其父节点（祖父节点）中的行号
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
  return node ? node->children.size() : 0;
}

int OPCUADataBlockModel::columnCount(const QModelIndex &parent) const {
  return 5; // 名称、类型、值、读取权限、注解
}

bool OPCUADataBlockModel::hasChildren(const QModelIndex &parent) const {
  qDebug() << "=== hasChildren() called ===";

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
  qDebug() << "  node:" << node->displayName
           << "is container:" << (node->m_dataBlock == nullptr)
           << "children count:" << node->children.size()
           << "hasChildren:" << hasChild;

  return hasChild;
}

// QVariant OPCUADataBlockModel::data(const QModelIndex &index, int role) const {
//   if (!index.isValid())
//     return QVariant();

//   if (index.row() >= m_OPCUADataBlock->getVariableVectorSize()) {
//     return QVariant();
//   }

//   // 获取数据（注意：这里需要非const引用，因为可能需要在DisplayRole中读取）
//   std::vector<OPCUAModernDataStruct> &items = m_OPCUADataBlock->getVariabeDataVector();
//   OPCUAModernDataStruct &item = items[index.row()];

//   TreeNode *node = static_cast<TreeNode *>(index.internalPointer());

//   // EditRole - 返回原始数据用于编辑
//   if (role == Qt::EditRole) {
//     switch (index.column()) {
//     case 0: // Name 列
//       return QString::fromStdString(node->m_dataBlock->variable_name);

//     case 1: // Data Type 列（只读）
//       return static_cast<int>(node->m_dataBlock->data_type_enum);

//     case 2: // limit of authority 列（只读）
//       return node->m_dataBlock->access_level;

//     case 3: // Value 列 - 根据数据类型返回原始值
//       switch (node->m_dataBlock->data_type_enum) {
//       case S7DataType::BOOL:
//         return node->m_dataBlock->data_pointer->get<bool>();
//       case S7DataType::BYTE:
//         return node->m_dataBlock->data_pointer->get<uint8_t>();
//       case S7DataType::INT:
//         return node->m_dataBlock->data_pointer->get<int16_t>();
//       case S7DataType::DINT:
//         return node->m_dataBlock->data_pointer->get<int32_t>();
//       case S7DataType::WORD:
//         return node->m_dataBlock->data_pointer->get<uint16_t>();
//       case S7DataType::DWORD:
//         return node->m_dataBlock->data_pointer->get<uint32_t>();
//       case S7DataType::UDINT:
//         return node->m_dataBlock->data_pointer->get<uint32_t>();
//       case S7DataType::REAL:
//         return node->m_dataBlock->data_pointer->get<float>();
//       case S7DataType::STRING:
//         return QString::fromStdString(node->m_dataBlock->data_pointer->get<std::string>());
//       default:
//         return node->m_dataBlock->data_pointer->get<QVariant>();
//       }

//     case 4: // Comment 列
//       return QString::fromStdString(node->m_dataBlock->description);

//     default:
//       return QVariant();
//     }
//   }

//   // DisplayRole - 返回格式化的显示数据
//   if (role == Qt::DisplayRole) {
//     switch (index.column()) {
//     case 0: // Name 列
//       return QString::fromStdString(node->m_dataBlock->variable_name);

//     case 1: // Data Type 列
//     {
//       auto it = S7DataTypeToString.find(node->m_dataBlock->data_type_enum);
//       if (it != S7DataTypeToString.end()) {
//         return QString::fromStdString(it->second);
//       }
//       return QString::fromStdString("UNKNOWN");
//     }

//     case 2: // limit of authority 列
//       return node->m_dataBlock->access_level;

//     case 3: // Value 列 - 格式化显示
//       switch (node->m_dataBlock->data_type_enum) {
//       case S7DataType::BOOL:
//         return node->m_dataBlock->data_pointer->get<bool>() ? "true" : "false";

//       case S7DataType::BYTE:
//         return QString::number(node->m_dataBlock->data_pointer->get<uint8_t>());

//       case S7DataType::INT:
//         return QString::number(node->m_dataBlock->data_pointer->get<int16_t>());

//       case S7DataType::DINT:
//         return QString::number(node->m_dataBlock->data_pointer->get<int32_t>());

//       case S7DataType::WORD:
//         return QString::number(node->m_dataBlock->data_pointer->get<uint16_t>());

//       case S7DataType::DWORD:
//         return QString("0x%1").arg(node->m_dataBlock->data_pointer->get<uint32_t>(), 8, 16,
//                                    QChar('0'));

//       case S7DataType::UDINT:
//         return QLocale(QLocale::English)
//             .toString(node->m_dataBlock->data_pointer->get<uint32_t>());
//         // 结果示例： "1,234,567" 而不是 "1234567"

//       case S7DataType::REAL:
//         return QString::number(node->m_dataBlock->data_pointer->get<float>(), 'f', 6);

//       case S7DataType::STRING:
//         return QString::fromStdString(node->m_dataBlock->data_pointer->get<std::string>());

//       default:
//         return QString::fromStdString(node->m_dataBlock->data_pointer->get<std::string>());
//       }

//     case 4: // Comment 列
//       return QString::fromStdString(node->m_dataBlock->description);

//     default:
//       return QVariant();
//     }
//   }

//   return QVariant();
// }

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
            std::cout << "Saved REAL value: " << savedValue
                      << ", expected: " << floatValue << std::endl;
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
    
    if (index.column() == 4) { // Comment 列
        node->m_dataBlock->description = value.toString().toStdString();
        emit dataChanged(index, index, {Qt::DisplayRole});
        return true;
    }
    
    // Data Block Number 和 OffReset_Value 列通常只读，不允许编辑
    if (index.column() == 0 || index.column() == 1 || index.column() == 2) {
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
  if (node->m_dataBlock && index.column() == 1) {
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
    // 查找最后一个点前面的双引号内容
    size_t lastDotPos = input.rfind('.');
    if (lastDotPos == std::string::npos) {
      return "";
    }

    // 在最后一个点之前查找双引号内容
    std::string beforeLastDot = input.substr(0, lastDotPos);
    // 使用普通字符串，需要双重转义
    std::regex pattern("\"([^\"]+)\"$");
    std::smatch match;

    if (std::regex_search(beforeLastDot, match, pattern)) {
      return match[1];
    }

    return "";
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
        qWarning() << "Failed to set variable value:" << e.what();
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

  return createIndex(row, column, node); // 存储节点指针作为内部ID
}

void OPCUADataBlockModel::buildTree(TreeNode* parent) {
  if(!m_rootNode)
  {
    m_rootNode = std::make_shared<TreeNode>();
    m_rootNode->displayName = "Root";
  }

  for(auto &element : m_OPCUADataBlock->getVariabeDataVector())
  {
    //  special node skip it
    if (element.filter_reason != "" ||
        m_parentNodeIDMap.find(QString::fromStdString(element.variable_name)) !=
            m_parentNodeIDMap.end()) {
      continue;
    }
    TreeNode *parent = nullptr;

    std::string parentName{};
    //  ensure the parent node ID of element in Map
    parentName = getParentName(element);
    // DB....Test,Motor,Array_Template
    auto it = m_parentNodeIDMap.find(QString::fromStdString(parentName));
    if(it != m_parentNodeIDMap.end() )
    {
      //  find it !
      parent = it.value();
    }
    else
    {
      //  can not find it ! mean we need build parent Node in Map fisrt
      parent = createPlaceholderNode(parentName);
    }
    
    TreeNode *varNode = new TreeNode();
    if(element.array_dimension == -1)
    {
      varNode->m_dataBlock = &element;
    }
    //  Array_Template do not need set m_dataBlock
    varNode->parent = parent;
    varNode->displayName = QString::fromStdString(element.variable_name);

    //  build parent-son relationship
    parent->children.append(varNode);
    m_parentNodeIDMap[varNode->displayName] = varNode;
  }
}

TreeNode *
OPCUADataBlockModel::createPlaceholderNode(const std::string &parentName) {
  if (m_parentNodeIDMap.find(QString::fromStdString(parentName)) !=
      m_parentNodeIDMap.end()) {
        //  mean the parent node has exist 
        return nullptr;
  }

  TreeNode *placeholder = new TreeNode();
  placeholder->displayName = QString::fromStdString(parentName);
  m_parentNodeIDMap[placeholder->displayName] = placeholder;

  //  get parent data block
  auto parentPointer = findNode(parentName);
  if (parentPointer) {
    //  generate parent node by parent data block when the parent node do not exist in parent map
    auto gradParentPointer = createPlaceholderNode(getParentName(*parentPointer));
    if(!gradParentPointer)
    {
      //  mean the gradparent node has exist , need find by map
      placeholder->parent = m_parentNodeIDMap[QString::fromStdString(
          getParentName(*parentPointer))];
    }
    else
    {
      placeholder->parent = gradParentPointer;
    }
  }
  else
  {
    //  find node result -> nullptr means the node is root node (DB......)
    placeholder->parent = m_rootNode.get();
  }

  placeholder->parent->children.append(placeholder);
  return placeholder;
}

OPCUAModernDataStruct * 
OPCUADataBlockModel::findNode(const std::string &targetName) {
  for(auto &element : m_OPCUADataBlock->getVariabeDataVector()) 
  {
    if(element.variable_name == targetName)
    {
      return &element;
    }
  }
  return nullptr;
}

std::string
OPCUADataBlockModel::getParentName(const OPCUAModernDataStruct &element) {
  std::string parentName{};
  if (element.variable_name.find("[") != std::string::npos) {
    parentName = extractLastPartWithoutIndexForArray(element.parent_node_id);
  } else {
    parentName = extractLastPartWithoutIndexForNormal(element.variable_nodeID);
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
  for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
    int level = *it;
    if (level == 0)
      continue;

    const QList<QModelIndex> &indices = levelMap[level];
    for (const QModelIndex &idx : indices) {
      if (idx.isValid()) {
        // 只更新特定列
        QModelIndex colIndex = index(idx.row(), targetColumn, idx.parent());
        if (colIndex.isValid()) {
          emit dataChanged(colIndex, colIndex, {Qt::DisplayRole, Qt::EditRole});
        }
      }
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
  auto result = this->add_OPCUADataBlock_from_OPCUADataBlockDefinition(content,ip_Address);

  if (result.is_success()) {
    emit requestSaveOPCUADataBlock(std::move(result.unwrap_returnLeftValue()));
    return Result<bool, RichError>(true);
  } else {
    return Result<bool, RichError>(RichError{result.unwrap_err()});
  }
}

Result<std::shared_ptr<OPCUAParseResult>, RichError>
OPCUADataBlockBuilder::add_OPCUADataBlock_from_OPCUADataBlockDefinition(
    const QString &file_path, const std::string &ip_Address) {
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
QWidget* OPCUADataDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                                      const QModelIndex& index) const {
    if (index.column() == 3) { // Value 列
        // 获取第1列（数据类型列）
        QModelIndex typeIndex = index.sibling(index.row(), 1);
        // 读取数据类型
        S7DataType dataType = static_cast<S7DataType>(
            typeIndex.data(Qt::EditRole).toInt());

        switch (dataType) {
        case S7DataType::BOOL:
            return createBoolEditor(parent);
        case S7DataType::BYTE:
            return createNumberEditor(parent, S7DataType::BYTE);
        case S7DataType::INT:
            return createNumberEditor(parent, S7DataType::INT);
        case S7DataType::DINT:
            return createNumberEditor(parent, S7DataType::DINT);
        case S7DataType::WORD:
            return createNumberEditor(parent, S7DataType::WORD);
        case S7DataType::DWORD:
            return createHexEditor(parent,dataType);
        case S7DataType::UDINT:
            return createHexEditor(parent,dataType);
        case S7DataType::REAL:
            return createfloatEditor(parent);
        case S7DataType::STRING:
            return createStringEditor(parent);

        default:
            return createNumberEditor(parent, dataType);
        }
    }
    
    // 其他列使用默认编辑器
    return QStyledItemDelegate::createEditor(parent, option, index);
}

void OPCUADataDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const {
    QVariant value = index.data(Qt::EditRole);
    
    if (index.column() == 3) {
        QModelIndex typeIndex = index.sibling(index.row(), 1);
        S7DataType dataType = static_cast<S7DataType>(
            typeIndex.data(Qt::EditRole).toInt());

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
        QModelIndex typeIndex = index.sibling(index.row(), 1);
        S7DataType dataType = static_cast<S7DataType>(
            typeIndex.data(Qt::EditRole).toInt());
        
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
  // 复制选项
  QStyleOptionViewItem opt = option;

  // 设置文本对齐方式为居中
  opt.displayAlignment = Qt::AlignCenter;

  // 调用基类绘制
  QStyledItemDelegate::paint(painter, opt, index);
}

//OPCUAView-----------------------------------------------------------------
OPCUADataBlockView::OPCUADataBlockView(QWidget *parent) {
  setupUI();
  initializeConnection();
};

OPCUADataBlockView::~OPCUADataBlockView() {
  std::cout << "~OPCUADataBlockView call" << std::endl;
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
    // m_tableView = new QTableView();
    // m_tableView->setAlternatingRowColors(true);
    // m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    // m_tableView->setEditTriggers(QAbstractItemView::EditKeyPressed);
    // m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // m_tableView->setSortingEnabled(true);
    treeView = new QTreeView;
    treeView->setAlternatingRowColors(true);
    treeView->setAnimated(true);  // 展开/折叠动画
    treeView->setIndentation(20); // 设置缩进
    treeView->expandAll(); // 展开所有节点，验证是否都能正常显示

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

void OPCUADataBlockView::initializeConnection()
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

  // connect(m_tableView, &QTableView::doubleClicked, this,
  //         &OPCUADataBlockView::onRowdoubleClicked);
  connect(treeView, &QTreeView::doubleClicked, this,
          &OPCUADataBlockView::onRowdoubleClicked);
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


//OPCUAController----------------------------------------------------------
void OPCUADataBlockController::initialize(Scope *scope) {
  if (!scope) {
    return;
  } else {
    std::shared_ptr<OPCUADataBlockModel> model = scope->getShared<OPCUADataBlockModel>();
    if (!model) {
      std::cout << "OPCUADataBlockModel getSharedPtr is fail " << std::endl;
    } else {
      m_model = std::move(model);
    }

    std::shared_ptr<OPCUADeviceReader> reader = scope->getShared<OPCUADeviceReader>();
    if (!reader) {
      std::cout << "OPCUADeviceReader getSharedPtr is fail " << std::endl;
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
    std::cout<<"initialize vie fail : view is nullptr "<<std::endl;
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
  connect(m_OPCUADataBlockBuild.get(), &OPCUADataBlockBuilder::requestSaveOPCUADataBlock, this,
          &OPCUADataBlockController::onSaveOPCUADataBlock);
  // connect(m_view, &OPCUADataBlockView::requestFile, this,
  //         &OPCUADataBlockController::onbuildDataBlockFromDBFile);
  connect(m_view, &OPCUADataBlockView::requestRefresh, this,
          &OPCUADataBlockController::onViewReadRequested);
  connect(m_view, &OPCUADataBlockView::requestWrite, this,
          &OPCUADataBlockController::onViewWriteRequested);
}

void OPCUADataBlockController::onViewReadRequested() {
  //    notify model update
  auto result = m_reader->ReadDataFromPLC(m_OPCUADataBlock.get());
  if(result.is_fail())
  {
    std::cout<<"ViewReadRequest is fail and error : "<<result.unwrap_err().what()<<std::endl;
    return;
  }
  m_model->batchSetDataForOPCUA();
};

void OPCUADataBlockController::onViewWriteRequested() {
  //  update buffer from data_pointer in littleEndian
  auto result = m_reader->batchWriteSOPCUABlock_ToPLC(m_OPCUADataBlock.get());
  if (result.is_fail()) {
    std::cout << "onViewWriteRequested fail : " << result.unwrap_err().what()
              << std::endl;
  }
};

void OPCUADataBlockController::onSaveOPCUADataBlock(const std::shared_ptr<OPCUAParseResult> &parseResult) {
  if (parseResult) {
    //  update lastest data block
    m_OPCUADataBlock = std::make_shared<OPCUADataBlock>();
    m_OPCUADataBlock->setParseResult(parseResult);
    m_model->setOPCUADataBlock(m_OPCUADataBlock);
    m_view->setModel();
    m_view->setDelegate();
  } else {
    std::cout << "onSaveOPCUADataBlock : dataBlock is nullptr" << std::endl;
  }
}

//OPCUAView-------------------------------------------------------------------------
bool
OPCUADataBlockManager::createTableView(const QString &ipAddress,
                                       const QString &filePath) {
  QFileInfo info{filePath};
  OPCUADataBlockKey key(ipAddress, info.fileName());

  // 创建新的 DataBlock
  OPCUADataBlockContext *context =
      createOPCUADataBlockContext(ipAddress, info.fileName());
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
bool OPCUADataBlockManager::buildOPCUAConnect(const QString &ipAddress, int nameSpace,
                                      int port,const std::string &connectWay) {
  auto context = getSpecialReader(ipAddress.toStdString());

  if (!context) {
    emit errorOccurred(ipAddress, "", " not found");
    auto reader = std::make_shared<OPCUADeviceReader>();
    bool result =
        reader->onRequestBuildOPCUA(ipAddress.toStdString(), nameSpace, port);
    if (result) {
      m_readerVector.push_back(std::move(reader));
    } else {
      return false;
    }

    return true;
  }

  std::cout << ipAddress.data() << " has exist " << std::endl;
  return context->onRequestOPCUACheckConnect();
}

bool OPCUADataBlockManager::checkConnectToDevice(const QString& ipAddress,
                                      const std::string& connectWay)
{
    auto client = getClient(ipAddress,connectWay);
    if(!client)
    {
      return false;
    }
    else
    {
      {
        return client->onRequestOPCUACheckConnect();
      }
    }
}

// ========== 辅助函数 ==========
OPCUADataBlockContext *
OPCUADataBlockManager::getOPCUADataBlockContext(const QString &ipAddress,
                                      const QString &filePath) {
  QFileInfo info{filePath};
  OPCUADataBlockKey key(ipAddress, info.fileName());
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
        //  create dataBlock for filling contextt into model
        return context->controller->onbuildOPCUADataBlockFromXMLFile(
            filePath, ip_Address.toStdString());
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