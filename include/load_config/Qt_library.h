#ifndef QT_LIBRARY_H
#define QT_LIBRARY_H

#include <QtWidgets/qapplication.h>
#include <QtWidgets/qmainwindow.h>
#include <QtCore/qstringlist.h>
#include <QtWidgets/qlistwidget.h>
#include <QtWidgets/qpushbutton.h>
#include <QtWidgets/qboxlayout.h>
#include <QtWidgets/qfiledialog.h>
#include <QtWidgets/qmessagebox.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qdebug.h>
#include <QtGui/qdrag.h>
#include <QtGui/qevent.h>
#include <QtCore/qdiriterator.h>
#include <QtCore/qpropertyanimation.h>
#include <QtWidgets/qlabel.h>
#include <QtWidgets/qprogressbar.h>
#include <QtCore/qthreadpool.h>
#include <QtCore/qobjectdefs.h>
#include <QtCore/qrunnable.h>
#include <QtCore/qmutex.h>
#include <QtCore/qcache.h>
#include <QtCore/qmimedata.h>
#include <QtWidgets/qstackedwidget.h>
#include <QtWidgets/qtablewidget.h>
#include <QtWidgets/qcheckbox.h>
#include <QtWidgets/qspinbox.h>
#include <QtWidgets/qlineedit.h>
#include <QtWidgets/qsplitter.h>
#include <QtCore/qtimer.h>
#include <QtWidgets/qtableview.h>
#include <QtWidgets/qheaderview.h>
#include <QtWidgets/qtreewidget.h>
#include "QtWidgets/qmenu.h"
#include "QtWidgets/qinputdialog.h"
#include <QtWidgets/qdockwidget.h>
#include <QScrollArea>
#include <QTextEdit>
#include <qtoolbar.h>
#include <qstringlistmodel.h>
#include <qcompleter.h>
#include <QComboBox>
#include <QFormLayout>
#include <QScreen>
#include <QtCore/qjsonobject.h>
#include <qurlquery.h>
#include <QMetaMethod>
#include <QMetaObject>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QAbstractSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QVXYModelMapper>
#include <QtCharts/QLegendMarker>
#include <QPropertyAnimation>
#include <QGraphicsDropShadowEffect>
#include <QDesktopWidget>
#include <QReadWriteLock> 
#include <QReadLocker>        // 读锁辅助类
#include <QWriteLocker>       // 写锁辅助类
#include <QScrollBar>
#include <QStatusBar>
#include <QToolButton>
#include <QStyledItemDelegate>
#include <QStandardItemModel>
#include <QAbstractProxyModel>
#include <qtimezone.h>

#include "PLC/ByteOrderConverter.h"



QT_CHARTS_USE_NAMESPACE
class IChartFeature : public QObject {
    Q_OBJECT
    
public:
    // ✅ 添加显式构造函数
    explicit IChartFeature(QObject *parent = nullptr); 
    
    // ✅ 虚析构函数
    virtual ~IChartFeature() override = default;
    
    // 纯虚函数 - 必须实现
    virtual void attachTo(QChartView* chartView) = 0;
    virtual void detach() = 0;
    
    // 可选函数 - 有默认实现
    virtual QString displayName() const;
    
    virtual QIcon icon() const; 
    
    // ✅ 添加一些常用的虚函数
    virtual bool isAttached() const;
    
    virtual void update() {}
    
signals:
    // ✅ 添加一些有用的信号
    void attached();
    void detached();
    void errorOccurred(const QString &message);
    
};

struct DataPoint {
  // QDateTime timestamp;
  qint64 timestamp;
  float value; // 数值
};



struct TimeSeries {
    QString name;
    QColor color{Qt::blue};      // 类内默认值
    bool isReason{true};          // 类内默认值
    QList<DataPoint> points;
    
    // 简化构造函数
    TimeSeries() = default;
    
    explicit TimeSeries(const QString& seriesName) 
        : name(seriesName) {}
    
    TimeSeries(const QString& seriesName, const QColor& seriesColor)
        : name(seriesName), color(seriesColor) {}
    
    TimeSeries(const QString& seriesName, const QColor& seriesColor, bool reason)
        : name(seriesName), color(seriesColor), isReason(reason) {}
};

using ChartData = QList<TimeSeries>;
    
// 工具函数
namespace TimeUtils {
    inline QString toUtcIsoString(const QDateTime& dt) {
        QString iso = dt.toUTC().toString(Qt::ISODate);
        return iso.endsWith('Z') ? iso : iso + 'Z';
    }
}

// 作用域限定的依赖注入容器
class Scope {
private:
  // 同时支持 QObject 和非 QObject 服务
  QHash<QString, QObject *> m_qobjectServices;
  QHash<QString, std::shared_ptr<void>> m_nonQobjectServices;
    
public:
    // 注册 QObject 服务
    template<typename T>
    void registerService(T* service) {
        static_assert(std::is_base_of_v<QObject, T>, "T must inherit from QObject");
        m_qobjectServices[typeid(T).name()] = service;
    }
    
    // 注册非 QObject 服务（使用 shared_ptr）
    template<typename T>
    void registerService(std::shared_ptr<T> service) {
        m_nonQobjectServices[typeid(T).name()] = service;
    }
    
    // 获取 QObject 服务
    template<typename T>
    T* get() {
        static_assert(std::is_base_of_v<QObject, T>, "T must inherit from QObject");
        auto it = m_qobjectServices.find(typeid(T).name());
        if (it != m_qobjectServices.end()) {
            return qobject_cast<T*>(it.value());
        }
        return nullptr;
    }
    
    // 获取非 QObject 服务
    template<typename T>
    std::shared_ptr<T> getShared() {
        auto it = m_nonQobjectServices.find(typeid(T).name());
        if (it != m_nonQobjectServices.end()) {
            return std::static_pointer_cast<T>(it.value());
        }
        return nullptr;
    } 
};


#endif