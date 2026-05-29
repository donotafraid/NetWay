#include "MainWindows/MainWindows.h"
// #include "Sqlite_DB/Sqlite_DB.h"

MQTT_Platform_UI::MQTT_Platform_UI(QWidget *parent,DownloadTasks* downloadTasks,download_path_manager &download_path_manager,buffer_administrator& buffer_administrator_ref,Single_Data_Block* single_data_block)
    :QMainWindow(parent), m_downloadTasks(downloadTasks),m_download_path_manager(download_path_manager),m_single_data_block(single_data_block)
{
    initUI(buffer_administrator_ref);
    m_mqtt_buffer_monitor = new MQTT_buffer_monitor(this,buffer_administrator_ref);
    setWindowTitle("tool of file transmission");
    setAcceptDrops(true);
    resize(600, 400);   
}

MQTT_Platform_UI::~MQTT_Platform_UI()
{
    delete m_mqtt_buffer_monitor;
    std::cout<<"MQTT_Platform_UI::~MQTT_Platform_UI() called";
}

void MQTT_Platform_UI::check_DownLoadFolder_initalize()
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

//  文件请求的起点
void MQTT_Platform_UI::addFileToList(const QString & file_path)
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

void MQTT_Platform_UI::initUI(buffer_administrator& buffer_administrator_ref)
{
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
    m_selectFolderPathButton = new QPushButton("select folder path button", this);
    m_messageBufferButton = new QPushButton("message buffer button", this);
    m_PLC_ReceiveMessageButton = new QPushButton("PLC receive message button", this);

    // 显示控件到布局
    QVBoxLayout *main_layout = new QVBoxLayout(this);
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
    button_layout_2->addWidget(m_selectFolderPathButton);
    button_layout_2->addWidget(m_messageBufferButton);
    button_layout_2->addWidget(m_PLC_ReceiveMessageButton);
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
    connect(m_selectFolderPathButton,SIGNAL(clicked()),this,SLOT(on_selectFolderPathButton_clicked()));
    connect(m_messageBufferButton,SIGNAL(clicked()),this,SLOT(on_messageBufferButton_clicked()));
    connect(m_downloadTasks,&DownloadTasks::update_progress_bar,this,&MQTT_Platform_UI::update_buffer_progress);
}


void MQTT_Platform_UI::update_buffer_progress()
{
    m_mqtt_buffer_monitor->progressUpdate();
}

void MQTT_Platform_UI::on_messageBufferButton_clicked()
{
    if(m_mqtt_buffer_monitor==nullptr)
    {
        std::cout<<"m_mqtt_buffer_monitor is nullptr";
        return;
    }
    m_mqtt_buffer_monitor->show();
}

void MQTT_Platform_UI::on_selectFolderPathButton_clicked()
{
    QString folder_path = QFileDialog::getExistingDirectory(this,"select folder path");
    if(folder_path.isEmpty())
    {
        std::cout<<"select folder path failed"<<std::endl;
        return;
    }

    m_download_path_manager.download_folder_path = (folder_path.toStdString());
}

void MQTT_Platform_UI::on_mergeSQLiteDateButton_clicked()
{
    //build QDiaglog 
    QDialog *dialog = new QDialog(this);
    // build layout
    QVBoxLayout *layout = new QVBoxLayout(dialog);
    // build QListWidget
    QListWidget *listWidget = new QListWidget(dialog);
    layout->addWidget(listWidget);

}


void MQTT_Platform_UI::on_DownloadButton_clicked()
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


void MQTT_Platform_UI::on_DeleteButton_clicked()
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

void MQTT_Platform_UI::on_ClearButton_clicked()
{
    {
        m_fileListWidget->clear();
        m_downloadTasks->clear_corresponding_cache();
    }
}


void MQTT_Platform_UI::on_information_downloadList_clicked()
{
    QString currentDirPath = QDir::currentPath();

    QString targetDir = currentDirPath + "/DownloadFileManagement";
    QDir downloadDir (targetDir);

    if( !downloadDir.exists())
    {
        QMessageBox::information(this, "提示", "下载文件夹不存在");
        return;
    }

    if(!m_DownLoadedListDialog)
    {
        m_DownLoadedListDialog = new QDialog(this);
    }
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

void MQTT_Platform_UI::on_pauseButton_clicked()
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

void MQTT_Platform_UI::on_loadTaskButton_clicked()
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

void MQTT_Platform_UI::initialize(DownloadTasks *downloadTasks)
{
    m_downloadTasks = downloadTasks;
}

std::string MQTT_Platform_UI::get_current_date_string()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d"); // 格式化为 "20240520"
    return oss.str();
}

void MQTT_Platform_UI::process_file(const QString& file_path)
{
    return; 
}

void MQTT_Platform_UI::dragEnterEvent(QDragEnterEvent * event)
{
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
    }
}


void MQTT_Platform_UI::dropEvent(QDropEvent * event)
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
        .arg(status ? "red" : "green")
    );

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
    m_delete_status = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
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

        is_download_complete = m_fileProgressMap[task_info_Ptr->input_file_path()]->is_download_complete() & is_download_complete;

        // push task to task pool
        m_taskExecution_ptr->m_memory_pool_ref.push(std::move(task_info_Ptr));
    }
    return is_download_complete;
}

void DownloadTasks::start_update_message_buffer()
{
    std::thread(
        [this](){
            while(m_delete_status)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                emit update_progress_bar();
            }
            std::cout<<"~start_update_message_buffer called !"<<std::endl;
            return;
        }
    ).detach();
}

void DownloadTasks::run()
{
    bool m_download_complete = false;
    try
    {
        while(m_isFinished<=20000 && !m_download_complete )
        {
            {
                this->m_taskExecution_ptr->send_task();
                this->update_fileProgress();
                m_download_complete = is_download_complete();
            }    

            while(return_isPause())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(3000));
                std::cout<<"the thread is paused !"<<std::endl;
                m_isFinished = 1000000;
            }

            m_isFinished++;
            if(m_isFinished%100 == 0)
            {
                this->m_taskExecution_ptr->write_task_to_db_suborinate_file();
            }

        }

        if(m_download_complete)
        {
            std::cout<<"the run() is overed,ready to clear some sources!\n";
            merge_done_task();
            clear_corresponding_cache();
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
    const std::string missing_string (task_info_Ptr->missing_slices_index_json());
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

bool DownloadTasks::clear_corresponding_cache()
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


int DownloadTasks::interface_create_new_file_on_db_information(const std::string& file_path)
{
    //  create new clauses for this file in db_information.db
    return m_sqlite_information_ref.m_sqlite_db_function_ref.create_new_file_on_db_information(file_path);
}

int DownloadTasks::intetface_read_missing_slices_from_db_information_file(const std::string& file_path)
{
    //  return result the result of if the lack index of this file 
    //  if do not exist the lack , return false; else if return true
    return m_sqlite_information_ref.m_sqlite_db_function_ref.read_missing_slices_from_db_information_file(file_path);
}

void DownloadTasks::merge_done_task()
{ 
    for(size_t i = 0; i < m_taskExecution_ptr->m_memory_pool_ref.size(); i++) 
    {
        //  get the missing_index_number of each task from memory_pool 
        auto task_info_Ptr = m_taskExecution_ptr->m_memory_pool_ref.return_pre_ptr();
        m_sqlite_information_ref.m_sqlite_db_function_ref.merge_select_file(*(task_info_Ptr.get()));
        m_sqlite_information_ref.m_sqlite_db_function_ref.delete_select_file(*(task_info_Ptr.get()));
        m_taskExecution_ptr->m_memory_pool_ref.push(std::move(task_info_Ptr));
    }
}

MQTT_buffer_monitor::MQTT_buffer_monitor(QWidget *parent,buffer_administrator& mqtt_buffer_monitor):
m_buffer_administrator_ref(mqtt_buffer_monitor)
{
    this->setWindowTitle("MQTT_buffer_monitor");
    this->setWindowFlags(this->windowFlags() | Qt::WindowMinimizeButtonHint);

    QHBoxLayout* layout_h = new QHBoxLayout();
    QHBoxLayout* layout_h2 = new QHBoxLayout();
    QVBoxLayout* layout_v = new QVBoxLayout(this);

    this->setWindowTitle("MQTT_buffer_monitor");

    m_buffer_label = new QLabel(this);
    m_inflight_label = new QLabel(this);

    m_buffer_label->setText("buffer_condition: " + QString::number(m_buffer_administrator_ref.m_buffer_size) + "       max_buffer_size: "+ QString::number(1000) );
    m_inflight_label->setText("inflight_condition: " + QString::number(m_buffer_administrator_ref.m_inflight_size) + "     max_inflight_size: "+ QString::number(1000));

    m_buffer_indicator = new QProgressBar(this);
    m_buffer_indicator->setGeometry(0,0,500,50);
    m_buffer_indicator->setStyleSheet("background: grey; border: 25px;");

    m_inflight_indicator = new QProgressBar(this);
    m_inflight_indicator->setGeometry(0,50,500,50);
    m_inflight_indicator->setStyleSheet("background: grey; border: 25px;");

    layout_h->addWidget(m_buffer_label);
    layout_h->addWidget(m_buffer_indicator);

    layout_h2->addWidget(m_inflight_label);
    layout_h2->addWidget(m_inflight_indicator);

    layout_v->addLayout(layout_h);
    layout_v->addLayout(layout_h2);

}

void MQTT_buffer_monitor::progressUpdate()
{ 
    m_buffer_indicator->setStyleSheet(
       QString(
        "background-color: %1;border: 25px;").
        arg(m_buffer_administrator_ref.m_buffer_size>800?"red":"green")
    );

    m_inflight_indicator->setStyleSheet(
       QString(
        "background-color: %1;border: 25px;").
        arg(m_buffer_administrator_ref.m_inflight_size>800?"red":"green")
    );

    m_buffer_label->setText("buffer_condition: " + QString::number(m_buffer_administrator_ref.m_buffer_size.load(std::memory_order_acquire)) + "       max_buffer_size: "+ QString::number(1000) );
    m_inflight_label->setText("inflight_condition: " + QString::number(m_buffer_administrator_ref.m_inflight_size.load(std::memory_order_acquire)) + "     max_inflight_size: "+ QString::number(1000));

}

SystemSetting::SystemSetting(QWidget *parent,std::shared_ptr<spdlog::logger> log_pointer):QDialog(parent),m_log(log_pointer)
{
    QVBoxLayout *m_main_layout_V = new QVBoxLayout(this);
    m_main_layout_H = new QHBoxLayout(this);
    m_stackWidget = new QStackedWidget ();
    m_fileListWidget = new QListWidget();
    m_QuerySettingUpdate = new QPushButton("Ready HotUpdate");
    
    m_main_layout_H->addWidget(m_fileListWidget,1);
    m_main_layout_H->addWidget(m_stackWidget,3);

    m_main_layout_V->addWidget(m_QuerySettingUpdate);
    m_main_layout_V->addLayout(m_main_layout_H);

    createMQTTPage();
    createPLCPage();
    createOperationPage();
    createPlateformIntegrationPage();

    connect(m_fileListWidget, &QListWidget::currentRowChanged, m_stackWidget,&QStackedWidget::setCurrentIndex);
    connect(m_QuerySettingUpdate, &QPushButton::clicked, this,&SystemSetting::hotUpdate_setting);

    hotUpdate_setting();
}


void SystemSetting::createPLCPage()
{
    QWidget *page = new QWidget();
    QVBoxLayout *m_main_layout_V = new QVBoxLayout(page);

    //  FRESH SETTING
    QHBoxLayout *m_refresh_layout_H = new QHBoxLayout();
    QLabel *m_refresh_label = new QLabel("Refresh Frequency setting(millisecond) : ");
    m_refresh_Edit = new QLineEdit("3000");
    m_refresh_layout_H->addWidget(m_refresh_label);
    m_refresh_layout_H->addWidget(m_refresh_Edit);
    m_main_layout_V->addLayout(m_refresh_layout_H);

    //  AUTHENTICATION SETTING
    QHBoxLayout *m_authentication_layout = new QHBoxLayout();
    QPushButton *userPassword = new QPushButton (tr("user/password"),this);
    QPushButton *client_certificate = new QPushButton (tr("client_certificate"),this);
    userPassword->setCheckable(true);
    userPassword->setChecked(false);
    client_certificate->setCheckable(true);
    client_certificate->setChecked(false);
    m_authentication_layout->addWidget(userPassword);
    m_authentication_layout->addWidget(client_certificate);
    m_main_layout_V->addLayout(m_authentication_layout);

    //  AUTHENTICATION SETTING -- USER_PASSWORD SETTING
    userPassword_page = new QWidget();
    QLabel *user_name = new QLabel("user_name : ");
    user_name_edit = new QLineEdit("admin");
    QLabel *password = new QLabel("password : ");
    password_edit = new QLineEdit("123456");

    QVBoxLayout *user_password_layout = new QVBoxLayout(userPassword_page);
    user_password_layout->addWidget(user_name);
    user_password_layout->addWidget(user_name_edit);
    user_password_layout->addWidget(password);
    user_password_layout->addWidget(password_edit);    
    m_main_layout_V->addWidget(userPassword_page);

    //  AUTHENTICATION SETTING -- CLIENT CERTIFICATE
    clientCertificate_page = new QWidget(); 
    QHBoxLayout *m_layout_H = new QHBoxLayout(clientCertificate_page);
    m_main_layout_V->addWidget(clientCertificate_page);

    connect(client_certificate,&QPushButton::clicked,[this](bool checked){
        this->clientCertificate_page->setVisible(checked);
        if(checked)
        {
            QString folder_path = QFileDialog::getExistingDirectory(
                this,
                tr("Select certificate folder path"),
                QDir::homePath(),
                QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
            );
            if(!folder_path.isEmpty())
            {
                this->certificate_folder_path = std::move(folder_path);
            }
        }
    });

    connect(userPassword, &QPushButton::clicked, [this](bool checked) {
        this->userPassword_page->setVisible(checked);
    });
    
    m_stackWidget->addWidget(page);
    m_fileListWidget->addItem("PLC");
}

void SystemSetting::createMQTTPage()
{
    QWidget *page = new QWidget();
    QVBoxLayout *m_main_layout_V = new QVBoxLayout(page);

    //  TLS/SSL BOX SETTING
    QVBoxLayout *m_TLS_SSL_layout = new QVBoxLayout();
    m_TLS_box = new QCheckBox("Enable TLS");
    m_SSL_box = new QCheckBox("Enable SSL");
    m_TLS_box->setCheckable(true);
    m_TLS_box->setChecked(false);
    m_SSL_box->setCheckable(true);
    m_SSL_box->setChecked(false);
    m_TLS_SSL_layout->addWidget(m_TLS_box);
    m_TLS_SSL_layout->addWidget(m_SSL_box);
    m_main_layout_V->addLayout(m_TLS_SSL_layout);

    //  MQTT PUBLISHING CONFIGURATION
    QHBoxLayout *m_publish_configuration_layout = new QHBoxLayout();
    QLabel *m_configuration_label = new QLabel("MQTT Publishing Frequency (millisecond): ");
    m_mqtt_publish_frequency_edit = new QLineEdit("3000");
    m_publish_configuration_layout->addWidget(m_configuration_label);
    m_publish_configuration_layout->addWidget(m_mqtt_publish_frequency_edit);
    m_main_layout_V->addLayout(m_publish_configuration_layout);

    //  MQTT RETRY STRATEGY
    QHBoxLayout *m_retry_layout = new QHBoxLayout();
    QLabel *m_retry_label = new QLabel("retry times : ");
    m_mqtt_retry_times_edit = new QLineEdit("5");
    QHBoxLayout *m_retry_distance_layout = new QHBoxLayout();
    QLabel *m_retry_distance_label = new QLabel("retry distance in time(millisecond) : ");
    m_mqtt_retry_distance_time_edit = new QLineEdit("3000");
    m_retry_layout->addWidget(m_retry_label);
    m_retry_layout->addWidget(m_mqtt_retry_times_edit);
    m_main_layout_V->addLayout(m_retry_layout);
    m_retry_distance_layout->addWidget(m_retry_distance_label);
    m_retry_distance_layout->addWidget(m_mqtt_retry_distance_time_edit);
    m_main_layout_V->addLayout(m_retry_distance_layout);

    m_stackWidget->addWidget(page);
    m_fileListWidget->addItem("MQTT");
}

void SystemSetting::createOperationPage()
{
    m_operation_page = new QWidget();
    QVBoxLayout *m_main_layout_V = new QVBoxLayout(m_operation_page);

    //  LOG GRADE SELECT
    QHBoxLayout *m_log_layout = new QHBoxLayout();
    QLabel *m_log_label = new QLabel("log level select : ");
    m_log_combobox = new QComboBox();
    m_log_combobox->addItem("UNKNOWN");
    m_log_combobox->addItem("DEBUG");
    m_log_combobox->addItem("INFO");
    m_log_combobox->addItem("ERROR");
    m_log_layout->addWidget(m_log_label);
    m_log_layout->addWidget(m_log_combobox);
    m_main_layout_V->addLayout(m_log_layout);

    //  LOG OUTPUT FORMAT
    QHBoxLayout *m_log_output_format_layout = new QHBoxLayout();
    QLabel *m_log_output_format_label = new QLabel("log output format select : ");
    m_log_output_format_combobox = new QComboBox();
    m_log_output_format_combobox->addItem("UNKNOWN");
    m_log_output_format_combobox->addItem("JSON");
    m_log_output_format_combobox->addItem("TXT");
    m_log_output_format_layout->addWidget(m_log_output_format_label);
    m_log_output_format_layout->addWidget(m_log_output_format_combobox);
    m_main_layout_V->addLayout(m_log_output_format_layout);
    
    //  PROMETHEUS ENABLE BOX
    QHBoxLayout *m_prometheus_layout = new QHBoxLayout();
    QHBoxLayout *m_prometheus_port_layout = new QHBoxLayout();
    QLabel *m_prometheus_label = new QLabel("enable or not prometheus: ");
    m_prometheus_box = new QCheckBox();
    QLabel *m_prometheus_port_label = new QLabel("set the prometheus port ");
    m_prometheus_edit = new QLineEdit("9090");
    m_prometheus_box->setCheckable(true);
    m_prometheus_box->setChecked(false);
    m_prometheus_layout->addWidget(m_prometheus_label);
    m_prometheus_layout->addWidget(m_prometheus_box);
    m_main_layout_V->addLayout(m_prometheus_layout);
    m_prometheus_port_layout->addWidget(m_prometheus_port_label);
    m_prometheus_port_layout->addWidget(m_prometheus_edit);
    m_main_layout_V->addLayout(m_prometheus_port_layout);
 
    m_stackWidget->addWidget(m_operation_page);
    m_fileListWidget->addItem("Operation");
}

void SystemSetting::createPlateformIntegrationPage()
{
    m_platformIntegration_page = new QWidget();
    QVBoxLayout *m_main_layout_V = new QVBoxLayout(m_platformIntegration_page);

    //  CLOUD PLATFORM SELECT
    QHBoxLayout *m_cloud_platform_layout = new QHBoxLayout();
    QLabel *m_cloud_platform_label = new QLabel("select cloud-platform type : "); 
    m_cloud_platform_box = new QComboBox();
    m_cloud_platform_box->addItem("UNKNOWN");
    m_cloud_platform_box->addItem("AWS");
    m_cloud_platform_box->addItem("Azure");
    m_cloud_platform_box->addItem("ThingsBoard");
    m_cloud_platform_layout->addWidget(m_cloud_platform_label);
    m_cloud_platform_layout->addWidget(m_cloud_platform_box);
    m_main_layout_V->addLayout(m_cloud_platform_layout);

    //  API CONFIGURATION 
    QHBoxLayout *enable_API_layout = new QHBoxLayout();
    QLabel *m_API_config_label = new QLabel("select enable API : "); 
    m_check_box = new QCheckBox();
    m_check_box->setCheckable(true);
    m_check_box->setChecked(false);
    enable_API_layout->addWidget(m_API_config_label);
    enable_API_layout->addWidget(m_check_box);
    m_main_layout_V->addLayout(enable_API_layout);

    QHBoxLayout *API_certificate_layout = new QHBoxLayout();
    QLabel *m_API_certificate_label = new QLabel("select API certification type : "); 
    m_API_certification_type_box = new QComboBox();
    m_API_certification_type_box->addItem("UNKNOWN");
    m_API_certification_type_box->addItem("Token");
    API_certificate_layout->addWidget(m_API_certificate_label);
    API_certificate_layout->addWidget(m_API_certification_type_box);
    m_main_layout_V->addLayout(API_certificate_layout);

    QHBoxLayout *API_key_layout = new QHBoxLayout();
    QLabel *m_API_key_label = new QLabel("please input API Key : "); 
    m_API_key_edit = new QLineEdit("");
    API_key_layout->addWidget(m_API_key_label);
    API_key_layout->addWidget(m_API_key_edit);
    m_main_layout_V->addLayout(API_key_layout);

    m_stackWidget->addWidget(m_platformIntegration_page);
    m_fileListWidget->addItem("Platform Integration");
}

void SystemSetting::hotUpdate_setting()
{
  m_par.enable_TLS = this->m_TLS_box->isChecked();
  m_par.enable_SSL = this->m_SSL_box->isChecked();
  m_par.MQTT_Publish_Frequency = this->m_mqtt_publish_frequency_edit->text();
  m_par.MQTT_Connect_Retry_Times = this->m_mqtt_retry_times_edit->text();
  m_par.MQTT_Retry_distance_times = this->m_mqtt_retry_distance_time_edit->text();

  m_par.PLC_Refresh_Frequency = this->m_refresh_Edit->text();
  m_par.PLC_User = this->user_name_edit->text();
  m_par.PLC_Password = this->password_edit->text();

  {
    QString currentText = m_log_combobox->currentText();
    auto it = QStringToLogLevel.find(currentText);
    if (it != QStringToLogLevel.end()) {
      m_par.current_level = it->second;

      //    Hot update log_level
      auto level = LogLevelToSpdlogLevel.find(it->second);
      m_log->set_level(level->second);
    } else {
      // 可选：处理无效值（例如设为默认值）
      m_par.current_level = Log_Level::UNKNOWN; // 默认
      QMessageBox::warning(this, "Error", "Unknown log level selected.");
    }
  }
  {
    QString currentText = m_log_output_format_combobox->currentText();
    auto it = QStringToLogOutputFormat.find(currentText);
    if (it != QStringToLogOutputFormat.end()) {
      m_par.current_format = it->second;
    } else {
      // 可选：处理无效值（例如设为默认值）
      m_par.current_format = Log_OUTPUT_FORMAT::UNKNOWN; // 默认
      QMessageBox::warning(this, "Error", "Unknown log format selected.");
    }
  }
  m_par.enable_prometheus = this->m_prometheus_box->isChecked();
  m_par.prometheus_port = this->m_prometheus_edit->text();

  {
    QString currentText = m_cloud_platform_box->currentText();
    auto it = QStringToCloudPlatform.find(currentText);
    if (it != QStringToCloudPlatform.end()) {
      m_par.current_cloud_platform = it->second;
    } else {
      // 可选：处理无效值（例如设为默认值）
      m_par.current_cloud_platform = Cloud_Platform::UNKNOWN; // 默认
      QMessageBox::warning(this, "Error", "Unknown Cloud_Platform selected.");
    }
  }
  m_par.enable_cloud_api = m_check_box->isChecked();
  {
    QString currentText = m_API_certification_type_box->currentText();
    auto it = QStringToCloudPlatformApi.find(currentText);
    if (it != QStringToCloudPlatformApi.end()) {
      m_par.cloud_api_certificate_type = it->second;
    } else {
      // 可选：处理无效值（例如设为默认值）
      m_par.cloud_api_certificate_type = Cloud_Platform_API::UNKNOWN; // 默认
      QMessageBox::warning(this, "Error", "Unknown Cloud_Platform_API selected.");
    }
  }
  m_par.cloud_api_key = m_API_key_edit->text();

}

void SystemSetting::display_current_setting()
{
  this->m_TLS_box->setChecked(m_par.enable_TLS);
  this->m_SSL_box->setChecked(m_par.enable_SSL);
  this->m_mqtt_publish_frequency_edit->setText(
      (m_par.MQTT_Publish_Frequency));
  this->m_mqtt_retry_times_edit->setText(
      (m_par.MQTT_Connect_Retry_Times));
  this->m_mqtt_retry_distance_time_edit->setText(
      (m_par.MQTT_Retry_distance_times));

  this->m_refresh_Edit->setText(
      (m_par.PLC_Refresh_Frequency));
  this->user_name_edit->setText((m_par.PLC_User));
  this->password_edit->setText((m_par.PLC_Password));

  {
      auto it = LogLevelToQString.find(m_par.current_level);
      if(it != LogLevelToQString.end())
      {
        int index = m_log_combobox->findText(it->second);
        if(index != -1)
        m_log_combobox->setCurrentIndex(index);
      }
  }
  {
     auto it = LogOutputFormatToQString.find(m_par.current_format);
     if(it != LogOutputFormatToQString.end())
     {
       int index = m_log_output_format_combobox->findText(it->second);
       if(index != -1)
       m_log_output_format_combobox->setCurrentIndex(index);
     }
  }
  m_prometheus_box->setChecked(m_par.enable_prometheus);
  m_prometheus_edit->setText((m_par.prometheus_port));

  {
    auto it = CloudPlatformToQString.find(m_par.current_cloud_platform);
    if (it != CloudPlatformToQString.end()) {
      int index = m_cloud_platform_box->findText(it->second);
      if (index != -1)
        m_cloud_platform_box->setCurrentIndex(index);
    }
  }
  m_check_box->setChecked(m_par.enable_cloud_api);
  {
    auto it = CloudPlatformApiToQString.find(m_par.cloud_api_certificate_type);
    if (it != CloudPlatformApiToQString.end()) {
      int index = m_API_certification_type_box->findText(it->second);
      if (index != -1)
        m_API_certification_type_box->setCurrentIndex(index);
    }
  }
  m_API_key_edit->setText((m_par.cloud_api_key));
}

void logMessage(Log_Level level, const QString &message) {
  auto it = LogLevelToSpdlogLevel.find(level);
  if (it != LogLevelToSpdlogLevel.end()) {
    spdlog::log(it->second, "{}", message.toStdString());
  }
}

bool SystemSetting::connect_retry(int &times)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(m_par.MQTT_Retry_distance_times.toInt()));
    if(times < m_par.MQTT_Connect_Retry_Times.toInt())
    {
        return true;
    }
    else
    {
        return false;
    }
}

S7_MainWindows::S7_MainWindows(QWidget *parent,ServiceMetrics *serviceMetric_ptr):QMainWindow(parent),m_metrics(serviceMetric_ptr)
{
    //  Build log variable
    m_log = spdlog::basic_logger_mt("basic_logger", "logs/basic.txt");

    // Create central widget and set layout
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    m_main_layout_V = new QVBoxLayout(centralWidget);

    QSplitter *splitter_left = new QSplitter(Qt::Vertical);
    QSplitter *splitter_right = new QSplitter(Qt::Vertical);
    QSplitter *splitter_main_H = new QSplitter(Qt::Horizontal);

    //  create left file tree area
    m_datablockname_tree = new QTreeWidget();
    m_search_bar = new QLineEdit();
    m_datablockname_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_search_bar->setPlaceholderText("Search device ip address ...");
    m_search_bar->setClearButtonEnabled(true);
    splitter_left->addWidget(m_search_bar);
    splitter_left->addWidget(m_datablockname_tree);

    //  create right side tab area
    m_tab_widget = new QTabWidget();
    m_tab_widget->setTabsClosable(true);
    m_tab_widget->setMovable(true);
    splitter_right->addWidget(m_tab_widget);
    
    // //  ADD BUTTON_LAYOUT INTO SPLIITER
    // m_button_layout = new S7_Button_Layout();
    // splitter_right->addWidget(m_button_layout);

    //  ADD DOCKWIDGET INTO MAINWINDOWS
    m_device_monitor = new DeviceMonitorWidget(this);
    m_device_monitor->build_original_connet(&m_plc_device_pointer_vector);
    initialize_Add_Device_Page();

    //  SYSTEM SETTING 
    m_systemSetting = new SystemSetting(this,m_log);

    //  BUILD TOOLBAR CONNECTION
    build_connect_searchBar();
    build_connect_searchBar();

    // GrafanaClient *m_grafanaClient = new GrafanaClient();
    // m_monitor_dashBoard = new MonitoringDashboard(this);
    // m_monitor_dashBoard->setGrafanaClient(m_grafanaClient);

    //  延迟设置精确比例（双重保证）
    QTimer::singleShot(50, this, [this, splitter_right,splitter_left]() {
        if (splitter_right->height() > 100) {
            QList<int> sizes = splitter_right->sizes();

            int total = sizes[0] + sizes[1];
            if (total > 0) {
                // 如果当前比例偏差较大，强制修正
                float currentRatio = (float)sizes[0] / total;
                if (abs(currentRatio - 0.7) > 0.1) {  // 如果偏差超过10%
                    splitter_right->setSizes({
                        total * 80 / 100,
                        total * 20 / 100
                    });
                    splitter_left->setSizes({
                        total * 20 / 100,
                         total * 80 / 100}
                    );
                }
            }
        }
    });

    splitter_main_H->addWidget(splitter_left);
    splitter_main_H->addWidget(splitter_right);
    m_main_layout_V->addWidget(splitter_main_H);

    this->resize(1200, 800);

    //  connect signal and slot
    connect(m_datablockname_tree, &QTreeWidget::customContextMenuRequested,
            this, &S7_MainWindows::onCustomContextMenu);
    connect(m_datablockname_tree, &QTreeWidget::itemExpanded, this,
            &S7_MainWindows::onTreeItemExpanded);
    connect(m_datablockname_tree, &QTreeWidget::itemClicked, this,
            &S7_MainWindows::onOpenSelectFileClicked);
    connect(m_datablockname_tree, &QTreeWidget::itemCollapsed, this,
            &S7_MainWindows::onTreeItemCollapsed);

    connect(m_tab_widget, &QTabWidget::tabCloseRequested, this,
            [this](int index) { this->onDeleteSelectTabClicked(index); });

    // connect(m_button_layout->m_writeButton, &QPushButton::clicked, this,
    //         &S7_MainWindows::write_DataBlock_From_TableBuffer);
    // connect(m_button_layout->m_upadateButton, &QPushButton::clicked, this,
    //         &S7_MainWindows::write_Table_From_S7_DatabBlockBuffer);
    // connect(m_button_layout->m_readButton, &QPushButton::clicked, this,
    //         &S7_MainWindows::read_DataBlock_From_DataBlockBuffer);

    connect(m_search_bar,&QLineEdit::textChanged,this,&S7_MainWindows::filterTree);
}

S7_MainWindows::~S7_MainWindows()
{
    //  USE COUNT() NEED AVOID REDUCE THAT DYNAMIC DELETE ELEMENT, IT MAY CAUSE LOOP STANDARD DYNAMIC CHANGE. THAT IS WHY USE COUNT() INITALIZE ITERATOR RATHER THAN AS LOOP STANDARD 
    for(int i = m_datablockname_tree->topLevelItemCount() - 1 ; i >=0 ; --i)
    {
        QTreeWidgetItem *item = m_datablockname_tree->topLevelItem(i);
        QVariant itemData = item -> data(0,Qt::UserRole);

        if(itemData.isValid())
        {
            void *storedPtr = itemData.value<void*>();
            PLC_Device *storeDevice = static_cast<PLC_Device*>(storedPtr);
            //  REMOVE ITEM FROM QTREEWIDGET AND DELETE THE SELECT QTREEWIDGET ITEM
            delete m_datablockname_tree->takeTopLevelItem(i);
            //  REMOVE THE DEVICE FROM DEVICE_VECTOR
            clear_delete_device(storeDevice);
            //  DESTORY THE DEVICE MEMORY
            delete storeDevice;
        }
    }

    if(drop_Area != nullptr)
    {
        delete drop_Area;
    }
    if(m_prometheus_view)
    {
        delete m_prometheus_view;
    }

    std::cout << "S7_MainWindows::~S7_MainWindows()\n";
}

void S7_MainWindows::filterTree(const QString& ip_Address)
{
    m_match_list.clear();
    for(int i = 0 ; i < m_datablockname_tree->topLevelItemCount() ; ++i)
    {
        QTreeWidgetItem *item = m_datablockname_tree->topLevelItem(i);
        QString ipAddress = item->text(0);

        bool match = ipAddress.contains(ip_Address,Qt::CaseInsensitive);
        item->setHidden(!match);

        //  COLLECT MATCH IPADDRESS
        if (match)
        {
            m_match_list<<ipAddress;
        }
    }

    //  UPDATE SET_STRING_LIST REAL_TIME FOR GETTING LASTEST INPUT_RESULT
    m_completerModel->setStringList(m_match_list);

    //  DISPLAY DROPDOWN WITH MATCH MEMBER 
    if(!ip_Address.isEmpty() && !m_match_list.isEmpty())
    {
        m_completer->complete();
    }
}

//  SELECT QTreeWidget Item
void S7_MainWindows::onOpenSelectFileClicked(QTreeWidgetItem *item)
{ 
    if(item == nullptr)
    {
        std::cout<<"S7_MainWindows::onOpenSelectFileClicked() error: item == nullptr\n";
        return;
    }
   
    QString file_name = item->text(0);
    std::cout << "Find_Result : "
              << (CheckSelectFileInTab(file_name,item)).is_success()
              << " (0 mean fail , 1 mean success)" << std::endl;
    return;
}

//  CLOSE THE TAB 
void S7_MainWindows::onDeleteSelectTabClicked(int index)
{
    if(index >=0 && index < m_tab_widget->count())
    {
        QWidget* widget = m_tab_widget->widget(index);
        if(widget)
        {
            m_tab_widget->removeTab(index);
            std::cout<<"onDeleteSelectTabClicked called!\n";
        }
    }
    else
    {
        std::cerr<<"onDeleteSelectTabClicked call error\n";
    }

}

// COLLAPS THE QTREEWIDGET
void S7_MainWindows::onTreeItemCollapsed(QTreeWidgetItem* item)
{
    std::cout<<"QTreeWidge Collapsed \n";
}

//  EXPAND THE QTREEWIDGET 
void S7_MainWindows::onTreeItemExpanded(QTreeWidgetItem* item)
{
    void *device_data = item->data(0,Qt::UserRole).value<void*>();
    m_plc_device_pointer = static_cast<PLC_Device*>(device_data);
    this->update_lastest_itemPointer(item, nullptr);
    update_lastest_folder_path();

    std::cout<<"device tree have refreshed\n";
}

//  RIGHT CLICKED QTREEWIDGET ITEM FOR EXPANEDING THE MENU
void S7_MainWindows::onCustomContextMenu(const QPoint& pos)
{
    // initialize select device pointer
    initalize_last_parameter();
    QTreeWidgetItem *item = m_datablockname_tree->itemAt(pos);
    if(!item)
    {
        return showBlankAreaContextMenu(pos);
    }

    //  IF THE SELECT ITEM IS DATASTRUCT ITEM
    auto result = this->validExistenceParent(item);
    if(result.is_fail())
    {
        std::cout<<result.unwrap_err().what()<<std::endl;
        return ;
    }

    //  UPDATE LAST SELECT  ITEM
    if(result.unwrap_returnLeftValue())
    {
        //  MEANS THE ITEM HAS ITS PARENT ITEM
        std::cout<<"select item belong to DataStruct item\n";
        this->showDataStructMenu(pos);
    }
    else
    {
        //  MEANS THE ITEM DO NOT HAS ITS PARENT ITEM
        void *deviceData = item->data(0, Qt::UserRole).value<void *>();
        m_plc_device_pointer = static_cast<PLC_Device *>(deviceData);
        this->update_lastest_itemPointer(item, nullptr);
        update_lastest_folder_path();
        showDeviceContextMenu(pos);
    }
  
    return ;
}

void S7_MainWindows::onAddDataBlockConfig()
{
    if(drop_Area == nullptr)
    {
      drop_Area = new DropArea(this, m_plc_device_pointer, m_lastest_device_TreeWidget_item);
      //    PROBLEM IS BUILD CONNECT DO NOT PASS PARAMETERS FOR AVOIDING STORED PARAMETER IS EXPIRED
      connect(drop_Area, &DropArea::finished, this,&S7_MainWindows::addItem_To_TreeAndTab);
    }

    //  UPDATE DEVICE POINTER
    drop_Area->device_pointer = m_plc_device_pointer ;
    drop_Area->exec();
}

void S7_MainWindows::onLoadExistingDataConfig()
{
    this->loadExistingDataConfigFile();
}

void S7_MainWindows::addItem_To_TreeAndTab() {
  for (int i = 0;
       i < this->m_plc_device_pointer->m_dataBlock_config_list.size(); ++i) {
    //  ADD ITEM INTO dataStructEdit_map
    std::string source_file_content = "";
    bool parse_result = this->m_plc_device_pointer->parse_file_content(
        source_file_content,
        m_plc_device_pointer->m_dataBlock_config_list.back().toStdString());
    if (!parse_result) {
      std::cout << "parse dataBlock config file is error " << std::endl;
    }
  
    // DEAL PARSED DATA BLOCK CONFIG CONTENT
    QFileInfo file(m_plc_device_pointer->m_dataBlock_config_list.back());
    auto result = this->deal_parsedDataBlockConfig(file,source_file_content);
    if(result.is_fail())
    {
        std::cout<<result.unwrap_err().what()<<std::endl;
    }
    m_plc_device_pointer->m_dataBlock_config_list.pop_back();
  }
}

void S7_MainWindows::loadExistingDataConfigFile() {
  auto it = this->m_plc_device_pointer->return_input_ipAddress();
  if (it.is_fail()) {
    std::cout << "Acquire PLC Device ipAddress fail" << std::endl;
    return;
  }
  QStringList dbFiles;
  std::string source_file_content;
  {
    QString ipAddress = QString::fromStdString(it.unwrap_returnLeftValue());
    auto result = this->create_device_folder(ipAddress);
    if (result.is_fail()) {
      std::cout << it.unwrap_err().what() << std::endl;
      return;
    }

    this->findDataConfig(result.unwrap_returnLeftValue(), dbFiles);
  }

  if (dbFiles.count() > 0) {
    for (auto &it : dbFiles) {
      source_file_content = "";
      if (!this->m_plc_device_pointer->parse_file_content(source_file_content,
                                                          it.toStdString())) {
        std::cerr << "parse_file_content is error\n";
        continue;
      }
      QFileInfo file(it);
      auto result = this->deal_parsedDataBlockConfig(file,source_file_content);
      if(result.is_fail())
      {
        std::cerr << result.unwrap_err().what() << std::endl;
      }
    }
  }
  else
  {
    std::cerr<<"db Files count = 0 !\n"<<std::endl;
  }
}

void S7_MainWindows::findDataConfig(const QString &folder_path, QStringList &result)
{
  // 递归遍历所有文件
  QDirIterator it(folder_path,
                  QDir::Files,                   // 只查找文件
                  QDirIterator::Subdirectories); // 递归子目录

  while (it.hasNext()) {
    it.next();
    QString fileName = it.fileName();

    if (fileName.contains("DB", Qt::CaseInsensitive)) {
      result.append(it.filePath());
      std::cout << "Add successfully !\n";
    }
    std::cout<<"Add done !\n";
  }
}

void S7_MainWindows::save_DataConfig(const std::string &source_file_content, const QString &dest_file_path)
{
  std::ofstream out_file(dest_file_path.toStdString(), std::ios::out | std::ios::trunc);
  if (!out_file.is_open()) {
    std::cerr << "Failed to open file: " << dest_file_path.toStdString() << std::endl;
    return ;
  }

  out_file << source_file_content;
  out_file.close();
}

QString S7_MainWindows::getFullFilePath(const QString &file_basename)
{
  QDir dir(m_lastest_device_folder_path);
  return dir.absoluteFilePath(file_basename);
}

Result<bool, RichError>
S7_MainWindows::deal_parsedDataBlockConfig(const QFileInfo &file,std::string &source_file_content) {
  {
    auto result =
        m_plc_device_pointer->parse_dataBlock_To_device(source_file_content);
    if (result.is_fail()) {
      //  CLEAR RESULT OF DRAG AND DROP
      m_plc_device_pointer->m_dataBlock_config_list.clear();
      return result;
    }
  }

  //  ADD dataStruct INTO TAB
  auto result = Display_File_To_Tab(file.baseName());
  if (result.is_fail()) {
    return result;
  }

  //  ADD CHILD WIDGET ITEM INTO PARENT WIDGET ITEM IN QTREEWIDGET
  QTreeWidgetItem *child_item = new QTreeWidgetItem();
  child_item->setData(0, Qt::UserRole,
                      QVariant::fromValue<void *>(m_data_struct_editor));
  child_item->setText(0, file.baseName());
  m_lastest_device_TreeWidget_item->addChild(child_item);
  m_plc_device_pointer->add_subTreeWidgetItem(file.baseName(), child_item);

  //  SAVE DATA BLOCK CONFIG TEXT INTO DEVICE FOLDER
  QString dest_file_path = this->getFullFilePath(file.fileName());
  this->save_DataConfig(source_file_content, dest_file_path);

  return Result<bool,RichError> (true);
}

void S7_MainWindows::onDeleteDataBlockConfig(PLC_Device *device_pointer,QTreeWidgetItem *item)
{
    for(int i = 0 ; i < item->childCount() ; ++i)
    {
        item->takeChild(i);
    }
    std::cout<<"remove DataBlock in QTreeWdiget \n";
}


void S7_MainWindows::onAddDevice() 
{
    if(m_addDevice_page->exec() != QDialog::Accepted)
    {
        return;
    }

    QString ipAddress(m_ip_Address_edit->text());
    QString port(m_port_edit->text());
    QString nameSpace(m_nameSpace_edit->text());
    auto string_ipAddress = ipAddress.toStdString();
    auto string_port = port.toInt();
    auto string_nameSpace = nameSpace.toInt();

    //  INITALIZE DEVICE_POINTER
    if(m_communicate_type_box->currentText() == "OPC_UA Address")
    {
      m_plc_device_pointer = PLC_Device::builder::create_builder()
                                 .set_UA_Access(string_ipAddress,string_nameSpace,string_port)
                                 .raw_ptr_build();
    
    }
    else if(m_communicate_type_box->currentText() == "Offset Address")
    {
      m_plc_device_pointer = PLC_Device::builder::create_builder()
                                 .set_S7_Access(string_ipAddress, 0, 1)
                                 .raw_ptr_build();
    }
    
    //  INTIALIZE QTREEWIDGET ITEM
    QTreeWidgetItem* device_item = new QTreeWidgetItem();
    device_item->setText(0,ipAddress.toStdString().data());
    device_item->setData(0,Qt::UserRole,QVariant::fromValue(static_cast<void*>(m_plc_device_pointer)));
    this->update_lastest_itemPointer(device_item, nullptr);

    //  TRT CONNECT PLC
    int i = 0;
    while(1)
    {
        if(!m_systemSetting->connect_retry(i))
        {
            std::cerr << "retry connect times exceeded limitation" << std::endl;
            std::cout << "device connect fail" << std::endl;
            this->update_lastest_itemPointer(nullptr, nullptr);
            return;
        }
        ;
        auto result = m_plc_device_pointer->connect();
        if(result.is_fail())
        {
            i = i + 1;
            this->update_parent_item_color(false);
            continue;
        }
        else
        {
            this->update_parent_item_color(true);
        }
        std::cout << "device connect successfully" << std::endl;
        break;
    }

    //  ADD QTREEWIDGET_ITEM INTO QTREEWIDGET
    m_datablockname_tree->addTopLevelItem(device_item);
    //  ADD PLC_DEVICDE POINTER INTO DEVICE_VECTOR
    m_plc_device_pointer_vector.push_back(m_plc_device_pointer);
    //  UPDATE DEVICE_MONITOR
    m_device_monitor->update_widget();

    auto it = this->create_device_folder(ipAddress);
    if(it.is_fail())
    {
        std::cout<<it.unwrap_err().what()<<std::endl;
    }
    else
    {
        std::cout<<"create PLC Device successfully"<<std::endl;
    }
  
}

void S7_MainWindows::onDeleteDevice()
{
    for(int i = 0 ; i < m_datablockname_tree->topLevelItemCount() ; ++i)
    {
        QTreeWidgetItem *item = m_datablockname_tree->topLevelItem(i);

        QVariant itemData = item -> data(0,Qt::UserRole);
        if(itemData.isValid())
        {
            void *storePtr = itemData.value<void*>();
            PLC_Device *storeDevice = static_cast<PLC_Device*>(storePtr);
            if(storeDevice == m_plc_device_pointer)
            {
                //  CHECK THE DEVICE POINTER WHETHER EXIST IN DEVICE_VECTOR
                auto it = clear_delete_device(storeDevice);
                if(it.is_success())
                {
                    //  DELETE DEVICE FROM QTREEWIDGET
                    delete m_datablockname_tree->takeTopLevelItem(i);
                    //  DELETE RAW POINTER
                    delete storeDevice;
                    //  UPDATE DEVICE_MONITOR
                    m_device_monitor->update_widget();
                }
                break;
            }
        }
        //  DELETE SELECT DEVICE IN DEVICE_VECTOR
        auto it = std::find(m_plc_device_pointer_vector.begin(),m_plc_device_pointer_vector.end(),m_plc_device_pointer);
        if(it != m_plc_device_pointer_vector.end())
        {
            m_plc_device_pointer_vector.erase(it);
            m_plc_device_pointer = nullptr ;
        }
        //  UPDATE DEVICE_MONITOR
        m_device_monitor->update_widget();
    }
}

void S7_MainWindows::onDeleteDataStruct()
{
    for(int i = 0 ; i < m_datablockname_tree->topLevelItemCount() ; ++i)
    {
      QTreeWidgetItem *item = m_datablockname_tree->topLevelItem(i);
      if (m_lastest_device_TreeWidget_item == item) {
        for (int i = 0; i < m_lastest_device_TreeWidget_item->childCount();
             ++i) {
          QTreeWidgetItem *child = m_lastest_device_TreeWidget_item->child(i);
          int tab_index = this->return_tab_index(child);
          if(tab_index == -1)
          {
            std::cerr<<"tab index return exist error\n";
            continue;
          }
          if (child == m_lastest_dataBlock_TreeWidget_item) { // 根据文本判断
            this->onDeleteSelectTabClicked(tab_index);
            QString file_name(child->text(0));
            // 从父项中移除并删除子项
            delete m_lastest_device_TreeWidget_item->takeChild(i);
            //  CHECK THE DEVICE POINTER WHETHER EXIST IN DEVICE_VECTOR
            QString dataStructEditor_name(item->text(0));
            //    DELETE DATA STRUCT ITEM IN DATA STRUCT MAP
            this->m_plc_device_pointer->delete_subConfig(dataStructEditor_name);
            break;
          }
        }
          break;
        }
    }
}

Result<bool,RichError> S7_MainWindows::clear_delete_device(PLC_Device* pointer)
{
    auto it = std::find(m_plc_device_pointer_vector.begin(),
                        m_plc_device_pointer_vector.end(), pointer);
    if (it != m_plc_device_pointer_vector.end()) {
        m_plc_device_pointer_vector.erase(it);
        return Result<bool,RichError> (true);
    }
        return Result<bool,RichError> (RichError("device pointer do not exist in vector"));
}

Result<bool,RichError> S7_MainWindows::validExistenceParent(const QString& QString_file_path)
{
    for(auto *it : (m_plc_device_pointer_vector))
    {
       if (it->return_input_ipAddress().unwrap_returnRightValue() == QString_file_path.toStdString()) 
       {
            m_plc_device_pointer = it ; 
            return Result<bool,RichError> (true);
       }
    }
    return Result<bool,RichError> (RichError("lastest device do not exist"));
}

Result<bool, RichError>
S7_MainWindows::validExistenceParent(QTreeWidgetItem *item) {
  QTreeWidgetItem *parent_item = item->parent();
  if (parent_item) {
    //  the item do not have parent , it means the item is device
    QVariant itemData = parent_item->data(0, Qt::UserRole);
    this->update_lastest_itemPointer(parent_item, item);

    if (itemData.isValid()) {
      // the sub item have correct address
      void *storePtr = itemData.value<void *>();
      m_plc_device_pointer = static_cast<PLC_Device *>(storePtr);
      return Result<bool, RichError>(true);
    }
    else
    {
      return Result<bool, RichError>(RichError("itemData is invalid"));
    }
  }
  return Result<bool, RichError>(false);
}

void S7_MainWindows::update_lastest_folder_path()
{
    QString ip_Address(QString::fromStdString(m_plc_device_pointer->return_input_ipAddress().unwrap_returnRightValue()));
    auto result = create_device_folder(ip_Address);
    if(result.is_success())
    {
        m_lastest_device_folder_path = result.unwrap_returnLeftValue(); 
    }
    else
    {
        m_lastest_device_folder_path = "";
        return;
    }
}

void S7_MainWindows::update_lastest_itemPointer(QTreeWidgetItem *parent_item,QTreeWidgetItem *sub_item)
{
    m_lastest_device_TreeWidget_item = parent_item;
    m_lastest_dataBlock_TreeWidget_item = sub_item;
    std::cout<<"update_lastest_itemPointer done\n";
}


void S7_MainWindows::initalize_last_parameter()
{
    m_plc_device_pointer = nullptr;
    m_lastest_device_folder_path = "";
    this->update_lastest_itemPointer(nullptr, nullptr);
}

void S7_MainWindows::update_select_tab(DataStructeEditor *item,const QString &file_path)
{
  m_data_struct_editor = item;
  m_data_struct_editor->update_LittleEndianBuffer_from_table();
  m_data_struct_editor->write_Table_From_S7_DatabBlockBuffer();
  int tab_index = m_tab_widget->addTab(m_data_struct_editor, file_path);
  m_tab_widget->setCurrentIndex(tab_index);
}

int S7_MainWindows::return_tab_index(QTreeWidgetItem *item) {
  QVariant itemData = item->data(0, Qt::UserRole);
  if (!itemData.isValid()) {
    std::cout << "item store ptr is invalid\n";
    return -1;
  }
  void *storePtr = itemData.value<void *>();
  DataStructeEditor *m_lastest_editor(
      static_cast<DataStructeEditor *>(storePtr));

  // m_data_struct_editor 是唯一的
  for (int i = 0; i < m_tab_widget->count(); ++i) {
    DataStructeEditor *editor =
        qobject_cast<DataStructeEditor *>(m_tab_widget->widget(i));

    if (editor == m_lastest_editor) {
        return i;
    }
  }
  return -1; // 未找到
}

void S7_MainWindows::update_parent_item_color(bool status)
{
    if(status)
    {
        //  CONNECT SUCCESSFULLY -> GREEN
        m_lastest_device_TreeWidget_item->setForeground(0, QBrush(QColor("#AED311")));
    }
    else
    {
        //  CONNECT SUCCESSFULLY -> RED 
        m_lastest_device_TreeWidget_item->setForeground(0, QBrush(QColor("#E80A00")));
    }
}

void S7_MainWindows::showBlankAreaContextMenu(const QPoint& pos)
{
    QMenu device_menu_var;

    QAction *addDeviceAction = device_menu_var.addAction("Add PLC Device");
    device_menu_var.addSeparator();
    
    //  connect actions
    connect(addDeviceAction,&QAction::triggered,this,&S7_MainWindows::onAddDevice);

    device_menu_var.exec(QCursor::pos());
}

void S7_MainWindows::showDeviceContextMenu(const QPoint& globalPos)
{
    if(loadExternalDataConfigAction == nullptr || deleteDataBLockAction == nullptr || loadInternalDataConfigAction == nullptr )
    {
        loadExternalDataConfigAction = device_menu_var.addAction("Add DataBlock");
        deleteDataBLockAction = device_menu_var.addAction("Delete PLC Device");
        loadInternalDataConfigAction = device_menu_var.addAction("Load Existing DataConfig");

        device_menu_var.addSeparator();

        connect(loadExternalDataConfigAction, &QAction::triggered, this,
                &S7_MainWindows::onAddDataBlockConfig);
        connect(deleteDataBLockAction, &QAction::triggered, this,
                &S7_MainWindows::onDeleteDevice);
        connect(loadInternalDataConfigAction, &QAction::triggered, this,
                &S7_MainWindows::onLoadExistingDataConfig);
    }
 
    device_menu_var.exec(m_datablockname_tree->viewport()->mapToGlobal(globalPos));
}

void S7_MainWindows::showDataStructMenu(const QPoint& globalPos)
{
    if(deleteExistingDataConfigAction == nullptr)
    {
        deleteExistingDataConfigAction = dataStruct_menu_var.addAction("delete dataStruct");
        dataStruct_menu_var.addSeparator();

        connect(deleteExistingDataConfigAction, &QAction::triggered, this,
                &S7_MainWindows::onDeleteDataStruct);
    }
 
    dataStruct_menu_var.exec(m_datablockname_tree->viewport()->mapToGlobal(globalPos));
}

void S7_MainWindows::clear_tree_DataBlock(QTreeWidgetItem* item)
{
    QList<QTreeWidgetItem*> children;
    //  COLLECT ALL CHILDREN ITEM OF ITEM
    for(int i = 0 ; i < item->childCount() ; ++i)
    {
        children.append(item->child(i));
    }

    //  BREAK DOWN PARENT-CHILD RELATIONSHIP AND DELETE CHILED ITEM
    for(auto &item : children)
    {
        item->removeChild(item);
        delete item;
    }
    std::cout<<"clear_tree_DataBlock : done\n";
}

void S7_MainWindows::load_tree_DataBlock(QTreeWidgetItem* item,PLC_Device* device_ptr)
{
    if(device_ptr)
    {
        auto dataStruct_map = device_ptr->getEditors();
        for(auto& data_block : dataStruct_map)
        {
            QTreeWidgetItem* data_block_item = new QTreeWidgetItem();
            data_block_item->setText(0,data_block.first.data());
            data_block_item->setData(0,Qt::UserRole,QVariant::fromValue(static_cast<void*>(data_block.second)));

            item->addChild(data_block_item);
        }
    }
    else
    {
        std::cerr<<"load_tree_DataBlock : device_ptr is nullptr\n";
    }
    std::cout<<"load_tree_dataBlock"<<std::endl;
}

void S7_MainWindows::initialize_Add_Device_Page()
{
  m_addDevice_page = new QDialog(this);
  QVBoxLayout *m_addDevice_layout = new QVBoxLayout(m_addDevice_page);

  m_ip_Address_edit = new QLineEdit("192.168.0.2");
  m_nameSpace_edit = new QLineEdit("3");
  m_port_edit = new QLineEdit("4840");

  m_communicate_type_box = new QComboBox();
  m_communicate_type_box->addItem("Offset Address");
  m_communicate_type_box->addItem("OPC_UA Address");

  QFormLayout *formLayout = new QFormLayout;
  formLayout->addRow("PLC nameSpace:", m_nameSpace_edit);
  formLayout->addRow("PLC Port:", m_port_edit);
  formLayout->addRow("PLC IP Address:", m_ip_Address_edit);
  formLayout->addRow("PLC Type:", m_communicate_type_box);
  m_addDevice_layout->addLayout(formLayout);

  QPushButton *okButton = new QPushButton("OK");
  QPushButton *cancelButton = new QPushButton("Cancel");

  QHBoxLayout *buttonLayout = new QHBoxLayout;
  buttonLayout->addStretch();
  buttonLayout->addWidget(okButton);
  buttonLayout->addWidget(cancelButton);
  m_addDevice_layout->addLayout(buttonLayout);

  connect(okButton, &QPushButton::clicked, m_addDevice_page, &QDialog::accept);
  connect(cancelButton, &QPushButton::clicked, m_addDevice_page,
          &QDialog::reject);
}

Result<QString, RichError>
S7_MainWindows::create_device_folder(const QString &ipAddress) {
  if (ipAddress.isEmpty()) {
    return Result<QString, RichError>(RichError("ipAddress is empty"));
  }

  QDir dir(ipAddress);

  // 标准化路径
  QString canonicalPath = dir.absolutePath();

  if (dir.exists()) {
    if (dir.isReadable()) {
      return Result<QString, RichError>(canonicalPath); // 已存在，返回路径
    }
    return Result<QString, RichError>(RichError("folder path is invalid"));
  }

  // 创建文件夹
  if (dir.mkpath(".")) {
    // 验证文件夹确实被创建
    if (dir.exists() && dir.isReadable()) {
      return Result<QString, RichError>(canonicalPath);
    }
  }

  return Result<QString, RichError>(RichError("folder create fail"));
}

void S7_MainWindows::initialize_toolBar()
{
    QToolBar *monitor_ToolBar = new QToolBar(tr("device bar"),this);
    monitor_ToolBar->setMovable(false);
    addToolBar(Qt::TopToolBarArea, monitor_ToolBar);

    QAction *toggleMonitorAction = new QAction(tr("device monitor"),this);
    toggleMonitorAction->setCheckable(true);
    toggleMonitorAction->setChecked(false);

    QAction *toggleSystemSetting = new QAction (tr("system setting"),this);
    toggleSystemSetting->setCheckable(true);
    toggleSystemSetting->setChecked(false);

    QAction *test_send_prometheus_message = new QAction (tr("send_prometheus_message"),this);
    test_send_prometheus_message->setCheckable(true);
    test_send_prometheus_message->setChecked(false);

    QAction *test_display_grafana_web_view =
        new QAction(tr("display_grafana_web_view"), this);
    test_display_grafana_web_view->setCheckable(true);
    test_display_grafana_web_view->setChecked(false);

    connect(toggleMonitorAction, &QAction::toggled, [this](bool checked) {
      if (m_device_monitor && m_device_monitor->m_device_dock) {
        m_device_monitor->m_device_dock->setVisible(checked);
      }
    });

    connect(toggleSystemSetting, &QAction::toggled, [this](bool checked) {
      if(m_systemSetting)
      {
        m_systemSetting->setVisible(checked);
        if(checked)
        {
            m_systemSetting->display_current_setting();
        }
      }
    });

    connect(test_send_prometheus_message, &QAction::toggled, [this](bool checked) {
      if(m_metrics!=nullptr && checked)
      {
        m_metrics->simulateConcurrentRequests(20);
      }
    });

    connect(test_display_grafana_web_view, &QAction::toggled,
            [this](bool checked) {
            //   if (checked) {
            //     //  设置阻断对话框，直到输入完成才进行下一步
            //     bool ok;
            //     QString text =
            //         QInputDialog::getText(this, tr("输入"), tr("请输入内容:"),
            //                               QLineEdit::Normal, "", &ok);
            //     if (ok && !text.isEmpty()) {
            //     } else {
            //       return;
            //     }
            //     QDateTime now = QDateTime::currentDateTime();
            //     QDateTime from = now.addSecs(-3600); // 1小时前
            //     QDateTime to = now;                  // 当前时间
            //     // 确保使用 UTC 时间
            //     QDateTime utcTime = from.toUTC();
            //     // 格式化为 ISO 8601 并添加 'Z' 表示 UTC
            //     QString isoString = utcTime.toString(Qt::ISODate);
            //     // Qt::ISODate 可能不会添加 'Z'，需要手动添加
            //     if (!isoString.endsWith('Z')) {
            //       isoString += 'Z';
            //     }
            //     // 确保使用 UTC 时间
            //     QDateTime utcTime_to = to.toUTC();
            //     // 格式化为 ISO 8601 并添加 'Z' 表示 UTC
            //     QString isoString_to = utcTime_to.toString(Qt::ISODate);
            //     // Qt::ISODate 可能不会添加 'Z'，需要手动添加(实际上已添加)
            //     if (!isoString_to.endsWith('Z')) {
            //       isoString_to += 'Z';
            //     }
            //     // qDebug() << "Formatted time for Prometheus:"
            //     //          << from.toString("yyyy-MM-dd HH:mm:ss") << "→" <<
            //     //          isoString;
            //     auto result = ObjectRouter::instance().invoke(
            //         "config://dashboard/server_01", "queryGrafanaPreset",
            //         QVariantList() << "afam1c7tfp05cc" << isoString
            //                        << isoString_to << text);
            //   }
                if(checked)
                {
                    if(m_prometheus_view == nullptr)
                    {
                        this->m_LineChart = new LineChart();
                        GrafanaClient *m_grafanaClient = new GrafanaClient();
                        ObjectRouter::instance().registerObject(
                            "config://dashboard/server_01", m_grafanaClient);

                        DataProcessor *dataProcessor = new DataProcessor();
                        ChartController *item = new ChartController();
                        // m_prometheus_view = this->m_monitor_dashBoard->createRealtimePage();
                        m_prometheus_view = this->m_LineChart->createChartView(
                            "request rate", "request_per_second", Qt::blue);
                        item->m_connectionManager->safeConnect(
                            m_grafanaClient, &GrafanaClient::JsonData_ready,
                            dataProcessor, &DataProcessor::processJsonData);
                        item->m_connectionManager->safeConnect(
                            dataProcessor, &DataProcessor::dataParsed,
                            this->m_LineChart->return_Model(), &MyModel::addChartPointData);
                    }
                    m_prometheus_view->show();
                }
        });

    monitor_ToolBar->addAction(toggleMonitorAction);
    monitor_ToolBar->addAction(toggleSystemSetting);
    monitor_ToolBar->addAction(test_send_prometheus_message);
    monitor_ToolBar->addAction(test_display_grafana_web_view);

    //  ADD SEPARATOR FRAME FOR EVERY ACTION
    monitor_ToolBar->addSeparator();
}

void S7_MainWindows::build_connect_searchBar()
{
    m_completerModel = new QStringListModel(this);
    m_completer = new QCompleter(m_completerModel,this);

    m_completer->setCaseSensitivity(Qt::CaseSensitive);
    m_completer->setFilterMode(Qt::MatchContains);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);

    //  ENABLE THE DROPDOWN FEATURE
    m_search_bar->setCompleter(m_completer);
}

Result<bool, RichError> S7_MainWindows::CheckIpAddressFileName(const QString& fileName)
{
    // Regular expression for IP address pattern
    QRegularExpression ipRegex("^\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}$");
    return Result<bool,RichError>(ipRegex.match(fileName).hasMatch());
}

Result<bool,RichError> S7_MainWindows::CheckSelectFileInTab(const QString& QString_file_path,QTreeWidgetItem *item)
{ 
    if(CheckIpAddressFileName(QString_file_path).unwrap_returnRightValue() == true)
    {
        //  UPDATE LATEST DEVICE POINTER ACCORDING TO SELECT QSTRING_FILE_PATH 
        auto it = validExistenceParent(QString_file_path);
        if(it.is_fail())
        {
            std::cout<<it.unwrap_err().what()<<std::endl;
            return Result<bool,RichError> (it);
        }
    }

    //  if the select item is subItem,need update its parent item
    auto it = validExistenceParent(item);
    if (it.is_fail()) {
      return Result<bool, RichError>(it);
    }

    //  UPDATE CONTENT OF DISPLAYING IN TAB WIDGET  
    {
        auto result = m_plc_device_pointer->find_related_dataStruct(QString_file_path.toStdString());
        if(result.is_success())
        {
          this->update_select_tab(result.unwrap_returnLeftValue(),
                                  QString_file_path);
          return Result<bool, RichError>(
              m_data_struct_editor->write_Table_From_S7_DatabBlockBuffer());
        }
    }
    return Result<bool,RichError>(RichError("object is not find !"));
}

Result<bool,RichError> S7_MainWindows::Display_File_To_Tab(const QString& file_baseName)
{ 
    //  create new file editor and store into TabWidget
    auto result = this->m_plc_device_pointer->find_related_dataStruct(file_baseName.toStdString());
    if(result.is_fail()) 
    {
        return Result<bool,RichError> (RichError(result.unwrap_err()));
    }
    else
    {
      this->update_select_tab(result.unwrap_returnLeftValue(), file_baseName);
    }
    return Result<bool,RichError>(true);
}

Result<bool, RichError>
S7_MainWindows::is_connect_check() {
  bool is_find_device = false;
  for (int i = 0; i < m_datablockname_tree->topLevelItemCount(); ++i) {
    QTreeWidgetItem *item = m_datablockname_tree->topLevelItem(i);

    QVariant itemData = item->data(0, Qt::UserRole);
    if (!itemData.isValid()) {
        continue;
    }
    else{
      void *storePtr = itemData.value<void *>();
      PLC_Device *storeDevice = static_cast<PLC_Device *>(storePtr);
      if (storeDevice != m_plc_device_pointer) {
        //  THE DEVICE IS NOT TARGET DEVICE
        continue;
      } else {
        is_find_device = true;
        if (m_plc_device_pointer->connect().is_fail()) {
          //  CONNECT FAIL -> RED
          item->setForeground(0, QBrush(QColor("#E80A00")));
          return Result<bool, RichError>(RichError("connect stauts in error"));
        } else {
          //  CONNECT SUCCESSFULLY -> GREEN
          item->setForeground(0, QBrush(QColor("#AED311")));
          break;
        }
      }
    }
  }

  return Result<bool, RichError>(is_find_device);
}

void S7_MainWindows::write_DataBlock_From_TableBuffer(bool is_update_buffer)
{
  // m_data_struct_editor->m_single_data_block->SendBuffer_ToPLC(client_object);
  if (m_plc_device_pointer->m_S7_Access != nullptr) {
    auto result = m_data_struct_editor->update_BigEndianBuffer_from_table();
    if (result.is_success()) {
      m_data_struct_editor->SendBuffer_ToPLC(m_plc_device_pointer);
    }

  } else if (m_plc_device_pointer->m_UA_Access != nullptr) {
    auto result = m_data_struct_editor->update_LittleEndianBuffer_from_table();
    if (result.is_success()) {
      m_data_struct_editor->SendBuffer_ToPLC(m_plc_device_pointer);
    }
  } else {
    std::cout << "update_buffer_from_table call is error" << std::endl;
  }
}

void S7_MainWindows::write_Table_From_S7_DatabBlockBuffer(bool is_write_Table_From_S7_DatabBlockBuffer)
{ 
    m_data_struct_editor->write_Table_From_S7_DatabBlockBuffer();
    std::cout<<"write_Table_From_S7_DatabBlockBuffer is successful"<<std::endl;
} 

void S7_MainWindows::read_DataBlock_From_DataBlockBuffer(bool is_done_successfully)
{
    auto check_result = m_plc_device_pointer->connect();
    if(check_result.is_fail())
    {
       std::cout<<check_result.unwrap_err().what()<<std::endl; 
       return;
    }
    else
    {
        if(!check_result.unwrap_returnLeftValue())
        {
            std::cout<<"target device do not find"<<std::endl;
            return;
        }
    }

    auto it = this->m_plc_device_pointer->read_DataBlock(this->m_data_struct_editor,this->m_plc_device_pointer);
    if(it.is_success())
    {
        std::cout<<"read_DataBlock_From_PLC is successful\n";
    }
    else {
        std::cerr<<it.unwrap_err().what()<<std::endl;
    }
}

Result<bool,RichError> S7_MainWindows::Delete_File_To_Tab(int index)
{ 
    if(index >= 0 && index < m_tab_widget->count())
    {
        m_tab_widget->removeTab(index);
        return Result<bool,RichError>(true);
    }
    else
    {
        return Result<bool,RichError>(RichError("index out of range"));
    }
}
