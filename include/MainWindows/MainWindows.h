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

//class declare
class FileProgressItem : public QWidget
{
    Q_OBJECT
public:
    explicit FileProgressItem(QWidget *parent,MainWindows_Intermediate_Struct&& file_info);
    ~FileProgressItem();
    void setFileName(const std::string& file_name);
    int setProgress(int progress);
    void set_status_indicator(const bool status);
    int return_countSize(const std::string& file_path);
    std::string returnFileName() const;
    QString fileName() const;
    MainWindows_Intermediate_Struct fileInfo();
private:
    int m_current ;
    int m_total ;
    MainWindows_Intermediate_Struct m_fileInfo;
    QLabel *m_fileNameLabel;
    QLabel *m_progressTextLabel;
    QWidget *m_statusIndicator;
};

class DownloadTasks : public QObject , public QRunnable
{
    Q_OBJECT
    public:
    //构造/析构函数********************************************
        explicit DownloadTasks(Sqlite_DB_Manager* sqlite_DB):
            m_sqlite_DB_Manager(sqlite_DB),m_threadPool(QThreadPool::globalInstance())
        {
        }

        ~DownloadTasks() = default;

    //外部调用接口******************************************
    //核心功能API（线程安全）
    bool isNewDownTask(QString file_path);
    bool is_exist_targetFolder(std::string target_folder_path);
    bool addItem_cache(FileProgressItem* file_info);
    bool deleteItem_cache(FileProgressItem* file_info);
    bool cleaer_cache();
    void clear_run_status();
    int return_scheduled_tasks(const std::string& folder_path);
    void set_isPause(bool isPause);
    bool return_isPause();
    //状态查询接口
    
    //异常安全接口
    
    //状态变更通知
    signals:
    void update_statusFinished();
    
    public slots:
    //异步操作接口

    //同步操作接口

    //内部接口********************************************
    protected:
        //核心处理逻辑
        bool internalProgressUpdate();
        void run() override;

        //资源清理

        //线程安全操作

    private:
    //容器对象*******************************************
        //主数据容器(带访问控制)
        QVector<FileProgressItem*> m_fileProgressList;  
        //缓存容器(自动淘汰)
        //临时数据(作用域生命周期)
        QScopedPointer<FileProgressItem> m_tempData;

        //基本类型成员变量
        std::atomic_bool m_isFinished = false;
        std::unordered_map<std::string,MainWindows_Intermediate_Struct> m_fileInfoMap;
        std::unordered_map<std::string,FileProgressItem*> m_fileProgressMap;
        std::unordered_map<std::string,bool> m_tasks_done_status;
        bool m_isPause = false;

        //其余类对象指针/引用 
        Sqlite_DB_Manager*  m_sqlite_DB_Manager;

    //辅助工具*******************************************
        std::mutex m_mutex;
        QThreadPool* m_threadPool;
};

class mergeSQLData
{
    public:

    const char *sql_select_from_record = "SELECT file_id , output_file_path FROM slice_records";
    const char *sql_select_from_slice_content = "SELECT file_id , slice_index , plaintext FROM slice_contents WHERE file_id = ? ORDER BY slice_index ASC";
    sqlite3* m_db_ptr = nullptr;
    sqlite3_stmt* m_stmt_select_from_record = nullptr;
    sqlite3_stmt* m_stmt_select_from_slice_content = nullptr;

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

    // 加载

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