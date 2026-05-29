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
#include <qtimezone.h>



class ByteOrderCoverter{
    public:
    template <typename T>
    static QByteArray to_bigEndian(T value)
    {
        QByteArray byte_array (sizeof(T),0);
        to_bigEndian(value,byte_array.data(),sizeof(value));
        return byte_array;
    }

    template <typename T>
    static QByteArray to_littleEndian(T value)
    {
        QByteArray byte_array (sizeof(T),0);
        to_littleEndian(value,byte_array.data(),sizeof(value));
        return byte_array;
    }

    template<typename T>
    static void to_bigEndian(T value,void* buffer,size_t value_type)
    {
        //  获取指针,对指针的值进行类型转换
        uint8_t * ptr = reinterpret_cast<uint8_t*>(buffer);

        for(size_t i = 0;i < value_type;i++)
        {
            //  强调，内容结果和内存布局存在明显差别
            //  对 value >> 或 << ，是对内容结果进行操作,而非内存布局
            //  下面是对内容结果操作，逐步得到各字节的结果
            //  0x12345678 -> 0x 00 00 00 12 
            //  0x12345678 -> 0x 00 00 34 00
            //  0x12345678 -> 0x 00 56 00 00
            //  0x12345678 -> 0x 78 00 00 00 
            //  大端序上读取的结果是 0X 12 34 56 78
            //  大端序/小端序对高位数据的识别方法一致，0X12345678,都是从左往右，依次是 高位字节，次高位字节对应的数据，然后依次类推
            //  左移/右移 都是对数据的字节进行操作，而不是对数据的内存地址进行数据的访问操作
            //  这里数据的按顺序写入，代表了低位数据->高位数据的写入
            ptr[i] = (value >> (8 * (value_type - i - 1))) & 0xFF;
        }
    }

    template<typename T>
    static void to_littleEndian(T value,void* buffer,size_t value_type)
    {
        //  获取指针,对指针的值进行类型转换
        uint8_t * ptr = reinterpret_cast<uint8_t*>(buffer);

        for(size_t i = 0;i < value_type;i++)
        {
            ptr[i] = (value >> (8 * (i))) & 0xFF;
        }
    }

    static QByteArray uint16ToBigEndian(uint16_t value)
    {
        return ByteOrderCoverter::to_bigEndian(value);
    }

    static QByteArray uint32ToBigEndian(uint32_t value)
    {
        return ByteOrderCoverter::to_bigEndian(value);
    }

    static QByteArray uint64ToBigEndian(uint64_t value)
    {
        return ByteOrderCoverter::to_bigEndian(value);
    } 

    static QByteArray int16ToBigEndian(int16_t value)
    {
        return ByteOrderCoverter::to_bigEndian(static_cast<uint16_t>(value));
    }

    static QByteArray int32ToBigEndian(int32_t value)
    {
        return ByteOrderCoverter::to_bigEndian(static_cast<uint32_t>(value));
    }

    static QByteArray int64ToBigEndian(int64_t value)
    {
        return ByteOrderCoverter::to_bigEndian(static_cast<uint64_t>(value));
    }
};


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