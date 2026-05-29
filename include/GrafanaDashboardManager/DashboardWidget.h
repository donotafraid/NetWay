#pragma  once

#include "GrafanaDashboardManager/GrafanaDashboard.h"

class DashboardWidget : public QWidget {
    Q_OBJECT
    
public:
    explicit DashboardWidget(GrafanaDashboard *dashboard, 
                           const QString &serverUrl,
                           QWidget *parent = nullptr);
    
    // 控制方法
    void startAutoRefresh(int intervalSeconds = 30);
    void stopAutoRefresh();
    void reload();
    
    // 设置
    void setTheme(const QString &theme);  // "light" 或 "dark"
    void setTimeRange(const QString &from, const QString &to);
    
private slots:
    void onRefreshTimer();
    void onLoadFinished(bool success);
    
private:
    GrafanaDashboard *m_dashboard;
    QWebEngineView *m_webView;
    QTimer *m_refreshTimer;
    QString m_serverUrl;
    
    void setupWebView();
    void applyCustomStyles();
};