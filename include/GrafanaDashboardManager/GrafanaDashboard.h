#pragma once
#include "load_config/Qt_library.h"
#include "load_config/load_config.h"

class GrafanaClient : public IChartFeature
{
    Q_OBJECT
public:
    explicit GrafanaClient();

    void attachTo(QtCharts::QChartView *chart){};
    void detach(){};
    // 配置
    void setConfig(const QString &baseUrl, const QString &apiKey);
    void setAuth(const QString &username, const QString &password);

    void outputJsonStructure(const QString &path, const QJsonValue &value,
                             int depth = 0);
    void outputJsonValue(const QString &name, const QJsonValue &value);
    void outputJsonObject(const QString &name, const QJsonObject &obj);
    void outputJsonArray(const QString &name, const QJsonArray &arr);
    void processVectorResult(const QJsonArray &results,
                             const QString &metricName);
    void processMatrixResult(const QJsonArray &results,
                             const QString &metricName);
    void processScalarResult(const QJsonValue &result);
    void processStringResult(const QJsonValue &result);
    void extractChartDataFromVector(const QJsonArray &results);
    void extractChartDataFromMatrix(const QJsonArray &results);
    QString buildSeriesName(const QJsonObject &metric);

    // 组织管理
    void getOrganizations();
    void getCurrentOrganization();

  signals:
    void dashboardReceived(const QJsonObject &dashboard);
    void panelDataReceived(const QJsonArray &data, int panelId);
    void errorOccurred(const QString &errorMessage);
    void authenticationRequired();
    void JsonData_ready(const QJsonObject &jsonData,const QString &client_name);
    
private slots:
    void onReplyFinished(QNetworkReply *reply);
    // Dashboard操作
    void getDashboard(const QString &dashboardUid);
    
    // 端点数据查询
    void queryPrometheusMetric(const QString &dashboardUid,
                        const QDateTime &from, const QDateTime &to,
                        const QString &presetName
                        );
    QVariant queryPrometheusMetric(const QVariantList &args);

    // 查询Grafana Dashboard中的预设查询
    QVariant queryGrafanaPreset(const QVariantList &args);
    // 在返回的dashboard里，尝试找到特定的模板变量
    QString findPresetInDashboard(const QJsonObject &dashboard,
                                  const QString &presetName);
    
private:
    QNetworkAccessManager *m_networkManager;
    QString m_baseUrl;
    QString m_apiKey;
    QString m_username;
    QString m_password;
    bool m_useApiKey;
    
    QNetworkRequest createRequest(const QString &endpoint);
    void handleDirectPrometheusResponse(QNetworkReply *reply);
    void handleDashboardForPreset(QNetworkReply *reply);
};