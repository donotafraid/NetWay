#pragma once

#include "PLC/OPC_UA.h"
#include "PLC/OPCUABrowser.h"
#include "PLC/OPCUACovert.h"
#include "PLC/TransformS7AndOPCUA.h"
#include "PLC_Collector/IDataNode.h"

class OPCUADataBlock;
class SpecialTreeView;

struct TreeNode {
  std::shared_ptr<IDataNode> m_data = nullptr;
  std::weak_ptr<TreeNode> parent ;
  QList<std::shared_ptr<TreeNode>> children;
  QString displayName = "";
  bool isExpanded = false;

  std::shared_ptr<TreeNode> getChildNode(int index){
    return children[index];
  }
};

// 职责：将OPCUADataBlock适配为Qt的Model
class OPCUADataBlockModel : public QAbstractTableModel {
  Q_OBJECT

public:
  // 构造函数
  explicit OPCUADataBlockModel(QObject *parent = nullptr);

  // 析构函数
  ~OPCUADataBlockModel() override;

  // transform external request
  void sendMessage()
  {
    if (m_OPCUAStructVec ) {
      m_OPCUAStructVec->writeValueToPLC();
    } else {
      // 处理空指针或空容器的情况
      spdlog::info("sendMessage:Vec is empty or Vec is nullptr");
    }
  };
  void getMessage() {
    if (m_OPCUAStructVec ) {
      m_OPCUAStructVec->readValueFromPLC();
    } else {
      // 处理空指针或空容器的情况
      spdlog::info("getMessage:Vec is empty or Vec is nullptr");
    }
  };

  //  set internal parmeter
  void setRootNode(std::shared_ptr<TreeNode> rootNode){m_rootNode = rootNode;};
  void setOPCUAStructVec(const std::shared_ptr<IDataNode> block);

  // respond external request about variable
  std::vector<OPCUAModernDataStruct> &getModelItemVecotr();
  QString getDisplayValue(const OPCUAModernDataStruct &var, int column) const;
  QString getTypeString(S7DataType type) const;
  TreeNode *getNodeByVisualRow(int visualRow) const;
  TreeNode *getRootNode();
  TreeNode *getGlobalNode(const int globalIndex) {
    TreeNode *Node = nullptr;
    Node = this->getNodeByVisualRow(globalIndex);
    return Node;
  }

  //  rebuild function
  void rebuildVisualRowMap();

  // QAbstractTableModel 接口
  QModelIndex index(int row, int column,
                    const QModelIndex &parent) const override;
  QModelIndex parent(const QModelIndex &child) const override;

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  bool hasChildren(const QModelIndex &parent = QModelIndex()) const override;

  // return data from internal Model
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;

  // set data into internal Model
  bool setData(const QModelIndex &index, const QVariant &value,
               int role = Qt::EditRole) override;

  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;

  Qt::ItemFlags flags(const QModelIndex &index) const override;

  // batch update function
  void batchSetDataForOPCUA();

  

signals:
  void requestOPCUADataBlockModified();

private:
  enum DataRole {
    // 名称、类型、偏移量、值,注解
    VariableName = 0,
    DataType,
    Limit,
    Value,
    Comment
  };
  
  std::shared_ptr<IDataNode> m_OPCUAStructVec = nullptr;
  std::shared_ptr<TreeNode> m_rootNode;
  QMap<int, TreeNode *> m_visualRowMap;         // 视觉行号 → 节点指针
  bool m_visualRowMapValid = false;

  // 辅助方法
  // 输入：数据节点指针 (TreeNode*)
  // 输出：QModelIndex
  QModelIndex indexFromNode(TreeNode *node, int column = 0) const;
  void buildTree(TreeNode* parent = nullptr);
  TreeNode *createPlaceholderNode(const std::string &parent_nodeID);
  IDataNode *findOPCUADataStruct(const std::string &parent_nodeID);

  void buildVisualRowMapRecursive(TreeNode *node, int &currentRow);
  void updateAllDataByLevel(int targetColumn);
  void collectIndicesByLevel(TreeNode *node, int level,
                             QMap<int, QList<QModelIndex>> &levelMap);
  // validation function
  bool isValidIndex(const QModelIndex &index) const;
  void printTreeNode(TreeNode *node, int depth = 0, bool isLast = true);
  
  std::string extractVariableNameWithoutIndex(const std::string &input); 
  std::pair<std::string, std::string>
  extractVariableNameWithIndex(const std::string &input); 
  std::string extractLastPartWithoutIndexForArray(const std::string &input); 
  
  int getChildSize(TreeNode *node) const  {
    int size = 1;
    if (node->children.size() == 0) {
      return 1;
    } else {
      for (auto &element : node->children) {
        size += getChildSize(element.get());
      }
    }
    return size;
  }

    // 辅助函数：在父节点中查找子节点索引（可以缓存优化）
  int findChildIndex(const std::shared_ptr<TreeNode> &parent,
                     const std::shared_ptr<TreeNode> &child) const {
    // 如果有缓存，可以直接返回
    // return parent->childIndexCache.value(child.get(), -1);

    return parent->children.indexOf(child);
  }
};

//  OPCUA Device Reader
class OPCUADeviceReader {
public:
    // 构造函数
    OPCUADeviceReader() = default;
    explicit OPCUADeviceReader(const std::string& identifier);
    ~OPCUADeviceReader() = default;
    
    // 禁止拷贝
    OPCUADeviceReader(const OPCUADeviceReader&) = delete;
    OPCUADeviceReader& operator=(const OPCUADeviceReader&) = delete;
    
    // 允许移动
    OPCUADeviceReader(OPCUADeviceReader&& other) noexcept = default;
    OPCUADeviceReader& operator=(OPCUADeviceReader&& other) noexcept = default;

    // 读取数据方法
    Result<bool, RichError> batchReadOPCUADataBlock_FromPLC(
        std::vector<OPCUAModernDataStruct> &dataVec);
    Result<bool, RichError> batchReadS7DataBlock_FromPLC(
        std::vector<OPCUAModernDataStruct> &dataVec);
    Result<int, RichError>
    meastureStringObjectLength(int startPostion, OPCUAModernDataStruct &var);

    // 写入数据方法
    Result<bool, RichError> batchWriteS7DataBlock_ToPLC(OPCUADataBlock *data);
    Result<bool, RichError> batchWriteS7DataBlock_ToPLC(std::vector<OPCUAModernDataStruct> &dataVec);
    Result<bool, RichError>
    batchWriteOPCUABlock_ToPLC(std::vector<OPCUAModernDataStruct> &dataVec);

    // get function
    Result<std::string, RichError> getIdentifier();
    
    // respond function
    bool onRequestBuildOPCUA(const std::string& ip_Address, int nameSpace, int port);
    bool onRequestOPCUACheckConnect() const;
    
    // S7 相关方法
    bool onRequestBuildS7(const std::string& ip_Address, int rack, int slot, int timeout);
    bool onRequestS7CheckConnect() const;
    
    // 重置连接
    void resetConnections();

private:
    OPCUADataCovert m_covert;
    std::unique_ptr<S7_Access> m_s7Acess = nullptr;
    std::unique_ptr<OPCUA_Access> m_opcUA = nullptr;
    std::vector<uint8_t> readbuffer;
    std::string m_identifier = "";
    
    // 辅助方法
    void logError(const std::string& function, const std::string& error) const;
    void logInfo(const std::string& message) const;
    
    // 数据转换辅助方法
    Result<bool, RichError> convertOPCUAToDataPointer(std::vector<OPCUAModernDataStruct> &dataVec );
    Result<bool, RichError> convertDataPointerToOPCUA(std::vector<OPCUAModernDataStruct> &dataVec );
};

// 职责：从定义构建OPCUADataBlock
class OPCUADataBlockBuilder : public QObject{
  Q_OBJECT
public:
    void initialize(Scope* scope);

    Result<std::vector<OPCUAModernDataStruct>, RichError>
    add_OPCUADataBlock_from_OPCUADataBlockDefinition(
        const QString &file_path, const std::string &ip_Address,const InputFormat &buildType);
    Result<std::vector<OPCUAModernDataStruct>, RichError>
    add_OPCUADataBlock_from_OPCUADataBlockDefinition(
        const OPCUADataBlockDefinition &data_block_definition,
        const std::string &ip_Address);
    Result<bool, RichError> add_variable_from_OPCUADataBlockDefinition(
        const S7XMLVariableDefinition &variable_definition,
        int data_block_number, const std::string &prefix,
        std::vector<OPCUAModernDataStruct> &m_variable_vector);

    //  update function
    Result<bool, RichError> updateTypeEnum(std::shared_ptr<OPCUAParseResult> &data);

    // build function
    std::shared_ptr<TreeNode> buildTree(std::vector<OPCUAModernDataStruct> &dataNodes);
    std::shared_ptr<TreeNode> createPlaceholderNode(std::shared_ptr<TreeNode> &rootNode,const std::string &parent_nodeID, QHash<QString, std::shared_ptr<TreeNode >> &m_parentNodeIDMap,std::vector<OPCUAModernDataStruct> &dataNodes);
    OPCUAModernDataStruct *findOPCUADataStruct(const std::string &parent_nodeID,std::vector<OPCUAModernDataStruct> &dataNodes);

    Result<bool, RichError> build(const QString &file_path,
                                  const std::string &ip_Address);
    Result<bool, RichError> build_CSV(const QString &file_path,
                                      const std::string &ip_Address);
    Result<bool, RichError> build_InlineBrowse(const std::string &ip_Address);
    Result<bool, RichError> build_S7(const OPCUADataBlockDefinition &content,
                                     const std::string &ip_Address);

  signals:
    void requestSaveOPCUAParseResult(
        const std::shared_ptr<IDataNode> &dataPointer,const std::shared_ptr<TreeNode> &rootNode);
    void requestSaveOPCUADataBlock(
        const std::shared_ptr<IDataNode> &dataPointer,const std::shared_ptr<TreeNode> &rootNode);

  private:
    std::string m_name;
    int m_dbNumber = 0;
    std::shared_ptr<OPCUADeviceReader> m_reader;

    // trim function
    Result<bool, RichError> isNumber(const std::string &s) {
      if (s.empty())
        return false;

      char *end = nullptr;
      // 使用 strtod 而不是 atof，因为 atof 无法检测错误
      strtod(s.c_str(), &end);

      // end 指向第一个未转换的字符
      return Result<bool, RichError>(end == s.c_str() + s.length());
    }

    Result<bool, RichError>
    calculate_data_block_size(std::vector<OPCUAModernDataStruct> &varVector) {
      //  RECORD LAST USEABLE POSITION
      int last_free_byte_offset = 0;
      //  RECORD LAST USED POSITION
      float last_used_var_byte_offset = -1;
      int current_bit_quality = 0;
      bool is_effective_calculate = true;
      S7DataType last_data_S7_type = S7DataType::UNKNOWN;
      for (auto &var : varVector) {
        if (var.data_type_enum == S7DataType::UNKNOWN) {
          continue;
        }
        is_effective_calculate &=
            calculate_variable_offset(
                var, last_free_byte_offset, current_bit_quality,
                last_used_var_byte_offset, last_data_S7_type)
                .is_success();
        last_used_var_byte_offset = var.bytes_offset;
      }

      if (is_effective_calculate) {
        return Result<bool, RichError>(true);
      } else {
        return Result<bool, RichError>(
            RichError("calculate data block size failed"));
      }
    }

    Result<bool, RichError> calculate_variable_offset(
        OPCUAModernDataStruct &var, int &last_free_byte_offset,
        int &current_bit_quality, float &last_used_var_byte_offset,
        S7DataType &last_data_S7_type) {
      // update the latest byte position to be allocated
      switch (var.data_type_enum) {
      case S7DataType::BOOL:
        //  CHECK THE EXISTENCE OF CONTINUOUS BOLL VARIABLE
        if (last_data_S7_type == S7DataType::BOOL) {
          //  IN S7 , VARIABLE_OFFSET DECIMAL PART IS EQUAL TO OR LESS THAN 7
          int used_decimal_part = last_used_var_byte_offset * 10 -
                                  std::floor(last_used_var_byte_offset) * 10;
          if (used_decimal_part < 7) {
            var.bytes_offset = last_used_var_byte_offset + 0.1;
            var.bit_offset = used_decimal_part + 1;
          } else {
            var.bytes_offset = std::floor(last_used_var_byte_offset) + 1;
            var.bit_offset = 0;
          }
          //  float PART CAN UPDATE BY VAR.BYTES_OFFSET , BECAUSE
          last_free_byte_offset = (std::floor(var.bytes_offset) + 1);
        } else {
          var.bytes_offset = last_free_byte_offset;
          var.bit_offset = 0;
          last_free_byte_offset = var.bytes_offset + 1;
        }
        last_data_S7_type = S7DataType::BOOL;
        //  AVOID THE RUNNING OF ASSIGNMENT OF IS_CONTINUOUS_BOOL TO FALSE
        return Result<bool, RichError>(true);
      case S7DataType::BYTE:
        last_data_S7_type = S7DataType::BYTE;
        var.bytes_offset = last_free_byte_offset;
        last_free_byte_offset = var.bytes_offset + 1;
        break;
      case S7DataType::INT:
        last_data_S7_type = S7DataType::INT;
        if (last_free_byte_offset % 2 == 0) {
          var.bytes_offset = last_free_byte_offset;
        } else {
          var.bytes_offset = last_free_byte_offset + 1;
        }
        last_free_byte_offset = var.bytes_offset + 2;
        break;
      case S7DataType::WORD:
        last_data_S7_type = S7DataType::WORD;
        if (last_free_byte_offset % 2 == 0) {
          var.bytes_offset = last_free_byte_offset;
        } else {
          var.bytes_offset = last_free_byte_offset + 1;
        }
        last_free_byte_offset = var.bytes_offset + 2;
        break;
      case S7DataType::DWORD:
        last_data_S7_type = S7DataType::DWORD;
        if (last_free_byte_offset % 2 == 0) {
          var.bytes_offset = last_free_byte_offset;
        } else {
          var.bytes_offset = last_free_byte_offset + 1;
        }
        last_free_byte_offset = var.bytes_offset + 4;
        break;
      case S7DataType::UDINT:
        last_data_S7_type = S7DataType::UDINT;
        if (last_free_byte_offset % 2 == 0) {
          var.bytes_offset = last_free_byte_offset;
        } else {
          var.bytes_offset = last_free_byte_offset + 1;
        }
        last_free_byte_offset = var.bytes_offset + 4;
        break;
      case S7DataType::DINT:
        last_data_S7_type = S7DataType::DINT;
        if (last_free_byte_offset % 2 == 0) {
          var.bytes_offset = last_free_byte_offset;
        } else {
          var.bytes_offset = last_free_byte_offset + 1;
        }
        last_free_byte_offset = var.bytes_offset + 4;
        break;
      case S7DataType::REAL:
        last_data_S7_type = S7DataType::REAL;
        if (last_free_byte_offset % 2 == 0) {
          var.bytes_offset = last_free_byte_offset;
        } else {
          var.bytes_offset = last_free_byte_offset + 1;
        }
        last_free_byte_offset = var.bytes_offset + 4;
        break;
      case S7DataType::STRING: {
        last_data_S7_type = S7DataType::STRING;
        var.bytes_offset = last_free_byte_offset;
        auto result =
            m_reader->meastureStringObjectLength(last_free_byte_offset, var);
        if (result.is_fail())
        {
          var.s7_data_type_length = 2;
        }
        else
        {
          var.s7_data_type_length = result.unwrap_returnLeftValue() + 2;
        }
        last_free_byte_offset += (var.s7_data_type_length) % 2
                                     ? (var.s7_data_type_length / 2 + 1) * 2
                                     : var.s7_data_type_length;
      } break;
      default:
        return Result<bool, RichError>(RichError("unknown data type"));
      }
      return Result<bool, RichError>(true);
    }

    std::string extractLastPartWithoutIndexForNormal(const std::string &input);
    std::string
    extractLastPartWithoutIndexForNormal(const OPCUAModernDataStruct &data);
};

//  responsibility : The logic behind the data presented 
class OPCUADataDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                         const QModelIndex& index) const override;
    
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;

    void setModelData(QWidget* editor, QAbstractItemModel* model,
                     const QModelIndex& index) const override;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;


  private:
    QWidget* createBoolEditor(QWidget* parent) const;
    QWidget* createfloatEditor(QWidget* parent) const;
    QWidget* createHexEditor(QWidget* parent,S7DataType &dataType) const;
    QWidget* createStringEditor(QWidget* parent) const;
    QWidget* createNumberEditor(QWidget* parent, S7DataType dataType) const;
};

//  responsibility : Provides UI controls and UI business logic
class OPCUADataBlockView : public QWidget {
  Q_OBJECT

public:
  explicit OPCUADataBlockView(QWidget *parent = nullptr);
  ~OPCUADataBlockView();

  // 设置Model
  void getModel(OPCUADataBlockModel *model);
  void getDelegate(OPCUADataDelegate *delegate);
  void setModel();
  void setDelegate();

  // 外部响应层接口 - 接收外部信号
  void onRefreshComplete(bool success, const QString &error);
  void onWriteComplete(bool success, const QString &error);
  void onConnectWithOPCUADataBlockView(QSplitter *splitter);

  //  get function
  QTreeView *getTableView();
  QWidget *getView();

  // 功能响应层接口 - 响应用户操作
  void refreshData() {}                                    // 刷新数据
  void writeData() { std::cout << "write Data !\n"; }      // 写入数据
  void exportToCSV() { std::cout << "export To CSV !\n"; } // 导出CSV
  void findValue() { std::cout << "find Value!\n"; }       // 查找值
  void filterByType() { std::cout << "filterByType !\n"; } // 按类型过滤
  void importFile();                                       // 导入配置文件

signals:
  // 发送给外部层的请求信号
  void requestRefresh();
  void requestWrite();
  void requestRead();
  void requestDataTypeChange();
  void requestFile(const QString &file_path);

private slots:
  void onRefreshClicked();
  void onWriteClicked();
  void onExportClicked() { std::cout << "Export clicked !\n"; }
  void onImportClicked() {
  }
  void selectCell(const QModelIndex &index);
  void onFindClicked() { std::cout << "Find Clicked !\n"; }
  void onFilterChanged(const QString &text) {
    std::cout << "Filter Changed !\n";
  }

  void onRowdoubleClicked(const QModelIndex &index);

  // 在程序启动时设置
  void printCallStack();

  bool eventFilter(QObject *obj, QEvent *event) override;

  //  trim function for Index
  QModelIndex findIndexByNode(TreeNode *node, int column) const;
  QModelIndex findIndexByY(const QModelIndex &parent, int &currentY,
                           int targetY, int targetX);
  QModelIndex mapVisualRowToModelIndex(const QModelIndex &parent,
                                       int targetGlobalRow, int targetCol,
                                       int &currentVisualRow);
  QModelIndex mapVisualRowToModelIndex(const QModelIndex &parent,
                                       int targetGlobalRow, int targetCol);
  int calculateColumnAtX(int x) const;
  int getDepth(const QModelIndex &index);

private:
  void setupUI();
  void showStatusMessage(const QString &message, bool isError = false) {}
  void updateButtonStates(bool isWorking = false) {}

  //  connect function
  void buildConnection();

  // QTreeView *treeView;
  SpecialTreeView *treeView;
  QPushButton *m_refreshBtn;
  QPushButton *m_writeBtn;
  QPushButton *m_exportBtn;
  QPushButton *m_importBtn;

  QLineEdit *m_searchEdit;
  QComboBox *m_typeFilter;
  QStatusBar *m_statusBar;
  QProgressBar *m_progressBar;

  QString m_currentIp;
  QString m_currentOPCUADataBlock;

  OPCUADataBlockModel *m_model = nullptr;
  OPCUADataDelegate *m_delegate = nullptr;

  int headerHeight = -1;
  QModelIndex lastSelectIdx;
  bool initializeStatus = false;
  QPoint lastClickLocation;

  // 辅助函数：在父节点中查找子节点索引（可以缓存优化）
  int findChildIndex(const std::shared_ptr<TreeNode> &parent,
                     const TreeNode *child) const {
                      int row = 0 ;
    for (auto element : parent->children) {
      if(element.get() == child)
      {
        return row;
      }
      ++row;
    }
    return -1;
  }
};

class SpecialTreeView : public QTreeView {
  Q_OBJECT

public:
  explicit SpecialTreeView(QWidget *parent = nullptr);
  ~SpecialTreeView();
  struct FindRelativeIndex {
    TreeNode *node;
    int col;

    // 默认构造函数
    FindRelativeIndex() : node(nullptr), col(0) {}

    // 带参构造函数
    FindRelativeIndex(TreeNode *n, int c) : node(n), col(c) {}

    // 拷贝构造函数
    FindRelativeIndex(const FindRelativeIndex &other)
        : node(other.node), col(other.col) {}

    // 移动构造函数
    FindRelativeIndex(FindRelativeIndex &&other) noexcept
        : node(other.node), col(other.col) {
      other.node = nullptr;
      other.col = 0;
    }

    // 拷贝赋值运算符
    FindRelativeIndex &operator=(const FindRelativeIndex &other) {
      if (this != &other) {
        node = other.node;
        col = other.col;
      }
      return *this;
    }

    // 移动赋值运算符
    FindRelativeIndex &operator=(FindRelativeIndex &&other) noexcept {
      if (this != &other) {
        node = other.node;
        col = other.col;
        other.node = nullptr;
        other.col = 0;
      }
      return *this;
    }

    // 析构函数（默认即可，因为指针不负责管理内存）
    ~FindRelativeIndex() = default;
  };

  //  get function
  void getModel(OPCUADataBlockModel *model);
  void getDelegate(OPCUADataDelegate *delegate);
  QTreeView *getTableView() { return this; }
  QWidget *getView() { return this; }
  int getGlobalIndex(){return globalRowPassager;}

  //  set function
  void setHeaderHeight(int height);

  // override part
  QModelIndex indexAt(const QPoint &pos) const override;
  
  // check click location
  int calculateColumnAtX(int x) const;

  // 缓存检查 - 返回 Result<QModelIndex, RichError>
  Result<QModelIndex, RichError> checkCache(const QPoint &pos) const {
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();

    if (std::abs(pos.y() - m_cachedPos.y()) <= 3 &&
        std::abs(pos.x() - m_cachedPos.x()) <= 3 &&
        (currentTime - m_lastCacheTime) < 1000) {
      m_hitCount++;
      if (m_hitCount % 10 == 0) {
        qDebug() << "indexAt cache hit:" << m_hitCount << "times";
      }
      return Result<QModelIndex, RichError>(m_cachedIndex);
    }
    return Result<QModelIndex, RichError>(RichError("Cache miss or expired"));
  }

  // 获取视觉行号
  Result<int, RichError> getGlobalRow(const QPoint &pos) const;

  // 获取节点
  Result<TreeNode *, RichError> getNodeByGlobalRow(int globalRow) const;
  // 获取列号
  Result<int, RichError> getColumnAtX(const QPoint &pos) const;

  // 查找索引
  Result<QModelIndex, RichError> findIndexByNode(const FindRelativeIndex &item) const;
  // 辅助函数：在父节点中查找子节点索引（可以缓存优化）
  int findChildIndex(const std::shared_ptr<TreeNode> &parent,
                     const TreeNode *child) const {
    int row = 0;
    for (auto element : parent->children) {
      if (element.get() == child) {
        return row;
      }
      ++row;
    }
    return -1;
  }

  // build external connection
  void buildConnect();
private:
  int headerHeight = -1;
  OPCUADataBlockModel *m_model = nullptr;
  OPCUADataDelegate *m_delegate = nullptr;
 
  mutable QPoint m_cachedPos;
  mutable QModelIndex m_cachedIndex;
  mutable qint64 m_lastCacheTime = 0;
  mutable int m_hitCount = 0;
  mutable int globalRowPassager = 0 ;
};

//  responsibility : Coordinating business interactions
class OPCUADataBlockController : public QObject {
    Q_OBJECT
    
public:
    explicit OPCUADataBlockController(QObject* parent = nullptr){};
    ~OPCUADataBlockController(){
      std::cout << "OPCUADataBlockController destory call" << std::endl;
    }
    
    void initialize(Scope* scope);
    void initializeView(OPCUADataBlockView* view);
    void buildConnection();
    
    // 处理外部请求
    void handleExternalRefreshRequest(){};
    void handleExternalWriteRequest(){};
    bool handleExternalOPCUAConnectRequest(const std::string &ip_Address, int nameSpace,
                                         int port) {
      return m_reader->onRequestBuildOPCUA(ip_Address,nameSpace,port);
    };
    bool handleExternalOPCUAConnectCheckRequest() {
      return m_reader->onRequestOPCUACheckConnect();
    };
    bool handleSwithView(const QString &ip_Address,const QString &OPCUADataBlockName){
      return true;
    }
    //  on function
    bool onbuildOPCUADataBlockFromXMLFile(const QString &file_path,
                                          const std::string &ip_Address) {

      // 1. 检查文件路径是否为空
      if (file_path.isEmpty()) {
        // 记录错误日志
        std::cerr << "Error: Empty file path" << std::endl;
        return false;
      }

      // 2. 检查文件是否存在
      if (!QFile::exists(file_path)) {
        std::cerr << "Error: File does not exist: " << file_path.toStdString()
                  << std::endl;
        return false;
      }

      // 3. 获取文件后缀名（转小写以进行不区分大小写的比较）
      QString suffix = QFileInfo(file_path).suffix().toLower();

      // 4. 根据后缀名路由到不同的处理函数
      if (suffix == "xml") {
        // 处理 XML 文件
        auto result = m_OPCUADataBlockBuild->build(file_path, ip_Address);
        if (result.is_fail()) {
          return false;
        } else {
          return true;
        }
      } else if (suffix == "csv") {
        // 处理 CSV 文件
        auto result = m_OPCUADataBlockBuild->build_CSV(file_path, ip_Address);
        if (result.is_fail()) {
          return false;
        } else {
          return true;
        }
      } else {
        // 不支持的文件格式
        std::cerr << "Error: Unsupported file format: " << suffix.toStdString()
                  << ". Supported formats: XML, CSV" << std::endl;
        return false;
      }
    }
    bool onbuildOPCUAInlineBrowse(const std::string &ip_Address) {
      auto result = m_OPCUADataBlockBuild->build_InlineBrowse(ip_Address);
      if (result.is_fail()) {
        return false;
      } else {
        return true;
      }
    }
    bool onbuildOPCUADataBlockFromDBFile(const QString &file_path,
                                         const std::string &ip_Address) {
      auto parse_result =m_DBParser.read_file_content(file_path.toStdString()).and_then(
          [this](std::string &fileContent) {
            return this->m_DBParser.parse(fileContent);
          });

      if (parse_result.is_success()) {
        m_OPCUADataBlockBuild->build_S7(parse_result.unwrap_returnLeftValue(),
                                ip_Address);
        return true;
      } else {
        std::cout << "error : SCL_Parser::Parser exist problem" << std::endl;
        return false;
      }
    }

  private slots:
    // 连接View的信号到Model的操作
    void onViewReadRequested();
    void onViewWriteRequested();
    void onViewDataTypeChangeRequested(){};
    void onViewExportRequested(){};
    void onViewFindRequested(){};

    //  OPCUADataBlock function
    void onSaveOPCUAParseResult(const std::shared_ptr<IDataNode> &dataBlockResult,const std::shared_ptr<TreeNode> &rootNode);
    void onSaveOPCUADataBlock(const std::shared_ptr<IDataNode> &dataBlock,const std::shared_ptr<TreeNode> &rootNode);

    // Model变化时的响应

  private:
    OPCUADataBlockView *m_view = nullptr;
    std::shared_ptr<OPCUADataBlockModel> m_model = nullptr;
    std::shared_ptr<OPCUADataBlockBuilder> m_OPCUADataBlockBuild = nullptr;
    std::shared_ptr<OPCUADeviceReader> m_reader = nullptr;


    std::shared_ptr<IDataNode> m_varVec;
    SCL_Parser m_DBParser;

    Result<bool, RichError> calculate_variable_offset(
        OPCUAModernDataStruct &var, int &last_free_byte_offset,
        int &current_bit_quality, float &last_used_var_byte_offset,
        S7DataType &last_data_S7_type) {
      // update the latest byte position to be allocated
      switch (var.data_type_enum) {
      case S7DataType::BOOL:
        //  CHECK THE EXISTENCE OF CONTINUOUS BOLL VARIABLE
        if (last_data_S7_type == S7DataType::BOOL) {
          //  IN S7 , VARIABLE_OFFSET DECIMAL PART IS EQUAL TO OR LESS THAN 7
          int used_decimal_part = last_used_var_byte_offset * 10 -
                                  std::floor(last_used_var_byte_offset) * 10;
          if (used_decimal_part < 7) {
            var.bytes_offset = last_used_var_byte_offset + 0.1;
            var.bit_offset = used_decimal_part + 1;
          } else {
            var.bytes_offset = std::floor(last_used_var_byte_offset) + 1;
            var.bit_offset = 0;
          }
          //  float PART CAN UPDATE BY VAR.BYTES_OFFSET , BECAUSE
          last_free_byte_offset = (std::floor(var.bytes_offset) + 1);
        } else {
          var.bytes_offset = last_free_byte_offset;
          var.bit_offset = 0;
          last_free_byte_offset = var.bytes_offset + 1;
        }
        last_data_S7_type = S7DataType::BOOL;
        //  AVOID THE RUNNING OF ASSIGNMENT OF IS_CONTINUOUS_BOOL TO FALSE
        return Result<bool, RichError>(true);
      case S7DataType::BYTE:
        last_data_S7_type = S7DataType::BYTE;
        var.bytes_offset = last_free_byte_offset;
        last_free_byte_offset = var.bytes_offset + 1;
        break;
      case S7DataType::INT:
        last_data_S7_type = S7DataType::INT;
        if (last_free_byte_offset % 2 == 0) {
          var.bytes_offset = last_free_byte_offset;
        } else {
          var.bytes_offset = last_free_byte_offset + 1;
        }
        last_free_byte_offset = var.bytes_offset + 2;
        break;
      case S7DataType::WORD:
        last_data_S7_type = S7DataType::WORD;
        if (last_free_byte_offset % 2 == 0) {
          var.bytes_offset = last_free_byte_offset;
        } else {
          var.bytes_offset = last_free_byte_offset + 1;
        }
        last_free_byte_offset = var.bytes_offset + 2;
        break;
      case S7DataType::DWORD:
        last_data_S7_type = S7DataType::DWORD;
        if (last_free_byte_offset % 2 == 0) {
          var.bytes_offset = last_free_byte_offset;
        } else {
          var.bytes_offset = last_free_byte_offset + 1;
        }
        last_free_byte_offset = var.bytes_offset + 4;
        break;
      case S7DataType::UDINT:
        last_data_S7_type = S7DataType::UDINT;
        if (last_free_byte_offset % 2 == 0) {
          var.bytes_offset = last_free_byte_offset;
        } else {
          var.bytes_offset = last_free_byte_offset + 1;
        }
        last_free_byte_offset = var.bytes_offset + 4;
        break;
      case S7DataType::DINT:
        last_data_S7_type = S7DataType::DINT;
        if (last_free_byte_offset % 2 == 0) {
          var.bytes_offset = last_free_byte_offset;
        } else {
          var.bytes_offset = last_free_byte_offset + 1;
        }
        last_free_byte_offset = var.bytes_offset + 4;
        break;
      case S7DataType::REAL:
        last_data_S7_type = S7DataType::REAL;
        if (last_free_byte_offset % 2 == 0) {
          var.bytes_offset = last_free_byte_offset;
        } else {
          var.bytes_offset = last_free_byte_offset + 1;
        }
        last_free_byte_offset = var.bytes_offset + 4;
        break;
      case S7DataType::STRING: {
        last_data_S7_type = S7DataType::STRING;
        var.bytes_offset = last_free_byte_offset;
        auto result = meastureStringObjectLength(last_free_byte_offset,var);
        if (result.is_fail())
        {
          var.s7_data_type_length = 2;
        }
        else
        {
          var.s7_data_type_length = result.unwrap_returnLeftValue() + 2;
        }
        last_free_byte_offset += (var.s7_data_type_length) % 2
                                     ? (var.s7_data_type_length / 2 + 1) * 2
                                     : var.s7_data_type_length;
      } break;
      default:
        return Result<bool, RichError>(RichError("unknown data type"));
      }
      return Result<bool, RichError>(true);
    }

    Result<int, RichError>
    meastureStringObjectLength(int last_free_byte_offset,OPCUAModernDataStruct &var) {
      return Result<int, RichError>(m_reader->meastureStringObjectLength(last_free_byte_offset,var));
    }
};

struct OPCUADataBlockKey {
    QString ipAddress;
    QString OPCUADataBlockName;
    
    OPCUADataBlockKey() = default;
    OPCUADataBlockKey(const QString& ip, const QString& block) 
        : ipAddress(ip), OPCUADataBlockName(block) {}
    
    bool operator==(const OPCUADataBlockKey& other) const {
        return ipAddress == other.ipAddress && 
               OPCUADataBlockName == other.OPCUADataBlockName;
    }
    
    bool operator<(const OPCUADataBlockKey& other) const {
        if (ipAddress != other.ipAddress) 
            return ipAddress < other.ipAddress;
        return OPCUADataBlockName < other.OPCUADataBlockName;
    }
    
    QString toString() const {
        return QString("%1@%2").arg(OPCUADataBlockName, ipAddress);
    }
};

inline uint qHash(const OPCUADataBlockKey& key, uint seed) {
    return qHash(key.ipAddress, seed) ^ qHash(key.OPCUADataBlockName, seed);
}

struct OPCUADataBlockContext {
  OPCUADataBlockKey key;
  QWidget m_controllWidget;
  OPCUADataBlockView *view = nullptr;      // 独立的View
  std::shared_ptr<OPCUADataBlockController> controller;
  std::shared_ptr<OPCUADataBlockModel> model;    // 独立的Model
  std::shared_ptr<OPCUADataDelegate> delegate; // 独立的Delegate
  QDateTime createTime;
  QDateTime lastAccessTime;
  bool isModified;
  bool isConnected;

  OPCUADataBlockContext()
      : view(nullptr), model(nullptr), delegate(nullptr), isModified(false),
        isConnected(false), controller(nullptr) {}
};

class OPCUADataBlockManager : public QObject {
    Q_OBJECT

public:
    OPCUADataBlockManager() = default;
    ~OPCUADataBlockManager() = default;

    // ========== 核心接口：获取或创建 View ==========
    /**
     * @brief 获取或创建指定 OPCUADataBlock 的 View
     * @param ipAddress IP地址
     * @param OPCUADataBlockName OPCUADataBlock名称
     * @param params 创建参数（首次创建时使用）
     * @return QTableView* 对应的表格视图（由 Manager 管理生命周期）
     */
    bool createTableView(const QString& ipAddress,
                                     const QString& OPCUADataBlockName);
    /**
     * @brief 获取已存在的 View（不创建）
     * @return QTableView* 如果不存在返回 nullptr
     */
    QTreeView* getTableView(const QString& ipAddress, const QString& OPCUADataBlockName);
    // QTableView* getTableView(const QString& ipAddress, const QString& OPCUADataBlockName);
    QWidget* getView(const QString& ipAddress, const QString& OPCUADataBlockName);
    
    // ========== 查找接口 ==========
    bool hasOPCUADataBlock(const QString& ipAddress, const QString& OPCUADataBlockName) const;
    OPCUADataBlockContext* getOPCUADataBlockContext(const QString& ipAddress, 
                                          const QString& filePath);
    QList<OPCUADataBlockKey> getAllOPCUADataBlockKeys() const;
    QList<OPCUADataBlockContext> getAllOPCUADataBlocks() const;
    std::shared_ptr<OPCUADeviceReader> getSpecialReader(const std::string &ip_Address) const;
    // ========== 删除接口 ==========
    bool removeOPCUADataBlock(const QString& ipAddress, const QString& OPCUADataBlockName);
    int removeOPCUADataBlocksByIp(const QString& ipAddress);
    void removeAllOPCUADataBlocks(){
      for(auto &element : m_OPCUADataBlocks)
      {
        if(element)
        {
          delete element;
          std::cout<<"element delete successfully"<<std::endl;
        }
      }
    };
    
    // ========== 更新接口 ==========
    bool updateOPCUADataBlock(const QString& ipAddress, 
                        const QString& OPCUADataBlockName,
                        const QVariantMap& newParams);
    
    void markAsModified(const QString& ipAddress, 
                       const QString& OPCUADataBlockName, 
                       bool modified);
    
    bool markAsConnected(const QString& ipAddress, 
                        const QString& OPCUADataBlockName, 
                        bool connected);
    
    // ========== 刷新接口 ==========
    void refreshOPCUADataBlock(const QString& ipAddress, const QString& OPCUADataBlockName);
    void refreshAllOPCUADataBlocks();
    
    // ========== 设备连接管理 ==========
    std::shared_ptr<OPCUADeviceReader> getClient(const QString& ipAddress,const std::string &connectWay);
    
    // ========== 原有业务接口（需要指定操作哪个 OPCUADataBlock）==========
    bool buildS7Connect(const QString &ipAddress, int rack, int slot,
                        const std::string &connectWay = "S7_Offset"); // S7参数

    bool buildOPCUAConnect(const QString &ipAddress,
                             int nameSpace, int port,const std::string &connectWay = "OPC_UA");

    bool buildOPCUAInlineBrowse(const QString &ipAddress, int nameSpace, int port,
                           const std::string &connectWay = "OPC_UA");

    bool checkConnectToDevice(const QString& ipAddress,
                        const std::string& connectWay);
    
    bool buildDataFromFile(const QString& ipAddress,
                         const QString& fileName);
    
signals:
    // 信号携带 ip + OPCUADataBlockName，让外部知道是哪个发生了变化
    void OPCUADataBlockCreated(const QString& ipAddress, const QString& OPCUADataBlockName);
    void OPCUADataBlockRemoved(const QString& ipAddress, const QString& OPCUADataBlockName);
    void OPCUADataBlockModified(const QString& ipAddress, const QString& OPCUADataBlockName, bool modified);
    void OPCUADataBlockConnected(const QString& ipAddress, const QString& OPCUADataBlockName, bool connected);
    void OPCUADataBlockRefreshed(const QString& ipAddress, const QString& OPCUADataBlockName);
    void errorOccurred(const QString& ipAddress, const QString& OPCUADataBlockName, const QString& error);

private:
    // 内部辅助函数
    OPCUADataBlockContext* createOPCUADataBlockContext(const QString& ipAddress,
                                             const QString& OPCUADataBlockName);

    void setupOPCUADataBlockConnections(OPCUADataBlockContext* context);
    void cleanupOPCUADataBlockContext(OPCUADataBlockContext* context);
    
    std::shared_ptr<OPCUADataBlockModel> createModel(const QString& ipAddress,
                                               const QString& OPCUADataBlockName);
    
    std::shared_ptr<OPCUADataDelegate> createDelegate(const QString& OPCUADataBlockName);
    OPCUADataBlockView *createView();

  private:
    // 核心数据结构：Key -> OPCUADataBlockContext
    QMap<OPCUADataBlockKey, OPCUADataBlockContext*> m_OPCUADataBlocks;
    
    // 设备客户端缓存：IP -> Client（同一IP复用连接）
    QMap<QString, std::shared_ptr<OPCUADeviceReader>> m_clients;
    
    // 共享组件
    std::shared_ptr<OPCUADataBlockBuilder> m_builder;
    std::vector<std::shared_ptr<OPCUADeviceReader>> m_readerVector;
    std::unique_ptr<OPCUADataBlockController> m_controller;
};

class MyApplication : public QApplication
{
    Q_OBJECT
public:
    MyApplication(int &argc, char **argv) : QApplication(argc, argv) {}

    bool notify(QObject *receiver, QEvent *event) override
    {
        // 核心：在事件被分发到目标对象之前进行拦截
        if (event->type() == QEvent::MouseButtonPress || 
            event->type() == QEvent::MouseMove || 
            event->type() == QEvent::Paint) 
        {
            qDebug() << "------ Event Dispatched ------";
            qDebug() << "Event Type:" << event->type();
            qDebug() << "Receiver Object:" << receiver->metaObject()->className();
            
            // 如果是鼠标事件，可以打印更多细节
            if (event->type() == QEvent::MouseButtonPress) {
                QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
                qDebug() << "Mouse Press Position:" << mouseEvent->pos();
            }
        }

        // 调用父类的notify，保证事件的正常处理
        return QApplication::notify(receiver, event);
    }
};