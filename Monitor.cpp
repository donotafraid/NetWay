#include "MainWindows/Monitor.h"

DeviceInformationPanel::DeviceInformationPanel(QWidget*parent) 
{
    this->m_laytout_V = new QVBoxLayout(this);
    this->m_inline = new QLabel(); 
    this->m_error = new QLabel();
    this->m_warning = new QLabel();
    this->m_detailDisplay = new QPushButton();
    this->m_scrollArea = new QScrollArea();
    this->m_contentWidget = new QWidget();
    this->m_contentLayout_V = new QVBoxLayout(m_contentWidget);
    this->m_TextEdit = new QTextEdit();

    this->m_inline->setStyleSheet(" color:#2ecc71;font-weight: bold; font-size: 12px;");
    this->m_inline->setText(QString("inline_device : %1").arg(inline_device_total));
    this->m_error->setText(QString("error_device : %1").arg(error_device_total));
    this->m_error->setStyleSheet(" color:#e74c3c;font-weight: bold; font-size: 12px;");
    this->m_warning->setText(QString("warning_device : %1").arg(warning_device_total));
    this->m_warning->setStyleSheet(" color:#f39c12;font-weight: bold; font-size: 12px;");
    this->m_detailDisplay->setText(QString("detail information"));
    this->m_detailDisplay->setStyleSheet(" color:#000000;font-weight: bold;font-size: 20px;");
    this->m_detailDisplay->setCheckable(true);
    this->m_TextEdit->setReadOnly(true);
    this->m_TextEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    this->m_TextEdit->setFixedSize(600, 400); // 宽600px，高400px
    
    this->m_contentLayout_V->addWidget(m_inline);
    this->m_contentLayout_V->addWidget(m_error);
    this->m_contentLayout_V->addWidget(m_warning);
    this->m_scrollArea->setWidget(m_contentWidget);
    this->m_scrollArea->setWidgetResizable(true);

    this->m_laytout_V->addWidget(m_scrollArea);
    this->m_laytout_V->addWidget(m_detailDisplay);
    connect(m_detailDisplay, &QPushButton::clicked, this, &DeviceInformationPanel::update_detailErrorAndWarning);

}

DeviceInformationPanel::~DeviceInformationPanel()
{
    delete m_TextEdit;
}

Result<bool,RichError> DeviceInformationPanel::update_panel()
{
    inline_device_total = 0 ;
    error_device_total = 0 ;
    for (auto *item : *(m_plc_device_pointer))
    {
        tmp_object = item->connect();    
        if(tmp_object.is_success())
        {
            inline_device_total += 1;
        }
        else
        {
            error_device_total += 1;
        }
    }
    this->m_inline->setText(QString("inline_device : %1").arg(inline_device_total));
    this->m_error->setText(QString("error_device : %1").arg(error_device_total));
    return Result<bool,RichError>(true);
}

Result<bool,RichError> DeviceInformationPanel::update_pointer_device_vector(std::vector<PLC_Device*> *pointer)
{
    if(pointer)
    {
        m_plc_device_pointer = pointer ;
        return Result<bool, RichError> (true);
    }
    else
    {
        return Result<bool, RichError> (RichError("update_pointer_device_vector : pointer is nullptr"));
    }
}

void DeviceInformationPanel::update_detailErrorAndWarning(bool status)
{
    if(!status)
    {
        m_TextEdit->hide();
        return;
    }
    m_TextEdit->clear();
    for(auto *it : *(m_plc_device_pointer))
    {
        if(it->connect().is_fail())
        {
            m_TextEdit->append(QString::fromStdString(it->return_input_ipAddress().unwrap_returnRightValue() + " : connect fail"));
        }
    }
    m_TextEdit->show();

    QTimer::singleShot(50, this, [this] {
      if (!m_TextEdit->isVisible())
        return;

      //  CALCAULATE CENTER OF SCREEN AND PUT TEXTEDIT INTO IT
      QScreen *screen = QGuiApplication::primaryScreen();
      if (screen) {
        QRect screenGemometry = screen->geometry();
        int x = screenGemometry.center().x() - m_TextEdit->width() / 2;
        int y = screenGemometry.center().y() - m_TextEdit->height() / 2;
        std::cout << "screenGemometry.center().x() "
                  << screenGemometry.center().x() << std::endl;
        std::cout << "screenGemometry.center().y() "
                  << screenGemometry.center().y() << std::endl;
        std::cout << "new TextEdit x position " << x << std::endl;
        std::cout << "new TextEdit y position " << y << std::endl;
        m_TextEdit->setGeometry(x, y, m_TextEdit->width(),
                                m_TextEdit->height());
      }
    });
}

DeviceListPanel::DeviceListPanel(QWidget* parent)
{
    m_mainlayout_Grid = new QGridLayout(this);
    m_mainlayout_Grid->setHorizontalSpacing(20);
    m_mainlayout_Grid->setVerticalSpacing(10);
}

Result<bool,RichError> DeviceListPanel::build_connection_deviceLabel_and_GridLayout()
{
    int check_counter ;
    for(auto *item : *(m_plc_device_pointer))
    {
        check_counter = 0 ;
        //  CHECK THE ITEM WHTHER EXIST IN GRID_MAIN_LAYOUT
        for(int i = 0 ; i < m_mainlayout_Grid->count() ; ++i)
        {
            QLayoutItem *exist_item = m_mainlayout_Grid->itemAt(i); 
            if(exist_item && exist_item->widget())
            {
                QLabel *existing_label = qobject_cast<QLabel*>(exist_item->widget());
                if(existing_label && existing_label->text() == QString::fromStdString(item->return_input_ipAddress().unwrap_returnRightValue()))
                {
                    std::cout<<"input_ipAddress : "<<item->return_input_ipAddress().unwrap_returnRightValue()<<" exist in Grid_layout"<<std::endl;
                    break;
                }
            }
            check_counter += 1;
        }

        //  THE ITEM EXIST GRID_MAIN_LAYOUT
        //  CONDITION : ALL ITEM IS NOT MATCH AND MAIN_LAYOUT_GRID IS NOT EMPTY
        if (check_counter != (m_mainlayout_Grid->count()) && (m_mainlayout_Grid->count() != 0))
        {
            std::cout<<"check_counter = "<<check_counter<<" and m_mainLayout_Grid->count() = "<<m_mainlayout_Grid->count()<<std::endl;
            continue;
        }
        
        //  THE ITEM DO NOT GRID_MAIN_LAYTOUT
        QLabel *device_label = new QLabel(QString::fromStdString(item->return_input_ipAddress().unwrap_returnRightValue()));
        
        if(item->connect().is_success())
        {
            device_label->setStyleSheet("color:#2ecc71;font-weight: bold; font-size: 12px;");
        }
        else
        {
            device_label->setStyleSheet("color:#e74c3c;font-weight: bold; font-size: 12px;");
        }
        m_mainlayout_Grid->addWidget(device_label,current_row,current_line,Qt::AlignLeft);
        
        if(current_line == 3)
        {
            current_line = 0 ;
            current_row += 1;
        }
        else
        {
            current_line += 1 ;
        }
 }
    return Result<bool,RichError> (true);
}

Result<bool,RichError> DeviceListPanel::update_devicePanel( )
{
    for(auto *item:*(m_plc_device_pointer))
    {
        auto device_pos = this->m_device_pos_map[item];
        auto it = m_mainlayout_Grid->itemAtPosition(device_pos.first, device_pos.second);
        if(item->connect().is_success())
        {
            it->widget()->setStyleSheet("color:#2ecc71;font-weight: bold; font-size: 12px;");
        }
        else
        {
            it->widget()->setStyleSheet("color:#e74c3c;font-weight: bold; font-size: 12px;");
        }
    }
    return Result<bool,RichError> (true);
}

Result<bool,RichError> DeviceListPanel::update_pointer_device_vector(std::vector<PLC_Device*> *pointer)
{
    if(pointer)
    {
        m_plc_device_pointer = pointer ;
        return Result<bool, RichError> (true);
    }
    else
    {
        return Result<bool, RichError> (RichError("update_pointer_device_vector : pointer is nullptr"));
    }
}

DeviceMonitorWidget::DeviceMonitorWidget(QMainWindow *parent):QWidget(parent),m_main_windows(parent)
{
    //  INITIAZLIE WIDGET , LAYOUT
    m_device_dock = new QDockWidget();

    m_main_layout_V = new QVBoxLayout(this);
    m_deviceInformation = new DeviceInformationPanel();

    //  ADD_WIDGET FOR MAIN_LAYOUT
    m_main_layout_V->addWidget(m_deviceInformation);

    //  Set this widget as the content of the dock
    m_device_dock->setWidget(this);
    //  SET THE LAYOUT DIRECTLY 
    this->setLayout(m_main_layout_V);

    //  SET DOCK_WIDGET POSITION
    update_position_extern_left();

    //  TRACK MAIN WINDOWS MOVES TO KEEP RELATIVE POSITION
    if(parent)
    {
        parent->installEventFilter(this);
    }

    //  Defines WHERE the dock can be parked
    m_device_dock->setAllowedAreas(Qt::NoDockWidgetArea);
    //  Can drag and reposition the dock and Has a close button (X)
    m_device_dock->setFeatures(QDockWidget::DockWidgetMovable );
    m_device_dock->setVisible(false);
}

DeviceMonitorWidget::~DeviceMonitorWidget()
{
    if(m_device_dock)
    {
        delete m_device_dock;
        std::cout<<"m_device_dock delete !"<<std::endl;
    }
}


Result<bool,RichError> DeviceMonitorWidget::build_original_connet(std::vector<PLC_Device*> *pointer)
{
    auto it = this->update_device_ptr_vector(pointer);
    if (it.is_success()) {
    it = this->m_deviceInformation->update_panel();
    if (it.is_fail()) {
        return Result<bool, RichError>(it);
    }
  
    }
    return Result<bool, RichError>(it);
}

bool DeviceMonitorWidget::eventFilter(QObject *obj, QEvent *event)
{
    //  MONITOR OBJECT EVENT AND FILTER SPECIAL EVENT TO DO SPECIAL ACTIONS
    if(event->type() == QEvent::Move || event->type() == QEvent::Resize)
    {
        if(obj == m_main_windows)
        {
            this->update_position_extern_left();
        }
    }
    return QWidget::eventFilter(obj, event);
}


void DeviceMonitorWidget::update_position_extern_left()
{
    if(!m_main_windows)
    {
        std::cout<<"update_position_extern_feft detected nullptr"<<std::endl;
        return;
    }

    QRect mainFrame = m_main_windows->frameGeometry(); // Includes title bar
    QRect mainInner = m_main_windows->geometry();      // Client area only
    int titleBarHeight = (mainInner.top() - mainFrame.top())/2; // return distance between two coordinate 
    int dock_width = 300;
    m_device_dock->setFixedWidth(dock_width);

    m_device_dock->setGeometry(mainFrame.left() - dock_width + 20,
                               mainInner.top() - titleBarHeight, 
                               dock_width, mainInner.height()*0.25);
    

}

Result<bool,RichError> DeviceMonitorWidget::update_device_ptr_vector(std::vector<PLC_Device*> *pointer)
{
    auto it = this->m_deviceInformation->update_pointer_device_vector(pointer);
    if(it.is_fail())
    {
        return Result<bool,RichError> (it);
    }
   
    return Result<bool,RichError> (it);
}

Result<bool,RichError> DeviceMonitorWidget::update_widget()
{
    return Result<bool,RichError> (this->m_deviceInformation->update_panel());
}

QWidget *MonitoringDashboard::createRealtimePage() {
  QWidget *page = new QWidget();
  QVBoxLayout *mainLayout = new QVBoxLayout(page);
  // 1. 顶部状态栏
  QWidget *statusBar = createStatusBar();
  mainLayout->addWidget(statusBar);
  // 2. 主图表区域（网格布局）
  QScrollArea *m_scrollArea = new QScrollArea();
  m_scrollArea->setWidgetResizable(true); // 关键：允许内容小部件调整大小
  m_scrollArea->setHorizontalScrollBarPolicy(
      Qt::ScrollBarAsNeeded); // 根据需要显示水平滚动条
  m_scrollArea->setVerticalScrollBarPolicy(
      Qt::ScrollBarAsNeeded); // 根据需要显示垂直滚动条

  // 创建一个内容容器
  QWidget *scrollContent = new QWidget();
  QVBoxLayout *contentLayout = new QVBoxLayout(scrollContent);

  QSplitter *chartSplitter = new QSplitter(Qt::Vertical);  // 垂直分隔
  // 关键指标卡片
  chartSplitter->addWidget(createMetricCard("request rate", "request_total",
                                            Qt::blue, chartSplitter));

  //   chartSplitter->addWidget(createMetricCard("API V1 Query P95",
  //                                             buildP95Query("/api/v1/query"),
  //                                             Qt::green, chartSplitter));

  //   chartSplitter->addWidget(createMetricCard(
  //       "API V1 Query P95", buildP95Query("/metrics"), Qt::blue,
  //       chartSplitter));

  //   chartGrid->addWidget(
  //       createMetricCard("error rate", "rate(error_total[5m])", Qt::red), 0,
  //       1);
 
  //   chartGrid->addWidget(
  //       createMetricCard("active connection", "http_connections",
  //       Qt::yellow), 0, 3);

  // 主趋势图表
  QtCharts::QChartView *chartView = createInteractiveChart("request trend");
  chartSplitter->addWidget(chartView); 

  // 3. 底部控制面板
  QWidget *controlPanel = createControlPanel();

  contentLayout->addWidget(chartSplitter);
  m_scrollArea->setWidget(scrollContent);
  mainLayout->addWidget(controlPanel);
  mainLayout->addWidget(m_scrollArea);

  // 完成page控制权的转换
  return page;
}

QWidget* MonitoringDashboard::createStatusBar() {
    QWidget *bar = new QWidget;
    QHBoxLayout *layout = new QHBoxLayout(bar);
    
    // 刷新时间显示
    QLabel *lastUpdateTimeLabel = new QLabel("last time update: --:--:--");
    lastUpdateTimeLabel->setObjectName("refreshTimeLabel");  // 用于后续更新
    
    // 数据源状态
    QLabel *sourceLabel = new QLabel("data source : conneting...");
    sourceLabel->setObjectName("sourceStatusLabel");
    
    // 自动刷新开关
    autoRefresh = new QCheckBox("Auto Refresh");
    connect(autoRefresh, &QCheckBox::toggled, 
            this, &MonitoringDashboard::onAutoRefreshToggled);
    
    // 刷新按钮
    QPushButton *refreshBtn = new QPushButton("Refresh Immediately");
    connect(refreshBtn, &QPushButton::clicked,
            this, &MonitoringDashboard::onRefreshAllData);
    
    layout->addWidget(lastUpdateTimeLabel);
    layout->addStretch();
    layout->addWidget(sourceLabel);
    layout->addStretch();
    layout->addWidget(autoRefresh);
    layout->addWidget(refreshBtn);
    
    return bar;
}

// 指标卡片组件,定时发送Prometheus请求
QtCharts::QChartView *
MonitoringDashboard::createMetricCard(const QString &title,
                                      const QString &query, const QColor &color,
                                      QWidget *widgetItem) {
  // 使用QChartView作为基础Widget，不需要额外包装
  QtCharts::QChartView *chartView = new QtCharts::QChartView();
  chartView->setStyleSheet("QChartView {"
                           "  background: white;"
                           "  border-radius: 8px;"
                           "  padding: 0px;"
                           "}");

  // 设置布局管理
  QVBoxLayout *mainLayout = new QVBoxLayout(chartView);
  mainLayout->setContentsMargins(12, 12, 12, 12);
  mainLayout->setSpacing(8);
  // 创建图表
  QtCharts::QChart *chart = new QtCharts::QChart();
  chart->setBackgroundRoundness(0);
  chart->setBackgroundBrush(Qt::white);
  chart->setMargins(QMargins(0, 0, 0, 0));

  QtCharts::QLineSeries *series = new QtCharts::QLineSeries();
  series->setColor(color);
  series->setName("Value");

  chart->addSeries(series);
  // 坐标轴
  QtCharts::QDateTimeAxis *axisX = new QtCharts::QDateTimeAxis();
  axisX->setFormat("HH:mm");
  axisX->setTitleText("Time");
  chart->addAxis(axisX, Qt::AlignBottom);

  QtCharts::QValueAxis *axisY = new QtCharts::QValueAxis();
  axisY->setTitleText("Value");
  chart->addAxis(axisY, Qt::AlignLeft);

  series->attachAxis(axisX);
  series->attachAxis(axisY);

  // 隐藏图例
  chart->legend()->hide();

  // 设置图表到ChartView
  chartView->setChart(chart);
  chartView->setRenderHint(QPainter::Antialiasing);

  // 定时更新数据
  QTimer *timer = new QTimer(chart);
  connect(timer, &QTimer::timeout, [this, query]() {
    // UTC 时间 02：21 + 8 小时 = 北京时间 10：21。
    QDateTime now = QDateTime::currentDateTime();
    QDateTime from = now.addSecs(-5*60); // 5minutes前
    QDateTime to = now;                  // 当前时间

    // 确保使用 UTC 时间
    QDateTime utcTime = from.toUTC();
    // 格式化为 ISO 8601 并添加 'Z' 表示 UTC
    QString isoString = utcTime.toString(Qt::ISODate);
    // Qt::ISODate 可能不会添加 'Z'，需要手动添加
    if (!isoString.endsWith('Z')) {
      isoString += 'Z';
    }
    // 确保使用 UTC 时间
    QDateTime utcTime_to = to.toUTC();
    // 格式化为 ISO 8601 并添加 'Z' 表示 UTC
    QString isoString_to = utcTime_to.toString(Qt::ISODate);
    // Qt::ISODate 可能不会添加 'Z'，需要手动添加(实际上已添加)
    if (!isoString_to.endsWith('Z')) {
      isoString_to += 'Z';
    }

    auto result = ObjectRouter::instance().invoke(
        "config://dashboard/server_01", "queryPrometheusMetric",
        QVariantList() << "PC67C731E4884519C" << isoString << isoString_to
                       << query);
  });
  timer->start(15000); // 15秒更新一次

  ChartController  *item = new ChartController (this,chartView);
  m_map.insert(query,item);
  //    Chart Zoom Control 
  ZoomController *control = new ZoomController();
  control->setWidget(widgetItem,chartView);
  item->m_chartFeatureManager->addFeature(control);
  //    Data Processor 
  DataProcessor *dataProcessor = new DataProcessor();
  item->m_chartFeatureManager->addFeature(dataProcessor);
  //    UI Internal operation logic
  ChartRenderer *chartRenderer = new ChartRenderer();
  chartRenderer->addList(m_map);
  item->m_chartFeatureManager->addFeature(chartRenderer);
  //    Connect Logic 
  item->m_connectionManager->safeConnect(
      grafanaClient, &GrafanaClient::JsonData_ready, dataProcessor,
      &DataProcessor::processJsonData);
  item->m_connectionManager->safeConnect(
      dataProcessor, &DataProcessor::dataParsed, chartRenderer,
      &ChartRenderer::onDataParsed);
  //    Function Run
  item->m_chartFeatureManager->attachAllTo(chartView);

  return chartView;
}

QtCharts::QChartView* MonitoringDashboard::createInteractiveChart(const QString& title) {
    // 创建图表
    QtCharts::QChart *chart = new QtCharts::QChart();
    QtCharts::QChartView *chartView= new QtCharts::QChartView();
    chart->setTitle(title);
    
    // 创建系列
    QtCharts::QLineSeries *series = new QtCharts::QLineSeries();
    series->setName("request total");
    
    // 添加到图表
    chart->addSeries(series);
    
    // 创建坐标轴
    QtCharts::QDateTimeAxis *axisX = new QtCharts::QDateTimeAxis();
    axisX->setFormat("HH:mm:ss");
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);
    
    QtCharts::QValueAxis *axisY = new QtCharts::QValueAxis();
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);
    
    // 创建视图
    chartView = new QtCharts::QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);
    
    // 设置交互功能
    chartView->setRubberBand(QtCharts::QChartView::RectangleRubberBand);  // 矩形缩放
    chartView->setInteractive(true);  // 允许交互
    
    // 存储指针用于后续更新
    chartView->setObjectName("mainTrendChart");

    ChartController  *item = new ChartController (nullptr, chartView);
    //    Chart Zoom Control
    ZoomController *control = new ZoomController();
    item->m_chartFeatureManager->addFeature(control);
    //    Data Processor
    DataProcessor *dataProcessor = new DataProcessor();
    item->m_chartFeatureManager->addFeature(dataProcessor);
    //    UI Internal operation logic
    ChartRenderer *chartRenderer = new ChartRenderer();
    item->m_chartFeatureManager->addFeature(chartRenderer);
    //    Connect Logic
    // item->m_connectionManager->safeConnect(
    //     grafanaClient, &GrafanaClient::JsonData_ready, dataProcessor,
    //     &DataProcessor::processJsonData);
    item->m_connectionManager->safeConnect(
        dataProcessor, &DataProcessor::dataParsed, chartRenderer,
        &ChartRenderer::onDataParsed);
    //    Function Run
    item->m_chartFeatureManager->attachAllTo(chartView);

    m_list.append(item);
    return chartView;
}

QWidget* MonitoringDashboard::createControlPanel() {
    QWidget *panel = new QWidget;
    QHBoxLayout *layout = new QHBoxLayout(panel);
    
    // 时间范围选择
    timeRangeCombo = new QComboBox();
    timeRangeCombo->addItems({"5 minutes", "15 minutes", "1 hours", "6 hours", "24 hours"});
    connect(timeRangeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MonitoringDashboard::onCurrentTimeRangeChange);
    
    // 刷新间隔
    intervalSpin = new QSpinBox();
    intervalSpin->setRange(5, 300);
    intervalSpin->setValue(30);
    intervalSpin->setSuffix(" second");
    connect(intervalSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MonitoringDashboard::onUpdateIntervalChanged);
    
    // 图表类型选择
    chartTypeCombo = new QComboBox();
    chartTypeCombo->addItems({"line chart", "bar chart", "area chart"});
    
    // 添加/移除指标按钮
    QPushButton *addMetricBtn = new QPushButton("+ Add indicators");
    connect(addMetricBtn, &QPushButton::clicked,
            this, &MonitoringDashboard::clicked);
    
    layout->addWidget(new QLabel("time range:"));
    layout->addWidget(timeRangeCombo);
    layout->addSpacing(20);
    layout->addWidget(new QLabel("refresh interval:"));
    layout->addWidget(intervalSpin);
    layout->addSpacing(20);
    layout->addWidget(new QLabel("chart type:"));
    layout->addWidget(chartTypeCombo);
    layout->addStretch();
    layout->addWidget(addMetricBtn);
    
    return panel;
}

void MonitoringDashboard::onCurrentTimeRangeChange()
{
  qDebug() << "update time range : "<<timeRangeCombo->currentText();
}

void MonitoringDashboard::onUpdateIntervalChanged()
{
  qDebug() << "refresh update interval : "<<intervalSpin->value();
}

void MonitoringDashboard::clicked()
{
  qDebug() << "chart type selection : "<<chartTypeCombo->currentText();
}

void MonitoringDashboard::onAutoRefreshToggled()
{
  qDebug() << "automatic refresh switch : "<<autoRefresh->isCheckable();
}

void MonitoringDashboard::onRefreshAllData()
{
  qDebug() << "Refresh Immediately\n";
}

void MonitoringDashboard::onChartUpdated()
{
  qDebug() << "onChartUpdated call\n";
}

ChartData DataProcessor::convertJsonToChartData(const QJsonObject &jsonData,const QString &QueryName) {
    if(jsonData.isEmpty())
    {
      ChartData data{{QueryName, Qt::blue, false}};
      return data;
    }
    ChartData data;
    qDebug() << "createMetricCard begin";
    static QList<QColor> defaultColors = {QColor("#FF6384"), QColor("#36A2EB"),
                                          QColor("#FFCE56")};
    QJsonArray item = jsonData["result"].toArray();
    for (int i = 0; i < item.size(); ++i) {
      QJsonObject object = item[i].toObject();
      QJsonArray values = object["values"].toArray();
      QJsonObject metric = object["metric"].toObject();
      TimeSeries series;
      //    set default color 
      series.color = defaultColors[0];
      //    set series name
      series.name = QueryName;
      // 遍历结构体里面的“value”的数据
      for (int j = 0; j < values.size(); ++j) {
        DataPoint dataPoint;
        QJsonArray point = values[j].toArray();
        dataPoint.timestamp = static_cast<qint64>(point[0].toFloat()); 
        dataPoint.value = point[1].toString().toFloat();
        qDebug() << QString("    [%1] %2: %3 -series.name =%4")
                        .arg(j + 1)
                        .arg(static_cast<qint64>(point[0].toFloat()))
                        .arg(dataPoint.value)
                        .arg(series.name);
                        // .arg(dt.toString("HH:mm:ss"))
                        series.points.append(dataPoint);
    }
    data.append(series);
    }

    qDebug() << "createMetricCard end";
    return data;
}

  InfoBalloon::InfoBalloon(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::ToolTip)
{
    setAttribute(Qt::WA_TranslucentBackground);
    
    // 创建阴影效果
    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(10);
    shadow->setColor(QColor(0, 0, 0, 100));
    shadow->setOffset(0, 2);
    setGraphicsEffect(shadow);
    
    // 设置样式
    setStyleSheet(R"(
        QWidget {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #ffffff, stop:1 #f8f9fa);
            border: 1px solid #dee2e6;
            border-radius: 6px;
        }
        QLabel {
            background: transparent;
        }
    )");
    
    // 创建布局和标签
    titleLabel = new QLabel(this);
    titleLabel->setStyleSheet("font-weight: bold; color: #343a40; font-size: 12px;");
    
    infoLabel = new QLabel(this);
    infoLabel->setStyleSheet("color: #6c757d; font-size: 11px;");
    
    layout = new QVBoxLayout(this);
    layout->addWidget(titleLabel);
    layout->addWidget(infoLabel);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(4);
    
    adjustSize();
}

void InfoBalloon::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // 绘制圆角矩形背景
    painter.setBrush(palette().window());
    painter.setPen(QPen(QColor("#dee2e6"), 1));
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);
    
    
    painter.setBrush(palette().window());
    painter.setPen(Qt::NoPen);
}

void InfoBalloon::showAt(const QPoint &pos, const QString &title, const QString &info)
{
    try{
        titleLabel->setText(title);
        infoLabel->setText(info);
        adjustSize();
        
        // 设置位置（让三角形指向点击点）
        QPoint windowPos = pos - QPoint(width()/2, height() + 8);
        // 确保不超出屏幕
        QRect screenRect = QGuiApplication::primaryScreen()->availableGeometry();
        if (windowPos.x() < screenRect.left())
            windowPos.setX(screenRect.left());
        if (windowPos.x() + width() > screenRect.right())
            windowPos.setX(screenRect.right() - width());
        if (windowPos.y() < screenRect.top())
            windowPos.setY(pos.y() + 20); // 显示在下方
        
        // 淡入动画
        setWindowOpacity(0);
        move(windowPos);
        show();

        if(!m_animation)
        {
            m_animation = new QPropertyAnimation(this, "windowOpacity");
            m_animation->setDuration(200);
            m_animation->setStartValue(0);
            m_animation->setEndValue(1);
        }
        if (m_animation->state() == QAbstractAnimation::Running) {
          m_animation->stop();
        }
        setWindowOpacity(0.0);
        m_animation->start();
    }
    catch(const std::exception &e)
    {
        std::cout<<e.what()<<std::endl;
    }
    
    // 2秒后自动关闭
    QTimer::singleShot(2000, this, &InfoBalloon::hide);
}

AnimatedZoomChartView::AnimatedZoomChartView(QWidget *parent)
    : QChartView(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    
    // 创建动画
    m_animation = new QPropertyAnimation(this, "geometry");
    m_animation->setDuration(300); // 300毫秒动画
    m_animation->setEasingCurve(QEasingCurve::InOutQuad);
    connect(m_animation, &QPropertyAnimation::finished, 
            this, &AnimatedZoomChartView::onAnimationFinished);
}

void AnimatedZoomChartView::enterEvent(QEvent *event)
{
    QChartView::enterEvent(event);

       // 强制获取焦点
    QApplication::setActiveWindow(this);  // 激活窗口
    setFocus(Qt::MouseFocusReason);       // 设置焦点
    
    // 可选：高亮显示表示已获得焦点
    setStyleSheet("border: 2px solid blue;");
}

void AnimatedZoomChartView::leaveEvent(QEvent *event)
{
    QChartView::leaveEvent(event);

    // 恢复样式
    setStyleSheet("");
}

void AnimatedZoomChartView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_V && event->modifiers() == Qt::NoModifier)
    {
        toggleZoomWithAnimation();
        event->accept();
    }
    else
    {
        QChartView::keyPressEvent(event);
    }
}

void AnimatedZoomChartView::toggleZoomWithAnimation()
{
    if (m_animation->state() == QAbstractAnimation::Running)
        return;
    
    if (!m_isZoomed)
    {
        m_originalGeometry = geometry();
        m_animation->setStartValue(m_originalGeometry);
        m_animation->setEndValue(m_zoomedGeometry);
        m_isZoomed = true;
    }
    else
    {
        m_animation->setStartValue(geometry());
        m_animation->setEndValue(m_originalGeometry);
        m_isZoomed = false;
    }
    
    m_animation->start();
}

void AnimatedZoomChartView::onAnimationFinished()
{
    // 动画完成后的处理
    if (m_isZoomed)
    {
        // 放大后可以调整图表显示
        chart()->axes(Qt::Horizontal);
        chart()->axes(Qt::Vertical);
    }
}

void ZoomController::onAnimationFinished()
{
    std::cout<<"AnimationFinsihed called"<<std::endl;
}

void ZoomController::setupAnimation()
{
  m_animation = new QPropertyAnimation(m_chart, "geometry", this);
  m_animation->setDuration(300);
  m_animation->setEasingCurve(QEasingCurve::InOutQuad);
  connect(m_animation, &QPropertyAnimation::finished, this,
          &ZoomController::onAnimationFinished);
}

void ZoomController::enableZoom(bool enable)
{
    m_enabled = enable;
    if (!enable && m_isZoomed) {
        // 如果禁用时处于放大状态，恢复原始大小
        toggleZoom();
    }
}

bool ZoomController::eventFilter(QObject *obj, QEvent *event)
{
    if (obj != m_chart || !m_enabled) {
        return QObject::eventFilter(obj, event);
    }
    
    switch (event->type()) {
    case QEvent::Enter:
        m_chart->setFocus();
        applyZoomStyle(true);
        if (m_focusTimer->isActive()) {
            m_focusTimer->stop();
        }
        break;
        
    case QEvent::Leave:
        m_focusTimer->start();
        break;
        
    case QEvent::MouseButtonPress:
        m_chart->setFocus();
        if (m_focusTimer->isActive()) {
            m_focusTimer->stop();
        }
        break;
        
    case QEvent::KeyPress:
    {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        if (m_chart->hasFocus() && keyEvent->key() == Qt::Key_V && keyEvent->modifiers() == Qt::NoModifier) {
            toggleZoom();
            return true; // 事件已处理
        }
        else if (keyEvent->key() == Qt::Key_Escape && m_isZoomed) {
            // ESC键退出放大
            toggleZoom();
            return true;
        }
        break;
    }
        
    case QEvent::FocusIn:
        applyZoomStyle(true);
        break;
        
    case QEvent::FocusOut:
        if (!m_focusTimer->isActive()) {
            applyZoomStyle(false);
        }
        break;
        
    default:
        break;
    }
    
    return QObject::eventFilter(obj, event);
}

void ZoomController::toggleZoom()
{
    if (!m_chart || !m_enabled || m_animation->state() == QAbstractAnimation::Running) {
        return;
    }
    
    if (!m_isZoomed) {
        // 保存原始位置并放大
        m_originalGeometry = m_chart->geometry();
        m_zoomedGeometry = m_widget->geometry();
        
        m_animation->setStartValue(m_originalGeometry);
        m_animation->setEndValue(m_zoomedGeometry);
        m_isZoomed = true;
    }
    else {
        // 恢复到原始位置
        m_animation->setStartValue(m_widget->geometry());
        m_animation->setEndValue(m_originalGeometry);
        m_isZoomed = false;
    }
    
    m_animation->start();
}

void ZoomController::onFocusTimeout()
{
    if (m_chart && !m_chart->underMouse()) {
        m_chart->clearFocus();
        applyZoomStyle(false);
    }
}

void ZoomController::applyZoomStyle(bool hasFocus)
{
    if (!m_chart) return;
    
    if (hasFocus) {
        m_chart->setStyleSheet(
            "border: 3px solid #4CAF50;"
            "border-radius: 5px;"
            "background-color: #F1F8E9;"
        );
    } else {
        m_chart->setStyleSheet("");
    }
}