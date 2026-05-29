#include "MainWindows/LineChartTest.h"

// OptionDial-----------------------------------------
OptionDial::OptionDial(QWidget* parent, std::shared_ptr<Scope> scope)
    : QWidget(parent)
    , parentPointer(parent)
    , m_scope(scope)
{
    if(m_scope)
    {
        auto it = m_scope->get<MyModel>();
        if(it)
        {
            m_model = it;
        }
    }
    setupUi();
}

void OptionDial::setupUi()
{
    m_mainLayout = new QHBoxLayout(this);
    QLabel* titleLabel = new QLabel("Panel Title", nullptr);
    titleLabel->setStyleSheet("font-size: 14px;"
                              "font-weight: bold;"
                              "color: #333;");

    m_mainLayout->addWidget(titleLabel);
    m_mainLayout->addStretch(1);

    QWidget* leftPanel = createLeftPanel();
    m_mainLayout->addWidget(leftPanel);
}

QPushButton* OptionDial::createLeftPanel()
{
    // 创建主菜单按钮
    QPushButton* menuButton = new QPushButton("Menu", this);
    menuButton->setObjectName("menuButton");

    // 创建主菜单
    QMenu* mainMenu = new QMenu(menuButton);
    mainMenu->setObjectName("mainMenu");

    // 1. 添加一级菜单项
    QStringList navItems = { "Edit", "Explore", "Inspect" };
    for (const QString& item : navItems) {
        QAction* action = mainMenu->addAction(item);
        action->setObjectName(item + "Action");

        // 连接信号
        connect(action, &QAction::triggered, this, [item, this]() {
            if (item == "Edit") {
                this->createEditWidget();
            } else if (item == "Explore") {
                this->createExploreWidget();
            } else if (item == "Inspect") {
                this->createInspectWidget();
            }
        });
    }

    // 2. 添加分隔线
    mainMenu->addSeparator();
    // 添加 SubMenu
    createSubMenu(QString("More..."), mainMenu);

    // 将主菜单设置给按钮
    menuButton->setMenu(mainMenu);

    return menuButton;
}

QMenu* OptionDial::createSubMenu(QString&& SubMenuName, QMenu* mainMenu)
{
    // 3. 创建"More..."子菜单
    QMenu* moreMenu = new QMenu(SubMenuName, mainMenu);
    moreMenu->setObjectName(SubMenuName);

    // 添加子菜单项
    QStringList moreItems = {
        "Duplicate",      "Copy",        "Create library panel",
        "New alert rule", "Hide legend", "Get help"
    };

    for (const QString& item : moreItems) {
        QAction* action = moreMenu->addAction(item);
        action->setObjectName(item + "Action");

        connect(action, &QAction::triggered, this,
                [item]() { qDebug() << "Selected from More:" << item; });
    }

    // 将子菜单添加到主菜单
    mainMenu->addMenu(moreMenu);

    return moreMenu;
}

void OptionDial::createEditWidget()
{
    if (!editWidget.Page) {
        BuildEditWidget();
        editWidget.Page->setWindowFlags(Qt::Window);  // 作为独立窗口
    }
    editWidget.Page->show();
}

void OptionDial::createInspectWidget()
{
    if(!inspecSeriestWidget)
    {
        inspecSeriestWidget = new InspectWidget(this);
        inspecSeriestWidget->setupUI(m_model);
    }
    inspecSeriestWidget->show();
}

void OptionDial::createExploreWidget()
{
    // TODO: 实现Explore功能
}

void OptionDial::BuildEditWidget() {
  editWidget.Page = new QWidget(this);
  editWidget.Vlayout = new QVBoxLayout(editWidget.Page);

  // 设置添加系列的样式
  QString addStyle = R"(
        QLabel, QTextEdit, QPushButton {
            font-size: 14px;
            font-weight: bold;
        }
        QTextEdit {
            font-size: 20px;
            font-weight: bold;
            padding: 8px 4px;
            min-height: 30px;
        }
        QPushButton {
            padding: 4px 8px;
        }
    )";

  // 设置删除系列的样式
  QString deleteStyle = R"(
        QLabel {
            font-size: 14px;
            font-weight: bold;
            padding: 8px 4px;  
            qproperty-alignment: AlignVCenter;
        }
        
        QComboBox {
            font-weight: bold;
            font-size: 20px;
            padding: 8px 4px;
            min-height: 30px;
        }
        
        QComboBox::drop-down {
            width: 15px;
        }
        
        QPushButton {
            font-size: 14px;
            font-weight: bold;
            padding: 8px 12px;
            min-height: 30px;
        }
    )";

  // 初始化添加系列组件
  addSeriesWidget.setupUI(addStyle);
  editWidget.Vlayout->addWidget(addSeriesWidget.page);

  // 初始化删除系列组件
  deleteSeriesWidget.setupUI(deleteStyle, this);
  editWidget.Vlayout->addWidget(deleteSeriesWidget.page);

  // 连接事件：添加查询
  connect(addSeriesWidget.button, &QPushButton::clicked, this, [this]() {
    QString text = addSeriesWidget.getText();
    if (!text.isEmpty()) {
      bool issuccess = checkFairnessForQuery(text);
      if (issuccess) {
        this->onaddItemIntoCombox(text);
        addSeriesWidget.clearText(); // 清空输入框
      }
    }
  });

  // 连接事件：删除查询
  connect(deleteSeriesWidget.button, &QPushButton::clicked, this, [this]() {
    this->onDeleteItemFromCombox(deleteSeriesWidget.comboBox);
  });

}

bool OptionDial::checkFairnessForQuery( const QString& text) 
{ 
    if (text.isEmpty()) {
        return false;  // 空文本，不算有效
    }
    
    // findText 返回 -1 表示未找到
    return deleteSeriesWidget.getIndexOfItem(text) == -1;  // 未找到则返回 true（公平）
}

void OptionDial::onaddItemIntoCombox( const QString& Query)
{
    int index = this->deleteSeriesWidget.getIndexOfItem(Query);
    this->deleteSeriesWidget.allItems.append({Query,false});

    if (index == -1) {
        deleteSeriesWidget.addItem(Query);
        emit addSeriesEvent(Query);
    } else {
        deleteSeriesWidget.setSpecialIndexForCurrentItem(index);
    }
}

void OptionDial::onDeleteItemFromCombox(QComboBox* comboBox)
{
    int index = comboBox->currentIndex();

    if (index != -1) {
        emit deleteSeriesEvent(comboBox->currentText());
        this->deleteSeriesWidget.removeCurrentItem();
    }
}

void OptionDial::onRequestQueryResult(const QString &Query, bool isReason)
{
  if (this->isVisible() && !this->isMinimized()) {
    if (!isReason && addSeriesWidget.page != nullptr) {
      QString originalStyle = addSeriesWidget.button->styleSheet();
      return;
    }
    if(isReason && deleteSeriesWidget.page != nullptr)
    {
        deleteSeriesWidget.updateInvalidSeries(Query, isReason);
    }
  }
}

// ==================== AddSeriesWidget 实现 ====================

void OptionDial::AddSeriesWidget::setupUI(const QString &style)
{
    page = new QWidget();
    page->setFixedHeight(60);
    layout = new QHBoxLayout(page);

    label = new QLabel("add Query");
    textEdit = new QTextEdit();
    textEdit->setFixedHeight(40);
    textEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    button = new QPushButton("ensure Add");
    button->setFlat(true);
    button->setStyleSheet(
        "QPushButton { background-color: transparent; border: none; }"
        "QPushButton:hover { color: blue; }"
        "QPushButton:pressed { color: red; }");

    label->setStyleSheet(style);
    textEdit->setStyleSheet(style);
    button->setStyleSheet(style);

    layout->addWidget(label);
    layout->addWidget(textEdit);
    layout->addWidget(button);
}

void OptionDial::AddSeriesWidget::clearText()
{
    if (textEdit) {
        textEdit->clear();
    }
}

QString OptionDial::AddSeriesWidget::getText() const
{
    return textEdit ? textEdit->toPlainText().trimmed() : QString();
}

// ==================== DeleteSeriesWidget 实现 ====================

void OptionDial::DeleteSeriesWidget::setupUI(const QString &style, QWidget *parent)
{
    page = new QWidget();
    page->setFixedHeight(50);
    layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    label = new QLabel("delete Query");
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    comboBox = new QComboBox(parent);
    comboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    button = new QPushButton("ensure Delete");
    button->setFlat(true);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    button->setStyleSheet(
        "QPushButton { background-color: transparent; border: none; }"
        "QPushButton:hover { color: blue; }"
        "QPushButton:pressed { color: red; }");

    sortBox = new QCheckBox("Sort");
    sortBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    
    label->setStyleSheet(style);
    comboBox->setStyleSheet(style);
    button->setStyleSheet(style);
    sortBox->setStyleSheet(style);

    layout->addWidget(label);
    layout->addWidget(comboBox, 1);
    layout->addWidget(button);
    layout->addWidget(sortBox);

    // 连接复选框状态变化信号
    QObject::connect(sortBox, &QCheckBox::toggled, [this](bool checked) {
        if (checked) {
            applySorting();
        } else {
            restoreOriginalOrder();
        }
    });
}

void OptionDial::DeleteSeriesWidget::addItem(const QString &text)
{
    if (comboBox) {
        comboBox->addItem(text);
    }
}

void OptionDial::DeleteSeriesWidget::removeCurrentItem()
{
    if (comboBox && comboBox->count() > 0) {
        int currentIndex = comboBox->currentIndex();
        if (currentIndex >= 0) {
            // 从 allItems 中删除元素
            for (auto iter = allItems.begin(); iter != allItems.end();) {
                if (iter->Query == comboBox->currentText()) {
                    iter = allItems.erase(iter);
                } else {
                    ++iter;
                }
            }
            // 先删除数据再删除 UI 是正确的逻辑
            comboBox->removeItem(currentIndex);
        }
    }
}

QString OptionDial::DeleteSeriesWidget::getCurrentText() const
{
    return comboBox ? comboBox->currentText() : QString();
}

void OptionDial::DeleteSeriesWidget::applySorting()
{
    if (!comboBox)
        return;

    // 获取当前所有项目
    QStringList currentItems;

    // 添加特殊项目（无效的系列）
    for (auto &it : allItems) {
        if (!it.isValid) {
            currentItems.append(it.Query);
        }
    }
    
    // 更新comboBox
    comboBox->clear();
    comboBox->addItems(currentItems);
    isSorted = true;
}

void OptionDial::DeleteSeriesWidget::restoreOriginalOrder()
{
    if (!comboBox)
        return;

    comboBox->clear();
    for (auto &it : allItems) {
        comboBox->addItem(it.Query);
    }
    isSorted = false;
}

bool OptionDial::DeleteSeriesWidget::isSortedMode() const
{
    return sortBox ? sortBox->isChecked() : false;
}

void OptionDial::DeleteSeriesWidget::clearItems()
{
    if (comboBox) {
        comboBox->clear();
    }
}

int OptionDial::DeleteSeriesWidget::getIndexOfItem(const QString &Query)
{
    return comboBox ? comboBox->findText(Query) : -1;
}

void OptionDial::DeleteSeriesWidget::setSpecialIndexForCurrentItem(int index)
{
    if (comboBox) {
        comboBox->setCurrentIndex(index);
    }
}

void OptionDial::DeleteSeriesWidget::updateInvalidSeries(const QString &Query, bool isReason)
{
    for (auto &it : allItems) {
        if (it.Query == Query) {
            it.isValid = true;
            break;
        }
    }
}

// ==================== InspectWidget 实现 ====================
InspectWidget::InspectWidget(QWidget* parent)
    : QWidget(parent)
    , m_layout(nullptr)
    , m_tableView(nullptr)
    , m_plainNumberDelegate(nullptr)
    , m_beijingTimeDelegate(nullptr)
    , m_model(nullptr)
    , m_currentFormat(Timestamp)
    , m_updateTimer(nullptr) {
    
    // 设置窗口属性
    setWindowFlags(Qt::Window);
    setFixedHeight(60);
}

InspectWidget::~InspectWidget() {
    // 停止定时器
    if (m_updateTimer) {
        m_updateTimer->stop();
    }
}

void InspectWidget::setupUI(MyModel* model) {
    // 创建布局
    m_layout = new QVBoxLayout(this);
    
    // 创建表格视图
    m_tableView = new QTableView(this);
    
    // 创建 delegates
    m_plainNumberDelegate = new PlainNumberDelegate(this);
    m_beijingTimeDelegate = new BeijingTimeDelegate(this);
    
    // 设置表格属性
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    
    if (m_tableView->horizontalHeader()) {
        m_tableView->horizontalHeader()->setStretchLastSection(true);
    }
    
    // 设置模型
    if (model) {
        m_tableView->setModel(model);
        m_tableView->setItemDelegate(m_plainNumberDelegate);
        m_model = model;
    } else {
        qDebug() << "InspectWidget::setupUI error : model = nullptr";
    }
    
    // 添加表格到布局
    m_layout->addWidget(m_tableView);
    
    // 创建定时器
    m_updateTimer = new QTimer(this);
    
    // 建立所有连接
    setupConnections();
}

void InspectWidget::setupConnections() {
    // 连接表头双击信号
    if (m_tableView && m_tableView->horizontalHeader()) {
        connect(m_tableView->horizontalHeader(), &QHeaderView::sectionDoubleClicked,
                this, &InspectWidget::onFormatChanged);
    }
    
    // 连接定时器信号
    if (m_updateTimer && m_model) {
        connect(m_updateTimer, &QTimer::timeout,
                this, &InspectWidget::onTimerTimeout);
    }
}

void InspectWidget::onFormatChanged(int logicalIndex) {
    Q_UNUSED(logicalIndex);
    
    // 切换格式
    if (m_currentFormat == Timestamp) {
        m_currentFormat = BeijingTime;
    } else {
        m_currentFormat = Timestamp;
    }
    
    // 更新 delegate
    updateDelegate();
    
    // 通知 model 刷新视图
    if (m_model) {
        m_model->toggleXColumnFormat();
    }
}

void InspectWidget::updateDelegate() {
    if (!m_tableView) return;
    
    QAbstractItemDelegate* delegate = (m_currentFormat == Timestamp) 
                                      ? m_plainNumberDelegate 
                                      : m_beijingTimeDelegate;
    m_tableView->setItemDelegate(delegate);
}

void InspectWidget::onTimerTimeout() {
    if (m_model) {
        m_model->onTimerTimeout();
    }
}

// ========== 方案2.2 核心实现 ==========
void InspectWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    
    // ✅ 窗口显示时启动定时器
    if (m_updateTimer && !m_updateTimer->isActive()) {
        m_updateTimer->start(3 * 1000);  // 6秒间隔
        qDebug() << "InspectWidget shown, timer started";
    }
}

void InspectWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    
    // ✅ 窗口隐藏时停止定时器
    if (m_updateTimer && m_updateTimer->isActive()) {
        m_updateTimer->stop();
        qDebug() << "InspectWidget hidden, timer stopped";
    }
}


// dataChart--------------------------------------
dataChart::dataChart(QWidget *parent, Scope *scope)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    
    // 设置大小策略（允许拉伸）
    this->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    this->setMinimumSize(500, 500); // 最小限制
    
    if (scope) {
        initializeScope(scope);
    }
}

void dataChart::setUI(const QString &title, const QString &query, const QColor &color)
{
    createDial(m_scope);
    createChartView();
    createChart(chartView);
    createAxis(chart);
}

void dataChart::createDial(std::shared_ptr<Scope> m_scope)
{
    m_dial = new OptionDial(this,m_scope);
    AddSonWidget(m_dial);
    
    connect(m_dial, &OptionDial::addSeriesEvent, this, &dataChart::onAddSeries);
    connect(m_dial, &OptionDial::deleteSeriesEvent, this, &dataChart::onDeleteSeries);
    connect(this, &dataChart::requestResultAboutQuery, m_dial, &OptionDial::onRequestQueryResult);
}

void dataChart::createChartView()
{
    chartView = new QtCharts::QChartView();
    chartView->setStyleSheet("QChartView {"
                             "  background: white;"
                             "  border-radius: 8px;"
                             "  padding: 0px;"
                             "}");
    chartView->setInteractive(true); // 允许交互
    createChart(chartView);
    AddSonWidget(chartView);
}

void dataChart::createChart(QtCharts::QChartView *chartViewItem)
{
    // 创建图表
    chart = new QtCharts::QChart();
    chart->setBackgroundRoundness(0);
    chart->setBackgroundBrush(Qt::white);
    chart->setMargins(QMargins(0, 0, 0, 0));

    // 配置图例
    chart->legend()->setVisible(false);
    chart->legend()->setAlignment(Qt::AlignBottom);
    chart->legend()->setFont(QFont("Arial", 9));
    chart->legend()->setMarkerShape(QtCharts::QLegend::MarkerShapeCircle);

    // 配置图表外观
    chart->setTitleFont(QFont("Arial", 14, QFont::Bold));
    chart->setAnimationOptions(QtCharts::QChart::SeriesAnimations);
    chart->setTheme(QtCharts::QChart::ChartThemeLight);
    chart->setBackgroundBrush(QBrush(QColor(240, 240, 240)));
    chart->setDropShadowEnabled(true);

    // 设置图表到ChartView
    chartViewItem->setChart(chart);
    chartViewItem->setRenderHint(QPainter::Antialiasing);
}

void dataChart::createAxis(QtCharts::QChart *chart)
{
    // 坐标轴
    axisX = new QtCharts::QDateTimeAxis();
    axisX->setFormat("HH:mm");
    // 设置X轴范围（使用当前时间作为示例）
    QDateTime current = QDateTime::currentDateTime();
    axisX->setRange(current.addSecs(-60 * 30), current.addSecs(60 * 30)); // 过去1小时
    chart->addAxis(axisX, Qt::AlignBottom);
    
    // 设置Y轴范围
    axisY = new QtCharts::QValueAxis();
    axisY->setRange(0, 1000); // 设置Y轴范围
    chart->addAxis(axisY, Qt::AlignLeft);
}

void dataChart::createLegendButton(QtCharts::QScatterSeries *series)
{
    LegendButton = new LegendIconButton(nullptr, series, m_scope.get());
    AddSonWidget(LegendButton);
}

QtCharts::QScatterSeries *dataChart::createSeries(const QColor &color, const QString &query)
{
    QtCharts::QScatterSeries *series = new QScatterSeries();
    series->setName(query);
    series->setMarkerSize(10);
    series->setBorderColor(Qt::transparent); // 透明边框
    series->setColor(color);                 // 半透明红色

    chart->addSeries(series);
    series->attachAxis(axisX);
    series->attachAxis(axisY);

    seriesSets->insert(series);
    selectedLegends->insert(query);
    // 创建对应的 Legend Button
    createLegendButton(series);
    // 插入这个 Legend Button 到 map
    legendMarkerMap->insert(series->name(), LegendButton);

    return series;
}

void dataChart::initializeScope(Scope *scope)
{
    if (!m_scope) {
        m_scope = std::make_shared<Scope>();
        selectedLegends = std::make_shared<QSet<QString>>();
        legendMarkerMap = std::make_shared<QMap<QString, LegendIconButton *>>();
        seriesSets = std::make_shared<QSet<QtCharts::QScatterSeries *>>();
        
        m_scope->registerService(selectedLegends);
        m_scope->registerService(legendMarkerMap);
        m_scope->registerService(seriesSets);
        // 注册其他服务的层级
        {
            auto it = scope->get<RequestSender>();
            if(it)
            {
                m_scope->registerService(it);
            }
        }
        {
          auto it = scope->get<MyModel>();
          if (it) {
            m_scope->registerService(it);
          }
        }
    }
}

void dataChart::setDesiredSize(const QSize &size)
{
    if (size != m_desiredSize) {
        m_desiredSize = size;
        this->updateGeometry(); // 通知父 layout 大小已改变

        // 找到 E
        QWidget *widgetE = this->parentWidget();

        if (!widgetE) return;

        // 找到 F (QScrollArea)
        QScrollArea *scrollAreaF = qobject_cast<QScrollArea *>(widgetE->parentWidget()->parentWidget());

        if (scrollAreaF) {
            ContainerWidget *Parent = qobject_cast<ContainerWidget *>(this->parentWidget());
            if (Parent) {
                widgetE->setMinimumSize(Parent->return_suitableSize(m_desiredSize));
                if (Parent->whehterRestore(this, scrollAreaF->viewport()->size())) {
                    scrollAreaF->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                    scrollAreaF->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                } else {
                    scrollAreaF->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
                    scrollAreaF->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
                }
            }
        }
    }
}

void dataChart::AddSonWidget(QWidget *item)
{
    QtCharts::QChartView *view = qobject_cast<QtCharts::QChartView *>(item);
    if (view) {
        chartView = view;
    }
    if (item) {
        item->setParent(this);
        m_layout->addWidget(item);
    }
}

bool dataChart::isExistQuery(const QString &Query)
{
    if (selectedLegends->find(Query) == selectedLegends->end()) {
        qDebug() << Query << " do not select , so skip it";
        return false;
    }
    return true;
}

bool dataChart::replaceDataInAxisY(const QString &Query, QVector<QPointF> it, chartData_update *data)
{
    for (auto &item : *seriesSets) {
        if (item->name() == Query) {
            // 更新 Y 轴的限制
            {
                // 获取系列附加的所有轴
                QList<QAbstractAxis *> axes = item->attachedAxes();
                // 遍历找到需要的轴
                for (QAbstractAxis *axis : axes) {
                    if (axis->orientation() == Qt::Vertical) {
                        QValueAxis *yAxis = qobject_cast<QValueAxis *>(axis);
                        if (yAxis) {
                            axisY->setRange(data->min_value - 20, data->max_value + 20);
                        }
                    }
                }
            }
            // 替换系列的数据
            item->replace(it);
            return true;
        }
    }
    return false;
}

void dataChart::replaceDataInAxisX(QDateTime &now, int m_currentSeconds)
{
    for (auto &it : *seriesSets) {
        {
            // 获取系列附加的所有轴
            QList<QAbstractAxis *> axes = it->attachedAxes();
            // 遍历找到需要的轴
            for (QAbstractAxis *axis : axes) {
                if (axis->orientation() == Qt::Horizontal) {
                    QtCharts::QDateTimeAxis *xAxis = qobject_cast<QtCharts::QDateTimeAxis *>(axis);
                    if (xAxis) {
                        xAxis->setRange(now.addSecs(-m_currentSeconds), now.addSecs(m_currentSeconds));
                    }
                }
            }
        }
    }
}

void dataChart::getSeriesNames(QSet<QString> &set) const
{
    if (legendMarkerMap) {
        for (auto &it : *legendMarkerMap) {
            if (set.find(it->return_QueryName()) != set.end()) {
                continue;
            } else {
                set.insert(it->return_QueryName());
            }
        }
    }
}

void dataChart::removeSpecialSeries(const QString &query)
{
    // 参数验证
    if (query.isEmpty()) {
        qWarning() << "Cannot delete series: empty query";
        return;
    }

    // 1. 查找并验证 Series
    QtCharts::QScatterSeries *series = nullptr;
    if (seriesSets) {
        for (auto *s : *seriesSets) {
            if (s && s->name() == query) {
                series = s;
                break;
            }
        }
    }

    if (!series) {
        qWarning() << "Series not found:" << query;
        return;
    }

    // 2. 从图表中移除（断开与坐标轴的连接）
    if (chart) {
        chart->removeSeries(series);
    }

    // 3. 从 seriesSets 中移除
    if (seriesSets) {
        seriesSets->remove(series);
    }

    // 4. 从 selectedLegends 中移除
    if (selectedLegends) {
        selectedLegends->remove(query);
    }

    // 5. 处理对应的 Legend Button
    if (legendMarkerMap && legendMarkerMap->contains(query)) {
        LegendIconButton *button = legendMarkerMap->value(query);
        if (button) {
            // 断开所有信号连接
            button->disconnect();

            // 从父布局中移除
            if (button->parentWidget()) {
                QLayout *layout = button->parentWidget()->layout();
                if (layout) {
                    layout->removeWidget(button);
                }
            }

            // 延迟删除（如果在事件循环中）
            button->deleteLater(); // 推荐使用 deleteLater 而不是 delete
        }
        legendMarkerMap->remove(query);
    }

    // 6. 删除 Series 对象
    series->deleteLater(); // 使用 deleteLater 确保安全删除

    qDebug() << "Series deleted:" << query;
}

void dataChart::onAddSeries(const QString &Query)
{
    this->createSeries(Qt::red, Query);
    auto m_requestSender = m_scope->get<RequestSender>();
    if (m_requestSender) {
        m_requestSender->addSeriesIntoSet(Query);
    }
}

void dataChart::onDeleteSeries(const QString &Query)
{
    this->removeSpecialSeries(Query);
    auto m_requestSender = m_scope->get<RequestSender>();
    if (m_requestSender) {
        m_requestSender->deleteSerieFromSet(Query);
    }
}

QSize dataChart::sizeHint() const
{
    return m_desiredSize;
}


bool  dataChart::isOwnSpecialQuery(const QString &Query) 
{
    for(auto &it : *seriesSets)
    {
        if(it->name() == Query)
        {
            return true;
        }
    }
    return false;
}

QtCharts::QChartView *dataChart::returnChartView()
{
    return this->chartView;
}


// RequestSender-----------------------------------
RequestSender::RequestSender(QObject *parent, Scope *scope)
    : QObject(parent)
    , m_scope(scope)
{
    if (m_scope)
    {
        auto it = m_scope->getShared<QSet<dataChart *>>();
        if (it)
        {
            m_dataChartSet = it;
        }
    }
    createTimer();
}

RequestSender::~RequestSender()
{
    if (m_updateTimer)
    {
        m_updateTimer->stop();
        delete m_updateTimer;
        m_updateTimer = nullptr;
    }
}

void RequestSender::createTimer()
{
    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, [this]() {
        for (const QString &query : this->m_QuerySet) {
            this->onTimeout(query);
        }
    });
    m_updateTimer->start(15 * 1000);
}

void RequestSender::onTimeout(const QString &Query)
{
    QDateTime now = QDateTime::currentDateTime();
    QString fromIso = TimeUtils::toUtcIsoString(now.addSecs(-5 * 60));
    QString toIso = TimeUtils::toUtcIsoString(now);

    if (!fromIso.endsWith('Z'))
        fromIso += 'Z';
    if (!toIso.endsWith('Z'))
        toIso += 'Z';

    auto result = ObjectRouter::instance().invoke(
        "config://dashboard/server_01", "queryPrometheusMetric",
        QVariantList() << "PC67C731E4884519C" << fromIso << toIso << Query);
    
    // TODO: 处理 result 返回值
    Q_UNUSED(result);
}

void RequestSender::addSeriesIntoSet(const QString &Query)
{
    if (m_QuerySet.find(Query) == m_QuerySet.end())
    {
        m_QuerySet.insert(Query);
    }
    return;
}

void RequestSender::deleteSerieFromSet(const QString &Query)
{
    size_t count = 0;
    //  loop all dataChart for whether the Query is only one
    for (auto &it : *m_dataChartSet)
    {
        //  if the dataChart find the Query in their Series , count + 1
        if (it->isOwnSpecialQuery(Query))
        {
            count += 1;
        }
    }
    // If count ! = 1, which means that the total number of dataChart with X is greater than 1
    //  and the Query should be not delete
    if (count > 1)
    {
        return;
    }

    if (m_QuerySet.find(Query) == m_QuerySet.end())
    {
        return;
    }
    m_QuerySet.remove(Query);
}

void RequestSender::changeTimer(int value)
{
    if (m_updateTimer)
    {
        m_updateTimer->stop();
        m_updateTimer->start(value * 1000);
    }
}


// LineChart------------------------------------------
LineChart::LineChart(QWidget* parent)
    : QWidget(parent)
{
    m_dataChartSet = std::make_shared<QSet<dataChart*>>();
    m_scope = new Scope();
    m_scope->registerService(m_dataChartSet);

    model = new MyModel(this);
    m_scope->registerService(model);

    dataDeliverControl = new DataDeliverControl(this, m_scope);
    m_scope->registerService(dataDeliverControl);
    
    m_requestSender = new RequestSender(this, m_scope);
    m_scope->registerService(m_requestSender);
    m_controlPanel = new ControlPanel(this, m_scope);
}

LineChart::~LineChart()
{
    delete m_scope;
}

void LineChart::createScrollArea()
{
    scrollArea = new QScrollArea();
    scrollArea->setHorizontalScrollBarPolicy(
        Qt::ScrollBarAsNeeded); // 根据需要显示水平滚动条
    scrollArea->setVerticalScrollBarPolicy(
        Qt::ScrollBarAsNeeded); // 根据需要显示垂直滚动条

    scrollArea->setMouseTracking(true);
    scrollArea->setWidgetResizable(true); // 关键：允许内容小部件调整大小

    mainLayout->addWidget(scrollArea);
}

dataChart* LineChart::createEmbeddedChartView(const QString &title, const QString &query,
                                              const QColor &color, QWidget *Container)
{
    item = new dataChart(nullptr, m_scope);
    item->setUI(title, query, color);
    return item;
}

QWidget* LineChart::createEmbeddedWidget(const QString &title, const QString &query,
                                         const QColor &color)
{
    ContainerWidget *contain = new ContainerWidget();

    auto it = createEmbeddedChartView(title, query, color, contain);
    ZoomControllerBuilder *control = new ZoomControllerBuilder();
    contain->AddSonWidget(it);
    control->setWidget(scrollArea, it);
    control->attachTo();
    m_dataChartSet->insert(it);
   
    it = createEmbeddedChartView(title, query, color, contain);
    control = new ZoomControllerBuilder();
    contain->AddSonWidget(it);
    control->setWidget(scrollArea, it);
    control->attachTo();
    m_dataChartSet->insert(it);

    it = createEmbeddedChartView(title, query, color, contain);
    control = new ZoomControllerBuilder();
    contain->AddSonWidget(it);
    control->setWidget(scrollArea, it);
    control->attachTo();
    m_dataChartSet->insert(it);

    contain->ChangeMinmumSize();
    qDebug() << (QString("添加对象1高度: %1 ,宽度: %2 ").arg(it->height())).arg(it->width());

    return contain;
}

QWidget* LineChart::createChartView(const QString &title, const QString &query,
                                    const QColor &color)
{
    // 设置布局管理
    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);
    mainLayout->addWidget(m_controlPanel);
    this->createScrollArea();
    QWidget *widgetE = this->createEmbeddedWidget(title, query, color);
    scrollArea->setWidget(widgetE);

    // 2. 设置大小策略（允许拉伸）
    this->setSizePolicy(QSizePolicy::Preferred,
                        QSizePolicy::Preferred);
    this->setMinimumSize(200, 200); // 最小限制

    return this;
}

MyModel* LineChart::return_Model()
{
    return model;
}

//  PlainNumberDelegate-------------------------------------------------
PlainNumberDelegate::PlainNumberDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QString PlainNumberDelegate::displayText(const QVariant &value, const QLocale &locale) const
{
    Q_UNUSED(locale)
    
    // 检查是否为数字类型
    if (value.type() == QVariant::Int) {
        return QString::number(value.toInt());
    }
    else if (value.type() == QVariant::LongLong) {
        return QString::number(value.toLongLong());
    }
    else if (value.type() == QVariant::Double) {
        float d = value.toFloat();
        // 检查是否为整数（无小数部分）
        if (d == static_cast<long long>(d)) {
            return QString::number(static_cast<long long>(d));
        }
        // 使用 'f' 格式避免科学计数法
        return QString::number(d, 'f', 10).remove(QRegExp("\\.?0+$"));
    }
    
    // 其他类型使用默认处理
    return QStyledItemDelegate::displayText(value, locale);
}

void PlainNumberDelegate::paint(QPainter *painter,
                                const QStyleOptionViewItem &option,
                                const QModelIndex &index) const {
  // 直接显示原始数据
  QStyledItemDelegate::paint(painter, option, index);
}

// BeijingTimeDelegate-----------------------------------
BeijingTimeDelegate::BeijingTimeDelegate(QObject *parent):QStyledItemDelegate(parent)
{

}

QString BeijingTimeDelegate::displayText(const QVariant &value,
                                         const QLocale &locale) const {
    return QStyledItemDelegate::displayText(value, locale);
}

QString BeijingTimeDelegate::formatToBeijingTime(qint64 timestampMs) const {
  QDateTime dateTime;
  dateTime.setMSecsSinceEpoch(timestampMs);
  dateTime.setTimeZone(QTimeZone("Asia/Shanghai"));
  return dateTime.toString("yyyy-MM-dd HH:mm:ss");
}

void BeijingTimeDelegate::paint(QPainter *painter,
                                const QStyleOptionViewItem &option,
                                const QModelIndex &index) const {

    // ✅ 只对 X Coordinate 列进行格式转换
    if (index.column() == 1) {
      // 获取原始时间戳（从 EditRole 或直接转换）
      qint64 timestamp = index.data(Qt::EditRole).toLongLong();

      // 转换为北京时间字符串
      QString beijingTime = formatToBeijingTime(timestamp);

      // 创建新的选项，使用格式化后的文本
      QStyleOptionViewItem opt = option;
      opt.text = beijingTime;
      // ✅ 设置文本居中对齐
      opt.displayAlignment = Qt::AlignCenter; // 水平垂直居中

      // 绘制
      QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &opt,
                                         painter);
    } else {
      // 其他列使用默认绘制
      QStyledItemDelegate::paint(painter, option, index);
    }
}

// MyModel------------------------------------
MyModel::MyModel(QObject *parent, Scope *scope) 
    : QAbstractTableModel(parent)
    , m_scope(scope)
{
    if (m_scope) {
        m_dataChartSet = m_scope->getShared<QSet<dataChart *>>();
    }
}

QVector<QPointF> MyModel::getData(const QString &Query) const
{
    if (m_dataMap.find(Query) == m_dataMap.end()) {
        return QVector<QPointF>{};
    } else {
        return m_dataMap[Query];
    }
}

void MyModel::addChartPointData(const ChartData &chartData)
{
    if (!chartData[0].isReason) {
        emit dataChanged(chartData[0].name, chartData[0].isReason); // 自定义信号
        return;
    }
    
    for (const TimeSeries &series : chartData) {
        if (m_dataMap.find(series.name) == m_dataMap.end()) {
            QVector<QPointF> m_data;
            addDataPoint(series, m_data);
            m_dataMap[series.name] = m_data;
        } else {
            auto &m_data = m_dataMap[series.name];
            addDataPoint(series, m_data);
        }
        emit dataChanged(series.name, true);  // 自定义信号
    }
}

void MyModel::addDataPoint(const TimeSeries &series, QVector<QPointF> &m_data)
{
    for (const auto &point : series.points) {
        qreal xAsMs = static_cast<qreal>(point.timestamp * 1000);
        m_data.append(QPointF(xAsMs, point.value));
        
        if (point.value > update_data.max_value) {
            update_data.max_value = point.value;
        }
        if (point.value < update_data.min_value) {
            update_data.min_value = point.value;
        }
    }
    
    update_data.min_timeStamp = series.points.front().timestamp;
    update_data.max_timeStamp = series.points.back().timestamp;
    
    // 保持最近100个点
    if (m_data.size() > 100) {
        m_data.remove(0, m_data.size() - 100);
    }
}

chartData_update *MyModel::return_chartData()
{
    return &update_data;
}

void MyModel::onTimerTimeout()
{
    // 在独立线程或主线程中执行数据更新
    // 方案1: 直接在主线程更新（适合小数据量）
    performDataUpdate();

    // 方案2: 使用QtConcurrent在后台线程更新（适合大数据量）
    // QtConcurrent::run([this]() {
    //     performDataUpdate();
    // });
}

void MyModel::performDataUpdate()
{
    // 注意：这里可能需要在子线程中执行

    // // ========== 阶段1: 准备更新 ==========
    // // 调用 beginResetModel() 通知 View 即将重置
    // beginResetModel();
    // // ↓ 内部会发射 layoutAboutToBeChanged() 信号
    // // ↓ View 会准备重新布局，停止绘制

    // // ========== 阶段2: 数据更新（临界区） ==========
    // {
    //   QWriteLocker locker(&m_lock); // 获取写锁，保护数据
    //   m_dataMap = newData;          // 实际数据更新
    // }                               // 写锁释放

    beginResetModel();
    // ↓ 内部会发射 layoutAboutToBeChanged() 信号
    // ↓ View 会准备重新布局，停止绘制

    // ========== 阶段3: 通知完成 ==========
    endResetModel();

    //   // ========== 阶段4: 后续处理 ==========
    // notifyCharts();              // 通知关联的图表更新
    // emit updateProgress(100);    // 发射进度信号
    // update_data.isUpdating = false;  // 清除更新标志
    // 更新数据（线程安全）
    // updateDataBatch();
}

int MyModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    // 计算总行数 = 所有键的QVector长度之和
    int totalRows = 0;
    for (const auto &points : m_dataMap) {
        totalRows += points.size();
    }
    return totalRows;
}

int MyModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return 3; // Key, X坐标, Y坐标
}

MyModel::DataItem MyModel::getDataItem(const QModelIndex &index) const
{
    DataItem item;

    int row = index.row();
    int currentRow = 0;

    // 直接遍历 QMap
    for (auto it = m_dataMap.begin(); it != m_dataMap.end(); ++it) {
        const QString &key = it.key();
        const auto &points = it.value();
        int size = points.size();

        if (row < currentRow + size) {
            item.key = key;                     // 使用Map的键作为Key值
            item.pointIndex = row - currentRow;
            item.point = points[item.pointIndex];
            break;
        }
        currentRow += size;
    }

    return item;
}

QVariant MyModel::data(const QModelIndex &index, int role) const
{
    if (!isValidIndex(index))
        return QVariant();

    auto item = getDataItem(index);

    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        switch (index.column()) {
        case 0:
            return item.key;
        case 1:
            return item.point.x();
        case 2:
            return item.point.y();
        }
        break;

    case Qt::TextAlignmentRole:
        return Qt::AlignCenter;

    case Qt::UserRole:
        // 自定义角色，返回完整的数据项
        return QVariant::fromValue(item);
    }

    return QVariant();
}

QVariant MyModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();

    if (orientation == Qt::Horizontal) {
        switch (section) {
        case 0:
            return tr("Key");
        case 1:
            return tr("X Coordinate");
        case 2:
            return tr("Y Coordinate");
        }
    } else if (orientation == Qt::Vertical) {
        return section + 1;
    }

    return QVariant();
}

bool MyModel::isValidIndex(const QModelIndex &index) const
{
    return index.isValid() && 
           index.row() >= 0 && 
           index.row() < rowCount() &&
           index.column() >= 0 && 
           index.column() < columnCount();
}

void MyModel::appendData(const QString &key, const QPointF &newPoint)
{
    // ========== 阶段1: 计算插入位置 ==========
    int insertRow = -1;
    {
        QReadLocker locker(&m_lock);
        int currentRow = 0;
        for (auto it = m_dataMap.begin(); it != m_dataMap.end(); ++it) {
            if (it.key() == key) {
                insertRow = currentRow + it.value().size();
                break;
            }
            currentRow += it.value().size();
        }
    }

    // ========== 阶段2: 准备插入 ==========
    if (insertRow >= 0) {
        // 调用 beginInsertRows 通知 View
        beginInsertRows(QModelIndex(), insertRow, insertRow);
        // ↓ 内部会发射 rowsAboutToBeInserted() 信号
        // ↓ View 会准备插入新行

        // ========== 阶段3: 实际数据插入 ==========
        {
            QWriteLocker locker(&m_lock);    // 获取写锁
            m_dataMap[key].append(newPoint); // 追加数据
        }

        // ========== 阶段4: 完成插入 ==========
        endInsertRows();
        // ↓ 内部会发射 rowsInserted() 信号
        // ↓ View 收到信号后插入新行并显示数据
    } else {
        // 如果 key 不存在，创建新条目
        int newRow = rowCount();
        beginInsertRows(QModelIndex(), newRow, newRow);
        {
            QWriteLocker locker(&m_lock);
            m_dataMap[key] = QVector<QPointF>() << newPoint;
        }
        endInsertRows();
    }
}

void MyModel::toggleXColumnFormat() {
  // 关键：发射 dataChanged 信号通知视图更新
  if (m_dataMap.isEmpty())
    return;

  QModelIndex topLeft = index(0, 1);                        // X列第一行
  QModelIndex bottomRight = index(m_dataMap.size() - 1, 1); // X列最后一行
  emit QAbstractTableModel::dataChanged(topLeft, bottomRight,
                                        {Qt::DisplayRole});
}