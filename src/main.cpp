#include "MQTTEncryptionClient/MQTTEncryptionClient.h"
#include "MQTTDecryptionServer/MQTTDecryptionServer.h"
#include "ProtocolHeader/ProtocolHeader.h"
#include "Sqlite_DB/Sqlite_DB.h"
#include "MainWindows/MainWindows.h"
#include <QtCore/qtimer.h>
#include "PLC/Map_PLCStruct.h"


constexpr int Create_New = 0;
int main(int argc, char *argv[])
{
    auto logger = spdlog::basic_logger_mt("basic_logger", "logs/basic.txt");
    spdlog::shutdown();

    std::string file_path = "PLC_config.txt";
    SCL_Parser plc_parser(file_path);
    auto read_result = plc_parser.read_file_content(file_path);
    Single_Data_Block single_data_block;

    if(read_result.is_success())
    {
        auto parse_result = plc_parser.parse(file_path);
        if(parse_result.is_success())
        {
            single_data_block.add_datablock_from_DataBlockDefinition(parse_result.unwrap());
        }
        std::cout<<"Gateway Data Generator is done."<<std::endl;
    }

    
    download_path_manager download_path_manager;
    buffer_administrator buffer_administrator;

    auto Sqlite_DB_function_builder = Sqlite_DB_function::builder::create_builder();
    Sqlite_DB_function_builder.set_memory_pool_pointer(&memory_pool::getInstance());
    Sqlite_DB_function_builder.set_download_path_manager_ref(download_path_manager);
    std::unique_ptr<Sqlite_DB_function> m_Sqlite_function = Sqlite_DB_function_builder.unique_ptr_build();
    
    MqttClient mqttClient;
    mqttClient.config_load();

    auto Mqttserver_builder = MqttServer::builder::create_builder();
    Mqttserver_builder.set_inflight_pointer(&buffer_administrator);
    MqttServer mqttServer = Mqttserver_builder.instance_build();
    mqttServer.config_load();

    std::unique_ptr<Sqlite_DB_write_file> m_sqlite_DB_write_file_ptr = std::make_unique<Sqlite_DB_write_file>(*(m_Sqlite_function.get()));
    std::unique_ptr<Sqlite_information> m_sqlite_information_ptr = std::make_unique<Sqlite_information>(*(m_Sqlite_function.get()));
    std::unique_ptr<DownloadTasks> downloadTasks_ptr = std::make_unique<DownloadTasks>(*(m_sqlite_information_ptr.get()),mqttClient);
    
    QApplication a(argc, argv);
    qRegisterMetaType<MainWindows_Intermediate_Struct>("MainWindows_Intermediate_Struct");  // 注册自定义类型
    MainWindows w(nullptr,downloadTasks_ptr.get(),download_path_manager,buffer_administrator);
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
    downloadTasks_ptr->start_update_message_buffer();

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
