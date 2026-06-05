#include "MQTTEncryptionClient/MQTTEncryptionClient.h"
#include <QSurfaceFormat>
#include "MQTTDecryptionServer/MQTTDecryptionServer.h"
#include "ProtocolHeader/ProtocolHeader.h"
// #include "Sqlite_DB/Sqlite_DB.h"
#include "MainWindows/MainWindow_Rebuild.h"
#include "PLC/SingleDataBlockRebuild.h"
#include "Event_Tracking/Event_Tracking.h"
// #include "PLC/OPC_UA.h"
#include "GrafanaDashboardManager/ObjectRouter.h"
#include "GrafanaDashboardManager/GrafanaDashboard.h"
#include "PLC/Struct.h"
#include "PLC/XMLParser.h"

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
