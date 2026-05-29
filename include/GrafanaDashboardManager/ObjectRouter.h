#pragma  once

#include "load_config/Qt_library.h"
#include <iostream>
// // 配置文件 ≈ 文件夹中的文件
// // 仪表板对象 ≈ 文件内容
// // 管理器 ≈ 文件管理器
// // UI部件 ≈ 文件的查看器
// //  config = manager/config
// //  board = manager/config/board, manager/widget/board
// //  widget = manager/widget

class ObjectRouter : public QObject
{
    Q_OBJECT
    public:
        static ObjectRouter &instance();
        bool registerObject(const QString&url,QObject *object);
        //  return QObject pointer
        QObject *reslove(const QString &url);
        //  dynamic run function
        QVariant invoke(const QString &url,const QString &method,const QVariantList &args = QVariantList());

        template<typename ReturnType = QVariant , typename... Args>
        ReturnType invokeTyped(const QString &url,const QString &method,Args&&... args)
        {
            QObject *obj = reslove(url);
            if(!obj)
            {
                if constexpr (std::is_same_v<ReturnType, QVariant>)
                {
                    return QVariant();
                }
                else
                {
                    return ReturnType{};
                }
            }
            return invokeImpl<ReturnType>(obj, method,std::forward<Args>(args)...);
        }


        bool subscribe(const QString &eventUrl,QObject *receiver,const QString &slot);
        bool publish(const QString &eventUrl,const QVariant &data = QVariant());

        ObjectRouter (const ObjectRouter&) = delete;
        ObjectRouter &operator=(const ObjectRouter&) = delete;

    private:
        QVariant invokeImpl(QObject *obj,const QString &method,const QVariantList &args);
        template <typename ReturnType,typename... Args>
        ReturnType invokeImpl(QObject *obj,const QString &method,Args&&... args)
        {
            ReturnType result{};
            bool success = false;

            if constexpr (std::is_same_v<ReturnType, void>) {
              success = QMetaObject::invokeMethod(
                  obj, method.toUtf8().constData(), Qt::DirectConnection,
                  Q_ARG(Args, std::forward <Args>(args))...);
            } else {
              success = QMetaObject::invokeMethod(
                  obj, method.toUtf8().constData(), Qt::DirectConnection,
                  Q_RETURN_ARG(ReturnType, result),
                  Q_ARG(Args, std::forward<Args>(args))...);
            }

            if(!success)
            {
                std::cout<<"Failed to invoke"<<std::endl;
                if constexpr (!std::is_same_v<ReturnType, void>)
                {
                    return ReturnType{};
                }
            }
            else
            {
                return result;
            }
        }
        QMap<QString , QObject *> m_registry;
        QMultiMap<QString,QPair<QObject*,QString>> m_subscribers;
        ObjectRouter() = default;
        ~ObjectRouter() ;
};