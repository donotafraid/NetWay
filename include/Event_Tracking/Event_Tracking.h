#ifndef EVENT_TRACKING_H
#define EVENT_TRACKING_H

#include "load_config/Qt_library.h"
#include <sstream>
#include <backward.hpp>
#include <shared_mutex>

class WidgetDestructionTracker : public QObject
{
    public:
    static WidgetDestructionTracker& instance()
    {
        static WidgetDestructionTracker instance;
        return instance;
    }

    void install();

    protected:
    bool eventFilter(QObject* obj,QEvent* event);

    private:
    QStringList capture_stack_trace();
};


class ObjectRelationshipTracker : public QObject
{
    public:
    struct ObjectInfo
    {
        QString class_name;
        QObject* parent_ptr;
        // QDataTime create_time;
        QString createContext;
        std::vector<QObject*> children;
    };

    void trackCreation(QObject* obj,const QString& text);

    private slots:
    void onObjectDestoryed(QObject* obj);
    void generateObjectGraph(QObject* destroyedObj, const ObjectInfo& info);

    private:
    std::unordered_map<QObject*, ObjectInfo> m_objectInfo_map;
    std::shared_mutex m_mutex;
};
#endif