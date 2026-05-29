#include "GrafanaDashboardManager/ObjectRouter.h"

ObjectRouter::~ObjectRouter()
{
    for(auto &it : m_registry)
    {
        if(!it)
        {
            continue;
        }
        else
        {
            // delete it;
            std::cout<<"ObjectRouter delete successfully\n";
        }
    }
}

ObjectRouter &ObjectRouter::instance()
{
    static ObjectRouter instance;
    return instance;
}

bool ObjectRouter::registerObject(const QString&url,QObject *obejct)
{
    m_registry[url] = obejct;
    if(m_registry.find(url) != m_registry.end())
    {
        return true;
    }
    else
    {
        return false;
    }
}

QObject *ObjectRouter::reslove(const QString&url)
{
    return m_registry.value(url,nullptr);
}

QVariant ObjectRouter::invoke(const QString &url,const QString &method,const QVariantList &args)
{
    QObject *obj = reslove(url);
    if(!obj)
    {
        std::cerr<<"obj exist error"<<std::endl;
        return QVariant();
    }
    return invokeImpl(obj,method,args);
}

QVariant ObjectRouter::invokeImpl(QObject *obj, const QString &method,
                                  const QVariantList &args) {
  QVariant result;
  bool success = false;

  // 调试信息
  qDebug() << "=== Invoke Debug Info ===";
  qDebug() << "Object:" << obj;
  qDebug() << "Object class name:" << obj->metaObject()->className();
  qDebug() << "Method to invoke:" << method;
  qDebug() << "Args count:" << args.count();
  qDebug() << "Args:" << args;

  // 打印所有可用的方法
  const QMetaObject *metaObj = obj->metaObject();
  qDebug() << "Available methods:";
  for (int i = metaObj->methodOffset(); i < metaObj->methodCount(); ++i) {
    QMetaMethod metaMethod = metaObj->method(i);
    qDebug() << "  " << i << ":" << metaMethod.methodSignature();
  }

  success = QMetaObject::invokeMethod(
      obj, method.toUtf8().constData(), Qt::DirectConnection,
      Q_RETURN_ARG(QVariant,result),Q_ARG(QVariantList, args));

  qDebug() << "=== Invoke Method Result ===";
  qDebug() << "Success:" << success;
  qDebug() << "Result valid:" << result.isValid();
  qDebug() << "Result type:" << result.typeName();
  qDebug() << "Result value:" << result;

  if (!success) {
    std::cout << "Failed to invoke" << std::endl;
    return result;
  } else {
    return result;
  }
}

bool ObjectRouter::subscribe(const QString &eventUrl,QObject *receiver,const QString &slot)
{
    std::cout<<"call subscribe function\n";
    return true;
}

bool ObjectRouter::publish(const QString &eventUrl,const QVariant &data)
{
    std::cout<<"call publish function\n";
    return true;
}