#pragma  once
#include "GrafanaDashboardManager/GrafanaDashboard.h"


// class GrafanaServer : public QObject{
//    Q_OBJECT 
//    public:
//      explicit GrafanaServer(QObject *parent_pointer = nullptr);
//      ~GrafanaServer() = default;

//      void setUrl(const QUrl &url);
//      QUrl url()const;

//      void setApiKey(const QString &apiKey);

//      QList<GrafanaDashboard*> find_dashBoards();
//      bool test_connection();

//      QJsonObject getDashBoardJson(const QString &uid);
//      QByteArray getPanelImage(const QString &dashBoard,const QString &dashboardUid );

//      signals:
//         void connectionTested(bool success);
//         void dashBoardFinded(bool success);

//    private:
//     QUrl m_serverUrl;
//     QString m_apiKey;
//     QNetworkAccessManager *m_networkMannager;
//     QNetworkRequest createApiRequest(const QString &endpoint);
// };

