
#include "MQTTEncryptionClient/MQTTEncryptionClient.h"
#include "MQTTDecryptionServer/MQTTDecryptionServer.h"
#include "ProtocolHeader/ProtocolHeader.h"
#include "Sqlite_DB/Sqlite_DB.h"
#include "MainWindows/MainWindows.h"
#include <QtCore/qtimer.h>

constexpr int Create_New = 0;
int main(int argc, char *argv[])
{
    auto logger = spdlog::basic_logger_mt("basic_logger", "logs/basic.txt");
    spdlog::shutdown();

    MqttClient mqttClient;
    MqttServer mqttServer; 

    mqttClient.config_load();
    mqttServer.config_load();

    mqttClient.createinstance();
    mqttServer.createinstance();    

    Sqlite_DB& db = Sqlite_DB::GetInstance();

    std::unique_ptr<Sqlite_DB_Manager> db_manager_ptr = std::make_unique<Sqlite_DB_Manager>(&mqttClient,logger);
    std::unique_ptr<DownloadTasks> downloadTasks_ptr = std::make_unique<DownloadTasks>(db_manager_ptr.get());
    db_manager_ptr->thread_pool_start();

    qputenv("QT_PLUGIN_SYNCHRONOUS_LOAD", "1");  // 同步加载插件
    QApplication a(argc, argv);
    qRegisterMetaType<MainWindows_Intermediate_Struct>("MainWindows_Intermediate_Struct");  // 注册自定义类型
    MainWindows w(nullptr,downloadTasks_ptr.get());
    w.check_DownLoadFolder_initalize();
    w.show();

    QString currentDir = QDir::currentPath();
    QString folder_path = currentDir + "/" + "DownloadFileManagement" ;
    int rc = downloadTasks_ptr->is_exist_targetFolder(folder_path.toStdString());
    if (rc == false)
    {
        std::cerr<<"addFileToList:: Folder not exist! Please create it first!\n";
        return 0;
    }
    

    mqttServer.m_client->set_callback(mqttServer);
    
    mqttClient.connectinstance();
    mqttServer.connectinstance();
    mqttServer.m_token = mqttServer.m_client->subscribe(mqttServer.m_topicName, mqttServer.m_qos);

    {
        std::cout<<"Server is running , waiting for file .... \n";
    }

    // QTimer *time = new QTimer(&w);
    // QObject::connect(time, &QTimer::timeout, &w, [&]()
    // {
    //     std::cout<<"Auto shutdown timer triggered . \n";
    //     a.quit();
    // }
    // );
        
    // time->setSingleShot(true);
    // time->start(30000);
    rc = a.exec();
    Sqlite_DB::CloseDB();
    return rc;
}
