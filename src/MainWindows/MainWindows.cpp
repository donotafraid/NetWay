#include "MainWindows/MainWindows.h"
#include "Sqlite_DB/Sqlite_DB.h"

MainWindows::MainWindows(QWidget *parent,DownloadTasks* downloadTasks)
    :QMainWindow(parent), m_downloadTasks(downloadTasks)
{
    initUI();
    setWindowTitle("文件传输工具");
    setAcceptDrops(true);
    resize(600, 400);   
    
    #ifdef DEBUG_TEST
    {
        emit m_loadDownTaskButton->clicked(); 
        emit m_DownloadButton->clicked();
    }
    #endif
}

MainWindows::~MainWindows()
{
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
    if(!QFile::exists(file_path)||0) 
    {
        std::cout<<"File not found or File has exist! "<<std::endl;
        return;
    }

    //  检查提供的文件路径是否存在于已有db_information文件里，不存在则为该文件路径在db_information中创建新的条目
    //  其次，添加该文件路径进入list,视作任务之一
    int rc = false;
    while(!rc)
    {
        rc = this->m_downloadTasks->intetface_read_missing_slices_from_db_information_file(file_path.toStdString());
        if (rc == false)
        {
            int create_new_record_result = this->m_downloadTasks->interface_create_new_file_on_db_information(file_path.toStdString()); 
            if(create_new_record_result == false)
            {
                std::cout<<"addFileToList:update new file on db failed"<<std::endl;
                return;
            }
        }
    }

    FileProgressItem *progressItem = new FileProgressItem(this->m_fileListWidget,file_path.toStdString());
    QListWidgetItem* listItem = new QListWidgetItem(this->m_fileListWidget);
    
    listItem->setSizeHint(progressItem->sizeHint());
    progressItem->setFilePath(file_path.toStdString());
    progressItem->return_countSize(file_path.toStdString());

    //  添加文件进度部件
    m_fileListWidget->setItemWidget(listItem,progressItem);

    //  添加文件进度部件到缓存里的map里
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
    m_mergeSQLiteDateButton = new QPushButton("merge SQLite date button", this);

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
    button_layout_2->addWidget(m_mergeSQLiteDateButton);
    main_layout->addLayout(button_layout);
    main_layout->addLayout(button_layout_2);

    // 加载信号槽
    connect (m_DownloadButton, SIGNAL(clicked()), this, SLOT(on_DownloadButton_clicked()));
    connect (m_deleteSingleButton, SIGNAL(clicked()), this, SLOT(on_DeleteButton_clicked()));
    connect (m_clearButton, SIGNAL(clicked()), this, SLOT(on_ClearButton_clicked()));
    connect (m_DownLoadedListButton, SIGNAL(clicked()),this, SLOT(on_information_downloadList_clicked()));
    connect (m_pauseButton, SIGNAL(clicked()),this, SLOT(on_pauseButton_clicked()));
    connect (m_loadDownTaskButton, SIGNAL(clicked()),this, SLOT(on_loadTaskButton_clicked()));
    connect (m_mergeSQLiteDateButton, SIGNAL(clicked()),this, SLOT(on_mergeSQLiteDateButton_clicked()));
}

void MainWindows::on_mergeSQLiteDateButton_clicked()
{
    //build QDiaglog 
    QDialog *dialog = new QDialog(this);
    // build layout
    QVBoxLayout *layout = new QVBoxLayout(dialog);
    // build QListWidget
    QListWidget *listWidget = new QListWidget(dialog);
    layout->addWidget(listWidget);

    this->m_merger.initialize_db_ptr(); 
    this->m_merger.clear_struct_setting();
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
    {
        m_fileListWidget->clear();
        m_downloadTasks->clear_progreeMap_cache();
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
    //  get current pause status
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
            std::cout<<"full_path: "<<full_path.toStdString()<<std::endl;
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

FileProgressItem::FileProgressItem(QWidget *parent,const std::string& file_name):
m_file_name(file_name)
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

void FileProgressItem::setFilePath(const std::string& file_name)
{
    auto file_path = QString::fromStdString(file_name); 
    m_fileNameLabel->setText(file_path);
}

std::string FileProgressItem::returnFilePath() const
{
    return this->m_file_name;
}

int FileProgressItem::setProgress(int progress)
{
    m_missing_index_number = progress;
    m_progressTextLabel->setText(QString::number(m_total-m_missing_index_number) + "/" + QString::number(m_total));
    if ( m_missing_index_number == 0 )
    {
        set_status_indicator(m_missing_index_number);
    }

    return 0;
}

void FileProgressItem::set_status_indicator(const bool status)
{
    m_statusIndicator->setStyleSheet(
        QString( 
        "background-color: %1 ;border-radius: 25px;")
        .arg(status ? "red" : "green"));

}

bool FileProgressItem::is_download_complete() const
{
    return m_missing_index_number == 0;
}

int FileProgressItem::return_countSize(const std::string& file_path)
{
    auto input_file_path = QString::fromStdString(file_path); 
    QFile file(input_file_path);
    if(!file.open(QIODevice::ReadOnly))
    {
        std::cerr<<"return_countSize failed and error is : "<<file.errorString().toStdString();    
        return false;
    }

    m_total = ( file.size() + SLICE_SIZE - 1)/SLICE_SIZE;

    file.close();
    return m_total;
}


FileProgressItem::~FileProgressItem()
{
    std::cout<<"~FileProgressItem called ! \n";
}
DownloadTasks::DownloadTasks(Sqlite_information& sqlite_information_ref,MqttClient& mqtt_client_ptr):
m_sqlite_information_ref(sqlite_information_ref),
m_taskExecution_ptr( new taskExecution(mqtt_client_ptr,memory_pool::getInstance())),
m_threadPool(QThreadPool::globalInstance())
{
}

DownloadTasks::~DownloadTasks()
{
    delete m_taskExecution_ptr;
    std::cout<<"~DownloadTasks called ! \n";
}

bool DownloadTasks::is_download_complete()
{ 
    bool is_download_complete = true;
    for(size_t i = 0; i < m_taskExecution_ptr->m_memory_pool_ref.size(); i++) 
    {
        //  get the missing_index_number of each task from memory_pool 
        auto task_info_Ptr = m_taskExecution_ptr->m_memory_pool_ref.return_pre_ptr();

        is_download_complete = m_fileProgressMap[task_info_Ptr->input_file_path()]->is_download_complete() && is_download_complete;

        // push task to task pool
        m_taskExecution_ptr->m_memory_pool_ref.push(std::move(task_info_Ptr));
    }
    return is_download_complete;
}
void DownloadTasks::run()
{
    bool m_download_complete = false;
    try
    {
        while(m_isFinished<=2000 && !m_download_complete )
        {
            this->m_taskExecution_ptr->send_task();
            this->update_fileProgress();
            m_download_complete = is_download_complete();

            while(return_isPause())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(3000));
                std::cout<<"the thread is paused !"<<std::endl;
                m_isFinished = 1000000;
            }

            m_isFinished++;
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            this->m_taskExecution_ptr->write_task_to_db_suborinate_file();
        }

        if(m_isFinished<=20)
        {
            std::cout<<"the run() is overed,ready to clear some sources!\n";
            std::cout<<"the clear_source() is overed!\n";
        }
        else {
            std::cout<<"the download is time_out!\n";
        }
        m_isFinished = 0;

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

void DownloadTasks::update_fileProgress()
{ 
  for(size_t i = 0; i < m_taskExecution_ptr->m_memory_pool_ref.size(); i++) 
  {
    //  get the missing_index_number of each task from memory_pool 
    auto task_info_Ptr = m_taskExecution_ptr->m_memory_pool_ref.return_pre_ptr();
    //  judge task status between finish and unfinish
    if(m_fileProgressMap.find(task_info_Ptr->input_file_path())!=m_fileProgressMap.end())
    {
        bool task_status = m_fileProgressMap[task_info_Ptr->input_file_path()]->is_download_complete();
        if (task_status)
        {
            m_taskExecution_ptr->m_memory_pool_ref.push(std::move(task_info_Ptr));
            return;
        }
    }
    const std::string missing_string (task_info_Ptr->missing_slices_index_json(),task_info_Ptr->missing_slices_index_json().size());
    nlohmann::json missing_slices_index_json ;
    if(missing_string != "")
    {
        missing_slices_index_json = nlohmann::json::parse(missing_string);
    }
   
    size_t missing_size = missing_slices_index_json.size();

    //  update progress
    {
        m_fileProgressMap[task_info_Ptr->input_file_path()]->setProgress(missing_size);
    }

    //  push task to task pool
    m_taskExecution_ptr->m_memory_pool_ref.push(std::move(task_info_Ptr));
  }
}

bool DownloadTasks::addItem_cache(FileProgressItem* file_list_widget)
{
    m_fileProgressMap.emplace(file_list_widget->returnFilePath(), file_list_widget);
    return true;
}

bool DownloadTasks::deleteItem_cache(FileProgressItem* file_list_widget)
{
    m_fileProgressMap.erase(file_list_widget->returnFilePath());
    m_taskExecution_ptr->delete_task_from_memory_pool(file_list_widget->returnFilePath());
    return true;
}

bool DownloadTasks::clear_progreeMap_cache()
{
    m_fileProgressMap.clear();
    m_taskExecution_ptr->m_memory_pool_ref.clear_memory_pool();
    m_isFinished = false;
    return true;
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


bool DownloadTasks::internalProgressUpdate()
{
    return false;
}

void mergeSQLData::presetting()
{
    int rc ;
    while(sqlite3_step(m_request_stmt_select_from_record)==SQLITE_ROW)
    {
        const void* blob_missing_slices_json = sqlite3_column_text(m_request_stmt_select_from_record,1);
        std::string json_string = std::string(static_cast<const char*>(blob_missing_slices_json));
        if (json_string.size() != 0)
        {
            std::string tem_string;
            request_message request_union ;
            const void* blob_file_id = sqlite3_column_blob(m_request_stmt_select_from_record,0);
            int file_id_size = sqlite3_column_bytes(m_request_stmt_select_from_record,0);
            const void* blob_input_file_path = sqlite3_column_blob(m_request_stmt_select_from_record,2); 
            int input_file_path_size = sqlite3_column_bytes(m_request_stmt_select_from_record,2);
            const void* blob_output_file_path = sqlite3_column_blob(m_request_stmt_select_from_record,3);
            int output_file_path_size = sqlite3_column_bytes(m_request_stmt_select_from_record,3);

            request_union.set_missing_slices_index_json(blob_missing_slices_json , json_string.size());
            request_union.set_file_id(blob_file_id,file_id_size);
            request_union.set_input_file_path(blob_input_file_path,input_file_path_size);
            request_union.SerializeToString(&tem_string);

            m_protobuf_queue.push(tem_string);
        }    
    }
} 

QDir mergeSQLData::loop_dbFile_in_path()
{
  QDir dir("./DownloadFileManagement"); 

  if(!dir.exists())
  {
    std::cerr<<"mergeSQLData::loop_dbFile_in_path failed and error is : the folder is not exist !"<<std::endl;
    return dir;
  }

  dir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
  dir.setNameFilters(QStringList("*.db"));

  return dir;
}

void mergeSQLData::initialize_db_ptr()
{
    QDir dir = loop_dbFile_in_path();
    QFileInfoList list = dir.entryInfoList();
    for(const QFileInfo &fileInfo : list)
    {
        QString file_path = fileInfo.absoluteFilePath();
        int rc = sqlite3_open_v2(file_path.toStdString().c_str(),
        &(this->m_db_ptr),
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE|SQLITE_OPEN_NOMUTEX,
        nullptr);
        if(rc !=SQLITE_OK)
        {
            std::cerr<<"mergeSQLData::open db_file failed and error is : "<<sqlite3_errmsg(this->m_db_ptr)<<std::endl;
            sqlite3_close(this->m_db_ptr);
            return;
        }
        
        rc = sqlite3_prepare_v2(this->m_db_ptr,sql_select_from_record,-1,&this->m_stmt_select_from_record,nullptr);
        if(rc != SQLITE_OK)
        {
            std::cerr<<"mergeSQLData::stmt_select_from_record fail and error is : "<<sqlite3_errmsg(this->m_db_ptr)<<std::endl;
            sqlite3_close(this->m_db_ptr);
            return;
        }

        rc = sqlite3_prepare_v2(this->m_db_ptr,sql_select_from_slice_content,-1,&this->m_stmt_select_from_slice_content,nullptr);
        if (rc != SQLITE_OK)
        {
            std::cerr<<"mergeSQLData::stmt_select_from_slice_content fail and error is : "<<sqlite3_errmsg(this->m_db_ptr)<<std::endl;
            sqlite3_close(this->m_db_ptr);
            return;
        }

        rc = sqlite3_prepare_v2(this->m_db_ptr, request_sql_select_from_record,-1, &this->m_request_stmt_select_from_record, NULL);
        if (rc!=SQLITE_OK)
        {
            std::cerr<<"mergeSQLData::stmt_select_from_record fail and error is : "<<sqlite3_errmsg(this->m_db_ptr)<<std::endl;
            sqlite3_close(this->m_db_ptr);
            return;
        }

        this->presetting();
        this->merge_file();
    }
}

void mergeSQLData::merge_file()
{ 
    int rc ;
    // select file_id , input_file_path from record
    // in loop , should not reset the condition stmt , because the loop based on stmt to get next row 
    while ( (rc = (sqlite3_step(m_stmt_select_from_record) == SQLITE_ROW)) ) {
        int blob_file_id_size = sqlite3_column_bytes(m_stmt_select_from_record,0);
        const void*  blob_file_id = sqlite3_column_blob(m_stmt_select_from_record,0);
        const unsigned char* file_name = sqlite3_column_text(m_stmt_select_from_record,1);
        std::string file_name_string = reinterpret_cast<const char*>(file_name);

        sqlite3_reset(m_stmt_select_from_slice_content);

        // bind file_id with stmt_cotent 
        rc = sqlite3_bind_blob(m_stmt_select_from_slice_content,1,reinterpret_cast<const char*>(blob_file_id),blob_file_id_size,SQLITE_STATIC);
        if(rc != SQLITE_OK)
        {
            std::cerr<<"mergerSQLData : sqlite3_bind_blob error : "<<sqlite3_errmsg(m_db_ptr)<<std::endl;
            return;
        }

        // select slice_index , slice_content from slice_content where file_id = ?
        while((sqlite3_step(m_stmt_select_from_slice_content))==SQLITE_ROW)
        {
            const char* slice_content = reinterpret_cast<const char*>(sqlite3_column_blob(m_stmt_select_from_slice_content,2));
            int slice_content_size = sqlite3_column_bytes(m_stmt_select_from_slice_content,2);

           //write message to file 
           std::ofstream file_out("./"+file_name_string,std::ios::app | std::ios::binary);
           if(!file_out)
           {
             std::cerr<<"mergerSQLData : open file error : "<<file_name_string<<std::endl;
             return;
           }
            
           file_out.write((slice_content),slice_content_size);
        }
    }
}

void mergeSQLData::clear_struct_setting()
{
    if (m_db_ptr != nullptr||
    m_stmt_select_from_record != nullptr||
    m_stmt_select_from_slice_content != nullptr
    )
    {
        //before close() , should call finalize() 
        sqlite3_finalize(m_stmt_select_from_record);
        sqlite3_finalize(m_stmt_select_from_slice_content);
        sqlite3_close(m_db_ptr);
        std::cout<<"mergeSQLData::clear_struct_setting()"<<std::endl;
    }
}

int DownloadTasks::interface_create_new_file_on_db_information(const std::string& file_path)
{
    return m_sqlite_information_ref.m_sqlite_db_function_ref.create_new_file_on_db_information(file_path);
}

int DownloadTasks::intetface_read_missing_slices_from_db_information_file(const std::string& file_path)
{
    return m_sqlite_information_ref.m_sqlite_db_function_ref.read_missing_slices_from_db_information_file(file_path);
}