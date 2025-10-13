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


class MainWindows : public QMainWindow 
{
    Q_OBJECT

    public:
    MainWindows(QWidget *parent,DownloadTasks* downloadTasks,download_path_manager& download_path_manager,buffer_administrator& buffer_administrator);
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

    protected:
    void dragEnterEvent(QDragEnterEvent *event)  override;
    void dropEvent(QDropEvent *event) override ;
}; 



#endif