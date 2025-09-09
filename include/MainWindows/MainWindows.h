#ifndef MAINWINDOWS_H
#define MAINWINDOWS_H

//显示
#include <QtWidgets/qapplication.h>
#include <QtWidgets/qmainwindow.h>
#include <QtCore/qstringlist.h>
#include <QtWidgets/qlistwidget.h>
#include <QtWidgets/qpushbutton.h>
#include <QtWidgets/qboxlayout.h>
#include <QtWidgets/qfiledialog.h>
#include <QtWidgets/qmessagebox.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qdebug.h>
#include <QtGui/qdrag.h>
#include <QtGui/qevent.h>
#include <QtCore/qdiriterator.h>
#include <QtCore/qpropertyanimation.h>
#include <QtWidgets/qlabel.h>
#include <QtWidgets/qprogressbar.h>
#include <QtCore/qthreadpool.h>
#include <QtCore/qobjectdefs.h>
#include <QtCore/qrunnable.h>
#include <QtCore/qmutex.h>
#include <QtCore/qcache.h>
#include <QtCore/qmimedata.h>

#include "load_config/load_config.h"

//forward declare
class Sqlite_DB;
class Sqlite_DB_Manager;
class memory_pool;
class Sqlite_information;
class taskExecution;
class MqttClient;

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
    void set_isPause(bool isPause);
    bool return_isPause();
    void update_fileProgress();
    int intetface_read_missing_slices_from_db_information_file(const std::string& file_path); 
    int interface_create_new_file_on_db_information(const std::string& file_path);
    bool is_download_complete();

    //资源清理
    bool clear_progreeMap_cache();
    
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

    //辅助工具*******************************************
        QThreadPool* m_threadPool;
};

class mergeSQLData
{
    public:
    const char* request_sql_select_from_record = "SELECT file_id , missing_slices_json , input_file_path , output_file_path FROM slice_records";
    const char *sql_select_from_record = "SELECT file_id , output_file_path FROM slice_records";
    const char *sql_select_from_slice_content = "SELECT file_id , slice_index , plaintext  FROM slice_contents WHERE file_id = ? ORDER BY slice_index ASC";
    sqlite3* m_db_ptr = nullptr;
    std::queue<std::string> m_protobuf_queue;
    sqlite3_stmt* m_stmt_select_from_record = nullptr;
    sqlite3_stmt* m_stmt_select_from_slice_content = nullptr;
    sqlite3_stmt* m_request_stmt_select_from_record = nullptr;

    void presetting();
    void task_publish();
    QDir loop_dbFile_in_path();
    void initialize_db_ptr();
    void merge_file();
    void clear_struct_setting();
};

class MainWindows : public QMainWindow 
{
    Q_OBJECT

    public:
    MainWindows(QWidget *parent = nullptr,DownloadTasks* downloadTasks=nullptr);
    ~MainWindows();
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

    // 显示
    private:
    void initUI();

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

    // 储存
    QDialog *m_DownLoadedListDialog;
    QStringList m_fileList;
    QMap<QString,FileProgressItem*> m_fileProgressMap;
    QWidget* current_page = nullptr;

    // 后台处理器
    QThread m_workerThread;
    DownloadTasks* m_downloadTasks;
    mergeSQLData m_merger;

    protected:
    void dragEnterEvent(QDragEnterEvent *event)  override;
    void dropEvent(QDropEvent *event) override ;
}; 
#endif