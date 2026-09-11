#pragma once

#include "PLC/OPC_UA.h"
#include "PLC/OPCUABrowser.h"
#include "PLC/OPCUACovert.h"
#include "PLC_Collector/IDataNode.h"
#include "PLC/ParsingStrategy.h"
#include "PLC/TreeNode.h"
#include "PLC/MapperStrategy.h"
#include "PLC/GenericTreeBuilder.h"
#include "PLC/CreateContextStrategy.h"

class OPCUADataBlock;
class SpecialTreeView;

// 职责：将OPCUADataBlock适配为Qt的Model
class OPCUADataBlockModel : public QAbstractTableModel {
  Q_OBJECT

public:
  // 构造函数
  explicit OPCUADataBlockModel(QObject *parent = nullptr);

  // 析构函数
  ~OPCUADataBlockModel() override;

  //  set internal parmeter
  void setRootNode(std::shared_ptr<TreeNode> rootNode) {
    m_rootNode = rootNode;
    rebuildVisualRowMap();
  };

  // respond external request about variable
  TreeNode *getNodeByVisualRow(int visualRow) const;
  std::shared_ptr<TreeNode> getRootNode();

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

  std::shared_ptr<TreeNode> m_rootNode;
  QMap<int, TreeNode *> m_visualRowMap; // 视觉行号 → 节点指针
  bool m_visualRowMapValid = false;

  // 辅助方法
  // 输入：数据节点指针 (TreeNode*)
  // 输出：QModelIndex
  QModelIndex indexFromNode(TreeNode *node, int column = 0) const;
  void buildViewTree(TreeNode *parent = nullptr);
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


  int getChildSize(TreeNode *node) const {
    int size = 1;
    if (node->getChildCount() == 0) {
      return 1;
    } else {
      for (auto &element : node->getChildren()) {
        size += getChildSize(element.get());
      }
    }
    return size;
  }

  // 辅助函数：在父节点中查找子节点索引（可以缓存优化）
  int findChildIndex(const std::shared_ptr<TreeNode> &parent,
                     const std::shared_ptr<TreeNode> &child) const {
    // 如果有缓存，可以直接返回
    return parent->getChildren().indexOf(child);
  }

  // 无符号整数转换辅助函数
  template <typename TargetType>
  bool convertAndStoreUnsigned(const QVariant &value,
                               std::optional<ValueType> &coreValue) {
    bool ok = false;
    uint32_t temp = value.toUInt(&ok);
    if (!ok)
      return false;

    // 边界值检查（防止溢出）
    if (temp > std::numeric_limits<TargetType>::max()) {
      return false; // 数值超出目标类型范围
    }

    coreValue = static_cast<TargetType>(temp);
    return true;
  }

  // 有符号整数转换辅助函数（可选，用于统一处理）
  template <typename TargetType>
  bool convertAndStoreSigned(const QVariant &value, std::optional<ValueType> &coreValue) {
    bool ok = false;
    int32_t temp = value.toInt(&ok);
    if (!ok)
      return false;

    // 边界值检查
    if (temp < std::numeric_limits<TargetType>::min() ||
        temp > std::numeric_limits<TargetType>::max()) {
      return false;
    }

    coreValue = static_cast<TargetType>(temp);
    return true;
  }
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
    for (auto element : parent->getChildren()) {
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
      return Result<QModelIndex, RichError>(SuccessTag{},m_cachedIndex);
    }
    return Result<QModelIndex, RichError>(ErrorTag{},RichError("Cache miss or expired"));
  }

  // 获取视觉行号
  Result<int, RichError> getGlobalRow(const QPoint &pos) const;

  // 获取节点
  Result<TreeNode *, RichError> getNodeByGlobalRow(int globalRow) const;
  // 获取列号
  Result<FindRelativeIndex, RichError> getColumnAtX(const QPoint &pos,TreeNode *node) const;

  // 查找索引
  Result<QModelIndex, RichError> findIndexByNode(const FindRelativeIndex &item) const;
  // 辅助函数：在父节点中查找子节点索引（可以缓存优化）
  int findChildIndex(const std::shared_ptr<TreeNode> &parent,
                     const TreeNode *child) const {
    int row = 0;
    for (auto element : parent->getChildren()) {
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

    void initialize(std::shared_ptr<OPCUADataBlockView> view,
                    std::shared_ptr<OPCUADataBlockModel> model,
                    const QString &identifier, const ServiceRegistry &registry);
    void buildConnection();
    
    // 处理外部请求
    void handleExternalRefreshRequest(){};
    void handleExternalWriteRequest(){};
    //  on function
    bool onbuildOPCUADataBlockFromMap(const QString &file_path,
                                          const QString &identifier,const std::unordered_map<QString, ServiceRegistry> &m_serviceSuites);
  private slots:
    // 连接View的信号到Model的操作
    void onViewReadRequested();
    void onViewWriteRequested();
    void onViewDataTypeChangeRequested(){};
    void onViewExportRequested(){};
    void onViewFindRequested(){};

    // Model变化时的响应

  private:
  std::weak_ptr<OPCUADataBlockView> m_view ;
  std::weak_ptr<OPCUADataBlockModel> m_model ;
  QString m_identifier;
  ServiceRegistry m_registry;

  std::unique_ptr<GenericTreeBuilder> treeBuilder ;
  DataLoader m_loader;

  std::vector<std::shared_ptr<IDataNode>> dataNodeVec;
  std::vector<WriteRequest> m_requestVec;
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
  std::shared_ptr<OPCUADataBlockView> view; // 独立的View
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
    // QTableView* getTableView(const QString& ipAddress, const QString& OPCUADataBlockName);
    QWidget* getView(const QString& ipAddress, const QString& OPCUADataBlockName);
    
    // ========== 查找接口 ==========
    bool hasOPCUADataBlock(const QString& ipAddress, const QString& OPCUADataBlockName) const;
    OPCUADataBlockContext* getOPCUADataBlockContext(const QString& ipAddress, 
                                          const QString& filePath);
    QList<OPCUADataBlockKey> getAllOPCUADataBlockKeys() const;
    QList<OPCUADataBlockContext> getAllOPCUADataBlocks() const;
    std::shared_ptr<IDeviceReader> getSpecialReader(const QString &identifier) const;
    // ========== 删除接口 ==========
    bool removeOPCUADataBlock(const QString& ipAddress, const QString& OPCUADataBlockName);
    int removeOPCUADataBlocksByIp(const QString& ipAddress);
    void removeAllOPCUADataBlocks(){
          std::cout<<"element delete successfully"<<std::endl;
    };
    void cleanupOPCUADataBlockContext(OPCUADataBlockContext *context);

    // ========== 刷新接口 ==========
    
    // ========== 原有业务接口（需要指定操作哪个 OPCUADataBlock）==========
    bool buildS7Connect(const QString &ipAddress, int rack, int slot,
                        const QString &identifier,
                        const std::string &connectWay = "S7_Offset"); // S7参数

    bool buildOPCUAConnect(const QString &ipAddress, int nameSpace, int port,
                           const QString &identifier,
                           const std::string &connectWay = "OPC_UA");

    bool buildOPCUAInlineBrowseConnect(
        const QString &ipAddress, int nameSpace, int port,
        const std::string &urlPrefix, const int &objectId,
        const QString &identifier, const std::string &connectWay = "OPC_UA");

    bool checkConnectToDevice(const QString& ipAddress,
                        const std::string& connectWay);
    
    bool buildDataFromFile(const QString& identify,
                         const QString& fileName);
  signals:
    // 信号携带 ip + OPCUADataBlockName，让外部知道是哪个发生了变化
    void OPCUADataBlockCreated(const QString& ipAddress, const QString& OPCUADataBlockName);
    void OPCUADataBlockRemoved(const QString& ipAddress, const QString& OPCUADataBlockName);
    void OPCUADataBlockModified(const QString& ipAddress, const QString& OPCUADataBlockName, bool modified);
    void OPCUADataBlockConnected(const QString& ipAddress, const QString& OPCUADataBlockName, bool connected);
    void OPCUADataBlockRefreshed(const QString& ipAddress, const QString& OPCUADataBlockName);

private:
  // 核心数据结构：Key -> OPCUADataBlockContext
  QMap<OPCUADataBlockKey, std::shared_ptr<OPCUADataBlockContext>> m_blockMap;
  
  ContextStrategyFactory m_contextFactory;

  // 设备客户端缓存：IP -> Client（同一IP复用连接）
  std::unordered_map<QString, ServiceRegistry> m_serviceSuites;
};