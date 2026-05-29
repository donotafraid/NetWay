#include "Event_Tracking/Event_Tracking.h"

void WidgetDestructionTracker::install()
{
    QApplication::instance()->installEventFilter(this);
}

bool WidgetDestructionTracker::eventFilter(QObject* obj,QEvent* event)
{
    if(event->type() == QEvent::Destroy)
    {
        // 捕获销毁时的调用堆栈
        auto stackTrace = capture_stack_trace();
        auto widget = qobject_cast<QWidget*>(obj);
        
        qWarning() << "=== WIDGET DESTROYED ===";
        qWarning() << "Widget:" << obj << "Type:" << obj->metaObject()->className();
        qWarning() << "Parent:" << obj->parent();
        qWarning() << "Destruction stack trace:";
        for (const auto& frame : stackTrace) {
            qWarning() << "  " << frame;
        }
        qWarning() << "========================";
        
        // 保存到日志文件
        // saveDestructionLog(obj, stackTrace);
    }
    return QObject::eventFilter(obj, event);
}

QStringList WidgetDestructionTracker::capture_stack_trace()
{
    QStringList stack;
    backward::StackTrace trace;
    trace.load_here(32);

    backward::Printer printer;
    std::ostringstream oss;
    printer.print(trace, oss);

    QString trace_QString = QString::fromStdString(oss.str());
    stack = trace_QString.split("\n",Qt::SkipEmptyParts);

    return stack;
}


void ObjectRelationshipTracker::trackCreation(QObject* obj,const QString& context)
{
    std::shared_lock lock(m_mutex);
    ObjectInfo info;
    info.class_name = obj->metaObject()->className();
    info.parent_ptr = obj->parent();
    info.createContext = context;

    m_objectInfo_map[obj] = info;

    if(info.parent_ptr)
    {
        m_objectInfo_map[info.parent_ptr].children.emplace_back(obj);
    }

    connect(obj,&QObject::destroyed,this,[this,obj](){
        onObjectDestoryed(obj);
    });
}

void ObjectRelationshipTracker::onObjectDestoryed(QObject* obj)
{
    std::unique_lock lock(m_mutex);
    if(m_objectInfo_map.find(obj) == m_objectInfo_map.end())
    {
        auto info = m_objectInfo_map[obj];    
        std::cerr << "Object destroyed - Class:" << info.class_name.toStdString()
                    << "Context:" << info.createContext.toStdString();

        generateObjectGraph(obj,info);

        m_objectInfo_map.erase(obj);
    }
}

void ObjectRelationshipTracker::generateObjectGraph(QObject* destroyedObj, const ObjectInfo& info)
{
    QString graph;
    graph += "=== OBJECT DESTRUCTION GRAPH ===\n";
    graph +=
        QString("Destroyed: %1 (%2)\n").arg(info.class_name).arg((QString("0x%1").arg(reinterpret_cast<quintptr>(destroyedObj), 0, 16)));
    graph += QString("Context: %1\n").arg(info.createContext);
    if (info.parent_ptr) {
    graph += 
        QString("Parent: %1 (%2)\n").arg(m_objectInfo_map[info.parent_ptr].class_name).arg((QString("0x%1").arg(reinterpret_cast<quintptr>(destroyedObj), 0, 16)));
    }

    auto tmp_children_vector = m_objectInfo_map[info.parent_ptr].children;
    graph += "Sibling:\n";
    for(auto& sibling:tmp_children_vector)
    {
        if(sibling!=destroyedObj && m_objectInfo_map.find(sibling) != m_objectInfo_map.end())
        {
             graph += QString("  - %1 (%2)\n").arg(m_objectInfo_map[sibling].class_name).arg((QString("0x%1").arg(reinterpret_cast<quintptr>(sibling), 0, 16)));
        }
    }

    std::cout<<"graph : "<<graph.toStdString();
}