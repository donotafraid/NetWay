#include "MainWindows/MainWindow_Rebuild.h"

//  S7DataRepository---------------------------------------------------------
Result<QString, RichError>
S7DataRepository::create_device_folder(const QString &ipAddress) {
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

Result<bool, RichError> S7DataRepository::CheckIpAddressFileName(const QString& fileName)
{
    // Regular expression for IP address pattern
    QRegularExpression ipRegex("^\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}$");
    return Result<bool,RichError>(ipRegex.match(fileName).hasMatch());
}

//  S7_DeviceManager-------------------------------------------------------


//  S7_MainWindows_UI------------------------------------------------------- 
void S7_MainWindows_UI::onTreeItemClicked(QTreeWidgetItem *item, int column) {
  if (!item)
    return;
  auto result = this->validExistenceParent(item);
  // 检查是否为数据块项（通过父项判断,是）
  if (result.is_fail()) {
      std::cout<<result.unwrap_err().what()<<std::endl;
      return;
  } else {
    if(result.unwrap_returnLeftValue() == ItemType::DEVICE)
    {
      return;
    } else {
      QWidget *tableView = m_manager->handleSwithView(
          m_lastSelectItem.m_lastest_device_TreeWidget_item->text(0),
          m_lastSelectItem.m_lastest_dataBlock_TreeWidget_item->text(0));

      if (tableView) {
        // 直接添加到 TabWidget
        QString tabTitle = QString("%1@%2").arg(
            m_lastSelectItem.m_lastest_device_TreeWidget_item->text(0),
            m_lastSelectItem.m_lastest_dataBlock_TreeWidget_item->text(0));
        int tabIndex = blankInformationTableView->addTab(tableView, tabTitle);
        m_tabIndexMap[tabTitle] = tabIndex;

        // 切换到新添加的 Tab
        blankInformationTableView->setCurrentIndex(tabIndex);
      }
    }
  }
}

void S7_MainWindows_UI::onTabCloseRequested(int index) {
  // 获取 Tab 对应的 ip 和 blockName
  QString tabText = blankInformationTableView->tabText(index);
  // 假设格式是 "DB1@192.168.1.100" 或 "DB1@192.168.1.100 *"
  QString cleanText = tabText.split(" *").first();
  QStringList parts = cleanText.split('@');
  if (parts.size() == 2) {
    QString blockName = parts[0];
    QString ip = parts[1];
    onCloseDataBlockClicked(ip, blockName);
  }
}

void S7_MainWindows_UI::onTabChanged(int index) {
  if (index < 0)
    return;

  // 更新状态栏显示当前 Tab 信息
  QString tabText = blankInformationTableView->tabText(index);
  statusBar()->showMessage(QString("Current: %1").arg(tabText));
}

void S7_MainWindows_UI::onCloseDataBlockClicked(const QString &ip,
                                                const QString &blockName) {
  // 1. 构建完整的 tab 标题（注意格式要与添加时一致）
  QString tabTitle =
      QString("%1@%2").arg(blockName, ip); // 或 "ip@blockName"，保持一致

  // 2. 检查是否已打开
  if (!m_tabIndexMap.contains(tabTitle)) {
    return;
  }

  // 3. 从 Manager 删除（如果需要）
  // if (m_manager->removeDataBlock(ip, blockName))

  {
    // 4. 关闭 Tab
    int tabIndex = m_tabIndexMap[tabTitle];
    blankInformationTableView->removeTab(tabIndex);

    // 5. 清理索引映射
    m_tabIndexMap.remove(tabTitle);

    // 6. 重新映射索引（因为删除后索引会变化）
    rebuildTabIndexMap();
  }
}

void S7_MainWindows_UI::rebuildTabIndexMap() {
  m_tabIndexMap.clear();
  for (int i = 0; i < blankInformationTableView->count(); ++i) {
    QString tabText = blankInformationTableView->tabText(i);
    QString cleanText = tabText.split(" *").first(); // 如果有星号标记，去掉

    // 根据你添加时的格式来解析
    // 假设格式是 "blockName@ip"
    QStringList parts = cleanText.split('@');
    if (parts.size() == 2) {
      QString blockName = parts[0];
      QString ip = parts[1];
      m_tabIndexMap[cleanText] = i; // 直接使用完整标题作为 key
    }
  }
}

void S7_MainWindows_UI::onCustomContextMenu(const QPoint &pos) {
  // initialize select device pointer
  initalize_last_parameter();
  QTreeWidgetItem *item = m_datablockname_tree->itemAt(pos);
  if (!item) {
    return showBlankAreaContextMenu(pos);
  }

  //  JUDGET IF THE SELECT ITEM HAVE PARENT 
  auto result = this->validExistenceParent(item);
  if (result.is_fail()) {
    //  MEANS SELECT ITEM IS INVALID 
    std::cout << result.unwrap_err().what() << std::endl;
    return;
  }

  void *deviceData =
      m_lastSelectItem.m_lastest_device_TreeWidget_item->data(0, Qt::UserRole)
          .value<void *>();
  m_lastSelectItem.m_info = static_cast<DeviceTableInfo *>(deviceData);
  update_lastest_folder_path(m_lastSelectItem.m_info->ip_Address);

  //  MEAN THE ITEM IS DATABLOCK
  if (result.unwrap_returnLeftValue() == ItemType::DATABLOCK) {
    showDataStructMenu(pos);
  } else {
    //  MEANS THE ITEM IS DEVICE 
    showDeviceContextMenu(pos);
  }

  return;
}

void S7_MainWindows_UI::initalize_last_parameter() {
  // m_lastSelectItem.m_lastest_plc_device_pointer = nullptr;
  m_lastSelectItem.m_lastest_device_folder_path = "";
  m_lastSelectItem.m_lastest_device_TreeWidget_item = nullptr;
  m_lastSelectItem.m_lastest_dataBlock_TreeWidget_item = nullptr;
}

void S7_MainWindows_UI::showBlankAreaContextMenu(const QPoint &pos) {
  if (m_treeeMenu.device_menu_var.isVisible()) {
    m_treeeMenu.device_menu_var.hide(); // 立即隐藏
  }

  // 使用 popup 而不是 exec，避免阻塞
  m_treeeMenu.device_menu_var.popup(QCursor::pos());
}

void S7_MainWindows_UI::showDataStructMenu(const QPoint &globalPos) {
  std::cout << "showDataStructMenu call \n" << std::endl;
  m_dataBlockMenu.dataBlockMenu.exec(
      m_datablockname_tree->viewport()->mapToGlobal(globalPos));
}

void S7_MainWindows_UI::initalize_DeviceMenu() {
  // 创建菜单项（只创建一次）
  m_treeeMenu.addDeviceAction = new QAction("Add PLC Device", this);
  m_treeeMenu.device_menu_var.addAction(m_treeeMenu.addDeviceAction);
  m_treeeMenu.device_menu_var.addSeparator();

  m_DeviceMenu.deleteDeviceAction =
      new QAction("Delete PLC Device", this);
  m_DeviceMenu.subDeviceMenu.addAction(
      m_DeviceMenu.deleteDeviceAction);
  m_DeviceMenu.subDeviceMenu.addSeparator();
  m_DeviceMenu.loadExternalDataConfigAction =
      new QAction("Add PLC External DataBlock", this);
  m_DeviceMenu.subDeviceMenu.addAction(
      m_DeviceMenu.loadExternalDataConfigAction);
  m_DeviceMenu.subDeviceMenu.addSeparator();
  m_DeviceMenu.loadInternalDataConfigAction =
      new QAction("Load PLC Internal DataBlock", this);
  m_DeviceMenu.subDeviceMenu.addAction(
      m_DeviceMenu.loadInternalDataConfigAction);
  m_DeviceMenu.subDeviceMenu.addSeparator();
  m_DeviceMenu.loadOPCUAInlineBrowseAction =
      new QAction("Load OPCUA Inline Browse", this);
  m_DeviceMenu.subDeviceMenu.addAction(
      m_DeviceMenu.loadOPCUAInlineBrowseAction);
  m_DeviceMenu.subDeviceMenu.addSeparator();

  m_dataBlockMenu.deleteDataBLockAction =
      new QAction("Delete PLC Existing DataBlock", this);
  m_dataBlockMenu.dataBlockMenu.addAction(
      m_dataBlockMenu.deleteDataBLockAction);
  m_dataBlockMenu.dataBlockMenu.addSeparator();

}

void S7_MainWindows_UI::initalize_Drop()
{
  drop_Area = new DropArea(this);
}

void S7_MainWindows_UI::initalize_scope(std::shared_ptr<Scope> &scope) {
  m_scope = scope.get();
  auto item = m_scope->getShared<S7_DeviceManager>();
  if (item) {
    m_manager = item.get();
  } else {
    std::cout << "there do not exist S7_DeviceManager object in scope \n";
  }

}

Result<ItemType, RichError>
S7_MainWindows_UI::validExistenceParent(QTreeWidgetItem *item) {
  QTreeWidgetItem *parent_item = item->parent();
  if (parent_item) {
    //  the item do  have parent , it means the item is dataBlock 
    QVariant itemData = parent_item->data(0, Qt::UserRole);
    this->update_lastest_itemPointer(parent_item, item);

    if (itemData.isValid()) {
      return Result<ItemType, RichError>(ItemType{1});
    } else {
      return Result<ItemType, RichError>(RichError("select item is dataBlock , but itemData is invalid"));
    }
  }

  //  the item do not have parent , it means the item is device
  this->update_lastest_itemPointer(item, nullptr);
  return Result<ItemType, RichError>(ItemType{0});
}

void S7_MainWindows_UI::showDeviceContextMenu(const QPoint& globalPos)
{
    // m_DeviceMenu.subDeviceMenu.exec(m_datablockname_tree->viewport()->mapToGlobal(globalPos));
    if (m_DeviceMenu.subDeviceMenu.isVisible()) {
      m_DeviceMenu.subDeviceMenu.hide(); // 立即隐藏
    }

    // 使用 popup 而不是 exec，避免阻塞
    m_DeviceMenu.subDeviceMenu.popup(QCursor::pos());
}

Result<QString, RichError>
S7_MainWindows_UI::create_device_folder(const QString &ipAddress) {
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

void S7_MainWindows_UI:: update_lastest_itemPointer(QTreeWidgetItem *parent_item,
                                                   QTreeWidgetItem *sub_item) {
  m_lastSelectItem.m_lastest_device_TreeWidget_item = parent_item;
  m_lastSelectItem.m_lastest_dataBlock_TreeWidget_item = sub_item;
  std::cout << "update_lastest_itemPointer done\n";
}

void S7_MainWindows_UI::update_lastest_folder_path(const std::string &ipAddress) {
  QString ip_Address(QString::fromStdString(ipAddress));
  auto result = create_device_folder(ip_Address);
  if (result.is_success()) {
    m_lastSelectItem.m_lastest_device_folder_path =
        result.unwrap_returnLeftValue();
  } else {
    m_lastSelectItem.m_lastest_device_folder_path = "";
    return;
  }
}

void S7_MainWindows_UI::onTreeItemExpanded(QTreeWidgetItem *item) {
  void *device_data = item->data(0, Qt::UserRole).value<void *>();
  m_lastSelectItem.m_info =
      static_cast<DeviceTableInfo *>(device_data);
  this->update_lastest_itemPointer(item, nullptr);
  update_lastest_folder_path(m_lastSelectItem.m_info->ip_Address);

  std::cout << "device tree have refreshed\n";
}

void S7_MainWindows_UI::onTreeItemCollapsed(QTreeWidgetItem *item) {
  std::cout << "QTreeWidge Collapsed \n";
}

void S7_MainWindows_UI::onFilterTree(const QString& ip_Address)
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

Result<bool, RichError> S7_MainWindows_UI::CheckIpAddressFileName(const QString& fileName)
{
    // Regular expression for IP address pattern
    QRegularExpression ipRegex("^\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}$");
    return Result<bool,RichError>(ipRegex.match(fileName).hasMatch());
}

S7_MainWindows_UI::S7_MainWindows_UI(QWidget *parent):QMainWindow(parent)
{

}

S7_MainWindows_UI::~S7_MainWindows_UI() {
  for (auto &element : m_devieVector) {
    if (element) {
      delete element;
      std::cout << "delete element successfully" << std::endl;
    }
  }
  
  if(m_systemSetting)
  {
    delete m_systemSetting;
  }
}

void S7_MainWindows_UI::initialize() {
  // Create central widget and set layout
  QWidget *centralWidget = new QWidget(this);
  setCentralWidget(centralWidget);

  m_main_layout_V = new QVBoxLayout(centralWidget);

  splitter_left_v = new QSplitter(Qt::Vertical);
  blankInformationTableView = new QTabWidget();
  splitter_main_H = new QSplitter(Qt::Horizontal);

  //  create left file tree area
  m_datablockname_tree = new QTreeWidget();
  m_search_bar = new QLineEdit();
  m_datablockname_tree->setContextMenuPolicy(Qt::CustomContextMenu);
  blankInformationTableView->setTabsClosable(true); // 设置显示关闭按钮

  m_search_bar->setPlaceholderText("Search device ip address ...");
  m_search_bar->setClearButtonEnabled(true);
  splitter_left_v->addWidget(m_search_bar);
  splitter_left_v->addWidget(m_datablockname_tree);

  //  ADD DOCKWIDGET INTO MAINWINDOWS
  // m_device_monitor = new DeviceMonitorWidget(this);
  // m_device_monitor->build_original_connet(&m_plc_device_pointer_vector);
  // initialize_Add_Device_Page();

  //  SYSTEM SETTING
  m_systemSetting = new SystemSetting();

  //  BUILD TOOLBAR CONNECTION
  initialize_toolBar();
  initialize_searchBar();

  //  BUILD FUNCTION PAGE
  initialize_Add_Device_Page();
  initialize_Delete_Device_Page();
  initialize_Delete_DataBlock_Page();

  //  
  initalize_DeviceMenu();
  //  
  initalize_Drop();

  // GrafanaClient *m_grafanaClient = new GrafanaClient();
  // m_monitor_dashBoard = new MonitoringDashboard(this);
  // m_monitor_dashBoard->setGrafanaClient(m_grafanaClient);

  //  延迟设置精确比例（双重保证）
  // QTimer::singleShot(50, this, [this]() {
  //   if (this->splitter_right_v->height() > 100) {
  //     QList<int> sizes = splitter_right_v->sizes();
  //     int total = sizes[0] + sizes[1];
  //     if (total > 0) {
  //       // 如果当前比例偏差较大，强制修正
  //       float currentRatio = (float)sizes[0] / total;
  //       if (abs(currentRatio - 0.7) > 0.1) { // 如果偏差超过10%
  //         splitter_right_v->setSizes({total * 80 / 100, total * 20 / 100});
  //         splitter_left_v->setSizes({total * 20 / 100, total * 80 / 100});
  //       }
  //     }
  //   }
  // });

  splitter_main_H->addWidget(splitter_left_v);
  //  create right side tab area
  splitter_main_H->addWidget(blankInformationTableView);
  m_main_layout_V->addWidget(splitter_main_H);

  this->resize(1200, 800);
  buildConnection();
}

void S7_MainWindows_UI::initialize_toolBar()
{
    m_toolBar.monitor_ToolBar = new QToolBar(tr("device bar"),this);
    m_toolBar.monitor_ToolBar->setMovable(false);
    addToolBar(Qt::TopToolBarArea, m_toolBar.monitor_ToolBar);

    m_toolBar.toggleMonitorAction = new QAction(tr("device monitor"),this);
    m_toolBar.toggleMonitorAction->setCheckable(true);
    m_toolBar.toggleMonitorAction->setChecked(false);

    m_toolBar.toggleSystemSetting = new QAction (tr("system setting"),this);
    m_toolBar.toggleSystemSetting->setCheckable(true);
    m_toolBar.toggleSystemSetting->setChecked(false);

    m_toolBar.test_send_prometheus_message = new QAction (tr("send_prometheus_message"),this);
    m_toolBar.test_send_prometheus_message->setCheckable(true);
    m_toolBar.test_send_prometheus_message->setChecked(false);

    m_toolBar.test_display_grafana_web_view =
        new QAction(tr("display_grafana_web_view"), this);
    m_toolBar.test_display_grafana_web_view->setCheckable(true);
    m_toolBar.test_display_grafana_web_view->setChecked(false);

    m_toolBar.monitor_ToolBar->addAction(m_toolBar.toggleMonitorAction);
    m_toolBar.monitor_ToolBar->addAction(m_toolBar.toggleSystemSetting);
    m_toolBar.monitor_ToolBar->addAction(m_toolBar.test_send_prometheus_message);
    m_toolBar.monitor_ToolBar->addAction(m_toolBar.test_display_grafana_web_view);

    //  ADD SEPARATOR FRAME FOR EVERY ACTION
    m_toolBar.monitor_ToolBar->addSeparator();

}

void S7_MainWindows_UI::initialize_searchBar()
{
    m_completerModel = new QStringListModel(this);
    m_completer = new QCompleter(m_completerModel,this);

    m_completer->setCaseSensitivity(Qt::CaseSensitive);
    m_completer->setFilterMode(Qt::MatchContains);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);

    //  ENABLE THE DROPDOWN FEATURE
    m_search_bar->setCompleter(m_completer);
}

void S7_MainWindows_UI::initialize_Add_Device_Page() {
    m_addDevice_page = new QDialog(this);
    QVBoxLayout *m_addDevice_layout = new QVBoxLayout(m_addDevice_page);

    m_ip_Address_edit = new QLineEdit("192.168.0.2");
    m_nameSpace_edit = new QLineEdit("3");
    m_port_edit = new QLineEdit("4840");
    m_slot_edit = new QLineEdit("1");
    m_rack_edit = new QLineEdit("0");
    m_urlPrefix_edit = new QLineEdit("opc.tcp://");   
    m_organizesId_edit = new QLineEdit(); 

    m_communicate_type_box = new QComboBox();
    m_communicate_type_box->addItem("Offset Address");
    m_communicate_type_box->addItem("OPC_UA Address");

    QFormLayout *formLayout = new QFormLayout;
    formLayout->addRow("PLC nameSpace:", m_nameSpace_edit);
    formLayout->addRow("PLC Port:", m_port_edit);
    formLayout->addRow("PLC IP Address:", m_ip_Address_edit);
    formLayout->addRow("PLC Slot:", m_slot_edit);
    formLayout->addRow("PLC Rack:", m_rack_edit);
    formLayout->addRow("PLC urlPrefix:", m_urlPrefix_edit);
    formLayout->addRow("PLC organizeId:", m_organizesId_edit);

    formLayout->addRow("PLC Type:", m_communicate_type_box);
    m_addDevice_layout->addLayout(formLayout);

    okButton = new QPushButton("OK");
    cancelButton = new QPushButton("Cancel");

    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(okButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(cancelButton);
    m_addDevice_layout->addLayout(buttonLayout);
  }

  void S7_MainWindows_UI::initialize_Delete_Device_Page() {
    m_deleteDevice_page.deleteDevice_page = new QDialog(this);
    QVBoxLayout *m_addDevice_layout = new QVBoxLayout(m_deleteDevice_page.deleteDevice_page);

    m_deleteDevice_page.okButton = new QPushButton("OK");
    m_deleteDevice_page.cancelButton = new QPushButton("Cancel");

    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_deleteDevice_page.okButton);
    buttonLayout->addWidget(m_deleteDevice_page.cancelButton);
    m_addDevice_layout->addLayout(buttonLayout);
  }

  void S7_MainWindows_UI::initialize_Delete_DataBlock_Page() {
    m_deleteDataBlock_page.deleteDataBlock_page = new QDialog(this);
    QVBoxLayout *m_deleteDataBlock_layout = new QVBoxLayout(m_deleteDataBlock_page.deleteDataBlock_page) ;

    m_deleteDataBlock_page.okButton = new QPushButton("OK");
    m_deleteDataBlock_page.cancelButton = new QPushButton("Cancel");

    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(m_deleteDataBlock_page.okButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_deleteDataBlock_page.cancelButton);
    m_deleteDataBlock_layout->addLayout(buttonLayout);
  }

  void S7_MainWindows_UI::buildConnection() {
    connect(m_datablockname_tree, &QTreeWidget::itemClicked, this,
            &S7_MainWindows_UI::onTreeItemClicked);
    connect(m_datablockname_tree, &QTreeWidget::customContextMenuRequested,
            this, &S7_MainWindows_UI::onCustomContextMenu);
    connect(m_datablockname_tree, &QTreeWidget::itemExpanded, this,
            &S7_MainWindows_UI::onTreeItemExpanded);
    connect(m_datablockname_tree, &QTreeWidget::itemCollapsed, this,
            &S7_MainWindows_UI::onTreeItemCollapsed);

    connect(m_search_bar, &QLineEdit::textChanged, this,
            &S7_MainWindows_UI::onFilterTree);

    connect(m_toolBar.toggleSystemSetting, &QAction::toggled,
            [this](bool checked) {
              if (m_systemSetting) {
                m_systemSetting->setVisible(checked);
                if (checked) {
                  m_systemSetting->display_current_setting();
                }
              }
            });
    connect(m_toolBar.test_send_prometheus_message, &QAction::toggled,
            [this](bool checked) {
              if (m_manager != nullptr && checked) {
                // m_metrics->simulateConcurrentRequests(20);
                m_manager->handleMetricSendRequest(20);
              }
            });
    // connect(m_toolBar.test_display_grafana_web_view, &QAction::toggled,
    //         [this](bool checked) {
    //           if (checked) {
    //             if (m_prometheus_view == nullptr) {
    //               this->m_LineChart = new LineChart();
    //               GrafanaClient *m_grafanaClient = new GrafanaClient();
    //               ObjectRouter::instance().registerObject(
    //                   "config://dashboard/server_01", m_grafanaClient);
    //               DataProcessor *dataProcessor = new DataProcessor();
    //               ChartController *item = new ChartController();
    //               // m_prometheus_view =
    //               // this->m_monitor_dashBoard->createRealtimePage();
    //               m_prometheus_view = this->m_LineChart->createChartView(
    //                   "request rate", "request_per_second", Qt::blue);
    //               item->m_connectionManager->safeConnect(
    //                   m_grafanaClient, &GrafanaClient::JsonData_ready,
    //                   dataProcessor, &DataProcessor::processJsonData);
    //               item->m_connectionManager->safeConnect(
    //                   dataProcessor, &DataProcessor::dataParsed,
    //                   this->m_LineChart->return_Model(),
    //                   &MyModel::addChartPointData);
    //             }
    //             m_prometheus_view->show();
    //           }
    //         });

    connect(m_treeeMenu.addDeviceAction, &QAction::triggered, this,
            &S7_MainWindows_UI::onAddDevice);

    connect(m_DeviceMenu.deleteDeviceAction, &QAction::triggered, this,
            &S7_MainWindows_UI::onDeleteDevice);
    connect(m_DeviceMenu.loadExternalDataConfigAction, &QAction::triggered,
            this, &S7_MainWindows_UI::onLoadExternalDataBlock);
    connect(m_DeviceMenu.loadInternalDataConfigAction, &QAction::triggered,
            this, &S7_MainWindows_UI::onLoadInternalDataBlock);
    connect(m_DeviceMenu.loadOPCUAInlineBrowseAction, &QAction::triggered, this,
            [this] {
              auto identify =
                  m_lastSelectItem.m_lastest_device_TreeWidget_item->text(0);
              auto result = m_manager->handleParseFile(identify, identify);
              if(!result)
              {
                spdlog::info("handleParse is fail");
              } else {
                QTreeWidgetItem *dataBlockFolder = new QTreeWidgetItem(
                    this->m_lastSelectItem.m_lastest_device_TreeWidget_item);
                DeviceTableInfo m_tableInfo;

                m_tableInfo.connectWay = m_lastSelectItem.m_info->connectWay;
                m_tableInfo.dataBlockName = "2";
                m_tableInfo.ip_Address = m_lastSelectItem.m_info->ip_Address;
                m_deviceTableVector.push_back(std::move(m_tableInfo));

                dataBlockFolder->setText(0, identify);
              }
            });
    connect(m_deleteDevice_page.okButton, &QPushButton::clicked, m_deleteDevice_page.deleteDevice_page,
            &QDialog::accept);
    connect(m_deleteDevice_page.cancelButton, &QPushButton::clicked, m_deleteDevice_page.deleteDevice_page,
            &QDialog::reject);

    connect(m_dataBlockMenu.deleteDataBLockAction, &QAction::triggered, this,
            &S7_MainWindows_UI::onDeleteDataBlock);
    connect(m_deleteDataBlock_page.okButton, &QPushButton::clicked,
            m_deleteDataBlock_page.deleteDataBlock_page, &QDialog::accept);
    connect(m_deleteDataBlock_page.cancelButton, &QPushButton::clicked,
            m_deleteDataBlock_page.deleteDataBlock_page, &QDialog::reject);

    connect(okButton, &QPushButton::clicked, m_addDevice_page,
            &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, m_addDevice_page,
            &QDialog::reject);

    connect(
        drop_Area, &DropArea::requestFile, this,
        [this](QStringList &fileNameList) {
          auto identify =
              m_lastSelectItem.m_lastest_device_TreeWidget_item->text(0);
          for (auto &filePath : fileNameList) {
            QFileInfo fileInfo(filePath);
            bool result =
                checkRepeatDeviceTable(identify, filePath);
            if (result) {
              std::cout << "there is repeat dataBlock table" << std::endl;
              continue;
            } else {
              result = m_manager->handleParseFile(filePath, identify);
              if (result) {
                //  build new treeWidget item
                QTreeWidgetItem *dataBlockFolder = new QTreeWidgetItem(
                    this->m_lastSelectItem.m_lastest_device_TreeWidget_item);
                DeviceTableInfo m_tableInfo;

                m_tableInfo.connectWay = m_lastSelectItem.m_info->connectWay;
                m_tableInfo.dataBlockName = fileInfo.fileName().toStdString();
                m_tableInfo.ip_Address = m_lastSelectItem.m_info->ip_Address;
                m_deviceTableVector.push_back(std::move(m_tableInfo));

                dataBlockFolder->setText(0, filePath);
              }
            }
          }
        });

    // 连接 Tab 关闭信号
    connect(blankInformationTableView, &QTabWidget::tabCloseRequested, this,
            &S7_MainWindows_UI::onTabCloseRequested);
    // 连接 Tab 切换信号
    connect(blankInformationTableView, &QTabWidget::currentChanged, this,
            &S7_MainWindows_UI::onTabChanged);
  }

bool S7_MainWindows_UI::checkRepeatDeviceTable(const QString &identify,const QString &dataBlockName)
{
  for(auto &item : m_deviceTableVector)
  {
    if(item.ip_Address == identify.toStdString() && item.dataBlockName == dataBlockName.toStdString())
    {
      return true;
    }
  }
  return false;
}

void S7_MainWindows_UI::onAddDevice() {
  if (m_addDevice_page->exec() != QDialog::Accepted) {
    return;
  }

  QString ipAddress(m_ip_Address_edit->text());
  QString port(m_port_edit->text());
  QString nameSpace(m_nameSpace_edit->text());
  QString slot(m_slot_edit->text());
  QString rack(m_rack_edit->text());
  auto string_ipAddress = ipAddress.toStdString();
  auto int_port = port.toInt();
  auto int_nameSpace = nameSpace.toInt();
  auto int_slot = slot.toInt();
  auto int_rack = rack.toInt();
  auto urlPrefix = m_urlPrefix_edit->text().toStdString();
  auto organizesId = m_organizesId_edit->text();

  DeviceTableInfo *item = new DeviceTableInfo();
  QString identifier{QString::fromStdString(urlPrefix) + ipAddress + "-" +
                     m_communicate_type_box->currentText()};
  //  INITALIZE DEVICE_POINTER
  if (m_communicate_type_box->currentText() == "OPC_UA Address" ) {
    if(organizesId=="")
    {
      m_manager->handleExternalOPCUAConnectRequest(ipAddress,
                                                 int_nameSpace, int_port,identifier);
      item->ip_Address = (string_ipAddress);
      item->connectWay = "OPC_UA";
    }
    else
    {
      m_manager->handleExternalOPCUAInlineBrowsetConnectRequest(
          ipAddress, int_nameSpace, int_port, urlPrefix, organizesId.toInt(),identifier);
    }
    item->ip_Address = (string_ipAddress);
    item->connectWay = "OPC_UA";

  } else if (m_communicate_type_box->currentText() == "Offset Address") {
    m_manager->handleExternalS7ConnectRequest(ipAddress, int_rack, int_slot,identifier);
    item->ip_Address = (string_ipAddress);
    item->connectWay = "S7_Offset";
  }

  //  INTIALIZE QTREEWIDGET ITEM
  QTreeWidgetItem *device_item = new QTreeWidgetItem();

  device_item->setText(0, identifier);
  device_item->setData(0, Qt::UserRole,
                       QVariant::fromValue(static_cast<void *>(item)));
  this->update_lastest_itemPointer(device_item, nullptr);

  //  TRT CONNECT PLC
  int i = 0;
  while (i <= 3) {
    if (!m_systemSetting->connect_retry(i)) {
      std::cerr << "retry connect times exceeded limitation" << std::endl;
      std::cout << "device connect fail" << std::endl;
      this->update_lastest_itemPointer(nullptr, nullptr);
      return;
    };
    bool result = m_manager->handleConnectRequest(ipAddress, item->connectWay);
    ;
    if (!result) {
      i = i + 1;
      this->update_parent_item_color(false);
      continue;
    } else {
      this->update_parent_item_color(true);
    }
    std::cout << "device connect successfully" << std::endl;
    break;
  }

  //  ADD QTREEWIDGET_ITEM INTO QTREEWIDGET
  m_datablockname_tree->addTopLevelItem(device_item);
  //  ADD PLC_DEVICDE POINTER INTO DEVICE_VECTOR
  m_devieVector.push_back(item);
  //  UPDATE DEVICE_MONITOR
  // m_device_monitor->update_widget();

  auto it = this->create_device_folder(ipAddress);
  if (it.is_fail()) {
    std::cout << it.unwrap_err().what() << std::endl;
  } else {
    std::cout << "create PLC Device successfully" << std::endl;
  }
}

void S7_MainWindows_UI::onDeleteDevice() {
  if (m_deleteDevice_page.deleteDevice_page->exec() != QDialog::Accepted) {
    return;
  }

  std::string text{m_lastSelectItem.m_info->ip_Address + "-" +
                   m_lastSelectItem.m_info->connectWay};
  for (int i = 0; i < m_datablockname_tree->topLevelItemCount(); ++i) {
    QTreeWidgetItem *item = m_datablockname_tree->topLevelItem(i);
   
    if (item->text(0) == QString::fromStdString(text)) {
      // takeTopLevelItem 只移除不删除，需要手动 delete
      QTreeWidgetItem *taken = m_datablockname_tree->takeTopLevelItem(i);
      delete taken;
      break;
    }
  }

  for (auto it = m_devieVector.begin(); it != m_devieVector.end();) {
    if (*it == m_lastSelectItem.m_info) {
      it = m_devieVector.erase(it); // erase 返回下一个有效的迭代器
      delete m_lastSelectItem.m_info;
      m_lastSelectItem.m_info = nullptr;
    } else {
      ++it;
    }
  }
  
}

void S7_MainWindows_UI::onLoadExternalDataBlock() {
  std::cout << "onLoadExternalDataBlock call \n" << std::endl;
  drop_Area->exec();
}

void S7_MainWindows_UI::onDeleteDataBlock() {
  if (m_deleteDataBlock_page.deleteDataBlock_page->exec() !=
      QDialog::Accepted) {
    return;
  }

  QTreeWidgetItem *parent = m_lastSelectItem.m_lastest_device_TreeWidget_item;
  if (parent) {
    int index = parent->indexOfChild(m_lastSelectItem.m_lastest_dataBlock_TreeWidget_item);
    if (index >= 0) {
      delete parent->takeChild(index);
      return;
    }
  }

  QString tabTitle = QString("%1@%2").arg(
      m_lastSelectItem.m_lastest_device_TreeWidget_item->text(0),
      m_lastSelectItem.m_lastest_dataBlock_TreeWidget_item->text(0));
  update_removeTab(tabTitle);

  return;
}

void S7_MainWindows_UI::onLoadInternalDataBlock() {}

void S7_MainWindows_UI::update_parent_item_color(bool status)
{
    if(status)
    {
        //  CONNECT SUCCESSFULLY -> GREEN
        m_lastSelectItem.m_lastest_device_TreeWidget_item->setForeground(0, QBrush(QColor("#AED311")));
    }
    else
    {
        //  CONNECT SUCCESSFULLY -> RED 
        m_lastSelectItem.m_lastest_device_TreeWidget_item->setForeground(0, QBrush(QColor("#E80A00")));
    }
}

void S7_MainWindows_UI::update_removeTab(const QString &tabTitle) {
  if (!blankInformationTableView)
    return;

  if (!m_tabIndexMap.contains(tabTitle))
    return;

  int oldIndex = m_tabIndexMap[tabTitle];

  // 验证索引有效性
  if (oldIndex < 0 || oldIndex >= blankInformationTableView->count())
    return;

  // 删除映射中的旧项
  blankInformationTableView->removeTab(oldIndex);
  m_tabIndexMap.remove(tabTitle);

  // 重组：更新所有索引大于 oldIndex 的映射项
  for (auto it = m_tabIndexMap.begin(); it != m_tabIndexMap.end(); ++it) {
    if (it.value() > oldIndex) {
      it.value()--; // 索引前移
    }
  }
}
//  SystemSetting-------------------------------------------------------------------
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

//  S7_DeviceManager----------------------------------------------------------------------
S7_DeviceManager::S7_DeviceManager(QObject *parent) : QObject(nullptr) {
    initialize();
}

// 初始化函数
void S7_DeviceManager::initialize() {
    // 初始化相关代码
}

// 作用域初始化函数
void S7_DeviceManager::initalize_scope(std::shared_ptr<Scope> scope) {
    m_scope = scope.get();
    
    auto result = m_scope->getShared<ServiceMetrics>();
    if (result) {
        m_metrics = result.get();
    } else {
        std::cout << "there do not exist ServiceMetric object \n";
    }
    
    auto tmp_result = m_scope->getShared<OPCUADataBlockManager>();
    if (tmp_result) {
      m_OPCUAdataBlockManager = tmp_result.get();
    } else {
      std::cout << "there do not exist OPCUADataBlockManager object \n";
    }

    auto find_windowResult = m_scope->getShared<S7_MainWindows_UI>();
    if (find_windowResult) {
        m_UI = find_windowResult.get();
    } else {
        std::cout << "there do not exist S7_MainWindows_UI object \n";
    }
}

// 业务方法实现
void S7_DeviceManager::handleMetricSendRequest(int times) {
    std::cout << "handleMetricSendRequest call !\n";
}

void S7_DeviceManager::handleExternalOPCUAConnectRequest(const QString &ip_Address, 
                                                       int nameSpace,
                                                       int port,const QString &identifier) {
    m_OPCUAdataBlockManager->buildOPCUAConnect(ip_Address, nameSpace, port,identifier);
}

void S7_DeviceManager::handleExternalOPCUAInlineBrowsetConnectRequest(const QString &ip_Address, 
                                                       int nameSpace,
                                                       int port,const std::string &urlPrefix,const int &objectId,const QString &identifier) {
  m_OPCUAdataBlockManager->buildOPCUAInlineBrowseConnect(ip_Address, nameSpace, port,
                                                  urlPrefix, objectId,identifier);
}

void S7_DeviceManager::handleExternalS7ConnectRequest(const QString &ip_Address, 
                                                    int rack,
                                                    int slot,const QString &identifier) {
  m_OPCUAdataBlockManager->buildS7Connect(ip_Address, rack, slot, identifier);
}

bool S7_DeviceManager::handleConnectRequest(const QString &ip_Address,const std::string &connectWay
                                           ) {
    return m_OPCUAdataBlockManager->checkConnectToDevice(ip_Address , connectWay );
}

bool S7_DeviceManager::handleParseFile(const QString &filePath,
                                       const QString &identify) {
  bool singleResult =
      m_OPCUAdataBlockManager->buildDataFromFile(identify, filePath);

  if (singleResult == false) {
    std::cout << "deal with parse file : " << filePath.toStdString()
              << " is fail \n";
  }
  return singleResult;
}


QWidget *S7_DeviceManager::handleSwithView(const QString &identifier,
                                           const QString &filePath) {
  QWidget *ptr = m_OPCUAdataBlockManager->getView(identifier, filePath);
  if (!ptr) {
    std::cout << identifier.data() << " do not find corresponding view "
              << std::endl;
    return nullptr;
  } else {
    return ptr;
  }
}