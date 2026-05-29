#pragma once

#include "load_config/Qt_library.h"
#include "GrafanaDashboardManager/ObjectRouter.h"
#include <atomic>
#include <iostream>
#include <queue>

struct LayoutState {
  QLayout *layout = nullptr;
  int index = -1;
  int stretch = 0;
  QLayoutItem *item = nullptr;
  Qt::Alignment alignment = Qt::Alignment();

  bool isValid() const { return layout != nullptr && index >= 0; }
};

struct chartData_update
{
  qreal min_timeStamp = 0;
  qreal max_timeStamp = 0;
  float min_value = 0;
  float max_value = 0;
};

struct QueryInfo
{
  QString Query{};
  bool isValid = true;
};

class LineChart;
class dataChart;
class Scope;
class RequestSender;
class PlainNumberDelegate;
class OptionDial;

class MyModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    struct DataItem {
        QString key;
        QPointF point;
        int pointIndex;
    };

    explicit MyModel(QObject *parent = nullptr, Scope *scope = nullptr);
    ~MyModel() = default;

    // 数据访问接口
    QVector<QPointF> getData(const QString &Query) const;
    void addChartPointData(const ChartData &chartData);
    void addDataPoint(const TimeSeries &series, QVector<QPointF> &m_data);
    chartData_update *return_chartData();
    
    // 数据追加接口
    void appendData(const QString &key, const QPointF &newPoint);

    // QAbstractItemModel 接口实现
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void toggleXColumnFormat();

    // 辅助方法
    DataItem getDataItem(const QModelIndex &index) const;
    bool isValidIndex(const QModelIndex &index) const;

public slots:
    void onTimerTimeout();

signals:
    void dataChanged(const QString &Query, bool isReason);  // 通知视图更新

protected:
    void performDataUpdate();

private:
    QMap<QString, QVector<QPointF>> m_dataMap;
    mutable QReadWriteLock m_lock;           // 读写锁
    chartData_update update_data;
    std::shared_ptr<QSet<dataChart *>> m_dataChartSet = nullptr;
    Scope *m_scope = nullptr;
};
//  custom define class need declare like this 
Q_DECLARE_METATYPE(MyModel::DataItem)

class LegendIconButton : public QWidget {
    Q_OBJECT
public:
    LegendIconButton(QWidget *parent = nullptr,QAbstractSeries *series = nullptr,Scope *scope = nullptr)
        : QWidget(parent), m_series(series),m_scope(scope){
      parentWidget_pointer = parent;
      setupUI();
      updateStyle(false);
      addScopeObject();
    }

    void addScopeObject()
    {
      if(m_scope)
      {
          {
            auto orderService = m_scope->getShared<QSet<QString>>();
            if(orderService)
            {
              this->selectedLegends = orderService;
            }
          }
          {
            auto orderService = m_scope->getShared<QMap<QString, LegendIconButton *>>();
            if (orderService) {
              this->legendMarkerMap = orderService;
            }
          }
          {
            auto orderService =
                m_scope->getShared<QSet<QtCharts::QScatterSeries *>>();
            if (orderService) {
              this->seriesSets = orderService;
            }
        }
      }
    }

    QPushButton *return_PushButton()
    {
      return this->m_visibilityButton;
    }

    void UpdateLegendIconButton(bool Current_Button_PushDown)
    {
      this->updateStyle(Current_Button_PushDown);
      m_seriesVisible = !Current_Button_PushDown;
      m_series->setVisible(!Current_Button_PushDown);
    }

    QString return_QueryName() {
      if (QueryName != "") {
        return QueryName;
      } else {
        return "";
      }
    }

private:
    QAbstractSeries *m_series;
    QPushButton *m_visibilityButton;
    QLabel *m_colorIcon;
    QLabel *m_textLabel;
    QString QueryName = "";
    QWidget *parentWidget_pointer;
    
    bool m_seriesVisible = true;
    Scope *m_scope;
    std::shared_ptr<QSet<QString>> selectedLegends; // 存储选中的图例名称
    std::shared_ptr<QMap<QString, LegendIconButton *>>
        legendMarkerMap; // 系列名称到图例标记的映射
    std::shared_ptr<QSet<QtCharts::QScatterSeries *>> seriesSets;

  private:

    void updateStyle(bool Current_Button_PushDown) {
      QColor seriesColor = getSeriesColor(Current_Button_PushDown);

      // 更新颜色图标
      QString iconStyle = QString("background-color: %1;"
                                  "border: 1px solid %2;"
                                  "border-radius: 8px;")
                              .arg(seriesColor.name())
                              .arg(seriesColor.darker(150).name());
      m_colorIcon->setStyleSheet(iconStyle);

      // 更新文本样式
      QString textStyle = QString("color: %1;"
                                  "font-weight: %2;")
                              .arg(m_seriesVisible ? "#000000" : "#999999")
                              .arg(m_seriesVisible ? "bold" : "normal");
      m_textLabel->setStyleSheet(textStyle);
    }

    void setupUI() {
        QHBoxLayout *layout = new QHBoxLayout(this);
        layout->setContentsMargins(5, 2, 5, 2);
        layout->setSpacing(8);
        
        // 颜色图标
        m_colorIcon = new QLabel();
        m_colorIcon->setFixedSize(16, 16);
        
        // 文本标签
        m_textLabel = new QLabel(formatSeriesName(m_series->name()));
        m_textLabel->setFont(QFont("Consolas", 10));
        m_textLabel->setCursor(Qt::PointingHandCursor);
        m_textLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

         // 可见性切换按钮
        m_visibilityButton = new QPushButton("👁");
        m_visibilityButton->setFixedSize(24, 24);
        m_visibilityButton->setCheckable(true);
        m_visibilityButton->setChecked(true);
        m_visibilityButton->setCursor(Qt::PointingHandCursor);
        m_visibilityButton->setVisible(false);

        layout->addWidget(m_colorIcon);
        layout->addWidget(m_textLabel, 1); // 1 表示拉伸因子
        layout->addWidget(m_visibilityButton);

        //  QueryName Update
        QueryName = m_series->name();

        // 设置整体样式
        setStyleSheet("DualStateLegendButton {"
                      "   border: 1px solid #d4d4d4;"
                      "   border-radius: 4px;"
                      "   background-color: #f8f9fa;"
                      "}"
                      "DualStateLegendButton:hover {"
                      "   background-color: #e9ecef;"
                      "}");

        // 安装事件过滤器到文字标签
        m_textLabel->installEventFilter(this);

        // 连接信号
        connect(m_visibilityButton, &QPushButton::toggled, this,
                [this](){
                  {
                    onRequestTotalLegendClicked(this);
                  }
                });

        // 整体点击事件
        setMouseTracking(true);
    }

    QString formatSeriesName(const QString &original) {
        QString handler = extractValue(original, "handler");
        QString le = extractValue(original, "le");
        
        if (!handler.isEmpty() && !le.isEmpty()) {
            QString displayHandler = simplifyHandler(handler);
            QString displayLe = (le == "+Inf") ? "∞" : le;
            return QString("%1 (≤ %2)").arg(displayHandler).arg(displayLe);
        }
        
        return original;
    }

    QString extractValue(const QString &text, const QString &key) {
        QRegularExpression regex(QString("%1=\"([^\"]+)\"").arg(key));
        QRegularExpressionMatch match = regex.match(text);
        return match.hasMatch() ? match.captured(1) : QString();
    }

    QString simplifyHandler(const QString &handler) {
        if (handler == "/assets/*filepath") return "Assets";
        if (handler == "/favicon.svg") return "Favicon";
        return handler;
    }

    QColor getSeriesColor(bool Current_Button_PushDown) {
      if(Current_Button_PushDown)
      {
        //  status will from no push -> push down
        return Qt::gray;
      }

      //  status will from push down -> orignal color
      if (QLineSeries *lineSeries = qobject_cast<QLineSeries *>(m_series)) {
        return lineSeries->pen().color();
      } else if (QScatterSeries *lineSeries =
                     qobject_cast<QScatterSeries *>(m_series)) {
        return lineSeries->pen().color();
      }
      else
      {
        qDebug()<<"there is error in series type ";
        return Qt::black;
      }
    }

    void onRequestTotalLegendClicked(LegendIconButton *item) {
      if (!item) {
        // 获取发送信号的标记
        qDebug() << "item is nullptr";
      }
      // 获取键盘修饰符
      Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
      // 处理点击
      handleMarkerClick(item, modifiers);
    }

    void handleMarkerClick(LegendIconButton *clickedMarker,
                           Qt::KeyboardModifiers modifiers) {
      if (!clickedMarker)
        return;

      QString clickedSeriesName = clickedMarker->return_QueryName();
      if (!clickedSeriesName.isEmpty()) {
        qDebug() << "Found key:" << clickedSeriesName;
      } else {
        qDebug() << "Button not found in map";
        return;
      }

      if (modifiers & Qt::ControlModifier) {
        // Ctrl + 点击：切换选择状态
        if (selectedLegends->contains(clickedSeriesName)) {
          // 如果已经选中，则取消选中
          selectedLegends->remove(clickedSeriesName);
          LegendIconButtonAccordingTheStatusExchangeIts(clickedMarker, true);
        } else {
          // 未选中，则添加到选中集合
          selectedLegends->insert(clickedSeriesName);
          LegendIconButtonAccordingTheStatusExchangeIts(clickedMarker, false);
        }
      } else {
        // 普通点击：单选模式
        if (selectedLegends->contains(clickedSeriesName)) {
          if (selectedLegends->size() == 1) {
            //  display all item
            for (auto &it : *legendMarkerMap) {
              if (it->return_QueryName() == clickedSeriesName) {
                continue;
              } else {
                LegendIconButtonAccordingTheStatusExchangeIts(it, false);
                selectedLegends->insert(it->return_QueryName());
              }
            }
          } else {
            //  just display selected item
            CancelDisplaySpecialButton(clickedSeriesName);
            selectedLegends->clear();
            selectedLegends->insert(clickedSeriesName);
            LegendIconButtonAccordingTheStatusExchangeIts(
                legendMarkerMap->value(clickedSeriesName), false);
          }
        } else {
          //  if the selectSet do not contain the selectMarkt
          // just display selected item
          CancelDisplaySpecialButton(clickedSeriesName);
          selectedLegends->clear();
          selectedLegends->insert(clickedSeriesName);
          LegendIconButtonAccordingTheStatusExchangeIts(
              legendMarkerMap->value(clickedSeriesName), false);
        }
      }

    }

    void CancelDisplaySpecialButton(const QString &clickedSeriesName) {
      for (auto &it : *selectedLegends) {
        {
          LegendIconButton *Button = legendMarkerMap->value(it);
          LegendIconButtonAccordingTheStatusExchangeIts(Button,true);
        }
      }
    }

    void LegendIconButtonAccordingTheStatusExchangeIts(
        LegendIconButton *clickedMarker, bool Current_Button_PushDown) {
      if (clickedMarker) {
        clickedMarker->UpdateLegendIconButton(Current_Button_PushDown);
      } else {
        qDebug() << "LegendIconButtonAccordingTheStatusExchangeIts find "
                    "clickerMarker is nullptr";
      }
      return;
    }


protected:
    void mousePressEvent(QMouseEvent *event) override {
        // 点击整个项时切换可见性
        if (event->button() == Qt::LeftButton) {
            m_visibilityButton->toggle();
        }
        QWidget::mousePressEvent(event);
    }

    void enterEvent(QEvent *event) override {
        setCursor(Qt::PointingHandCursor);
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override {
        setCursor(Qt::ArrowCursor);
        QWidget::leaveEvent(event);
    }
};

class RequestSender : public QObject
{
    Q_OBJECT

public:
    explicit RequestSender(QObject *parent = nullptr, Scope *scope = nullptr);
    ~RequestSender();

    void createTimer();
    void onTimeout(const QString &Query);
    void addSeriesIntoSet(const QString &Query);
    void deleteSerieFromSet(const QString &Query);
    void changeTimer(int value);

private:
    QTimer *m_updateTimer = nullptr;
    Scope *m_scope = nullptr;
    std::shared_ptr<QSet<dataChart *>> m_dataChartSet;
    QSet<QString> m_QuerySet;
};

// 缩放控制器
class ZoomControllerBuilder : public QObject {
  Q_OBJECT
public:
    void attachTo()  {
        //  安装事件过滤器等
        m_monitored->setFocusPolicy(Qt::StrongFocus);
        m_monitored->installEventFilter(this);
        m_monitored->setMouseTracking(true);
        //  初始化动画
        this->setupAnimation();
        //  初始化定时器
        m_focusTimer = new QTimer(this);
        m_focusTimer->setSingleShot(true);
        //  1秒后清除焦点
        m_focusTimer->setInterval(300); 

        connect(m_focusTimer, &QTimer::timeout, this,
                &ZoomControllerBuilder::onFocusTimeout);
    }

    // 启用/禁用缩放功能
    void enableZoom(bool enable = true){
      m_enabled = enable;
    }

    void toggleZoom() {
      if (!m_monitored)
        return;

      if (!m_isZoomed) {
        saveLayoutState();
        startZoom();
        scrollToVerticalPosition(Index0WidgetPos.y());
      } else {
        startUnzoom();
        //  put the operation of restore behind done of unzoom
        QTimer::singleShot(200, this, [this]() {
          restoreLayoutState();
          scrollToVerticalPosition(monitorWidgetPos.y());
        });
      }
    }

    void saveLayoutState() {
      if (!m_monitored || !m_monitored->parentWidget())
        return;

      QWidget *parent = m_monitored->parentWidget();
      QLayout *layout = parent->layout();

      if (!layout)
        return;

      // 清空之前的状态
      m_layoutState = LayoutState();
      m_layoutRestored = false;
      
      // 遍历布局找到monitored的位置
      for (int i = layout->count()-1; i >= 0; --i) {
        QLayoutItem *item = layout->itemAt(i);
        if(i==0)
        {
          this->Index0WidgetPos = item->geometry().topLeft();  
        }

        // 检查是否是直接管理的widget
        if (item->widget() == m_monitored) {
          m_layoutState.layout = layout;
          m_layoutState.index = i;
          m_layoutState.item = item;
          m_layoutState.alignment = item->alignment();

          // 获取拉伸因子（如果是QBoxLayout）
          QBoxLayout *boxLayout = qobject_cast<QBoxLayout *>(layout);
          if (boxLayout) {
            m_layoutState.stretch = boxLayout->stretch(i);
          }

          // 获取位置信息（如果是QGridLayout）
          QGridLayout *gridLayout = qobject_cast<QGridLayout *>(layout);
          if (gridLayout) {
            int row, col, rowSpan, colSpan;
            gridLayout->getItemPosition(i, &row, &col, &rowSpan, &colSpan);
            // 可以保存这些信息，但这里简化处理
          }

          //  save this widget pos for restore this layout
          monitorWidgetPos = item->geometry().topLeft();
       
          // remove this widget for avoiding mannage by the parent layout
          layout->removeWidget(m_monitored);
        }
        else
        {
          item->widget()->hide();
        }
      }
    }

    void insertIntoLayout() {
      if (!m_monitored || !m_layoutState.isValid() || m_layoutRestored)
        return;

      QLayout *layout = m_layoutState.layout;
      int index = m_layoutState.index;

      //  display hided widget in this parent layout
      for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem *item = layout->itemAt(i);

        // 检查是否是直接管理的widget
        if (item->widget() == m_monitored) {
          continue;
        } else {
          item->widget()->show();
        }
      }

      // 根据布局类型重新插入
      QBoxLayout *boxLayout = qobject_cast<QBoxLayout *>(layout);
      if (boxLayout) {
        // 插入到QBoxLayout的指定位置
        boxLayout->insertWidget(index, m_monitored, m_layoutState.stretch,
                                m_layoutState.alignment);
      } else {
        QGridLayout *gridLayout = qobject_cast<QGridLayout *>(layout);
        if (gridLayout) {
          // 对于QGridLayout，需要知道原始的行列位置
          // 这里简化处理，直接添加到末尾
          gridLayout->addWidget(m_monitored);
        } else {
          // 其他布局类型
          layout->addWidget(m_monitored);
        }
      }

      m_layoutRestored = true;
    }

    void restoreLayoutState() {
      if (!m_monitored || !m_layoutState.isValid() || m_layoutRestored)
        return;

      insertIntoLayout();

      // 确保Widget恢复原始大小
      if (!m_isZoomed) {
        m_monitored->setGeometry(m_originalGeometry);
      }

      // 触发布局更新
      if (m_monitored->parentWidget()) {
        m_monitored->parentWidget()->layout()->update();
        m_monitored->parentWidget()->adjustSize();
      }
    }

    void scrollToVerticalPosition(int y) {
      // 直接设置垂直滚动条的值
      m_scrollArea->verticalScrollBar()->setValue(y);
    }

    void startZoom() {
      if (!m_monitored || !m_watcher || m_animating)
        return;

      // 保存原始位置
      m_originalGeometry = m_monitored->geometry();
      QScrollArea *scrollAreaF = qobject_cast<QScrollArea *>(m_watcher);
      if(!m_scrollArea)
      {
        m_scrollArea = scrollAreaF;
      }
      QPoint visibleTopLeft =
          QPoint(scrollAreaF->horizontalScrollBar()->value(),
                 scrollAreaF->verticalScrollBar()->value());

      {
        QSize zoomSize{m_watcher->size().width() - 40,
                       m_watcher->size().height()};
        m_zoomedGeometry = QRect(Index0WidgetPos, // 左上角坐标
                                 zoomSize        // 保持原有大小
        );
      }
      // 开始放大动画
      startAnimation(m_originalGeometry, m_zoomedGeometry, true);
    }

    void startUnzoom() {
      if (!m_monitored || !m_watcher || m_animating)
        return;

      // 开始缩小动画（从当前位置回到原始位置）
      startAnimation(m_monitored->geometry(), m_originalGeometry, false);
    }

    void startAnimation(const QRect &from, const QRect &to,
                        bool targetZoomState) {
      m_fromGeometry = from;
      m_toGeometry = to;
      m_targetZoomState = targetZoomState;

      m_animating = true;
      m_elapsedTimer.start();
      m_animationTimer.start();

      // 第一帧立即更新
      updateAnimation();
    }

    // 获取当前状态
    bool isZoomed() const { return m_isZoomed; }

    //  添加放缩对象
    void setWidget(QWidget *watcher, QWidget *monitor) {
      m_watcher = watcher;
      m_monitored = monitor;
    }

  protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
      if (obj != m_monitored || !m_enabled) {
        return QObject::eventFilter(obj, event);
      }

      switch (event->type()) {
      case QEvent::Enter:
        m_monitored->setFocus();
        applyZoomStyle(true);
        if (m_focusTimer->isActive()) {
          m_focusTimer->stop();
        }
        break;

      case QEvent::Leave:
        m_focusTimer->start();
        break;

      case QEvent::MouseButtonPress:
      {
        m_monitored->setFocus();
        if (m_focusTimer->isActive()) {
          m_focusTimer->stop();
        }
       
        break;
      }

      case QEvent::KeyPress: {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (m_monitored->hasFocus() && keyEvent->key() == Qt::Key_V &&
            keyEvent->modifiers() == Qt::NoModifier) {
          toggleZoom();
          return true; // 事件已处理
        } else if (keyEvent->key() == Qt::Key_Escape && m_isZoomed) {
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

  private slots:
    void onAnimationFinished(){qDebug()<<"onAnimationFinished called";}
    void onFocusTimeout() {
      if (m_monitored && !m_monitored->underMouse()) {
        m_monitored->clearFocus();
        applyZoomStyle(false);
      }
    }
    void updateAnimation() {
      if (!m_monitored || !m_animating) {
        return;
      }

      // 计算动画进度 (0.0 到 1.0)
      qint64 elapsed = m_elapsedTimer.elapsed();
      float progress = qMin(float(elapsed) / m_duration, 1.0f);

      // 应用缓动曲线
      float eased = m_easing.valueForProgress(progress);

      // 线性插值计算当前位置
      QRect currentRect;
      currentRect.setX(m_fromGeometry.x() +
                       (m_toGeometry.x() - m_fromGeometry.x()) * eased);
      currentRect.setY(m_fromGeometry.y() +
                       (m_toGeometry.y() - m_fromGeometry.y()) * eased);
      currentRect.setWidth(m_fromGeometry.width() +
                           (m_toGeometry.width() - m_fromGeometry.width()) *
                               eased);
      currentRect.setHeight(m_fromGeometry.height() +
                            (m_toGeometry.height() - m_fromGeometry.height()) *
                                eased);

      // 应用新位置
      m_monitored->setGeometry(currentRect);

      // 强制立即重绘
      m_monitored->update();

      // 检查动画是否完成
      if (progress >= 1.0f) {
        m_animationTimer.stop();
        m_animating = false;
        m_isZoomed = m_targetZoomState;

        // 确保最终位置精确
        m_monitored->setGeometry(m_toGeometry);

        // emit finished();
      }
    }

  private:
    void setupAnimation() {
      // 连接定时器
      connect(&m_animationTimer, &QTimer::timeout, this,
              &ZoomControllerBuilder::updateAnimation);
      m_animationTimer.setInterval(16); // 约60fps
  }
  void applyZoomStyle(bool hasFocus) {
    if (!m_monitored)
      return;
    if (hasFocus) {
      m_monitored->setStyleSheet("border: 3px solid #4CAF50;"
                             "border-radius: 5px;"
                             "background-color: #F1F8E9;");
    } else {
      m_monitored->setStyleSheet("");
    }
  }

  // QPropertyAnimation *m_animation = nullptr; // 动画
  QTimer *m_focusTimer = nullptr;            // 焦点定时器

  bool m_enabled = true;
  bool m_isZoomed = false;// 动画结束时的目标状态
  bool m_animating = false;
  bool m_targetZoomState = false; // 动画结束时的目标状态

  QRect m_originalGeometry;       // 原始几何位置
  QRect m_zoomedGeometry;         // 放大后的几何位置
  QRect m_parentOriginalGeometry; // 父窗口原始位置（可选）

  LayoutState m_layoutState;
  bool m_layoutRestored = false;

  // 动画控制
  QTimer m_animationTimer;
  QElapsedTimer m_elapsedTimer;
  int m_duration = 300; // 默认300ms
  QEasingCurve m_easing = QEasingCurve::OutCubic;

  // 动画插值数据
  QRect m_fromGeometry;
  QRect m_toGeometry;

  QWidget *m_monitored = nullptr;
  QWidget *m_watcher =nullptr;
  QScrollArea *m_scrollArea = nullptr;
  QPoint Index0WidgetPos{0,0};
  QPoint monitorWidgetPos{0,0};
};

class ContainerWidget : public QWidget {
    Q_OBJECT
public:
    explicit ContainerWidget(QWidget *parent = nullptr) : QWidget(parent) {
      m_layout = new QVBoxLayout(this);
      m_layout->setSpacing(10);  // 子控件之间的间距
      m_layout->setContentsMargins(0, 0, 0, 0);
      // 2. 设置大小策略（允许拉伸）
      this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
      this->setMinimumSize(200, 200); // 最小限制
    }

    void AddSonWidget(QWidget *item)
    {
      if(item)
      {
        item->setParent(this);
        m_layout->addWidget(item);
        m_map.insert(item,true);
      }
    }

    void AddEventFilter(QWidget *item) {
      if (item) {
        m_QuerySet.insert(item);
      }
    }

    void ChangeMinmumSize()
    {
      QSize total_size{0,0};
      for (int i = 0; i < m_layout->count(); ++i) {
        QLayoutItem *item = m_layout->itemAt(i);
        QWidget *childWidget = item->widget();
        if (childWidget) {
          total_size.setHeight(total_size.height()+childWidget->height());
        }
      }
      this->setMinimumSize(total_size.width(), total_size.height()+50); // 最小限制
    }

    QSize return_suitableSize(QSize &par) {
      QSize value(0, 0);
      if (this->sizeHint().height() > this->minimumHeight()) {
        value.setHeight(this->sizeHint().height());
      } else {
        value.setHeight(this->minimumHeight());
      }

      if (this->sizeHint().width() > this->minimumWidth()) {
        value.setWidth(this->sizeHint().width());
      } else {
        value.setWidth(this->minimumWidth());
      }

      if (value.height() > par.height()) {
      } else {
        value.setHeight(par.height());
      }

      if (value.width() > par.width()) {
      } else {
        value.setWidth(par.width());
      }

      return value;
    }

    bool whehterRestore(QWidget *item,QSize &&par)
    {
      //   check whether the item size < QScrollArea Size
      if (item->size().height() < par.height() &&item->size().width()<par.width()) {
        m_map[item]=true;
      } else {
        m_map[item]= false;
        return false;
      }
      //  need all item size < QScrollArea size , and then reset Widget E size
      for(auto &it:m_map)
      {
        if(it)
        {
          continue;
        }
        else
        {
          return false;
        }
      }
      return true;
    }

    void setmaxSizeSonWidget(QSize &par)
    {
      if(maxSizeSonWidget.height()<par.height())
      {
        maxSizeSonWidget.setHeight(par.height());
      }
      if(maxSizeSonWidget.width()<par.width())
      {
        maxSizeSonWidget.setWidth(par.width());
      }
    }

    void BuildParentRelation(QWidget *item)
    {
      if (item) {
        this->setParent(item);
      }
      this->parentWidgetPointer = item;
    }

protected:

private:
  QVBoxLayout *m_layout;
  QWidget *parentWidgetPointer;
  QSize maxSizeSonWidget{0,0};
  QMap<QWidget *,bool> m_map;
  QSet<QWidget *>m_QuerySet;
};

// 在主窗口或初始化 F 的地方
class ScrollAreaDebugger : public QObject {
  Q_OBJECT
public:
  bool eventFilter(QObject *obj, QEvent *event) override {
    QScrollArea *area = qobject_cast<QScrollArea *>(obj);
    if (event->type() == QEvent::Resize) {
      if (area) {
        qDebug() << "[F] ScrollArea Resized. Viewport:"
                 << area->viewport()->size() << "Internal Widget:"
                 << (area->widget() ? area->widget()->size() : QSize(0, 0));

        // 检查滚动条可见性
        qDebug() << "    [F] ScrollBars Visible? H:"
                 << area->horizontalScrollBar()->isVisible()
                 << " V:" << area->verticalScrollBar()->isVisible();
      }
    }
   
    return QObject::eventFilter(obj, event);
  }
};

class dataChart : public QWidget
{
    Q_OBJECT

public:
    explicit dataChart(QWidget *parent = nullptr, Scope *scope = nullptr);
    ~dataChart() = default;

    // 公共方法
    void setUI(const QString &title, const QString &query, const QColor &color);
    void setDesiredSize(const QSize &size);
    void AddSonWidget(QWidget *item);
    
    bool isExistQuery(const QString &Query);
    bool replaceDataInAxisY(const QString &Query, QVector<QPointF> it, chartData_update *data);
    void replaceDataInAxisX(QDateTime &now, int m_currentSeconds);
    void getSeriesNames(QSet<QString> &set) const;
    void removeSpecialSeries(const QString &query);
    QtCharts::QScatterSeries *createSeries(const QColor &color, const QString &query);
    bool isOwnSpecialQuery(const QString&Query);
    QtCharts::QChartView *returnChartView();

  signals:
    void requestResultAboutQuery(const QString &Query,bool isReason);

  protected:
    QSize sizeHint() const override;

private slots:
    void onAddSeries(const QString &Query);
    void onDeleteSeries(const QString &Query);

private:
    // 私有方法
    void createDial(std::shared_ptr<Scope> m_scope);
    void createChartView();
    void createChart(QtCharts::QChartView *chartViewItem);
    void createAxis(QtCharts::QChart *chart);
    void createLegendButton(QtCharts::QScatterSeries *series);
    void initializeScope(Scope *scope);

private:
    // 成员变量
    QSize m_desiredSize;
    QVBoxLayout *m_layout = nullptr;
    OptionDial *m_dial = nullptr;
    QtCharts::QChartView *chartView = nullptr;
    QtCharts::QChart *chart = nullptr;
    QtCharts::QDateTimeAxis *axisX = nullptr;
    QtCharts::QValueAxis *axisY = nullptr;

    LegendIconButton *LegendButton = nullptr;
    std::shared_ptr<QSet<QString>> selectedLegends;        // 存储选中的图例名称
    std::shared_ptr<QMap<QString, LegendIconButton *>> legendMarkerMap; // 系列名称到图例标记的映射
    std::shared_ptr<QSet<QtCharts::QScatterSeries *>> seriesSets;

    std::shared_ptr<Scope> m_scope = nullptr;
};

// class ResizeGrip : public QWidget {
//     Q_OBJECT
// public:
//     explicit ResizeGrip(QWidget *parent = nullptr,dataChart *parentWidget_pointer = nullptr) : QWidget(parent),parentWidget_pointer(parentWidget_pointer) {
//         setFixedSize(15, 15); // 手柄大小
//         setCursor(Qt::SizeFDiagCursor); // 设置光标为斜向调整
//         setAttribute(Qt::WA_NoSystemBackground);
//     }
// protected:
//   QSize sizeHint() const override { return m_desiredSize; }

//   void paintEvent(QPaintEvent *) override {
//     // 画一个小三角形，表示可以拖拽
//     QPainter painter(this);
//     painter.setPen(Qt::gray);
//     painter.drawLine(0, 10, 10, 10);
//     painter.drawLine(10, 0, 10, 10);
//     painter.drawLine(2, 10, 10, 2);
//     painter.drawLine(5, 10, 10, 5);
//     painter.drawLine(8, 10, 10, 8);
//     }

//     void mousePressEvent(QMouseEvent *e) override {
//         if (e->button() == Qt::LeftButton) {
//             m_startPos = e->globalPos();
//             m_startHeight = parentWidget_pointer->height();
//             m_startWidth = parentWidget_pointer->width();
//             m_isDragging = true;
//         }
//     }

//     void mouseMoveEvent(QMouseEvent *e) override {
//         if (m_isDragging && parentWidget()) {
//             int delta_Y = e->globalPos().y() - m_startPos.y();
//             int delta_X = e->globalPos().x() - m_startPos.x();
//             int newHeight = m_startHeight + delta_Y;
//             int newWidth = m_startWidth + delta_X;
//             // 设置最小高度限制，防止拖没了
//             if (newWidth > 500 && newHeight > 500) {
//               parentWidget_pointer->setDesiredSize(
//                   QSize(newWidth, newHeight)); // ✅ 正确方式
//             }
//         }
//          QWidget::mouseMoveEvent(e);
//     }

//     void mouseReleaseEvent(QMouseEvent *e) override {
//         m_isDragging = false;
//         int delta_Y = e->globalPos().y() - m_startPos.y();
//         int delta_X = e->globalPos().x() - m_startPos.x();
//         int newHeight = m_startHeight + delta_Y;
//         int newWidth = m_startWidth + delta_X;
//         // 设置最小高度限制，防止拖没了
//         if (newWidth > 500 && newHeight > 500) {
//           parentWidget_pointer->restoreDefaultLayout();
//         }
//     }

// private:
//     QPoint m_startPos;
//     QSize m_desiredSize ;
//     dataChart *parentWidget_pointer;
//     int m_startHeight = 0;
//     int m_startWidth = 0;
//     bool m_isDragging = false;
// };


// class ConnectionManager : public QObject {
//     Q_OBJECT
// public:
//     // 替代传统connect，但增加管理功能
//     template<typename Sender, typename Signal, typename Receiver, typename Slot>
//     QMetaObject::Connection safeConnect(
//         Sender* sender, Signal signal,
//         Receiver* receiver, Slot slot,
//         Qt::ConnectionType type = Qt::AutoConnection,
//         const QString& tag = QString()) {
//         // 建立连接
//         auto connection = QObject::connect(sender, signal, receiver, slot, type);
//         if (connection) {
//             // 记录连接信息
//             ConnectionInfo info;
//             info.connection = connection;
//             info.sender = sender;
//             info.receiver = receiver;
//             info.tag = tag;
//             info.created = QDateTime::currentDateTime();
            
//             connections_.append(info);
            
//             qDebug() << "Connection established:" 
//                      << info.sender->objectName() << "->" 
//                      << info.receiver->objectName()
//                      << "Tag:" << tag;
//         } else {
//             qWarning() << "Failed to establish connection";
//         }
        
//         return connection;
//     }

//     template <typename Sender, typename Signal, typename Functor>
//     QMetaObject::Connection
//     safeConnect(Sender *sender, Signal signal, Functor &&functor,
//                 Qt::ConnectionType type = Qt::AutoConnection,
//                 const QString &tag = QString()) {

//       // 为lambda创建一个上下文对象
//       auto *context = new QObject(sender);

//       // 连接信号到lambda
//       auto connection = QObject::connect(
//           sender, signal,
//           [context, functor = std::forward<Functor>(functor)](auto &&...args) {
//             if (context) {
//               functor(std::forward<decltype(args)>(args)...);
//             }
//           });

//       // 当sender销毁时，自动销毁context
//       QObject::connect(sender, &QObject::destroyed, context,
//                        &QObject::deleteLater);

//       return connection;
//     }
//     // 断开特定标签的连接
//     void disconnectByTag(const QString& tag) {
//         for (auto it = connections_.begin(); it != connections_.end(); ) {
//             if (it->tag == tag) {
//                 QObject::disconnect(it->connection);
//                 it = connections_.erase(it);
//                 qInfo() << "Disconnected connection with tag:" << tag;
//             } else {
//                 ++it;
//             }
//         }
//     }
    
//     // 断开特定对象的所有连接
//     void disconnectObject(QObject* obj, bool asSender = true, bool asReceiver = true) {
//         for (auto it = connections_.begin(); it != connections_.end(); ) {
//             bool shouldDisconnect = false;
//             if (asSender && it->sender == obj) shouldDisconnect = true;
//             if (asReceiver && it->receiver == obj) shouldDisconnect = true;
            
//             if (shouldDisconnect) {
//                 QObject::disconnect(it->connection);
//                 it = connections_.erase(it);
//             } else {
//                 ++it;
//             }
//         }
//     }
    
//     // 获取连接统计
//     void printConnectionStats() const {
//         qInfo() << "=== Connection Manager Statistics ===";
//         qInfo() << "Total connections:" << connections_.size();
        
//         QMap<QString, int> tagCount;
//         for (const auto& conn : connections_) {
//             tagCount[conn.tag.isEmpty() ? "(untagged)" : conn.tag]++;
//         }
        
//         for (auto it = tagCount.begin(); it != tagCount.end(); ++it) {
//             qInfo() << "  " << it.key() << ":" << it.value();
//         }
//     }
    
// private:
//     struct ConnectionInfo {
//         QMetaObject::Connection connection;
//         QObject* sender = nullptr;
//         QObject* receiver = nullptr;
//         QString tag;
//         QDateTime created;
//     };
    
//     QList<ConnectionInfo> connections_;
  
// };

class DataDeliverControl : public QObject
{
  Q_OBJECT
  public:
    explicit DataDeliverControl(QObject *parent = nullptr,Scope *scope = nullptr):QObject(parent),m_scope(scope){
      {
        auto it = m_scope->get<MyModel>();
        if (it) {
          model = it;
        }
      }
      {
        auto it = m_scope->getShared<QSet<dataChart *>>();
        if (it) {
          m_dataChartSet = it;
        }
      }
      connect(model, &MyModel::dataChanged, this,
              &DataDeliverControl::onUpdateSeries);
    }

     void onUpdateSeries(const QString &Query,bool isReason) {
        ConnectStatusQueue.push(true);
        QVector<QPointF> dataVector = this->model->getData(Query);
        chartData_update *data = this->model->return_chartData();

        for(auto &it:*m_dataChartSet)
        {
          if(it->isExistQuery(Query) && isReason)
          {
            it->replaceDataInAxisY(Query, dataVector, data);

          }
          emit it->requestResultAboutQuery(Query, isReason);
        }
        emit sendMessage();
      }

      bool returnTopElement()
      {
        if(ConnectStatusQueue.size() > 0)
        {
          bool result = ConnectStatusQueue.front();
          ConnectStatusQueue.pop();
          return result; 
        }
        else
        {
          return false;
        }
      }

    signals:
      void sendMessage();

    private:
      Scope *m_scope;
      std::queue<bool> ConnectStatusQueue;
      MyModel *model = nullptr; // 数据模型（存储原始数据）
      std::shared_ptr<QSet<dataChart *>> m_dataChartSet;
};

class ControlPanel: public QWidget
{
  Q_OBJECT
  public:
    explicit ControlPanel(QWidget *parent = nullptr, Scope *scope = nullptr)
        : QWidget(parent), m_scope(scope) {
      if (m_scope) {
        {
          auto it = m_scope->getShared<QSet<dataChart *>>();
          if (it) {
            m_dataChartSet = it;
          }
        }
        {
          auto it = m_scope->get<DataDeliverControl>();
          if (it) {
            dataDeliverControl = it;
          }
        }
        {
          auto it = m_scope->get<RequestSender>();
          if (it) {
            requestSender = it;
          }
        }
      }

      //  initialize mainLayout
      mainLayout = new QVBoxLayout(this);
      mainLayout->addWidget(createStatusBar());
      mainLayout->addWidget(createControlPanel());

      UpdateTimer = new QTimer(this);
      {
        connect(dataDeliverControl, &DataDeliverControl::sendMessage, this, [this](){
          this->onTimeCheckConnection();
        });
      }
    }

    QWidget *createStatusBar() {
      QWidget *bar = new QWidget;
      QHBoxLayout *layout = new QHBoxLayout(bar);

      // 刷新时间显示
      lastUpdateTimeLabel = new QLabel("last time update: --:--:--");
      lastUpdateTimeLabel->setObjectName("refreshTimeLabel"); // 用于后续更新

      // 数据源状态
      statusLabel = new QLabel("data source : conneting...");
      statusLabel->setObjectName("sourceStatusLabel"); // 必须在样式表之前
      setUnknownStyle();

      // 自动刷新开关
      autoRefresh = new QCheckBox("Auto Refresh");

      // 刷新按钮
      refreshBtn = new QPushButton("Refresh Immediately");

      layout->addWidget(lastUpdateTimeLabel);
      layout->addStretch();
      layout->addWidget(statusLabel);
      layout->addStretch();
      layout->addWidget(autoRefresh);
      layout->addWidget(refreshBtn);

      mainLayout->addWidget(bar);
      {
        connect(autoRefresh, &QCheckBox::toggled, this,
                &ControlPanel::onAutoRefreshToggled);

        connect(refreshBtn, &QPushButton::clicked, this,
                &ControlPanel::onRefreshAllData);
      }

      return bar;
    }

    QWidget *createControlPanel() {
      QWidget *panel = new QWidget;
      QHBoxLayout *layout = new QHBoxLayout(panel);

      // 时间范围选择
      timeRangeCombo = new QComboBox();
      timeRangeCombo->addItems(
          {"5 minutes", "15 minutes", "1 hours", "6 hours", "24 hours"});
      timeRangeCombo->setCurrentIndex(2); // "1 hours" 是第3项
      connect(timeRangeCombo,
              QOverload<int>::of(&QComboBox::currentIndexChanged), this,
              &ControlPanel::onCurrentTimeRangeChange);

      // 刷新间隔
      intervalSpin = new QSpinBox();
      intervalSpin->setRange(5, 300);
      intervalSpin->setValue(300);
      intervalSpin->setSuffix(" seconds");
      connect(intervalSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
              &ControlPanel::onUpdateIntervalChanged);

      // 图表类型选择
      chartTypeCombo = new QComboBox();
      chartTypeCombo->addItems({"line chart", "bar chart", "area chart"});

      // 添加/移除指标按钮
      QPushButton *addMetricBtn = new QPushButton("+ Add indicators");
      connect(addMetricBtn, &QPushButton::clicked, this, &ControlPanel::onClicked);

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

      mainLayout->addWidget(panel);
      return panel;
    }

    // 设置连接状态（绿色）
    void setConnectedStyle() {
      statusLabel->setStyleSheet(""); // 先清空
      statusLabel->setText("● Connecting...");
      statusLabel->setStyleSheet(
          "QLabel#sourceStatusLabel {"
          "   color: #00C851;" // 亮绿色
          "   font-weight: bold;"
          "   background-color: rgba(0, 200, 81, 0.1);" // 浅绿色背景
          "   padding: 4px 8px;"
          "   border-radius: 4px;"
          "   border: 1px solid #00C851;"
          "}");
    }

    // 设置断开状态（红色）
    void setDisconnectedStyle() {
      statusLabel->setStyleSheet(""); // 先清空
      statusLabel->setText("● Disconnected...");
      statusLabel->setStyleSheet(
          "QLabel#sourceStatusLabel {"
          "   color: #ff4444;" // 红色
          "   font-weight: bold;"
          "   background-color: rgba(255, 68, 68, 0.1);" // 浅红色背景
          "   padding: 4px 8px;"
          "   border-radius: 4px;"
          "   border: 1px solid #ff4444;"
          "}");
    }

    // 设置未知状态（灰色）
    void setUnknownStyle() {
      statusLabel->setStyleSheet(""); // 先清空
      statusLabel->setText("● Unknown...");
      statusLabel->setStyleSheet(
          "QLabel#sourceStatusLabel {"
          "   color: #999999;" // 灰色
          "   font-weight: bold;"
          "   background-color: rgba(153, 153, 153, 0.1);"
          "   padding: 4px 8px;"
          "   border-radius: 4px;"
          "   border: 1px solidrgba(243, 67, 67, 0.9);"
          "}");
    }

    void setLastUpdateTime() {
      QDateTime now = QDateTime::currentDateTime();
      lastUpdateTimeLabel->setStyleSheet("");
      lastUpdateTimeLabel->setText(now.toString());
      lastUpdateTimeLabel->setStyleSheet(
          "QLabel#sourceStatusLabel {"
          "   color:hsl(91, 80.20%, 47.60%);" // 灰色
          "   font-weight: bold;"
          "   background-color: rgba(153, 153, 153, 0.1);"
          "   padding: 4px 8px;"
          "   border-radius: 4px;"
          "   border: 1px solidrgba(243, 67, 67, 0.9);"
          "}");
    }

  private slots:
    void onClicked() {}

    void onCurrentTimeRangeChange() {
      QDateTime now = QDateTime::currentDateTime();
      int index = timeRangeCombo->currentIndex();
      QString text = timeRangeCombo->itemText(index);
      int m_currentSeconds = TIME_RANGES.value(text, 5 * 60);

      for (auto &it : *m_dataChartSet) {
        //  display time range from now-Xminutes to now
        it->replaceDataInAxisX(now,m_currentSeconds);
      }
    }

    void onUpdateIntervalChanged() {
      int value = intervalSpin->value();
      this->requestSender->changeTimer(value);

      UpdateTimer->stop();
      UpdateTimer->start(value * 1000);
    }

    void onTimeCheckConnection() {
      setLastUpdateTime();
      CurrentConnectStatus = dataDeliverControl->returnTopElement();
      if (CurrentConnectStatus == LastConnectStatus) {
        return;
     } else if (CurrentConnectStatus)
     {
       LastConnectStatus = true;
       setConnectedStyle();
     } else {
       setDisconnectedStyle();
     }
    }

    void onRefreshAllData() {
      UpdateTimer->stop();
      UpdateTimer->start(10);

      // 创建一个单次定时器等待100ms后执行
      QTimer::singleShot(100, this, [this]() {
        // 100ms后要执行的代码
        onCurrentTimeRangeChange();
        onUpdateIntervalChanged();
      });
    }

    void onAutoRefreshToggled() {}

  private:
    QVBoxLayout *mainLayout = nullptr;
    QPushButton *refreshBtn = nullptr;
    QLabel *statusLabel = nullptr;
    QLabel *lastUpdateTimeLabel = nullptr;
    QCheckBox *autoRefresh = nullptr;
    QSpinBox *intervalSpin = nullptr;
    QComboBox *timeRangeCombo = nullptr;
    QComboBox *chartTypeCombo = nullptr;
    const QMap<QString, int> TIME_RANGES = {{"5 minutes", 5 * 60},
                                            {"15 minutes", 15 * 60},
                                            {"1 hours", 60 * 60},
                                            {"6 hours", 6 * 60 * 60},
                                            {"24 hours", 24 * 60 * 60}};

    QTimer *UpdateTimer = nullptr;
    Scope *m_scope = nullptr;
    std::shared_ptr<QSet<dataChart *>> m_dataChartSet = nullptr;
    DataDeliverControl *dataDeliverControl = nullptr;
    RequestSender *requestSender = nullptr;
    bool LastConnectStatus = false;
    bool CurrentConnectStatus = false;
};


class LineChart : public QWidget
{
    Q_OBJECT

public:
    explicit LineChart(QWidget* parent = nullptr);
    ~LineChart();

    void createScrollArea();
    
    dataChart* createEmbeddedChartView(const QString &title, const QString &query,
                                       const QColor &color, QWidget *Container);
    
    QWidget* createEmbeddedWidget(const QString &title, const QString &query,
                                  const QColor &color);
    
    QWidget* createChartView(const QString &title, const QString &query,
                             const QColor &color);
    
    MyModel* return_Model();

private:
    // 图表相关组件
    QScrollArea *scrollArea;    // 滚动区域
    QVBoxLayout *mainLayout;    // 主布局

    // scope control
    Scope *m_scope = nullptr;
    dataChart *item = nullptr;
    DataDeliverControl *dataDeliverControl = nullptr;
    
    // Model-View-Delegate 组件
    MyModel *model = nullptr;           // 数据模型（存储原始数据）
    ControlPanel *m_controlPanel = nullptr;
    RequestSender *m_requestSender = nullptr;
    std::shared_ptr<QSet<dataChart *>> m_dataChartSet;
};

class InspectWidget : public QWidget {
    Q_OBJECT

public:
    enum Format {
        Timestamp,
        BeijingTime
    };

    explicit InspectWidget(QWidget* parent = nullptr);
    ~InspectWidget();

    void setupUI(MyModel* model);
    void onFormatChanged(int logicalIndex);
    
    // Getter 方法
    QTableView* getTableView() const { return m_tableView; }
    MyModel* getModel() const { return m_model; }
    Format getCurrentFormat() const { return m_currentFormat; }

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void setupConnections();
    void updateDelegate();

private slots:
    void onTimerTimeout();

private:
    // UI 组件
    QVBoxLayout* m_layout = nullptr;
    QTableView* m_tableView = nullptr;
    
    // Delegate
    QAbstractItemDelegate* m_plainNumberDelegate = nullptr;
    QAbstractItemDelegate* m_beijingTimeDelegate = nullptr;
    
    // 数据和状态
    MyModel* m_model = nullptr;
    Format m_currentFormat = Timestamp;
    
    // 定时器
    QTimer* m_updateTimer = nullptr;
};

class OptionDial : public QWidget
{
    Q_OBJECT

public:
    explicit OptionDial(QWidget *parent = nullptr, std::shared_ptr<Scope> scope = nullptr);
    ~OptionDial() = default;

    enum Format { Timestamp, BeijingTime };

    struct EditWidget {
      QWidget *Page = nullptr;
      QVBoxLayout *Vlayout = nullptr;
    };

    struct AddSeriesWidget {
      QWidget *page = nullptr;
      QHBoxLayout *layout = nullptr;
      QLabel *label = nullptr;
      QTextEdit *textEdit = nullptr;
      QPushButton *button = nullptr;

      void setupUI(const QString &style);
      void clearText();
      QString getText() const;
    };

    struct DeleteSeriesWidget {
      QWidget *page = nullptr;
      QHBoxLayout *layout = nullptr;
      QLabel *label = nullptr;
      QComboBox *comboBox = nullptr;
      QPushButton *button = nullptr;
      QCheckBox *sortBox = nullptr;

      QList<QueryInfo> allItems;
      bool isSorted = false;

      void setupUI(const QString &style, QWidget *parent);
      void addItem(const QString &text);
      void removeCurrentItem();
      QString getCurrentText() const;
      void applySorting();
      void restoreOriginalOrder();
      bool isSortedMode() const;
      void clearItems();
      int getIndexOfItem(const QString &Query);
      void setSpecialIndexForCurrentItem(int index);
      void updateInvalidSeries(const QString &Query, bool isReason);
    };

    // 公共方法
    QMenu* createSubMenu(QString&& SubMenuName, QMenu* mainMenu);
    void setButtonRed(const QString &originalStyle); 

  signals:
    void requestChartView();
    void addSeriesEvent(const QString& Query);
    void deleteSeriesEvent(const QString& Query);

  public slots:
    // 私有槽函数
    void onRequestQueryResult(const QString &Query, bool isReason);

private:
    // 私有方法
    QPushButton* createLeftPanel();
    void setupUi();
    void createEditWidget();
    void createExploreWidget();
    void createInspectWidget();
    void BuildEditWidget();
    
    bool checkFairnessForQuery(const QString& tex);
    void onaddItemIntoCombox( const QString& Query);
    void onDeleteItemFromCombox(QComboBox* comboBox);

private:
    // 成员变量
    QHBoxLayout* m_mainLayout = nullptr;
    QMenuBar* menuBar = nullptr;
    QToolBar* mainToolBar = nullptr;
    QWidget* centralWidget = nullptr;
    
    // 新增的构体成员
    EditWidget editWidget;
    AddSeriesWidget addSeriesWidget;
    DeleteSeriesWidget deleteSeriesWidget;
    InspectWidget *inspecSeriestWidget = nullptr;

    QStatusBar* statusBar = nullptr;
    QtCharts::QChartView* EditView = nullptr;
    
    std::shared_ptr<Scope> m_scope = nullptr;
    MyModel *m_model = nullptr;
    QWidget* parentPointer = nullptr;
};

class PlainNumberDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit PlainNumberDelegate(QObject *parent = nullptr);
    
    // 重写 displayText 方法，控制显示文本
    QString displayText(const QVariant &value, const QLocale &locale) const override;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
};

class BeijingTimeDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit BeijingTimeDelegate(QObject *parent = nullptr);
    QString formatToBeijingTime(qint64 timestampMs) const;

    // 重写 displayText 方法，控制显示文本
    QString displayText(const QVariant &value, const QLocale &locale) const override;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
};