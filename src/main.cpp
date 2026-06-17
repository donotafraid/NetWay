#include "MQTTEncryptionClient/MQTTEncryptionClient.h"
#include <QSurfaceFormat>
#include "MQTTDecryptionServer/MQTTDecryptionServer.h"
#include "ProtocolHeader/ProtocolHeader.h"
// #include "Sqlite_DB/Sqlite_DB.h"
#include "MainWindows/MainWindow_Rebuild.h"
#include "PLC/SingleDataBlockRebuild.h"
#include "Event_Tracking/Event_Tracking.h"
#include "GrafanaDashboardManager/ObjectRouter.h"
#include "GrafanaDashboardManager/GrafanaDashboard.h"
#include "PLC/Struct.h"
#include "PLC/XMLParser.h"
#include "PLC/OPCUACSV.h"

int main(int argc, char *argv[]) {
  std::string file_path = "PLC_config.txt";
  {
   spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    // 2. 设置全局日志级别，低于此级别的日志不会被记录
    spdlog::set_level(spdlog::level::debug); // 线上环境一般设为 info 或 warn

    // 4. 创建异步、滚动的文件日志器
    //    - "global_logger": 日志器的唯一标识名称
    //    - "logs/app.log":  日志文件的基础路径和名称，实际文件会像 app.1.log,
    //    app.2.log
    //    - 1048576 * 5:     单个日志文件的最大大小，这里是5MB
    //    - 3:               保留的日志文件数量，总占用 ≈ 5MB * 3 = 15MB
    // 只传文件路径，不传额外参数
    auto async_file_logger = spdlog::create<spdlog::sinks::basic_file_sink_st>(
        "global_logger", // 日志器名称
        "logs/app.log"   // 仅文件路径，文件会无限增长
    );


    // 5. (可选) 设置刷新策略，关键错误时立即刷新，防止数据丢失
    async_file_logger->flush_on(spdlog::level::err);

    // 6. 核心：将这个创建好的日志器设置为全局默认日志器
    //    此后，所有 spdlog::xxx(...) 的调用都会使用这个日志器
    spdlog::set_default_logger(async_file_logger);
  }
  spdlog::info("全局日志系统已启动！");

  //   ObjectRouter::instance().registerObject("config://dashboard/monitor_dashBoard",monitor_dashBoard);

  QApplication a(argc, argv);
  // MyApplication a(argc,argv);
  // WidgetDestructionTracker::instance().install();
  auto registry = std::make_shared<prometheus::Registry>();
  std::shared_ptr<ServiceMetrics> metrics = std::make_shared<ServiceMetrics>(registry);
  std::shared_ptr<Scope> m_scope = std::make_shared<Scope>();

  std::shared_ptr<S7_MainWindows_UI> mainWindows =
      std::make_shared<S7_MainWindows_UI>();
  std::shared_ptr<S7_DeviceManager> windowManager =
      std::make_shared<S7_DeviceManager>();
  std::shared_ptr<DataBlockManager> dataBlockManager =
      std::make_shared<DataBlockManager>();
  std::shared_ptr<OPCUADataBlockManager> OPCUAdataBlockManager =
      std::make_shared<OPCUADataBlockManager>();

  {
    m_scope->registerService(metrics);
    m_scope->registerService(mainWindows);
    m_scope->registerService(dataBlockManager);
    m_scope->registerService(OPCUAdataBlockManager);
    m_scope->registerService(windowManager);

    mainWindows->initalize_scope(m_scope);
    windowManager->initalize_scope(m_scope);
    
    mainWindows->initialize();
    windowManager->initialize();
  }

  { std::cout << "Server is running , waiting for file .... \n"; }

  mainWindows->show();

#ifdef VALGRIND_TEST
#endif
  int rc = a.exec();

  // 在退出前清理 Qt 资源
  QApplication::quit();
  // 等待事件循环结束
  QCoreApplication::processEvents();

  spdlog::shutdown();
  return rc;
}

//===========================================try load infromation from csv file
// int main() {
//   std::string filename = "123.csv";

//   std::cout << "开始解析文件: " << filename << std::endl;
//   std::cout << std::string(120, '=') << std::endl;

//   OPCUAParser parser;

//   // 1. 从文件读取内容
//   std::vector<std::string> lines = parser.readCSVFile(filename);

//   if (lines.empty()) {
//     std::cerr << "错误: 未能读取到任何数据，请检查文件是否存在且格式正确"
//               << std::endl;
//     return 1;
//   }

//   // 2. 显示原始数据预览
//   std::cout << "\n原始数据预览:\n";
//   std::cout << std::string(80, '-') << std::endl;
//   for (size_t i = 0; i < std::min(lines.size(), size_t(3)); ++i) {
//     std::cout << "行 " << (i + 1) << ": " << lines[i] << std::endl;
//   }
//   if (lines.size() > 3) {
//     std::cout << "... 还有 " << (lines.size() - 3) << " 行" << std::endl;
//   }
//   std::cout << std::string(80, '-') << std::endl;

//   // 3. 解析数据并填充结构体（hasHeader=true表示跳过第一行标题）
//   std::vector<OPCUAModernDataStructFromCSV> parsedData =
//       parser.parseAllData(lines, true);

//   // 4. 输出解析结果
//   parser.printData(parsedData);

//   // 5. 输出统计信息
//   parser.printStatistics(parsedData);

//   return 0;
// }

//===============================================try load variable without any external information
// int main(void) {

//   // 2. 设置全局日志级别，低于此级别的日志不会被记录
//   // auto async_file_logger =
//   //     spdlog::basic_logger_st("global_logger", // 日志器名称
//   //                             "logs/app.log" // 仅文件路径，文件会无限增长
//   //     );
//   auto async_file_logger =
//       spdlog::basic_logger_st("global_logger", "logs/app.log", true);

//   async_file_logger->set_level(
//       spdlog::level::debug); // 线上环境一般设为 info 或 warn
//   // 5. (可选) 设置刷新策略，关键错误时立即刷新，防止数据丢失
//   async_file_logger->flush_on(spdlog::level::debug);

//   // 6. 核心：将这个创建好的日志器设置为全局默认日志器
//   //    此后，所有 spdlog::xxx(...) 的调用都会使用这个日志器
//   spdlog::set_default_logger(async_file_logger);
//   spdlog::info("全局日志系统已启动！");

//   // 你的正常程序逻辑
//   spdlog::info("OPC UA 浏览器启动");

//   UA_Client *client = UA_Client_new();
//   // 设置更长的超时时间（因为要浏览很多节点）
//   UA_ClientConfig *ua_config = UA_Client_getConfig(client);
//   ua_config->timeout = 30000; // 30秒

//   spdlog::info("正在连接服务器 opc.tcp://192.168.0.2:4840 ...");
//   UA_StatusCode retval =
//       UA_Client_connect(client, "opc.tcp://192.168.0.2:4840");

//   if (retval != UA_STATUSCODE_GOOD) {
//     spdlog::error("连接失败: {}", UA_StatusCode_name(retval));
//     UA_Client_delete(client);
//     spdlog::shutdown();
//     return -1;
//   }

//   spdlog::info("成功连接到服务器");

//   BrowseConfig config;
//   config.maxDepth = 10;
//   config.maxNodes = 20000;
//   config.timeoutSeconds = 30;
//   config.browseObjects = true;
//   config.browseVariables = true;
//   config.browseObjectTypes = false; // 不浏览类型定义
//   config.browseVariableTypes = false;

//   OPCUAInlineBrowse item;
//   UA_NodeId objectsFolderId = UA_NODEID_NUMERIC(0, 85);
//   item.test_browseNodeChildren(client,objectsFolderId,0,10,"");
//   spdlog::info("浏览完成，准备断开连接");

//   // 断开连接并清理
//   UA_Client_disconnect(client);
//   UA_Client_delete(client);

//   spdlog::info("程序正常退出");
//   spdlog::shutdown();

//   return 0;
// }


//===============================================from XML find infromation for UA Expert
// int main(int argc, char* argv[]) {
//     OPCUANodePathTracer tracer;
    
//     // 默认文件名
//     std::string filename = "test.PLC_1.OPCUA.xml";
//     if (argc > 1) {
//         filename = argv[1];
//     }
    
//     std::cout << "========================================" << std::endl;
//     std::cout << "OPC UA 节点路径追溯工具" << std::endl;
//     std::cout << "========================================" << std::endl;
//     std::cout << "文件: " << filename << std::endl;
//     std::cout << std::endl;
    
//     // 1. 加载 XML 文件
//     if (!tracer.loadFile(filename)) {
//         std::cerr << "加载文件失败" << std::endl;
//         return -1;
//     }
    
//     // 2. 解析所有节点
//     tracer.parseAllNodes();
    
//     // 3. 处理所有包含 AccessLevel 的节点
//     // tracer.processAllNodesWithAccessLevel();
    
//     // 或者只处理第一个节点（简单模式）
//     tracer.processFirstNodeWithAccessLevel();

//     {
//       auto async_file_logger =
//           spdlog::basic_logger_st("global_logger", "logs/app.log", true);

//       async_file_logger->set_level(
//           spdlog::level::debug); // 线上环境一般设为 info 或 warn
//       // 5. (可选) 设置刷新策略，关键错误时立即刷新，防止数据丢失
//       async_file_logger->flush_on(spdlog::level::debug);

//       // 6. 核心：将这个创建好的日志器设置为全局默认日志器
//       //    此后，所有 spdlog::xxx(...) 的调用都会使用这个日志器
//       spdlog::set_default_logger(async_file_logger);
//       spdlog::info("全局日志系统已启动！");

//       // 你的正常程序逻辑
//       spdlog::info("OPC UA 浏览器启动");

//       UA_Client *client = UA_Client_new();
//       // 设置更长的超时时间（因为要浏览很多节点）
//       UA_ClientConfig *ua_config = UA_Client_getConfig(client);
//       ua_config->timeout = 30000; // 30秒

//       spdlog::info("正在连接服务器 opc.tcp://192.168.0.2:4840 ...");
//       UA_StatusCode retval =
//           UA_Client_connect(client, "opc.tcp://192.168.0.2:4840");

//       if (retval != UA_STATUSCODE_GOOD) {
//         spdlog::error("连接失败: {}", UA_StatusCode_name(retval));
//         UA_Client_delete(client);
//         spdlog::shutdown();
//         return -1;
//       }

//       spdlog::info("成功连接到服务器");

//       BrowseConfig config;
//       config.maxDepth = 10;
//       config.maxNodes = 20000;
//       config.timeoutSeconds = 30;
//       config.browseObjects = true;
//       config.browseVariables = true;
//       config.browseObjectTypes = false; // 不浏览类型定义
//       config.browseVariableTypes = false;

//       browseDBAndVariables(client,tracer);
//       spdlog::info("浏览完成，准备断开连接");

//       // 断开连接并清理
//       UA_Client_disconnect(client);
//       UA_Client_delete(client);

//       spdlog::info("程序正常退出");
//       spdlog::shutdown();
//     }
//     return 0;
// }