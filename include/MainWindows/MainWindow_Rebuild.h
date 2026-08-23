#pragma once

#include "MainWindows/Struct.h"
// #include "PLC/DataBlockView.h"
#include "MainWindows/DropArea.h"
#include "ServiceMetrics/serviceMetrics.h"
#include "GrafanaDashboardManager/ObjectRouter.h"
#include "MainWindows/LineChartTest.h"
#include "PLC/OPCUAManager.h"


class IS7Controller;
class SystemSetting;
class S7_DeviceManager;

//  1.UI层
class S7_MainWindows_UI : public QMainWindow {
  Q_OBJECT
  public:
    S7_MainWindows_UI(QWidget *parent = nullptr);
    ~S7_MainWindows_UI();

  private:
    // 左侧文件树/列表
    struct LastSelectItem {
      QTreeWidgetItem *m_lastest_dataBlock_TreeWidget_item = nullptr;    // UI状态
      QTreeWidgetItem *m_lastest_device_TreeWidget_item = nullptr; // UI状态
      DeviceTableInfo *m_info = nullptr;
      QString m_lastest_device_folder_path="";
    };
    struct TreeMenu {
      QMenu device_menu_var;
      QAction *addDeviceAction = nullptr;
    };
    struct DeviceMenu {
      //  DataBlock MENU
      QMenu subDeviceMenu;
      QAction *deleteDeviceAction = nullptr;

      QAction *loadExternalDataConfigAction = nullptr;
      QAction *loadInternalDataConfigAction = nullptr;
      QAction *loadOPCUAInlineBrowseAction = nullptr;
    };
    struct DataBlockMenu {
      //  DataBlock MENU
      QMenu dataBlockMenu;
      QAction *deleteDataBLockAction = nullptr;
    };
  QTreeWidget *m_datablockname_tree = nullptr;                 // UI组件
  QLineEdit *m_search_bar = nullptr;                           // UI组件
  LastSelectItem m_lastSelectItem;
  TreeMenu m_treeeMenu;
  DeviceMenu m_DeviceMenu;
  DataBlockMenu m_dataBlockMenu;
  std::vector<DeviceTableInfo *> m_devieVector;

  //  toolBar
  struct ToolBar{
  QToolBar *monitor_ToolBar;

  QAction *toggleMonitorAction;
  QAction *toggleSystemSetting; 
  QAction *test_send_prometheus_message; 
  QAction *test_display_grafana_web_view;
  }m_toolBar;

  // Spiliter 
  QSplitter *splitter_left_v = nullptr;
  QTabWidget *blankInformationTableView = nullptr;
  QMap<QString, int> m_tabIndexMap;
  QSplitter *splitter_main_H = nullptr;

  // 主布局
  QVBoxLayout *m_main_layout_V = nullptr;      // UI布局
  QVBoxLayout *right_layout = nullptr;         // UI布局

  // device ipAddress 搜索栏
  QCompleter *m_completer = nullptr;            // UI辅助
  QStringListModel *m_completerModel = nullptr; // UI模型
  QStringList m_match_list;           // UI数据

  // 系统设置
  SystemSetting *m_systemSetting = nullptr; // UI对话框

  // 监控视图
  QWidget *m_prometheus_view = nullptr;               // UI容器
  // MonitoringDashboard *m_monitor_dashBoard = nullptr; // UI组件
  LineChart *m_LineChart = nullptr;                   // UI组件

  // 添加设备页面
  QDialog *m_addDevice_page = nullptr; // UI对话框
  QPushButton *okButton = nullptr;
  QPushButton *cancelButton = nullptr;

  QLineEdit *m_ip_Address_edit = nullptr; // UI输入框
  QLineEdit *m_nameSpace_edit = nullptr;  // UI输入框
  QLineEdit *m_port_edit = nullptr;       // UI输入框
  QLineEdit *m_urlPrefix_edit = nullptr;       // UI输入框
  QLineEdit *m_organizesId_edit = nullptr;       // UI输入框

  QLineEdit *m_slot_edit = nullptr;            // UI输入框
  QLineEdit *m_rack_edit = nullptr;            // UI输入框
  QComboBox *m_communicate_type_box = nullptr; // UI下拉框

  // 删除设备页面
  struct DeleteDevicePage{
    QDialog *deleteDevice_page = nullptr; // UI对话框
    QPushButton *okButton = nullptr;
    QPushButton *cancelButton = nullptr;
  }m_deleteDevice_page;

  // 删除设备页面
  struct DeleteDataBlockPage {
    QDialog *deleteDataBlock_page = nullptr; // UI对话框
    QPushButton *okButton = nullptr;
    QPushButton *cancelButton = nullptr;
  } m_deleteDataBlock_page;

  // 其他UI组件
  // DeviceMonitorWidget *m_device_monitor; // UI组件
  DropArea *drop_Area = nullptr;         // UI拖放区域
  // QDialog *add_Dialog = nullptr;         // UI对话框

  // coordinate widget
  S7_DeviceManager *m_manager = nullptr;
  Scope *m_scope = nullptr;
  std::vector<DeviceTableInfo> m_deviceTableVector;

  bool VariableInitializeStatus = false;
public:
  // 业务方法
  void buildConnection();
  bool checkRepeatDeviceTable(const QString &identify,const QString &dataBlockName);

  //  initialize funtion
  void initialize();
  void initialize_toolBar();
  void initialize_searchBar();
  void initalize_last_parameter();
  void initalize_DeviceMenu();
  void initalize_Drop();
  void initalize_scope(std::shared_ptr<Scope> &scope);
  void initialize_Add_Device_Page();
  void initialize_Delete_Device_Page();
  void initialize_Delete_DataBlock_Page();

  //  menu show function
  void showBlankAreaContextMenu(const QPoint &pos);
  void showDataStructMenu(const QPoint &globalPos);
  void showDeviceContextMenu(const QPoint &globalPos);

public slots:
  void onCustomContextMenu(const QPoint &pos);
  void onAddDevice();
  void onDeleteDevice();
  
  //  search tool function
  void onFilterTree(const QString &ip_Address);

  //  data block config function
  void onLoadExternalDataBlock();
  void onDeleteDataBlock();
  void onLoadInternalDataBlock();

  //  tree item function
  void onTreeItemExpanded(QTreeWidgetItem *item);
  void onTreeItemCollapsed(QTreeWidgetItem *item); 
  void onTreeItemClicked(QTreeWidgetItem *item, int column);

  //  tab function
  void onTabCloseRequested(int index); 
  void onTabChanged(int index); 
  void onCloseDataBlockClicked(const QString &ip, const QString &blockName); 
  void rebuildTabIndexMap(); 

private:
  //  update status functino
  void update_lastest_itemPointer(QTreeWidgetItem *parent_item,
                                  QTreeWidgetItem *sub_item);
  void update_lastest_folder_path(const std::string &ip_Address);
  void update_parent_item_color(bool status);
  void update_removeTab(const QString &tabTitle); 

  //  create folder
  Result<QString, RichError> create_device_folder(const QString &ipAddress);

  // clear function

  //  check function
  Result<bool, RichError> CheckIpAddressFileName(const QString &fileName);
  Result<ItemType, RichError> validExistenceParent(QTreeWidgetItem *item);

signals:
  void requestTreeClicked(const QString &path, QTreeWidgetItem *item);
};

//  2.业务逻辑层
class S7_DeviceManager : public QObject{
  Q_OBJECT
public:
  explicit S7_DeviceManager(QObject *parent = nullptr);

  //  initialize function
  void initialize();
  void initalize_scope(std::shared_ptr<Scope> scope);

  // 业务方法
  void handleMetricSendRequest(int times);
 
  void handleExternalOPCUAConnectRequest(const QString &ip_Address, int nameSpace,
                                       int port,const QString &identifier);
  void handleExternalOPCUAInlineBrowsetConnectRequest(const QString &ip_Address,
                                         int nameSpace, int port,const std::string &urlPrefix,const int &objectId,const QString &identifier);
  void handleExternalS7ConnectRequest(const QString &ip_Address, int rack,
                                    int slot,const QString &identifier);
  bool handleConnectRequest(const QString &ip_Address,
                            const std::string &connectWay);
  bool handleParseFile(const QString &filePath,const QString &identify);

  QWidget* handleSwithView(const QString &identifier, const QString &filePath); 

private:
  // Scope Object
  Scope *m_scope = nullptr;

  //  PROMETHEUS VAIRABLE
  ServiceMetrics *m_metrics = nullptr;

  // 设备相关
  S7_MainWindows_UI *m_UI = nullptr;
  OPCUADataBlockManager *m_OPCUAdataBlockManager = nullptr;

  // 文件路径相关
  QString m_lastest_device_folder_path = ""; // 业务状态

  // 日志
  // std::shared_ptr<spdlog::logger> m_log; // 基础设施

private slots:
  void onRequestTreeClicked(const QString &path, QTreeWidgetItem *item){std::cout<<"onRequestTreeClicked call !n";}
};

//  3. 数据层
class S7DataRepository {
    Result<QString, RichError> create_device_folder(const QString& ip);
    Result<bool, RichError> Delete_File_To_Tab(int index);
    Result<bool, RichError> CheckIpAddressFileName(const QString &fileName);

    // 纯数据操作，不涉及UI
};

class SystemSetting : public QDialog 
{
    Q_OBJECT
    public:
      SystemSetting() = default;
      ~SystemSetting() = default;

      void createMQTTPage();
      void createPLCPage();
      void createOperationPage();
      void createPlateformIntegrationPage();
      void createModbusPage();

      void hotUpdate_setting();
      void display_current_setting();
      bool connect_retry(int times){return true;};

      System_Parameter m_par;
    private:
      std::shared_ptr<spdlog::logger> m_log;

      QCheckBox *m_TLS_box;
      QCheckBox *m_SSL_box;
      QLineEdit *m_mqtt_publish_frequency_edit;
      QLineEdit *m_mqtt_retry_times_edit ;
      QLineEdit *m_mqtt_retry_distance_time_edit;
      QLineEdit *m_refresh_Edit ;

      QListWidget *m_fileListWidget;
      QStackedWidget *m_stackWidget;
      QHBoxLayout *m_main_layout_H;
      QPushButton *m_QuerySettingUpdate;
      QLineEdit *user_name_edit;
      QLineEdit *password_edit ;

      QComboBox *m_log_combobox ;
      QComboBox *m_log_output_format_combobox ;
      QCheckBox *m_prometheus_box ;
      QLineEdit *m_prometheus_edit ;

      QComboBox *m_cloud_platform_box ;
      QCheckBox *m_check_box ;
      QComboBox *m_API_certification_type_box ;
      QLineEdit *m_API_key_edit ;

      QWidget *userPassword_page;
      QWidget *clientCertificate_page;
      QWidget *m_operation_page;
      QWidget *m_platformIntegration_page;

      QString certificate_folder_path;
};
