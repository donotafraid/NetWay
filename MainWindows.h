#ifndef MainWindows_H
#define MainWindows_H

//显示
#include "MainWindows/Struct.h"
#include "PLC/Map_PLCStruct.h"
#include "MainWindows/DropArea.h"
#include "MainWindows/Monitor.h"
#include "ServiceMetrics/serviceMetrics.h"
#include "GrafanaDashboardManager/ObjectRouter.h"
#include "MainWindows/LineChartTest.h"

//forward declare
class Sqlite_DB;
class Sqlite_DB_Manager;
class memory_pool;
class Sqlite_information;
class taskExecution;
class MqttClient;

//class declare
class MQTT_buffer_monitor : public QWidget
{
    Q_OBJECT
public:
    explicit MQTT_buffer_monitor(QWidget *parent,buffer_administrator& buffer_administrator_ref);
    ~MQTT_buffer_monitor() = default;
  
    void progressUpdate();
    
private:
    buffer_administrator& m_buffer_administrator_ref;

    QLabel *m_buffer_label;
    QLabel *m_progress_buffer_label;
    QWidget *m_buffer_indicator;

    QLabel *m_inflight_label;
    QLabel *m_progress_inflight_label;
    QWidget *m_inflight_indicator;
 
};


//class declare
class FileProgressItem : public QWidget
{
    Q_OBJECT
public:
    explicit FileProgressItem(QWidget *parent,const std::string& file_name);
    ~FileProgressItem();
    void setFilePath(const std::string& file_name);
    int setProgress(int progress);
    void set_status_indicator(const bool status);
    int return_countSize(const std::string& file_path);
    std::string returnFilePath() const;
    bool is_download_complete() const;
private:
    int m_missing_index_number ;
    int m_total ;
    std::string m_file_name;

    QLabel *m_fileNameLabel;
    QLabel *m_progressTextLabel;
    QWidget *m_statusIndicator;
};

class DownloadTasks : public QObject , public QRunnable
{
    Q_OBJECT
    public:
    //构造/析构函数********************************************
    explicit  DownloadTasks(Sqlite_information& sqlite_information,MqttClient& mqtt_client_ptr);
    ~DownloadTasks() ;

    //外部调用接口******************************************
    //核心功能API（线程安全）
    bool is_exist_targetFolder(std::string target_folder_path);
    bool addItem_cache(FileProgressItem* file_info);
    bool deleteItem_cache(FileProgressItem* file_info);
    void set_isPause(bool usPause);
    bool return_isPause();
    void update_fileProgress();
    int intetface_read_missing_slices_from_db_information_file(const std::string& file_path); 
    int interface_create_new_file_on_db_information(const std::string& file_path);
    bool is_download_complete();
    void clear_corresponding_file_information();
    void merge_done_task();
    void start_update_message_buffer();

    //资源清理
    bool clear_corresponding_cache();

    //信号量*******************************************
    signals:
        void update_progress_bar();
    
    //内部接口********************************************
    protected:
    //核心处理逻辑
    bool internalProgressUpdate();
    void run() override;

    private:
    //容器对象*******************************************
        //主数据容器(带访问控制)
        QVector<FileProgressItem*> m_fileProgressList;  
        //缓存容器(自动淘汰)
        //临时数据(作用域生命周期)
        QScopedPointer<FileProgressItem> m_tempData;

        //基本类型成员变量
        int m_isFinished = 0;
        std::unordered_map<std::string,FileProgressItem*> m_fileProgressMap;
        Sqlite_information& m_sqlite_information_ref;
        taskExecution* m_taskExecution_ptr;
        bool m_isPause = false;
        bool m_delete_status = true;

    //辅助工具*******************************************
        QThreadPool* m_threadPool;
};


class MQTT_Platform_UI : public QMainWindow 
{
    Q_OBJECT

    public:
    MQTT_Platform_UI(QWidget *parent,DownloadTasks* downloadTasks,download_path_manager& download_path_manager,buffer_administrator& buffer_administrator,Single_Data_Block* single_data_block_manager);
    ~MQTT_Platform_UI();
    void process_file(const QString& file_path);
    void check_DownLoadFolder_initalize();
    void addFileToList(const QString& file_path);
    void initialize(DownloadTasks* downloadTasks);
    std::string get_current_date_string();

    // 加载
    private slots:
    void on_DownloadButton_clicked();
    void on_DeleteButton_clicked();
    void on_ClearButton_clicked();
    void on_information_downloadList_clicked();
    void on_pauseButton_clicked();
    void on_loadTaskButton_clicked();
    void on_mergeSQLiteDateButton_clicked();
    void on_selectFolderPathButton_clicked();
    void on_messageBufferButton_clicked();
    void update_buffer_progress();

    // 显示
    private:
    void initUI(buffer_administrator& buffer_administrator_ref);

    // 显示
    QListWidget *m_fileListWidget;

    // 加载
    QPushButton *m_DownloadButton;
    QPushButton *m_deleteSingleButton;
    QPushButton *m_clearButton;
    QPushButton *m_DownLoadedListButton;
    QPushButton *m_pauseButton;
    QPushButton *m_loadDownTaskButton;
    QPushButton *m_mergeSQLiteDateButton;
    QPushButton *m_selectFolderPathButton;
    QPushButton *m_messageBufferButton;
    QPushButton *m_PLC_SendMessageButton;
    QPushButton *m_PLC_ReceiveMessageButton;

    // 储存
    QDialog *m_DownLoadedListDialog;
    QStringList m_fileList;
    QMap<QString,FileProgressItem*> m_fileProgressMap;
    QWidget* current_page = nullptr;

    // 后台处理器
    QThread m_workerThread;
    DownloadTasks* m_downloadTasks;
    download_path_manager& m_download_path_manager;
    MQTT_buffer_monitor*  m_mqtt_buffer_monitor = nullptr;
    Single_Data_Block* m_single_data_block = nullptr;

    protected:
    void dragEnterEvent(QDragEnterEvent *event)  override;
    void dropEvent(QDropEvent *event) override ;
}; 




class SystemSetting : public QDialog 
{
    Q_OBJECT
    public:
    SystemSetting(QWidget *parent = nullptr,std::shared_ptr<spdlog::logger> log_pointer = nullptr);
    ~SystemSetting() = default ;
    
    void createMQTTPage();
    void createPLCPage();
    void createOperationPage();
    void createPlateformIntegrationPage();
    void createModbusPage();

    void hotUpdate_setting();
    void display_current_setting();
    bool connect_retry(int &times);
    
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


class S7_MainWindows : public QMainWindow{
    Q_OBJECT
    public:
    class builder{
        private:
        PLC_Device* m_plc_device_pointer = nullptr;
        QWidget* m_parent = nullptr;
        ServiceMetrics *m_serviceMetrics = nullptr;

        public:
        builder() = default;
        ~builder() = default;
        builder& set_plc_pointer(PLC_Device* plc_device_pointer)
        {
            m_plc_device_pointer = plc_device_pointer;
            return *this;
        }

        builder& set_QtParent_pointer(QWidget* widget_Pointer)
        {
            m_parent = widget_Pointer;
            return *this;
        }

        builder& set_Metric_pointer(ServiceMetrics* serviceMetric_ptr)
        {
            m_serviceMetrics = serviceMetric_ptr;
            return *this;
        }

        static builder create_builder()
        {
            return builder();
        }
        std::unique_ptr<S7_MainWindows> unique_ptr_build()
        {
            return std::make_unique<S7_MainWindows>(m_parent,m_serviceMetrics);
        }
    };

    public:
        S7_MainWindows(QWidget *parent = nullptr,ServiceMetrics *serviceMetric_ptr = nullptr);
        ~S7_MainWindows();

        //  left file tree/list
        //  QTreeWidget 只用于显示文件列表（名称列表）
        QTreeWidget *m_datablockname_tree;
        QTreeWidgetItem *m_lastest_dataBlock_TreeWidget_item;
        QTreeWidgetItem *m_lastest_device_TreeWidget_item;
        QLineEdit *m_search_bar;
        PLC_Device* m_plc_device_pointer;
        std::vector<PLC_Device*> m_plc_device_pointer_vector;

        //  right label container
        //  QTabWidget:CONTENT TAB WIDGET 
        DataStructeEditor *m_data_struct_editor = nullptr; 
        QTabWidget *m_tab_widget;

        // Log variable
        std::shared_ptr<spdlog::logger> m_log;

        //  main layout
        QVBoxLayout *m_main_layout_V;

        //  other members
        // S7_Button_Layout* m_button_layout;
        QVBoxLayout *right_layout;
        QString m_lastest_device_folder_path = "";

        //  SEARCH BAR MEMBERS
        QCompleter *m_completer;
        QStringListModel *m_completerModel;
        QStringList m_match_list;

        //  SYSTEM SETTING BAR MEMBER
        SystemSetting *m_systemSetting = nullptr;

        //  prometheus view display
        QWidget *m_prometheus_view = nullptr;
        MonitoringDashboard *m_monitor_dashBoard = nullptr;
        LineChart *m_LineChart = nullptr;

        //  ADD DEVICE MEMBER
        QDialog *m_addDevice_page = nullptr;
        QLineEdit *m_ip_Address_edit;
        QLineEdit *m_nameSpace_edit;
        QLineEdit *m_port_edit;
        QComboBox *m_communicate_type_box;
        void initialize_Add_Device_Page();

        //  PLC DEVICE MENU
        QMenu device_menu_var;
        QAction *loadExternalDataConfigAction = nullptr;
        QAction *deleteDataBLockAction = nullptr;
        QAction *loadInternalDataConfigAction = nullptr;
        QMenu dataStruct_menu_var;
        QAction *deleteExistingDataConfigAction = nullptr;
        DeviceMonitorWidget *m_device_monitor;
        QDialog *add_Dialog = nullptr;
        DropArea *drop_Area = nullptr;

        //  BUILD DEVICE FOLDER ACCORDING TO DEVICE IP ADDRESS
        Result<QString,RichError> create_device_folder(const QString &ipAddress); 

        //  BUILD CONNECTION BETWEEN MAIN_WINDOWS AND TOOLBAR
        void initialize_toolBar();
        void build_connect_searchBar();

        Result<bool,RichError> CheckIpAddressFileName(const QString& fileName);
        Result<bool,RichError> CheckSelectFileInTab(const QString& file_path,QTreeWidgetItem *item);
        Result<bool,RichError> Delete_File_To_Tab(int index);
        Result<bool,RichError> Display_File_To_Tab(const QString& file_path);
        Result<bool,RichError> is_connect_check();

        void clear_tree_DataBlock(QTreeWidgetItem* item);
        void load_tree_DataBlock(QTreeWidgetItem* item,PLC_Device* device_ptr);
        
        //  SECONDARY MENU
        Result<bool,RichError> clear_delete_device(PLC_Device *pointer);
        Result<bool,RichError> validExistenceParent(const QString& QString_file_path);
        Result<bool,RichError> validExistenceParent(QTreeWidgetItem *item);
        void initalize_last_parameter();
        void update_lastest_folder_path();
        void update_lastest_itemPointer(QTreeWidgetItem *parent_item,QTreeWidgetItem *item);

        void loadExistingDataConfigFile();
        void findDataConfig(const QString &folder_path, QStringList &result);
        void save_DataConfig(const std::string &source_file_content,
                             const QString &dest_file_path);
        QString getFullFilePath(const QString &fileName);
        Result<bool, RichError>
        deal_parsedDataBlockConfig(const QFileInfo &file_name,
                                   std::string &file_content);

        //  UPDATE SELECT TAB_WIDGET
        void update_select_tab(DataStructeEditor *item,const QString &file_path);
        //  GET TAB WIDGET INDEX
        int return_tab_index(QTreeWidgetItem *item);
        //  UPDATE PARENT QTREEWIDGET ITEM COLOR
        void update_parent_item_color(bool status);
        
        void onDeleteSelectTabClicked(int index);
        //  PROMETHEUS VAIRABLE
        ServiceMetrics *m_metrics;
    private slots:
        void write_DataBlock_From_TableBuffer(bool is_last_success);
        void write_Table_From_S7_DatabBlockBuffer(bool is_last_success);
        void read_DataBlock_From_DataBlockBuffer(bool is_done_successfully);

        //  FILTER SPECIAL IP_ADDRESS FROM QTREEWIDGET
        void filterTree(const QString& text);

        //  SELECT ITEM IN QTRRRWIDGET
        void onOpenSelectFileClicked(QTreeWidgetItem* item);

        //  EXPAND/COLLAPS QTREEWIDGET ITEM
        void onTreeItemExpanded(QTreeWidgetItem* item);
        void onTreeItemCollapsed(QTreeWidgetItem* item);

        //  MENU ON QTREEWIDGET ITEM
        void onCustomContextMenu(const QPoint &pos);
        void showBlankAreaContextMenu(const QPoint &pos);
        void showDeviceContextMenu(const QPoint &globalPos);
        void showDataStructMenu(const QPoint &globalPos);

        //  BLANK AREA CONTEXT MENU
        void onAddDevice();
        //  DEVICE CONTEXT MENU
        void onDeleteDevice();
        void onAddDataBlockConfig();
        void onLoadExistingDataConfig();
        void onDeleteDataStruct();

        void addItem_To_TreeAndTab();
       
        //  DATABLOCK CONTEXT MENU
        void onDeleteDataBlockConfig(PLC_Device *device_pointer,QTreeWidgetItem *item);
};

#endif