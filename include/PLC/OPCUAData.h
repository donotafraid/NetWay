#pragma once

#include "PLC/OPC_UA.h"
#include "PLC/XMLParser.h"
#include "PLC/OPCUABrowser.h"

class OPCUADataBlock;
class SpecialTreeView;

struct TreeNode {
  OPCUAModernDataStruct *m_dataBlock = nullptr;
  TreeNode *parent;
  QList<TreeNode *> children;
  QString displayName = "";
  bool isExpanded = false;

  TreeNode *getChildNode(int index){
    return children[index];
  }
  TreeNode() : parent(nullptr) {}
  ~TreeNode() { qDeleteAll(children); }
};

// 职责：只存储数据和变量定义
class OPCUADataBlock {
public:
    // 构造函数
    OPCUADataBlock() = default;
    explicit OPCUADataBlock(const std::string& name, const std::string& ip);
    
    // 基本数据访问
    const std::string& getBlocktName() const;
    std::vector<OPCUAModernDataStruct>& getVariabeDataVector();
    bool hasVariable(const std::string& path) const;
    OPCUAParseResult* getVariable(const std::string& path);
    const int getVariableVectorSize() const;
    
    // 获取变量映射
    Result<bool, RichError> getVariableMap(std::vector<OPCUAModernDataStruct>& m_variable_vector);
    std::string getIdentifier();
    
    // set part
    void setName(const std::string& name);
    void setIpAddres(const std::string& ip_Address);
    void setParseResult(const std::shared_ptr<OPCUAParseResult> &parseResult);
    
    // update function
    void updateBufferFromS7ModernStructByLSB(std::vector<OPCUAModernDataStruct>& vector);
    
    // Model operation   
    Result<QVariant, RichError> readValue(QModelIndex index) const;
    
    // trait function
    Result<bool, RichError> isArray(OPCUAModernDataStruct& data_var);
    
    // 批量操作
    Result<bool, RichError> batchReadValues(const std::vector<std::string>& paths, 
                                            std::vector<QVariant>& out_values) const;
    Result<bool, RichError> batchWriteValues(const std::vector<std::string>& paths,
                                             const std::vector<QVariant>& values);

private:
    std::shared_ptr<OPCUAParseResult> m_variable;
    std::string data_block_name;
    std::string ip_address;
    
    // 辅助方法
    OPCUAModernDataStruct* findVariableByPath(const std::string& path);
    const OPCUAModernDataStruct* findVariableByPath(const std::string& path) const;
    bool validatePath(const std::string& path, std::string& error_msg) const;
};

// 职责：将OPCUADataBlock适配为Qt的Model
class OPCUADataBlockModel : public QAbstractTableModel {
  Q_OBJECT

public:
  // 构造函数
  explicit OPCUADataBlockModel(QObject *parent = nullptr);
  explicit OPCUADataBlockModel(std::shared_ptr<OPCUADataBlock> block,
                               QObject *parent = nullptr);

  // 析构函数
  ~OPCUADataBlockModel() override;

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

  //  set internal member function
  bool setVariableValue(OPCUAModernDataStruct &var, int column,
                        const QVariant &value);
  // set internal member function
  void setOPCUADataBlock(std::shared_ptr<OPCUADataBlock> &block);

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
  
  std::shared_ptr<OPCUADataBlock> m_OPCUADataBlock;
  std::shared_ptr<TreeNode> m_rootNode;
  QHash<QString, TreeNode *> m_parentNodeIDMap; // 路径到节点的映射
  QMap<int, TreeNode *> m_visualRowMap;         // 视觉行号 → 节点指针
  bool m_visualRowMapValid = false;

  // 辅助方法
  // 输入：数据节点指针 (TreeNode*)
  // 输出：QModelIndex
  QModelIndex indexFromNode(TreeNode *node, int column = 0) const;
  void buildTree(TreeNode* parent = nullptr);

  void buildVisualRowMapRecursive(TreeNode *node, int &currentRow);
  TreeNode *createPlaceholderNode(const std::string &parentName);
  OPCUAModernDataStruct *findOPCUADataStruct(const std::string &targetName);
  std::string getParentName(const OPCUAModernDataStruct &element);
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
  std::string extractLastPartWithoutIndexForNormal(const std::string &input); 
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
    Result<bool, RichError> ReadDataFromPLC(OPCUADataBlock* data);
    Result<bool, RichError> batchReadOPCUAOPCUADataBlock_FromPLC(OPCUADataBlock* data);
    
    // 写入数据方法
    Result<bool, RichError> WriteDataToPLC(OPCUADataBlock* data);
    Result<bool, RichError> batchWriteSOPCUABlock_ToPLC(OPCUADataBlock* data);
    
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
    std::unique_ptr<S7_Access> m_s7Acess = nullptr;
    std::unique_ptr<OPCUA_Access> m_opcUA = nullptr;
    std::vector<uint8_t> Sourcebuffer;
    std::vector<uint8_t> Destbuffer;
    std::string m_identifier = "";
    
    // 辅助方法
    bool validateDataBlock(OPCUADataBlock* data) const;
    void logError(const std::string& function, const std::string& error) const;
    void logInfo(const std::string& message) const;
    
    // 数据转换辅助方法
    Result<bool, RichError> convertS7ToOPCUA(const std::vector<uint8_t>& s7_data, 
                                             OPCUADataBlock* data);
    Result<bool, RichError> convertOPCUAToS7(OPCUADataBlock* data, 
                                             std::vector<uint8_t>& s7_data);
};

// 职责：从定义构建OPCUADataBlock
class OPCUADataBlockBuilder : public QObject{
  Q_OBJECT
public:
    Result<bool,RichError> build(const QString &file_path,const std::string &ip_Address);
    Result<bool,RichError> build_InlineBrowse(const std::string &ip_Address);
    void TransformDataVec();

    Result<std::shared_ptr<OPCUAParseResult>, RichError> add_OPCUADataBlock_from_OPCUADataBlockDefinition(
        const QString &file_path,const std::string &ip_Address);

    //  update function
    Result<bool, RichError> updateTypeEnum(std::shared_ptr<OPCUAParseResult> &data);
    void resetValueByTypeEnum(OPCUAModernDataStruct &data);

  signals:
    void requestSaveOPCUADataBlock(
        const std::shared_ptr<OPCUAParseResult> &dataPointer);

  private:
    std::string m_name;
    int m_dbNumber = 0;

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
  // QTableView* getTableView() { return m_tableView; }
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
    std::cout << "Import Clicked !\n";
    std::cout << "testWithStandardModel called " << std::endl;
    validateTreeStructure();
  }
  void selectCell(const QModelIndex &index);
  void onFindClicked() { std::cout << "Find Clicked !\n"; }
  void onFilterChanged(const QString &text) {
    std::cout << "Filter Changed !\n";
  }

  void onRowdoubleClicked(const QModelIndex &index);
  void validateTreeStructure();

  // 在程序启动时设置
  void printCallStack();

  bool eventFilter(QObject *obj, QEvent *event) override;

  //  trim function for Index
  QModelIndex manualIndexAt(const QPoint &pos);
  QModelIndex findIndexByNode(TreeNode *node, int column) const;
  QModelIndex findIndexByY(const QModelIndex &parent, int &currentY,
                           int targetY, int targetX);
  QModelIndex mapVisualRowToModelIndex(const QModelIndex &parent,
                                       int targetVisualRow, int targetCol,
                                       int &currentVisualRow);
  QModelIndex mapVisualRowToModelIndex(const QModelIndex &parent,
                                       int targetVisualRow, int targetCol);
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
  QTableView *m_tableView;
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
};

class SpecialTreeView : public QTreeView {
  Q_OBJECT

public:
  explicit SpecialTreeView(QWidget *parent = nullptr);
  ~SpecialTreeView();

  //  get function
  void getModel(OPCUADataBlockModel *model);
  void getDelegate(OPCUADataDelegate *delegate);
  QTreeView *getTableView() { return this; }
  QWidget *getView() { return this; }
  int getGlobalIndex(){return globalRowPassager;}
  //  set function
  void setHeaderHeight(int height);

  QModelIndex findIndexByNode(TreeNode *node, int column) const;
  QModelIndex findIndexByY(const QModelIndex &parent, int &currentY,
                           int targetY, int targetX);
  QModelIndex mapVisualRowToModelIndex(const QModelIndex &parent,
                                       int targetVisualRow, int targetCol,
                                       int &currentVisualRow);
  QModelIndex mapVisualRowToModelIndex(const QModelIndex &parent,
                                       int targetVisualRow, int targetCol);
  QModelIndex indexAt(const QPoint &pos) const override;

  int calculateColumnAtX(int x) const;

  int getDepth(const QModelIndex &index);

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
  Result<int, RichError> getVisualRow(const QPoint &pos) const {
    int scrollValue = this->verticalScrollBar()->value();
    int rowHeight = this->fontMetrics().height();

    // 计算行号逻辑
    int relativeY = pos.y();

    if (relativeY < headerHeight) {
      return Result<int, RichError>(RichError("Click on header area"));
    }

    int visualRow = (relativeY - headerHeight) / rowHeight;
    if (visualRow < 0) {
      return Result<int, RichError>(RichError("Invalid visual row: negative"));
    }

    int globalRow = scrollValue + visualRow;
    return Result<int, RichError>(globalRow);
  }

  // 获取节点
  Result<TreeNode *, RichError> getNodeByRow(int globalRow) const {
    TreeNode *node = m_model->getNodeByVisualRow(globalRow);
    if (!node) {
      return Result<TreeNode *, RichError>(
          RichError("No node found for row: " + std::to_string(globalRow)));
    }
    return Result<TreeNode *, RichError>(node);
  }

  // 获取列号
  Result<int, RichError> getColumnAtX(const QPoint &pos) const {
    int col = calculateColumnAtX(pos.x());
    if (col < 0) {
      return Result<int, RichError>(
          RichError("Invalid column: " + std::to_string(col)));
    }
    return Result<int, RichError>(col);
  }

  // 查找索引
  Result<QModelIndex, RichError> findIndexByNode(TreeNode *node,
                                                 int col) const {
    QModelIndex idx = findIndexByNode(node, col);
    if (!idx.isValid()) {
      return Result<QModelIndex, RichError>(
          RichError("Invalid model index for node: " + node->displayName.toStdString()));
    }
    return Result<QModelIndex, RichError>(idx);
  }

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
    std::string handleExternalReadIpAddress(){return m_OPCUADataBlock->getIdentifier();};
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
    //  on functino
    bool onbuildOPCUADataBlockFromXMLFile(const QString &file_path,
                                          const std::string &ip_Address) {
      auto result = m_OPCUADataBlockBuild->build(file_path, ip_Address);
      if (result.is_fail()) {
        return false;
      } else {
        return true;
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

  private slots:
    // 连接View的信号到Model的操作
    void onViewReadRequested();
    void onViewWriteRequested();
    void onViewDataTypeChangeRequested(){};
    void onViewExportRequested(){};
    void onViewFindRequested(){};

    //  OPCUADataBlock function
    void onSaveOPCUADataBlock(const std::shared_ptr<OPCUAParseResult> &OPCUADataBlock);

    // Model变化时的响应

  private:
    OPCUADataBlockView *m_view = nullptr;
    std::shared_ptr<OPCUADataBlockModel> m_model = nullptr;
    std::shared_ptr<OPCUADataBlockBuilder> m_OPCUADataBlockBuild = nullptr;
    std::shared_ptr<OPCUADeviceReader> m_reader = nullptr;


    std::shared_ptr<OPCUADataBlock> m_OPCUADataBlock = nullptr;
    std::shared_ptr<OPCUAParseResult> m_OPCUAParseResult = nullptr;
    SCL_Parser m_DBParser;
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

// OPCUADataBlock 完整信息
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