#pragma onece

#include "GrafanaDashboardManager/GrafanaDashboard.h"
#include "GrafanaDashboardManager/ObjectRouter.h"
class DashboardConfig : public QObject {
    Q_OBJECT
    
public:
    explicit DashboardConfig(QObject *parent = nullptr);
    
    // 文件操作
    bool loadFromFile(const QString &filePath);  // 如：C:\config\dashboards.json
    bool saveToFile(const QString &filePath);
    
    // 内存操作
    void addDashboard(GrafanaDashboard *dashboard);
    void removeDashboard(const QString &uid);
    GrafanaDashboard* getDashboard(const QString &uid) const;
    QList<GrafanaDashboard*> getAllDashboards() const;
    
    // 服务器配置
    void setServerConfig(const QString &url, const QString &apiKey);
    QString serverUrl() const;
    QString apiKey() const;
    
private:
    QList<GrafanaDashboard*> m_dashboards;
    QString m_serverUrl;
    QString m_apiKey;
    
    void clearDashboards();
};