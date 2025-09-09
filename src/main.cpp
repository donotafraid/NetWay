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
    
    std::unique_ptr<Sqlite_DB_function> m_Sqlite_function = std::make_unique<Sqlite_DB_function>( memory_pool::getInstance() );
    std::unique_ptr<Sqlite_DB_write_file> m_sqlite_DB_write_file_ptr = std::make_unique<Sqlite_DB_write_file>(*(m_Sqlite_function.get()));
    std::unique_ptr<Sqlite_information> m_sqlite_information_ptr = std::make_unique<Sqlite_information>(*(m_Sqlite_function.get()));
    std::unique_ptr<DownloadTasks> downloadTasks_ptr = std::make_unique<DownloadTasks>(*(m_sqlite_information_ptr.get()),mqttClient);
    
    
    qputenv("QT_PLUGIN_SYNCHRONOUS_LOAD", "1");  // 同步加载插件
    QApplication a(argc, argv);
    qRegisterMetaType<MainWindows_Intermediate_Struct>("MainWindows_Intermediate_Struct");  // 注册自定义类型
    MainWindows w(nullptr,downloadTasks_ptr.get());
    w.check_DownLoadFolder_initalize();
    w.show();
    
    m_sqlite_information_ptr->presetting_db_information();
    
    mqttClient.createinstance(m_sqlite_DB_write_file_ptr.get());
    mqttServer.createinstance(logger);    
    mqttClient.connectinstance();
    mqttServer.connectinstance();
    mqttServer.m_token = mqttServer.m_client->subscribe(mqttServer.m_topicName, mqttServer.m_qos);
    mqttServer.start_process_string_to_task();
    mqttServer.start_send_reponse_to_client();

    {
        std::cout<<"Server is running , waiting for file .... \n";
    }

    #ifdef VALGRIND_TEST
    QTimer *time = new QTimer(&w);
    QObject::connect(time, &QTimer::timeout, &w, [&]()
    {
        std::cout<<"Auto shutdown timer triggered . \n";
        a.quit();
    }
    );
        
    time->setSingleShot(true);
    time->start(10000);

    #endif
    int rc = a.exec();
    Sqlite_DB::CloseDB();
    return rc;
}
