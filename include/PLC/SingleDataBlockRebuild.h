#pragma once
#include "PLC/TransformS7AndOPCUA.h"
#include <cstdlib>
#include <cstring>
#include "PLC/OPC_UA.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include "load_config/Qt_library.h"
#include "PLC/XMLParser.h"

class DataBlock;

//  0 = BIG , ! = SMALL
enum class Endian { BIG, SMALL };

struct ConnectWayStatus
{
  bool S7Switch = false;
  bool OPCUASwitch = false;
};

enum DataRole {
  // 名称、类型、偏移量、值,注解
  VariableName = 0,
  DataType,
  DataOffset,
  Value,
  Comment
};

// 职责：字节序转换
class EndianConverter {
public:
  static Result<bool, RichError>
  S7BigEndianToLittleEndian(std::vector<uint8_t> &Sourcebuffer,
                 std::vector<uint8_t> &&Destbuffer,
                 const S7ModernDataStruct &var);

  static Result<bool, RichError>
  LittleEndianToS7BigEndian(std::vector<uint8_t> &Sourcebuffer,
                            std::vector<uint8_t> &&Destbuffer,
                            const S7ModernDataStruct &var);
};

class DataTypeConverter {
  static Result<NormalDataType, RichError>
  Data_transform_from_bytes(const S7DataType &type,
                            const std::vector<uint8_t> &bytes, int offset,
                            int length, int bit_offset) {
    switch (type) {
    //  the data order is little endian
    case S7DataType::BOOL: {
      auto tmp_byte = bytes[offset];
      if ((tmp_byte & (1 << bit_offset))) {
        return Result<NormalDataType, RichError>(bool{true});
      } else {
        return Result<NormalDataType, RichError>(bool{false});
      }
    }
    case S7DataType::BYTE:
      return Result<NormalDataType, RichError>(uint8_t{bytes[offset]});
    case S7DataType::INT: {
      int16_t value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(int16_t));
      return Result<NormalDataType, RichError>(int16_t{value});
    }
    case S7DataType::DINT: {
      int32_t value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(int32_t));
      return Result<NormalDataType, RichError>(int32_t{value});
    }
    case S7DataType::REAL: {
      float value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(float));
      return Result<NormalDataType, RichError>(float{value});
    }
    case S7DataType::WORD: {
      uint16_t value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(uint16_t));
      return Result<NormalDataType, RichError>(uint16_t{value});
    }
    case S7DataType::UDINT: {
      uint32_t value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(uint32_t));
      return Result<NormalDataType, RichError>(uint32_t{value});
    }
    case S7DataType::DWORD: {
      uint32_t value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(uint32_t));
      return Result<NormalDataType, RichError>(uint32_t{value});
    }
    case S7DataType::STRING: {
      std::string value(reinterpret_cast<const char *>(&bytes[offset + 2]),
                        length - 2);
      return Result<NormalDataType, RichError>(NormalDataType{std::move(value)}
                                               // move 避免拷贝
      );
    }
    default:
      return Result<NormalDataType, RichError>(
          RichError("Unsupported data type"));
    }
  }

  static std::string transform_uint32_to_string(uint32_t value) {
    return std::to_string(value);
  }

  static std::string transform_uint32_to_hex_string(uint32_t value) {
    std::ostringstream oss;
    oss << "0x" << std::hex << value;
    return oss.str();
  }
};

class DeviceReader
{
  public:
    Result<bool,RichError> ReadDataFromPLC(DataBlock *data);
    Result<bool, RichError> ReadS7DataBlock_FromPLC(DataBlock *data);
    Result<bool, RichError> ReadOPCUADataBlock_FromPLC(DataBlock *data);
    Result<bool, RichError> batchReadOPCUADataBlock_FromPLC(DataBlock *data);

    Result<bool,RichError> WriteDataToPLC(DataBlock *data);
    Result<bool,RichError> WriteS7DataBlock_ToPLC(DataBlock *data);
    Result<bool,RichError> WriteSOPCUABlock_ToPLC(DataBlock *data);
    Result<bool, RichError> batchWriteSOPCUABlock_ToPLC(DataBlock *data);

    
    //  get function
    Result<std::string,RichError> getIdentifier(){
      if(m_identifier == "")
      {
        return Result<std::string,RichError> (RichError{"DeviceReader identifier is null"});
      }
      else
      {
        return Result<std::string,RichError> (m_identifier);
      }
    }
    bool getConnectWayForS7() {
      if (m_connectStatus.S7Switch) {
        return true;
      } else {
        return false;
      }
    }

    //  set function
    void setConnectWayStatus(const std::string &way) {
      if (way.empty()) {
        return;
      } else {
        if (way == "S7_Offset") {
          m_connectStatus.S7Switch = true;
        } else if (way == "OPC_UA") {
          m_connectStatus.OPCUASwitch = true;
        }
      }
    }

    //  respond function
    bool onRequestBuildS7Object(const std::string &ip_Address, int rack, int slot) {
      m_identifier = ip_Address + "-" + "S7_Offset";
      m_s7Acess = std::make_unique<S7_Access>(ip_Address, rack, slot);
      auto result = m_s7Acess->connect();
      if (result.is_fail()) {
        std::cout << result.unwrap_err().what() << std::endl;
        return false;
      }
      else
      {
        std::cout<<"S7 Offset Connect successfully \n";
        return true;
      }
    };
    bool onRequestBuildOPCUA(const std::string &ip_Address, int nameSpace, int port) {
      m_identifier = ip_Address + "-" + "OPC_UA";
      m_opcUA = std::make_unique<OPCUA_Access>(ip_Address, nameSpace, port);
      auto result = m_opcUA->reconnect(5,1000);
      if(result.is_fail())
      {
        std::cout<<"OPCUA Connect fail , reason : "<<result.unwrap_err().what()<<std::endl;
        return false;
      }
      else
      {
        std::cout<<"OPCUA Connect successfully \n";
        return true;
      }
    };
    bool onRequest7ObjectCheckConnect() {
      return m_s7Acess->isConnected();
    };
    bool onRequestOPCUACheckConnect() {
      return m_opcUA->isConnected();
    };
  private:
  std::unique_ptr<S7_Access> m_s7Acess = nullptr;
  std::unique_ptr<OPCUA_Access> m_opcUA = nullptr;
  std::vector<uint8_t> Sourcebuffer;
  std::vector<uint8_t> Destbuffer;
  std::string m_identifier = "";
  ConnectWayStatus m_connectStatus;
};

// 职责：只存储数据和变量定义
class DataBlock {
public:
    DataBlock() = default;
    
    // 基本数据访问
    const std::string &getBlocktName() const { return data_block_name; }
    std::vector<S7ModernDataStruct>& getVariabeDataVector() { return m_variable_vector; }
    int getDataBlockLength() { return m_whole_data_block_length; };
    bool hasVariable(const std::string &path) const {
      std::cout << "hasVariable call !\n";
      return false;
    };
    S7ModernDataStruct *getVariable(const std::string &path);
    const int getVariableVectorSize() const {
      return m_variable_vector.size();
    };
    std::vector<uint8_t> &getVariableDataBuffer() {
      return m_variableDataBuffer;
    };
    Result<bool, RichError>
    getVariableMap(std::vector<S7ModernDataStruct> &m_variable_vector);
    Result<int, RichError> getSpecialStringLength(const std::string &dataName);
    std::string getIdentifier() { return ip_address; }

    //  set part
    void setName(const std::string &name) { data_block_name = name; }
    void setIpAddres(const std::string &ip_Address) { ip_address = ip_Address; }

    //  update part
    void updateS7ModernStructFromBuffer();
    void
    updateBufferFromS7ModernStructByMSB(std::vector<S7ModernDataStruct> &vector);
    void
    updateBufferFromS7ModernStructByLSB(std::vector<S7ModernDataStruct> &vector);

    //  Model operation 
    Result<QVariant, RichError> readValue(QModelIndex index) const;

    // trait function
    Result<bool, RichError> isArray(S7ModernDataStruct &data_var);
    void calculateDataBlockLength(int length) {
      m_whole_data_block_length += length;
      if(m_variableDataBuffer.size() == 0)
      {
        m_variableDataBuffer.resize(m_whole_data_block_length);
      }
    };

    // 批量操作
    void resize(int newSize){};
    void clear();
    
private:
  int m_whole_data_block_length;
  std::vector<S7ModernDataStruct> m_variable_vector;
  std::vector<uint8_t> m_variableDataBuffer;
  std::string data_block_name;
  std::string ip_address;
};

// 职责：将DataBlock适配为Qt的Model
class DataBlockModel : public QAbstractTableModel {
    Q_OBJECT
    
public:
    // respond external request about variable
  std::vector<S7ModernDataStruct> &getModelItemVecotr() {
    return m_dataBlock->getVariabeDataVector();
  };

    //  set internal member function
    void setDataBlock(std::shared_ptr<DataBlock> block) {
      beginResetModel(); // 告诉 View 准备完全重置
      m_dataBlock = block;
      endResetModel(); // View 会自动重新读取所有数据
    };

    // QAbstractTableModel 接口
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    //  return data from internal Model 
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    //  set data into internal Model
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    //  batch update function
    void batchSetDataForS7();
    void batchSetDataForOPCUA();

    // validation function
    bool isValidIndex(const QModelIndex &index) const;

signals:
    void requestDataBlockModified();
    
private:
  std::shared_ptr<DataBlock> m_dataBlock;
};

// 职责：从定义构建DataBlock
class DataBlockBuilder : public QObject{
  Q_OBJECT
public:
  Result<bool, RichError> build(const OPCUADataBlockDefinition &content,
                                const std::string &ip_Address);

  Result<std::shared_ptr<DataBlock>, RichError>
  add_datablock_from_OPCUADataBlockDefinition(
      const OPCUADataBlockDefinition &data_block_definition,
      const std::string &ip_Address);

  Result<bool, RichError> add_variable_from_S7XMLVariableDefinition(
      const S7XMLVariableDefinition &variable_definition, int data_block_number,
      const std::string &prefix,
      std::vector<S7ModernDataStruct> &m_variable_vector);

  Result<bool, RichError> isNumber(const std::string &s);

signals:
  void requestSaveDataBlock(const std::shared_ptr<DataBlock> &dataPointer);

private:
    std::string m_name;
    int m_dbNumber = 0;
    std::vector<S7XMLVariableDefinition> m_variables;
    
    int calculateOffset(const S7ModernDataStruct& var) const;
    void allocateBuffer(DataBlock& block);
    SCL_Parser m_scl_parser;
};

//  responsibility : The logic behind the data presented 
class S7DataDelegate : public QStyledItemDelegate {
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
class DataBlockView : public QWidget {
    Q_OBJECT
    
public:
    explicit DataBlockView(QWidget* parent = nullptr){
      setupUI();
      buildConnection();
    };
    ~DataBlockView()
    {
      std::cout<<"~DataBlockView call"<<std::endl;
    }
    
    // 设置Model
    void setModel(DataBlockModel* model){m_tableView->setModel(model);};
    void setDelegate(S7DataDelegate* delegate){m_tableView->setItemDelegate(delegate);};
    
    // 外部响应层接口 - 接收外部信号
    void onRefreshComplete(bool success, const QString& error);
    void onWriteComplete(bool success, const QString& error);
    void onConnectWithDataBlockView(QSplitter *splitter); 
    void onBuildNewTableView(const std::string &ip_Address,const std::string &dataBlockName); 
    
    //  get function
    QTableView* getTableView() { return m_tableView; }
    QWidget *getView(){return this;}

    // 功能响应层接口 - 响应用户操作
    void refreshData() {}                                    // 刷新数据
    void writeData() { std::cout << "write Data !\n"; }      // 写入数据
    void exportToCSV() { std::cout << "export To CSV !\n"; } // 导出CSV
    void findValue() { std::cout << "find Value!\n"; }       // 查找值
    void filterByType() { std::cout << "filterByType !\n"; } // 按类型过滤
    void importFile(); // 导入配置文件

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
    void onExportClicked(){std::cout<<"Export clicked !\n";}
    void onImportClicked(){std::cout<<"Import Clicked !\n";}
    void onFindClicked(){std::cout<<"Find Clicked !\n";}
    void onFilterChanged(const QString& text){std::cout<<"Filter Changed !\n";}
    void onRowdoubleClicked(const QModelIndex &index) {
      std::cout << "RowdoubleClicked !\n";
      m_tableView->edit(index);
    }

private:
    void setupUI();
    void showStatusMessage(const QString& message, bool isError = false){}
    void updateButtonStates(bool isWorking = false){}
    
    //  connect function
    void buildConnection();
    
    QTableView* m_tableView;
    QPushButton* m_refreshBtn;
    QPushButton* m_writeBtn;
    QPushButton* m_exportBtn;
    QPushButton* m_importBtn;

    QLineEdit* m_searchEdit;
    QComboBox* m_typeFilter;
    QStatusBar* m_statusBar;
    QProgressBar* m_progressBar;

    QString m_currentIp;
    QString m_currentDataBlock;
};

//  responsibility : Coordinating business interactions
class DataBlockController : public QObject {
    Q_OBJECT
    
public:
    explicit DataBlockController(QObject* parent = nullptr){};
    ~DataBlockController(){
      std::cout << "DataBlockController destory call" << std::endl;
    }
    
    void initialize(Scope* scope);
    void initializeView(DataBlockView* view);
    void buildConnection();
    
    // 处理外部请求
    void handleExternalRefreshRequest(){};
    void handleExternalWriteRequest(){};
    std::string handleExternalReadIpAddress(){return m_DataBlock->getIdentifier();};
    bool handleExternalOPCUAConnectRequest(const std::string &ip_Address, int nameSpace,
                                         int port) {
      return m_reader->onRequestBuildOPCUA(ip_Address,nameSpace,port);
    };
    bool handleExternalS7ConnectRequest(const std::string &ip_Address, int rack,
                                      int slot) {
      return m_reader->onRequestBuildS7Object(ip_Address,rack,slot);
    };
    bool handleExternalOPCUAConnectCheckRequest() {
      return m_reader->onRequestOPCUACheckConnect();
    };
    bool handleExternalS7ConnectCheckRequest() {
      return m_reader->onRequest7ObjectCheckConnect();
    };
    
    bool handleSwithView(const QString &ip_Address,const QString &dataBlockName){
      return true;
    }
    bool handleBuildNewTableView(const std::string &ip_Address,const std::string &dataBlockName);

    //  on functino
    bool onbuildDataBlockFromDBFile(const std::string &file_path,
                                    const std::string &ip_Address);
    bool onbuildDataBlockFromXMLFile(const std::string &file_path,
                                    const std::string &ip_Address);

  private slots:
    // 连接View的信号到Model的操作
    void onViewReadRequested();
    void onViewWriteRequested();
    void onViewDataTypeChangeRequested(){};
    void onViewExportRequested(){};
    void onViewFindRequested(){};

    //  datablock function
    void onSaveDataBlock(const std::shared_ptr<DataBlock> &dataBlock);

    // Model变化时的响应

  private:
    DataBlockView *m_view = nullptr;
    std::shared_ptr<DataBlockModel> m_model = nullptr;
    std::shared_ptr<DataBlockBuilder> m_DataBlockBuild = nullptr;
    std::shared_ptr<DeviceReader> m_reader = nullptr;

    std::shared_ptr<DataBlock> m_DataBlock = nullptr;
    SCL_Parser m_DBParser;
};

struct DataBlockKey {
    QString ipAddress;
    QString dataBlockName;
    
    DataBlockKey() = default;
    DataBlockKey(const QString& ip, const QString& block) 
        : ipAddress(ip), dataBlockName(block) {}
    
    bool operator==(const DataBlockKey& other) const {
        return ipAddress == other.ipAddress && 
               dataBlockName == other.dataBlockName;
    }
    
    bool operator<(const DataBlockKey& other) const {
        if (ipAddress != other.ipAddress) 
            return ipAddress < other.ipAddress;
        return dataBlockName < other.dataBlockName;
    }
    
    QString toString() const {
        return QString("%1@%2").arg(dataBlockName, ipAddress);
    }
};

inline uint qHash(const DataBlockKey& key, uint seed) {
    return qHash(key.ipAddress, seed) ^ qHash(key.dataBlockName, seed);
}

// DataBlock 完整信息
struct DataBlockContext {
  DataBlockKey key;
  QWidget m_controllWidget;
  std::shared_ptr<DataBlockController> controller;
  DataBlockView *view = nullptr;      // 独立的View
  std::shared_ptr<DataBlockModel> model;    // 独立的Model
  std::shared_ptr<S7DataDelegate> delegate; // 独立的Delegate
  QDateTime createTime;
  QDateTime lastAccessTime;
  bool isModified;
  bool isConnected;

  DataBlockContext()
      : view(nullptr), model(nullptr), delegate(nullptr), isModified(false),
        isConnected(false), controller(nullptr) {}
};

class DataBlockManager : public QObject {
    Q_OBJECT

public:
    explicit DataBlockManager(QObject* parent = nullptr);
    ~DataBlockManager();

    // ========== 核心接口：获取或创建 View ==========
    /**
     * @brief 获取或创建指定 DataBlock 的 View
     * @param ipAddress IP地址
     * @param dataBlockName DataBlock名称
     * @param params 创建参数（首次创建时使用）
     * @return QTableView* 对应的表格视图（由 Manager 管理生命周期）
     */
    bool createTableView(const QString& ipAddress,
                                     const QString& dataBlockName);
    /**
     * @brief 获取已存在的 View（不创建）
     * @return QTableView* 如果不存在返回 nullptr
     */
    QTableView* getTableView(const QString& ipAddress, const QString& dataBlockName);
    QWidget* getView(const QString& ipAddress, const QString& dataBlockName);
    
    // ========== 查找接口 ==========
    bool hasDataBlock(const QString& ipAddress, const QString& dataBlockName) const;
    DataBlockContext* getDataBlockContext(const QString& ipAddress, 
                                          const QString& filePath);
    QList<DataBlockKey> getAllDataBlockKeys() const;
    QList<DataBlockContext> getAllDataBlocks() const;
    std::shared_ptr<DeviceReader> getSpecialReader(const std::string &ip_Address) const;
    // ========== 删除接口 ==========
    bool removeDataBlock(const QString& ipAddress, const QString& dataBlockName);
    int removeDataBlocksByIp(const QString& ipAddress);
    void removeAllDataBlocks(){
      for(auto &element : m_dataBlocks)
      {
        if(element)
        {
          delete element;
          std::cout<<"element delete successfully"<<std::endl;
        }
      }
    };
    
    // ========== 更新接口 ==========
    bool updateDataBlock(const QString& ipAddress, 
                        const QString& dataBlockName,
                        const QVariantMap& newParams);
    
    void markAsModified(const QString& ipAddress, 
                       const QString& dataBlockName, 
                       bool modified);
    
    bool markAsConnected(const QString& ipAddress, 
                        const QString& dataBlockName, 
                        bool connected);
    
    // ========== 刷新接口 ==========
    void refreshDataBlock(const QString& ipAddress, const QString& dataBlockName);
    void refreshAllDataBlocks();
    
    // ========== 设备连接管理 ==========
    std::shared_ptr<DeviceReader> getClient(const QString& ipAddress,const std::string &connectWay);
    
    // ========== 原有业务接口（需要指定操作哪个 DataBlock）==========
    bool buildS7Connect(const QString& ipAddress, 
                       int rack, int slot , const std::string &connectWay = "S7_Offset");  // S7参数

    bool buildOPCUAConnect(const QString &ipAddress,
                             int nameSpace, int port,const std::string &connectWay = "OPC_UA");

    bool checkConnectToDevice(const QString& ipAddress,
                        const std::string& connectWay);
    
    bool buildDataFromFile(const QString& ipAddress,
                         const QString& fileName);
    
    // ========== 获取数据（供外部显示）==========
    QString getDataBlockInfo(const QString& ipAddress, const QString& dataBlockName) const;
    bool isDataBlockModified(const QString& ipAddress, const QString& dataBlockName) const;
    
signals:
    // 信号携带 ip + dataBlockName，让外部知道是哪个发生了变化
    void dataBlockCreated(const QString& ipAddress, const QString& dataBlockName);
    void dataBlockRemoved(const QString& ipAddress, const QString& dataBlockName);
    void dataBlockModified(const QString& ipAddress, const QString& dataBlockName, bool modified);
    void dataBlockConnected(const QString& ipAddress, const QString& dataBlockName, bool connected);
    void dataBlockRefreshed(const QString& ipAddress, const QString& dataBlockName);
    void errorOccurred(const QString& ipAddress, const QString& dataBlockName, const QString& error);

private:
    // 内部辅助函数
    DataBlockContext* createDataBlockContext(const QString& ipAddress,
                                             const QString& dataBlockName);

    void setupDataBlockConnections(DataBlockContext* context);
    void cleanupDataBlockContext(DataBlockContext* context);
    
    std::shared_ptr<DataBlockModel> createModel(const QString& ipAddress,
                                               const QString& dataBlockName);
    
    std::shared_ptr<S7DataDelegate> createDelegate(const QString& dataBlockName);
    DataBlockView *createView();

  private:
    // 核心数据结构：Key -> DataBlockContext
    QMap<DataBlockKey, DataBlockContext*> m_dataBlocks;
    
    // 设备客户端缓存：IP -> Client（同一IP复用连接）
    QMap<QString, std::shared_ptr<DeviceReader>> m_clients;
    
    // 共享组件
    std::shared_ptr<DataBlockBuilder> m_builder;
    std::vector<std::shared_ptr<DeviceReader>> m_readerVector;
    std::unique_ptr<DataBlockController> m_controller;
};
