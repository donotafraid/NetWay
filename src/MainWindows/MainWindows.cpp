#include "MainWindows/MainWindows.h"
#include "Sqlite_DB/Sqlite_DB.h"

MainWindows::MainWindows(QWidget *parent)
    : QMainWindow(parent)
{
    initUI();
    setWindowTitle("文件传输工具");
    setAcceptDrops(true);
    resize(600, 400);   

}

MainWindows::~MainWindows()
{
    delete m_fileListWidget ;
}

void MainWindows::check_DownLoadFolder_initalize()
{
    QString folder_name = "DownloadFileManagement";

    QString currentDir = QDir::currentPath();

    QString folder_path = currentDir + "/" + folder_name;

    QDir dir(folder_path);

    if (!dir.exists())
    {
       if( dir.mkpath(folder_path) )
       {
           qDebug() << "create folder success";
       }
       else
       {
            qDebug() << "create folder failed";
       }
    }
    else
    {
        qDebug() << "folder exist";
        return ;
    }
}

void MainWindows::addFileToList(const QString & file_path)
{
    
    if(!QFile::exists(file_path)||
    !m_downloadTasks->isNewDownTask(file_path)||
    0) 
    {
        std::cout<<"File not found or File has exist! "<<std::endl;
        return;
    }

    QFileInfo file(file_path);

    // 检测文件是否存在对应的文件夹
    QString currentDir = QDir::currentPath();
    QString folder_path = currentDir + "/" + "DownloadFileManagement";

    QString file_folder_name = file.fileName();

    MainWindows_Intermediate_Struct intermediate_struct;
    intermediate_struct.stored_DB_folder_path =  (folder_path + "/" + file_folder_name).toStdString();
    intermediate_struct.source_file_path = file_path.toStdString();
    intermediate_struct.db_file_path = folder_path.toStdString() + '/' + get_current_date_string() + ".db"; 
    intermediate_struct.output_folder_path = intermediate_struct.stored_DB_folder_path;
    intermediate_struct.output_file_path = intermediate_struct.output_folder_path + '/' + file.fileName().toStdString();
    intermediate_struct.file_size = file.size();

    FileProgressItem *progressItem = new FileProgressItem(this->m_fileListWidget,std::move(intermediate_struct));
    QListWidgetItem* listItem = new QListWidgetItem(this->m_fileListWidget);

    progressItem->setFileName(file_path.toStdString());
    progressItem->return_countSize(file_path.toStdString());
    progressItem->setProgress(0);

    listItem->setSizeHint(progressItem->sizeHint());
    m_fileListWidget->setItemWidget(listItem,progressItem);

    m_downloadTasks->addItem_cache(progressItem);
}

void MainWindows::initUI()
{
    // 显示
    QWidget *widget = new QWidget(this);
    setCentralWidget(widget);

    // 显示文件部件
    m_fileListWidget = new QListWidget(this);
    m_fileListWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileListWidget->setAcceptDrops(true);
    m_fileListWidget->setDragDropMode(QAbstractItemView::DropOnly);
    m_fileListWidget->setStyleSheet("QListWidget { border:1px solid gray;}");

    // 加载按钮
    m_DownloadButton = new QPushButton("download button", this);
    m_deleteSingleButton = new QPushButton("delete button", this);
    m_clearButton = new QPushButton("clear button", this);
    m_DownLoadedListButton = new QPushButton("download list button", this);
    m_pauseButton = new QPushButton("pause button", this);
    m_loadDownTaskButton = new QPushButton("load download task button", this);

    // 显示控件到布局
    QVBoxLayout *main_layout = new QVBoxLayout(centralWidget());
    main_layout->addWidget(m_fileListWidget,1);

    QHBoxLayout *button_layout = new QHBoxLayout();
    QHBoxLayout *button_layout_2 = new QHBoxLayout();
    button_layout->addWidget(m_DownloadButton);
    button_layout->addWidget(m_deleteSingleButton);
    button_layout->addWidget(m_clearButton);
    button_layout_2->addWidget(m_DownLoadedListButton);
    button_layout_2->addWidget(m_pauseButton);
    button_layout_2->addWidget(m_loadDownTaskButton);
    main_layout->addLayout(button_layout);
    main_layout->addLayout(button_layout_2);

    // 加载信号槽
    connect (m_DownloadButton, SIGNAL(clicked()), this, SLOT(on_DownloadButton_clicked()));
    connect (m_deleteSingleButton, SIGNAL(clicked()), this, SLOT(on_DeleteButton_clicked()));
    connect (m_clearButton, SIGNAL(clicked()), this, SLOT(on_ClearButton_clicked()));
    connect (m_DownLoadedListButton, SIGNAL(clicked()),this, SLOT(on_information_downloadList_clicked()));
    connect (m_pauseButton, SIGNAL(clicked()),this, SLOT(on_pauseButton_clicked()));
    connect (m_loadDownTaskButton, SIGNAL(clicked()),this, SLOT(on_loadTaskButton_clicked()));
}

void MainWindows::on_DownloadButton_clicked()
{
    QMessageBox::information(this, "提示", "点击OK后开始下载...");
    int item_count = m_fileListWidget -> count ();
    if (item_count == 0)
    {
        QMessageBox::information(this, "提示", "文件列表为空...");
        return;
    }
    m_downloadTasks->setAutoDelete(false);
    QThreadPool::globalInstance()->start(m_downloadTasks);
}


void MainWindows::on_DeleteButton_clicked()
{
    // store selected object
    QList<QListWidgetItem*> selected_item = m_fileListWidget->selectedItems();
    if (selected_item.isEmpty())
    {
        QMessageBox::information(this, "提示", "请选择要删除的文件");
        return;
    }
  
    for (auto& item : selected_item)
    {
        FileProgressItem* progressItem = qobject_cast<FileProgressItem*>
        (m_fileListWidget->itemWidget(item));

        if (progressItem)
        {
            m_downloadTasks->deleteItem_cache(progressItem);
            delete m_fileListWidget->takeItem(m_fileListWidget->row(item));
        }
    }
}

void MainWindows::on_ClearButton_clicked()
{
    // 清除所有的run（），之后再清空文件列表
    {
        m_fileListWidget->clear();
        m_downloadTasks->cleaer_cache();
    }

    // //清除 DB_Manager 相关的对象
    {
        this->m_downloadTasks->clear_run_status();
    }
}

void MainWindows::on_information_downloadList_clicked()
{
    QString currentDirPath = QDir::currentPath();

    QString targetDir = currentDirPath + "/DownloadFileManagement";
    QDir downloadDir (targetDir);

    if( !downloadDir.exists())
    {
        QMessageBox::information(this, "提示", "下载文件夹不存在");
        return;
    }

    m_DownLoadedListDialog = new QDialog(this);
    m_DownLoadedListDialog->setWindowTitle("下载列表");
    m_DownLoadedListDialog->setModal(false);
    m_DownLoadedListDialog->setWindowFlags(m_DownLoadedListDialog->windowFlags() | Qt::WindowMinimizeButtonHint);

    // main layout 
    QVBoxLayout* main_layout = new QVBoxLayout(m_DownLoadedListDialog);

    //file list widget
    QListWidget* file_list_widget = new QListWidget(m_DownLoadedListDialog);
    main_layout->addWidget(file_list_widget);

    //file in content
    //遍历 target dir 下面的全部文件夹，并获取全部的文件夹名
    //每获取一次文件夹名，就对当前的文件夹下面的文件进行遍历，判断该文件夹名是否存在对应文件在当前的文件夹里
    //如果存在，就添加到列表中
    QStringList subDirs = downloadDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    //loop signal folder
    for(const QString& dir_name:subDirs)
    {
        // extract folder_path 
        QDir subDir(downloadDir.filePath(dir_name));

        // extract file list in this folder
        QStringList files = subDir.entryList(QDir::Files); 
        
        // if the folder exist the file its name is same with the folder name , add it to file lis widget
        for(const QString& file:files)
        {
            if(file == dir_name)
            {
                QString itemText = QString("%1").arg(dir_name);
                file_list_widget->addItem(itemText);
                break;
            }
        }
    }
    
    //show dialog box
    m_DownLoadedListDialog->show();
}

void MainWindows::on_pauseButton_clicked()
{
    bool currentPaused = m_downloadTasks->return_isPause();

    // exchange the pause status
    bool newPaused = !currentPaused;
    this->m_downloadTasks->set_isPause(newPaused);

    //renew button style
    if(newPaused)
    {
        m_pauseButton->setStyleSheet("background-color: gray;border-radius: 25px;");
    }
    else{
        m_pauseButton->setStyleSheet("");
    }
}

void MainWindows::on_loadTaskButton_clicked()
{
    QString currentDirPath = QDir::currentPath();

    QString targetDir = currentDirPath + "/FileManagement";
    QDir downloadDir (targetDir);

    if( !downloadDir.exists())
    {
       if( downloadDir.mkpath(targetDir) )
       {
           qDebug() << "create folder success";
       }
       else
       {
           QMessageBox::information(this, "提示", "下载文件夹不存在");
           return;
       }
    }

    QStringList subDirs = downloadDir.entryList(QDir::Files | QDir::NoDotAndDotDot);
    for(const auto& file_path: subDirs)
        {
            QString full_path = downloadDir.filePath(file_path);
            this->addFileToList(full_path);
        }
}

void MainWindows::initialize(DownloadTasks *downloadTasks)
{
    m_downloadTasks = downloadTasks;
}

std::string MainWindows::get_current_date_string()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d"); // 格式化为 "20240520"
    return oss.str();
}

void MainWindows::process_file(const QString& file_path)
{
    return; 
}

void MainWindows::dragEnterEvent(QDragEnterEvent * event)
{
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
    }
}


void MainWindows::dropEvent(QDropEvent * event)
{
    const QMimeData *mimeData = event->mimeData();
    if (mimeData->hasUrls())
    {
        for(auto& url: mimeData->urls())
        {
            QString file_path = url.toLocalFile();
            this->addFileToList(file_path);
        }
    }
    event->acceptProposedAction();
}

FileProgressItem::FileProgressItem(QWidget *parent,MainWindows_Intermediate_Struct&& file_info):
m_fileInfo(file_info)
{
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setSpacing(50);

    m_fileNameLabel = new QLabel(this);
    m_fileNameLabel ->setMinimumWidth(200);

    m_progressTextLabel = new QLabel(this);
    m_progressTextLabel ->setMinimumWidth(100);

    m_statusIndicator = new QLabel(this);
    m_statusIndicator ->setMinimumWidth(100);
    m_statusIndicator->setFixedSize(50,50);
    m_statusIndicator->setStyleSheet("background-color: red;border-radius: 25px;");

    layout->addWidget(m_statusIndicator);
    layout->addWidget(m_fileNameLabel);
    layout->addWidget(m_progressTextLabel);
}

void FileProgressItem::setFileName(const std::string& file_name)
{
    auto file_path = QString::fromStdString(file_name); 
    m_fileNameLabel->setText(file_path);
}

std::string FileProgressItem::returnFileName() const
{
    return this->m_fileInfo.source_file_path;
}

int FileProgressItem::setProgress(int progress)
{
    m_current = progress;
    m_progressTextLabel->setText(QString::number(m_current) + "/" + QString::number(m_total));
    if (m_current == m_total)
    {
        set_status_indicator(m_current == m_total);
    }

    return 0;
}

void FileProgressItem::set_status_indicator(const bool status)
{
    m_statusIndicator->setStyleSheet(
        QString( 
        "background-color: %1 ;border-radius: 25px;")
        .arg(status ? "green" : "red"));

}

int FileProgressItem::return_countSize(const std::string& file_path)
{
    auto source_file_path = QString::fromStdString(file_path); 
    QFile file(source_file_path);
    if(!file.open(QIODevice::ReadOnly))
    {
        std::cerr<<"return_countSize failed and error is : "<<file.errorString().toStdString();    
        return false;
    }

    m_total = file.size();
    this->m_fileInfo.file_size = m_total;

    file.close();
    return m_total;
}

QString FileProgressItem::fileName() const{
    return m_fileNameLabel->text();
}

MainWindows_Intermediate_Struct FileProgressItem::fileInfo () 
{
    return m_fileInfo;
}

FileProgressItem::~FileProgressItem()
{
    std::cout<<"~FileProgressItem called ! \n";
}

void DownloadTasks::run()
{
    std::vector<MainWindows_Intermediate_Struct> tem_vector;
    int schduled_tasks  = 0;
    
    try
    {
        for(auto& item : m_fileInfoMap)
        {
            tem_vector.push_back(item.second);
            schduled_tasks +=(item.second.file_size + SLICE_SIZE -1 )/SLICE_SIZE - return_scheduled_tasks(item.second.output_folder_path); 
        }

        m_sqlite_DB_Manager->set_scheduled_tasks_to_ThreadPOol(schduled_tasks);
        m_sqlite_DB_Manager->Assign_tasks_to_sqlite_SB_Store(tem_vector);

        while(!m_isFinished)
        {
            m_isFinished = ( this->m_sqlite_DB_Manager->return_worked_tasks() == 
            this->m_sqlite_DB_Manager->return_scheduled_tasks() )||
            this->m_sqlite_DB_Manager->return_is_threadPool_active();
            auto tem_map = m_sqlite_DB_Manager->return_file_data_map();
            {
                for(auto& item : tem_map)
                {
                    // file_progress compare with file_size to check if the file is done
                    m_tasks_done_status[item.first] = item.second->load() && (m_fileProgressMap[item.first]->return_countSize(item.first));
                    m_fileProgressMap[item.first]->setProgress(item.second->load());
                }
            }
            m_sqlite_DB_Manager->load_work_from_lockfree_queue();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            while(return_isPause())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                std::cout<<"the thread is paused !"<<std::endl;
                m_isFinished = true;
            }
        }
        // this->m_sqlite_DB_Manager->merge_download_file(tem_vector);
        this->m_sqlite_DB_Manager->delete_done_tasks_in_process_map();
        this->clear_run_status();
    }
    catch(const std::exception& e)
    {
        std::cerr <<"run() error occured: "<< e.what() << '\n';
        backward::StackTrace st;
        st.load_here(32);
        backward::Printer p;
        p.print(st);
    }

}

bool DownloadTasks::addItem_cache(FileProgressItem* file_list_widget)
{
    m_fileInfoMap.emplace(file_list_widget->returnFileName(), file_list_widget->fileInfo());
    m_fileProgressMap.emplace(file_list_widget->returnFileName(), file_list_widget);
    return true;
}

bool DownloadTasks::deleteItem_cache(FileProgressItem* file_list_widget)
{
    m_fileInfoMap.erase(file_list_widget->returnFileName());
    m_fileProgressMap.erase(file_list_widget->returnFileName());
    return true;
}

bool DownloadTasks::cleaer_cache()
{
    m_fileInfoMap.clear();
    m_fileProgressMap.clear();
    return true;
}

void DownloadTasks::clear_run_status()
{
    m_isFinished = false;
    this->m_sqlite_DB_Manager->delete_done_tasks_in_process_map();
}

int DownloadTasks::return_scheduled_tasks(const std::string& folder_path)
{
    int count = 0;
    //the actual completed task count
    int rc = is_exist_targetFolder(folder_path);  
    if (rc == true)
    {

        for(const auto& it : fs::directory_iterator(folder_path))
        {
            if(it.is_regular_file())
            {
                std::string file_name = it.path().filename().string();
    
                if(file_name.find("tmp_") != std::string::npos)
                {
                    ++count;
                }
            }
        }
        return count;
    }
    else
    {
        std::cerr<<"return_scheduled_tasks failed and error is : the folder is not exist !"<<std::endl;
        return false;
    }
}

void DownloadTasks::set_isPause(bool isPause)
{
    this->m_isPause = isPause;
}

bool DownloadTasks::return_isPause()
{
    return this->m_isPause;
}

bool DownloadTasks::is_exist_targetFolder(std::string target_folder_path)
{
    QString folder_path = QString::fromStdString(target_folder_path.c_str()) ;

    QDir dir(folder_path);

    if (!dir.exists())
    // means the folder not exist
    {
        if(!dir.mkpath(folder_path))
        {
            std::cerr<<"is_exist_downLoadFolder failed and error is : create folder failed\n";
            return false;
        }
    }
    // means the folder exist
    return true;
}

bool DownloadTasks::isNewDownTask(QString file_path)
{
    if (m_fileInfoMap.find(file_path.toStdString()) == m_fileInfoMap.end())
    {
        return true;
    } 
    return false;
}

bool DownloadTasks::internalProgressUpdate()
{
    return false;
}

