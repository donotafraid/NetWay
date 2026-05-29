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

  //   ObjectRouter::instance().registerObject("config://dashboard/monitor_dashBoard",monitor_dashBoard);

  QApplication a(argc, argv);
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

// ------------------------------------------------
// int main() {
//   // 读取XML文件
//   std::ifstream file("test.PLC_1.OPCUA.xml");
//   if (!file.is_open()) {
//     std::cerr << "Failed to open XML file" << std::endl;
//     return 1;
//   }

//   std::string xml_content((std::istreambuf_iterator<char>(file)),
//                           std::istreambuf_iterator<char>());
//   file.close();

//   // 创建解析器并执行解析
//   OPCUAXMLParser parser(xml_content);
//   auto result = parser.parse();

//   // 输出解析结果
//   std::cout
//       << "==================== OPC UA XML 解析结果 ====================\n\n";

//   // 1. 生成器信息
//   std::cout << "【生成器信息】\n";
//   std::cout << "  " << result->generator_info << "\n\n";

//   // 2. 命名空间列表
//   std::cout << "【命名空间列表】\n";
//   for (size_t i = 0; i < result->namespace_uris.size(); ++i) {
//     std::cout << "  [" << (i + 1) << "] " << result->namespace_uris[i] << "\n";
//   }
//   std::cout << "\n";

//   // 3. 类型别名（只显示部分关键映射）
//   std::cout << "【关键类型别名】\n";
//   std::vector<std::string> key_aliases = {"BOOL",   "INT",  "DINT", "REAL",
//                                           "STRING", "BYTE", "WORD", "DWORD"};
//   for (const auto &alias : key_aliases) {
//     if (result->type_aliases.find(alias) != result->type_aliases.end()) {
//       std::cout << "  " << alias << " -> " << result->type_aliases[alias]
//                 << "\n";
//     }
//   }
//   std::cout << "\n";

//   // 4. 变量列表（只显示业务相关的变量，过滤系统变量）
//   std::cout << "【业务变量列表】\n";
//   std::cout << "---------------------------------------------------------------"
//                "-------------------------------------\n";
//   std::cout << "序号 | 变量名                    | 类型     | NodeID标识       "
//                "                        | 注释\n";
//   std::cout << "---------------------------------------------------------------"
//                "-------------------------------------\n";

//   int index = 1;
//   for (auto &var : result->variables) {
//     // 过滤掉系统变量（如EnumValues、EngineeringRevision等）
//     if (!parser.shouldKeepVariable(
//             var.variable_name, var.variable_nodeID, var.data_type,
//             var.browse_name,
//             var.filter_reason)) { // 过滤数组索引如"0","1"等
//       continue;
//     }

//     printf(" %-3d | %-25s | %-8s | %-38s | %s\n", index++,
//            var.variable_name.c_str(), var.data_type.c_str(),
//            var.variable_nodeID.c_str(), var.description.c_str());
//   }
//   std::cout << "---------------------------------------------------------------"
//                "-------------------------------------\n";
//   std::cout << "\n共解析 " << result->variables.size() << " 个UAVariable节点，";
//   std::cout << "过滤后显示 " << (index - 1) << " 个业务变量。\n\n";

//   // 5. 输出用于 OPC UA 读取的配置信息
//   std::cout << "【OPC UA 读取配置示例】（用于 Set_Read_NodeID 函数）\n";
//   std::cout << "---------------------------------------------------------------"
//                "-------------------------------------\n";

//   int sample_count = 0;
//   for (const auto &var : result->variables) {
//     if (var.filter_reason == "")
//       break;

//     std::cout << "变量名: " << var.variable_name << "\n";
//     std::cout << "  m_nameSpace = " << var.namespace_index << "\n";
//     std::cout << "  variable_nodeID = \"" << var.variable_nodeID << "\"\n";
//     std::cout << "  完整NodeId = ns=" << var.namespace_index
//               << ";s=" << var.variable_nodeID << "\n\n";
//     std::cout << "  完整过滤原因 : " << var.filter_reason << "\n\n";
//     sample_count++;
//   }

//   return 0;
// }