#ifndef MONITOR_H
#define MONITOR_H

#include "load_config/Qt_library.h"
#include "Rust_error_deal/error_deal.h"
#include "PLC/Map_PLCStruct.h"
#include "GrafanaDashboardManager/GrafanaDashboard.h"
#include "GrafanaDashboardManager/ObjectRouter.h"

class ChartController ;

class ConnectionManager : public QObject {
    Q_OBJECT
public:
    // 替代传统connect，但增加管理功能
    template<typename Sender, typename Signal, typename Receiver, typename Slot>
    QMetaObject::Connection safeConnect(
        Sender* sender, Signal signal,
        Receiver* receiver, Slot slot,
        Qt::ConnectionType type = Qt::AutoConnection,
        const QString& tag = QString()) {
        // 建立连接
        auto connection = QObject::connect(sender, signal, receiver, slot, type);
        if (connection) {
            // 记录连接信息
            ConnectionInfo info;
            info.connection = connection;
            info.sender = sender;
            info.receiver = receiver;
            info.tag = tag;
            info.created = QDateTime::currentDateTime();
            
            connections_.append(info);
            
            qDebug() << "Connection established:" 
                     << info.sender->objectName() << "->" 
                     << info.receiver->objectName()
                     << "Tag:" << tag;
        } else {
            qWarning() << "Failed to establish connection";
        }
        
        return connection;
    }

    template <typename Sender, typename Signal, typename Functor>
    QMetaObject::Connection
    safeConnect(Sender *sender, Signal signal, Functor &&functor,
                Qt::ConnectionType type = Qt::AutoConnection,
                const QString &tag = QString()) {

      // 为lambda创建一个上下文对象
      auto *context = new QObject(sender);

      // 连接信号到lambda
      auto connection = QObject::connect(
          sender, signal,
          [context, functor = std::forward<Functor>(functor)](auto &&...args) {
            if (context) {
              functor(std::forward<decltype(args)>(args)...);
            }
          });

      // 当sender销毁时，自动销毁context
      QObject::connect(sender, &QObject::destroyed, context,
                       &QObject::deleteLater);

      return connection;
    }
    // 断开特定标签的连接
    void disconnectByTag(const QString& tag) {
        for (auto it = connections_.begin(); it != connections_.end(); ) {
            if (it->tag == tag) {
                QObject::disconnect(it->connection);
                it = connections_.erase(it);
                qInfo() << "Disconnected connection with tag:" << tag;
            } else {
                ++it;
            }
        }
    }
    
    // 断开特定对象的所有连接
    void disconnectObject(QObject* obj, bool asSender = true, bool asReceiver = true) {
        for (auto it = connections_.begin(); it != connections_.end(); ) {
            bool shouldDisconnect = false;
            if (asSender && it->sender == obj) shouldDisconnect = true;
            if (asReceiver && it->receiver == obj) shouldDisconnect = true;
            
            if (shouldDisconnect) {
                QObject::disconnect(it->connection);
                it = connections_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // 获取连接统计
    void printConnectionStats() const {
        qInfo() << "=== Connection Manager Statistics ===";
        qInfo() << "Total connections:" << connections_.size();
        
        QMap<QString, int> tagCount;
        for (const auto& conn : connections_) {
            tagCount[conn.tag.isEmpty() ? "(untagged)" : conn.tag]++;
        }
        
        for (auto it = tagCount.begin(); it != tagCount.end(); ++it) {
            qInfo() << "  " << it.key() << ":" << it.value();
        }
    }
    
private:
    struct ConnectionInfo {
        QMetaObject::Connection connection;
        QObject* sender = nullptr;
        QObject* receiver = nullptr;
        QString tag;
        QDateTime created;
    };
    
    QList<ConnectionInfo> connections_;
  
};

// 缩放控制器
class ZoomController : public IChartFeature {
  Q_OBJECT
public:
    void attachTo(QtCharts::QChartView* chart) override {
        m_chart = chart;
        //  安装事件过滤器等
        m_chart->setFocusPolicy(Qt::StrongFocus);
        m_chart->installEventFilter(this);
        m_chart->setMouseTracking(true);
        //  设置缩放区域
        this->setZoomedGeometry();
        //  初始化动画
        this->setupAnimation();
        //  初始化定时器
        m_focusTimer = new QTimer(this);
        m_focusTimer->setSingleShot(true);
        //  1秒后清除焦点
        m_focusTimer->setInterval(300); 

        connect(m_focusTimer, &QTimer::timeout, this,
                &ZoomController::onFocusTimeout);
    }

    void detach() override {
        // 移除事件过滤器
        m_chart = nullptr;
    }

    // 设置缩放区域
    void setZoomedGeometry() {
      QRect screenRect = QApplication::primaryScreen()->availableGeometry();
      m_zoomedGeometry =
          QRect(screenRect.width() * 0.1, screenRect.height() * 0.1,
                screenRect.width() * 0.8, screenRect.height() * 0.8);
    }

    // 启用/禁用缩放功能
    void enableZoom(bool enable = true);

    // 手动触发缩放
    void toggleZoom();

    // 获取当前状态
    bool isZoomed() const { return m_isZoomed; }

    //  添加放缩对象
    void setWidget(QWidget *item, QtCharts::QChartView *chart) {
      m_widget = item;
      m_chart = chart;
    }

  protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

  private slots:
    void onAnimationFinished();
    void onFocusTimeout();
    
private:
  void setupAnimation();
  void applyZoomStyle(bool hasFocus);

  QPropertyAnimation *m_animation = nullptr; // 动画
  QTimer *m_focusTimer = nullptr;            // 焦点定时器

  bool m_isZoomed = false;
  bool m_enabled = true;

  QRect m_originalGeometry;       // 原始几何位置
  QRect m_zoomedGeometry;         // 放大后的几何位置
  QRect m_parentOriginalGeometry; // 父窗口原始位置（可选）

  QtCharts::QChartView *m_chart = nullptr;
  QWidget *m_widget =nullptr;
};

// 管理器（简化版）
class ChartFeatureManager {
public:
    void addFeature(IChartFeature* feature) {
        m_features.append(feature);
    }
    
    void attachAllTo(QtCharts::QChartView* chart) {
        for (auto feature : m_features) {
            feature->attachTo(chart);
        }
    }

    ~ChartFeatureManager()
    {
      for(auto &it : m_features)
      {
        if(it)
        {
          delete it;
        }
      }
    }
    
private:
    QList<IChartFeature*> m_features;
};

class AnimatedZoomChartView : public QtCharts::QChartView
{
    Q_OBJECT
    
public:
    explicit AnimatedZoomChartView(QWidget *parent = nullptr);
    ~AnimatedZoomChartView()
    {
      std::cout<<"AnimatedZoomChartView destory call"<<std::endl;
    }
    ;
    
protected:
    void keyPressEvent(QKeyEvent *event) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;
    
private slots:
    void onAnimationFinished();
    
private:
    bool m_isZoomed = false;
    QRect m_originalGeometry;
    QRect m_zoomedGeometry = QRect(50, 50, 800, 600);
    QPropertyAnimation *m_animation;
    
    void toggleZoomWithAnimation();
};


class InfoBalloon : public QWidget
{
    Q_OBJECT
public:
    explicit InfoBalloon(QWidget *parent = nullptr);
    ~InfoBalloon()
    {
      if(m_animation)
      {
        m_animation->deleteLater();
        std::cout<<"InfoBallon destrory called"<<std::endl;
      }
    }
    void showAt(const QPoint &pos, const QString &title, const QString &info);
    
protected:
    void paintEvent(QPaintEvent *event) override;
    
private:
    QLabel *titleLabel;
    QLabel *infoLabel;
    QVBoxLayout *layout;
    QPropertyAnimation *m_animation = nullptr;
};


class ChartController  : public QObject {
    Q_OBJECT
public:
  ChartController (QObject *parent = nullptr,
             QtCharts::QChartView *view = nullptr)
      : QObject(parent),m_view(view)
       {
    m_chartFeatureManager = new ChartFeatureManager();
    m_connectionManager = new ConnectionManager();
       }
       ~ChartController()
       {
        if(m_chartFeatureManager)
        {
          delete m_chartFeatureManager;
        }
        if(m_connectionManager)
        {
          delete m_chartFeatureManager;
        }
        std::cout<<"destroy chartController function call"<<std::endl;
       }
       QtCharts::QChartView *return_view_pointer() { return m_view; }

       //  Connection Manager
       ConnectionManager *m_connectionManager = nullptr;
       //  ChartView Manager
       ChartFeatureManager *m_chartFeatureManager = nullptr;
  private:
    QtCharts::QChartView *m_view = nullptr;
    QtCharts::QScatterSeries *m_scatterSeries = nullptr;
  };


class DeviceInformationPanel:public QWidget
{
    Q_OBJECT 
    public:
        DeviceInformationPanel(QWidget*parent = nullptr);
        ~DeviceInformationPanel () ;

        //  INTERNAL MENU
        Result<bool,RichError> update_panel();
        Result<bool,RichError> update_pointer_device_vector(std::vector <PLC_Device*> *tmp);

        //  ACCESSIBILITY MENU
        int inline_device_total = 0;
        int error_device_total = 0;
        int warning_device_total = 0;
        
        std::vector<PLC_Device*> *m_plc_device_pointer = nullptr;
        Result<bool,RichError> tmp_object = Result<bool,RichError>(true);
        QLabel *m_inline;
        QLabel *m_error;
        QLabel *m_warning;
        QPushButton *m_detailDisplay;
        QWidget *m_contentWidget;
        QVBoxLayout *m_contentLayout_V;
        QScrollArea *m_scrollArea;
        QVBoxLayout *m_laytout_V;
        QTextEdit *m_TextEdit;
    private slots:
        void update_detailErrorAndWarning(bool status);
    
};

// 2. 创建专门的数据处理器类
class DataProcessor : public IChartFeature {
    Q_OBJECT
    public:
      void attachTo(QtCharts::QChartView *chart) override {}

      void detach() override {}

    signals:
      // 原始JSON转换为结构化数据
      void dataParsed(const ChartData &chartData);
      // 错误信号
      void parseError(const QString &error);

    public slots:
      // 入口：接收原始JSON
      void processJsonData(const QJsonObject &jsonData,const QString &QueryName) {
        emit dataParsed(convertJsonToChartData(jsonData,QueryName));
    }
    
private:
    ChartData convertJsonToChartData(const QJsonObject &jsonData,const QString &QueryName); 
};

// 4. 图表渲染器类
class ChartRenderer : public IChartFeature{
    Q_OBJECT
public:
  void attachTo(QtCharts::QChartView *chart) override {
    m_infoBalloon = new InfoBalloon(nullptr);
    m_connectionManager = new ConnectionManager();
  }

  void detach() override {}

  void addList(QMap<QString,ChartController *>chartMap)
  {
    chartView_map = chartMap;
  }

  ~ChartRenderer()
  {
    if(m_infoBalloon)
    {
      delete m_infoBalloon;
    }
    if(m_connectionManager)
    {
      delete m_connectionManager;
    }

    std::cout<<"ChartRender destroy call"<<std::endl;
  }
  bool is_first = false;
signals:
    void chartUpdated();
    
public slots:
  void onDataParsed(const ChartData &chartData) {
      updateChartData(chartData);
}

  // 处理图表重建请求
  void onChartNeedsRebuild(const ChartData &chartData) {
    rebuildChart(chartData);
    emit chartUpdated();
  }
  // 处理图表更新请求
  void onChartNeedsUpdate(const ChartData &chartData) {
    updateChartData(chartData);
    emit chartUpdated();
  }

private:
  void rebuildChart(const ChartData &data) {
    if (data.isEmpty()) {
      qWarning() << "ChartManager::rebuildChart: Empty data provided";
      return;
    }

    for(auto i =chartView_map.begin() ; i != chartView_map.end() ; ++i)
    {
      m_chartView = i.value()->return_view_pointer();
      m_chart = m_chartView->chart();
      qDebug() << "-------clearAllSeries call before------";
      debugChartState();
      // 清除现有系列
      clearAllSeries();
      qDebug() << "-------clearAllSeries call after------";
      debugChartState();
      // 建立轴和图表之间的联系
      auto axis = createAxes();
      m_chart->addAxis(std::get<0>(axis), Qt::AlignBottom);
      m_chart->addAxis(std::get<1>(axis), Qt::AlignLeft);
      // 创建所有系列
      for (const TimeSeries &series : data) {
        try {
           auto lineSeries = createSeriesPair(series);
           auto scatterSeries = createScatterSeries();
           m_chart->addSeries(lineSeries);
           m_chart->addSeries(scatterSeries);
           // 将当前数据点存入map
           std::get<0>(m_seriesMap[series.name]) = lineSeries;
           std::get<1>(m_seriesMap[series.name]) = scatterSeries;
           qDebug()<<series.name<<" is save in m_seriesMap";
           qDebug() << "=== Chart Debug Info ===";
           qDebug() << "Chart series count:" << m_seriesMap.size();
           qDebug() << "Actual series in chart:" << m_chart->series().size();
           // 将X坐标数据映射到时间轴上
           lineSeries->attachAxis(std::get<0>(axis));
           scatterSeries->attachAxis(std::get<0>(axis));
           lineSeries->attachAxis(std::get<1>(axis)); 
           scatterSeries->attachAxis(std::get<1>(axis));
           // 设置chart_view和交互部件的关系
           addDataPoint(lineSeries, scatterSeries, series);
           setupInteractions(scatterSeries);
        } catch (const std::exception &e) {
          std::cout << "rebuild chart fail" << std::endl;
        }
    }
    // 设置图表的外观
    setupChartAppearance();
    }
  }

  void debugChartState() {
    if (!m_chart) {
      qDebug() << "Chart is null";
      return;
    }

    qDebug() << "=== Chart Debug Info ===";
    qDebug() << "Chart series count:" << m_seriesMap.size();
    qDebug() << "Actual series in chart:" << m_chart->series().size();

    // 列出所有系列
    int i = 0;
    for (auto series : m_chart->series()) {
      qDebug() << "  Series" << i++ << ":";
      qDebug() << "    Name:" << series->name();
      qDebug() << "    Pointer:" << series;
      qDebug() << "    In m_seriesMap?" << (m_seriesMap.find(series->name()) != m_seriesMap.end());

    }

    // 列出坐标轴
    qDebug() << "Chart axes count:" << m_chart->axes().size();
    for (auto axis : m_chart->axes()) {
      qDebug() << "  Axis:" << axis->titleText() << "pointer:" << axis;
    }

    qDebug() << "=== End Debug ===";
  }

 void  setupChartAppearance()
{
    // 配置图表外观
    m_chart->setTitle("test for prometheus");
    m_chart->setTitleFont(QFont("Arial", 14, QFont::Bold));
    m_chart->setAnimationOptions(QtCharts::QChart::SeriesAnimations);
    m_chart->setTheme(QtCharts::QChart::ChartThemeLight);
    m_chart->setBackgroundBrush(QBrush(QColor(240, 240, 240)));
    m_chart->setDropShadowEnabled(true);
    
  
}

  std::tuple<QtCharts::QDateTimeAxis *,
             QtCharts::QValueAxis *>
  createAxes() {
    auto axisX = new QtCharts::QDateTimeAxis ();
    auto axisY = new QtCharts::QValueAxis ();

    // 配置X轴
    axisX->setTitleText("time");
    axisX->setFormat("HH:mm");

    // 配置Y轴
    axisY->setTitleText("value");
    // 主刻度数量
    axisY->setTickCount(5);
    // 次刻度数量

    // 设置范围（稍后根据数据调整）
    QDateTime now = QDateTime::currentDateTime();
    axisX->setRange(now.addSecs(-300), now); // 默认显示最近1小时
    axisY->setRange(0, 100);                  // 默认范围

    return std::make_tuple(axisX,axisY);
  }

QtCharts::QLineSeries *
  createSeriesPair(const TimeSeries &series) {
    QtCharts::QLineSeries *lineSeries = new QtCharts::QLineSeries();
    lineSeries->setColor(series.color);
    lineSeries->setName(series.name);
    lineSeries->setUseOpenGL(true);

    return lineSeries;
  }

QtCharts::QScatterSeries *
  createScatterSeries() {
    // 创建散点系列用于显示数据点
    QtCharts::QScatterSeries *scatterSeries = new QtCharts::QScatterSeries();
    scatterSeries->setName("Data Points");
    scatterSeries->setMarkerSize(10);
    scatterSeries->setBorderColor(Qt::transparent);     // 透明边框
    scatterSeries->setColor(QColor(255, 99, 132, 150)); // 半透明红色

    return scatterSeries;
  }

  void addDataPoint(QtCharts::QLineSeries *lineSeries,
                    QtCharts::QScatterSeries *scatterSeries,
                    const TimeSeries &series) {
    // 填充数据点
    QVector<QPointF> points;
    points.reserve(series.points.size());
    // 遍历list，并依据里面的元素，创建坐标点
    for (const auto &point : series.points) {
      m_queue.push_back(point.timestamp);
      qreal xAsMs = static_cast<qreal>(point.timestamp*1000);
      points.append(QPointF(xAsMs, point.value));
    }
    // 实时替换图表上的数据点
    lineSeries->replace(points);
    scatterSeries->replace(points);
  }

  void updateChartData(const ChartData &data) {
    if (data.isEmpty()) {
      return;
    }
//  for (int i = 0; i < chartView_map.size(); ++i) {
  //       m_chartView = chartView_list[i]->return_view_pointer();
    for (auto it = chartView_map.begin(); it != chartView_map.end(); ++it) {
      m_chartView = it.value()->return_view_pointer();
      m_chart = m_chartView->chart();
      // 收集当前有效的系列ID
      QSet<QString> validSeriesIds;
      bool rangesNeedUpdate = false;
      // 更新或创建系列
      for (const TimeSeries &series : data) {
        validSeriesIds.insert(series.name);

        if (m_seriesMap.find(series.name) != m_seriesMap.end()) {
          // 更新现有系列
          if (updateExistingSeries(m_chart,series.name, series)) {
            rangesNeedUpdate = true;
          } else {
            std::cerr << "updateExistingSeries exist problem\n";
          }
        } else {
          qDebug()<<"seres.name: "<<series.name<<" can not find";
          // 创建新系列
          rebuildChart(data);
          rangesNeedUpdate = true;
        }
      }

      // 清理不再存在的系列
      // auto it = m_seriesMap.begin();
      // while (it != m_seriesMap.end()) {
      //   if (!validSeriesIds.contains(it->first)) {
      //     m_chart->removeSeries(std::get<0>(it->second));
      //     m_chart->removeSeries(std::get<1>(it->second));
      //     it = m_seriesMap.erase(it);
      //     rangesNeedUpdate = true;
      //   } else {
      //     ++it;
      //   }
      // }

    }
  }

bool updateExistingSeries(const QtCharts::QChart *m_chart,const QString &seriesId, const TimeSeries &series)
{
    auto it = m_seriesMap.find(seriesId);
    if (it == m_seriesMap.end()) {
        return false;
    }

    {
      // 更新数据点
      QVector<QPointF> points;
      points.reserve(series.points.size());
      // 获取子列表（最后20个元素）
      int totalSize = series.points.size();
      int elementsToShow = qMin(totalSize, 20);
      QList<DataPoint> sublist = series.points.mid(qMax(0, totalSize - 20));
      // qDebug() << "Showing" << elementsToShow << "of" << totalSize
      //          << "elements:";
      int startIndex = qMax(0, totalSize - 20);
      if(totalSize>20) 
      {
        std::get<0>(it->second)->remove(0, startIndex);
        std::get<1>(it->second)->remove(0, startIndex);
      }

      int max_value=0;
      int min_value=0;
      for (int idx = 0; idx < sublist.size(); ++idx) {
        const auto &point = sublist[idx];
        if (std::isnan(point.value)) {
          // 跳过NaN点，让图表显示缺口
          continue;
        }
        qreal xAsMs = static_cast<qreal>(point.timestamp*1000);
       
        points.append(QPointF(xAsMs, point.value));

        if (max_value < point.value) {
          max_value = point.value;
        }
        if (min_value > point.value) {
          min_value = point.value;
        }
      }
      std::get<0>(it->second)->replace(points);
      std::get<1>(it->second)->replace(points);

      QDateTime now = QDateTime::currentDateTime();
      QList<QtCharts::QAbstractAxis *> xaxes = m_chart->axes(Qt::Horizontal);
      QList<QtCharts::QAbstractAxis *> yAxes = m_chart->axes(Qt::Vertical);
    
      if (!xaxes.isEmpty()) {
        // 获取第一个水平轴（通常是主X轴）
        if (auto *valueAxis =
                qobject_cast<QtCharts::QDateTimeAxis *>(xaxes.first())) {
          valueAxis->setRange(now.addSecs(-300), now); // X轴范围 0-100
        }
      }

      if (!yAxes.isEmpty()) {
        if (auto *valueAxis =
                qobject_cast<QtCharts::QValueAxis *>(yAxes.first())) {
          valueAxis->setRange(min_value-30, max_value+30);
        }
      }
      
      return true;
    }
}

void clearAllSeries() {
    // 先断开所有信号连接
    for (auto series : m_chart->series()) {
        series->disconnect();
    }
    
    // 从图表中移除所有系列
    auto seriesList = m_chart->series();  // 获取副本
    for (auto series : seriesList) {
        m_chart->removeSeries(series);
        delete series;  // 删除所有系列，无论是否在 m_seriesMap 中
    }
    
    // 清空管理容器
    m_seriesMap.clear();
    m_queue.clear();
    
    // 清理图表上现有的坐标轴
    auto axesList = m_chart->axes();  // 获取副本
    for (auto axis : axesList) {
        qDebug() << "Removing existing axis:" << axis->titleText();
        axis->disconnect();
        m_chart->removeAxis(axis);
        delete axis;
    }
    
    qDebug() << "clear Series SingleCharts is over";
}

void validateComponents(QtCharts::QScatterSeries *scatterSeries) {
  qDebug() << "=== Component Validation ===";

  // 1. 检查指针是否有效
  qDebug() << "m_view is" << (m_chartView ? "valid" : "NULL");
  qDebug() << "m_scatterSeries is" << (scatterSeries ? "valid" : "NULL");

  if (m_chartView) {
    // 2. 检查视图的chart
    QtCharts::QChart *chart = m_chartView->chart();
    qDebug() << "Chart is" << (chart ? "valid" : "NULL");

    if (chart) {
      // 3. 检查系列是否附加到chart
      QList<QtCharts::QAbstractSeries *> seriesList = chart->series();
      qDebug() << "Number of series in chart:" << seriesList.size();

      bool seriesFound = false;
      //  for (auto *series : seriesList) {
      //    qDebug() << "Series type:" << typeid(*series).name();
      //    if (series == m_scatterSeries) {
      //      seriesFound = true;
      //      qDebug() << "Our scatterSeries IS attached to chart";
      //    }
      //  }
      if (!seriesFound) {
        qDebug() << "ERROR: scatterSeries NOT attached to chart!";
      }
    }
  }

  qDebug() << "=== Validation Complete ===\n";
}

void setupInteractions(QtCharts::QScatterSeries *scatterSeries) {
  // 1. 验证组件
  validateComponents(scatterSeries);
  if (!m_chartView) {
    qDebug() << "ERROR: m_view is null!";
    return;
  }
  // 2. 启用鼠标跟踪
  m_chartView->setMouseTracking(true);
  // 增加点击区域检测
  {
    // 获取所有点的位置
    QVector<QPointF> points = scatterSeries->pointsVector();
    qDebug() << "=== Point Positions ===";
    for (int i = 0; i < points.size(); ++i) {
      qDebug() << "Point" << i << ":" << points[i];
      // 转换为像素坐标（用于验证点击位置）
      if (m_chartView && m_chartView->chart()) {
        QPointF scenePos = m_chartView->chart()->mapToPosition(points[i]);
        qDebug() << "  Scene pixel position:" << scenePos;
      }
    }

    scatterSeries->setMarkerSize(20);
    scatterSeries->setColor(Qt::blue);
    scatterSeries->setBorderColor(Qt::yellow);
    // 检查点的可视化属性
    qDebug() << "Marker size:" << scatterSeries->markerSize();
    qDebug() << "Marker shape:" << scatterSeries->markerShape();
    qDebug() << "Brush color:" << scatterSeries->brush().color();
    qDebug() << "Pen color:" << scatterSeries->pen().color();

    // 确保点足够大以便点击
    if (scatterSeries->markerSize() < 10.0) {
      qDebug() << "WARNING: Marker size is small, increasing to 15.0 "
                  "for better clicking";
      scatterSeries->setMarkerSize(15.0);
    }
  }

  // 最简单的做法：只连接单参数信号，用 QCursor::pos() 获取位置
  m_connectionManager->safeConnect(
      scatterSeries, &QtCharts::QScatterSeries::clicked, 
      [this, scatterSeries](const QPointF &point) {
        QPoint screenPos = QCursor::pos();
        showPointInfo(point, screenPos, scatterSeries);
      });

  m_connectionManager->safeConnect(
      scatterSeries, &QtCharts::QScatterSeries::hovered,
      [this, scatterSeries](const QPointF &point, bool status) {
        QTimer::singleShot(0, [this, status, scatterSeries]() {
          scatterSeries->setMarkerSize(status ? 10 * 2 : 10);
        });
      });
}

void showPointInfo(const QPointF &point, const QPoint &screenPos,
                   QtCharts::QScatterSeries *series) {
  if (!series)
    return;

  // 坐标转换（如果是时间序列）
  QDateTime time = QDateTime::fromMSecsSinceEpoch(point.x());
  QString title = QString("Data Point - %1").arg(series->name());
  QString info = QString("Series: %1\n"
                         "Time: %2\n"
                         "Value: %3\n")
                     .arg(series->name())
                     .arg(time.toString("yyyy-MM-dd hh:mm:ss"))
                     .arg(point.y(), 0, 'f', 2);

  m_infoBalloon->showAt(screenPos, title, info);
}

public:
ConnectionManager *m_connectionManager = nullptr;

private:
QList<ChartController *> chartView_list;
QMap<QString,ChartController *> chartView_map;
QtCharts::QChartView *m_chartView = nullptr;
QtCharts::QChart *m_chart = nullptr;
QtCharts::QDateTimeAxis *m_axisX = nullptr;
QtCharts::QValueAxis *m_axisY = nullptr;
std::unordered_map<
    QString, std::tuple<QtCharts::QLineSeries *, QtCharts::QScatterSeries *>>
    m_seriesMap;
std::deque<qint64> m_queue;

InfoBalloon *m_infoBalloon = nullptr;
};


class DeviceListPanel:public QWidget 
{
    public:
        explicit DeviceListPanel(QWidget* parent=nullptr);
        
        //  INTERNAL MENU
        Result<bool,RichError> update_pointer_device_vector(std::vector<PLC_Device*> *pointer);
        Result<bool,RichError> build_connection_deviceLabel_and_GridLayout();
        Result<bool,RichError> update_devicePanel();
        
    private:
    std::vector<QLabel*> m_label_vector;
    std::unordered_map<PLC_Device*,std::pair<size_t,size_t>> m_device_pos_map;

    std::vector<PLC_Device*> *m_plc_device_pointer = nullptr;
    Result<bool,RichError> tmp_object = Result<bool,RichError>(true);

    const size_t max_per_row = 4;
    size_t current_row = 0 ;
    size_t current_line = 0 ;

    QGridLayout *m_mainlayout_Grid;
};

class DeviceMonitorWidget: public QWidget
{
    Q_OBJECT
    public:
        explicit DeviceMonitorWidget(QMainWindow* parent = nullptr);
        ~DeviceMonitorWidget();

        //  INTERNAL MENU
        Result<bool,RichError> build_original_connet(std::vector<PLC_Device*> *pointer);
        void update_position_extern_left();

        Result<bool,RichError> update_device_ptr_vector(std::vector<PLC_Device*> *pointer);
        Result<bool,RichError> update_widget();
        //  ACCESIBILITY MENU
        bool eventFilter(QObject *obj, QEvent *event);

        QDockWidget *m_device_dock;
        QMainWindow *m_main_windows;
    private:
        QVBoxLayout *m_main_layout_V;
        DeviceInformationPanel *m_deviceInformation;
        DeviceListPanel *m_deviceList = nullptr;
};

class MonitoringDashboard : public QMainWindow {
    Q_OBJECT
public:
    MonitoringDashboard(QWidget *parent = nullptr){
    };
    ~MonitoringDashboard()
    {
     std::cout<<"MoniotrDashboard destory called"<<std::endl;
    }

    void setGrafanaClient(GrafanaClient *client)
    {
      this->grafanaClient = client;
      ObjectRouter::instance().registerObject("config://dashboard/server_01",grafanaClient);
    };
    GrafanaClient *return_GrafanaClient()
    {
      return this->grafanaClient;
    }

     // 实时监控页
    QWidget * createRealtimePage();
    QWidget *createStatusBar();
    QtCharts::QChartView *createMetricCard(const QString &title,
                                           const QString &query,
                                           const QColor &color,QWidget *chartSplitter);
    QtCharts::QChartView *createInteractiveChart(const QString &title);
    QWidget *createControlPanel();
    // void updateRequestTrendChart();

    // 历史分析页
    QWidget *createHistoricalPage();

    // 告警管理页
    QWidget *createAlertsPage();
    
    // 系统状态页
    QWidget *createSystemPage();
    
    // 定义查询字符串
    QString buildP95Query(const QString &handler) {
      return QString("histogram_quantile(0.95, "
                     "rate(prometheus_http_request_duration_seconds_bucket{"
                     "handler=\"%1\"}[5m]))")
          .arg(handler);
    }

    //  Connection Manager
    ConnectionManager *m_connectionManager = nullptr;
    //  ChartView Manager
    ChartFeatureManager *m_chartFeatureManager = nullptr;
    //  容器列表
    QList<ChartController *> m_list;
    QMap<QString,ChartController *> m_map;


  private:
    // 核心组件
    QTabWidget *tabWidget;
    QComboBox *timeRangeCombo ;
    QSpinBox *intervalSpin ;
    QComboBox *chartTypeCombo ;
    QCheckBox *autoRefresh;

    //  Data operation logic
    DataProcessor *dataProcessor;
    // UI External operation logic
    AnimatedZoomChartView *charViewtZoom;
    // UI Internal operation logic 
    ChartRenderer *chartRenderer;

    // 数据源
    GrafanaClient *grafanaClient;
signals:
  void rawDataReceived(const QJsonObject &jsonData);

public slots:
  void onCurrentTimeRangeChange();
  void onUpdateIntervalChanged();
  void clicked();
  void onAutoRefreshToggled();
  void onRefreshAllData();
  void onChartUpdated();
};

#endif