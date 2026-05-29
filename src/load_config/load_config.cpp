#include "load_config/load_config.h"
#include "load_config/Qt_library.h"

// 构造函数
IChartFeature::IChartFeature(QObject *parent)
    : QObject(parent) {
}

// 显示名称
QString IChartFeature::displayName() const {
    return "Chart Feature";
}

// 图标
QIcon IChartFeature::icon() const {
    return QIcon();
}

bool IChartFeature::isAttached() const { return true; }
