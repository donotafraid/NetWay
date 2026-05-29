#include "GrafanaDashboardManager/GrafanaDashboard.h"

GrafanaClient::GrafanaClient()
    : m_networkManager(new QNetworkAccessManager(this)), m_useApiKey(false) {
  // 自动清理回复对象 reply->deleteLater()
  connect(m_networkManager, &QNetworkAccessManager::finished, this,
          &GrafanaClient::onReplyFinished);
}

void GrafanaClient::setConfig(const QString &baseUrl, const QString &apiKey)
{
    m_baseUrl = baseUrl;
    m_apiKey = apiKey;
    m_useApiKey = true;
}

void GrafanaClient::setAuth(const QString &username, const QString &password)
{
    m_username = username;
    m_password = password;
    m_useApiKey = false;
}

// 统一封装：所有请求通过此工厂方法创建
QNetworkRequest GrafanaClient::createRequest(const QString &endpoint)
{
    QUrl url(m_baseUrl + endpoint);
    QNetworkRequest request(url);

    // Content-Type: application/json - 指定请求体格式
    // Accept: application/json - 指定期望的响应格式
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");

    // Bearer Token认证：用于API密钥认证
    // Authorization: Bearer <api_key>
    // 用于Grafana的API密钥认证方式
    if (m_useApiKey && !m_apiKey.isEmpty()) {
        request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());
    } else if (!m_username.isEmpty() && !m_password.isEmpty()) {
      // Basic认证：用于用户名/密码认证
      // Authorization: Basic <base64_credentials>
      // 用于传统的用户名密码认证方式
      QString credentials = QString("%1:%2").arg(m_username).arg(m_password);
      QString base64Credentials = credentials.toUtf8().toBase64();
      request.setRawHeader("Authorization",
                           QString("Basic %1").arg(base64Credentials).toUtf8());
    }
    
    return request;
}

void GrafanaClient::getDashboard(const QString &dashboardUid)
{
    QString endpoint = QString("/api/dashboards/uid/%1").arg(dashboardUid);
    QNetworkRequest request = createRequest(endpoint);
    m_networkManager->get(request);

    // getDashboard() / queryPrometheusMetric()
    //     ↓
    // createRequest()  // 创建标准化请求
    //     ↓
    // 添加认证头、内容类型、自定义头
    //     ↓
    // m_networkManager->get(request)  // 发起异步请求
}

void GrafanaClient::queryPrometheusMetric(const QString &dashboardUid,
                                          const QDateTime &from,
                                          const QDateTime &to,
                                          const QString &presetName) {
  QString endpoint = QString("http://localhost:3001/api/datasources/proxy/uid/%1/api/v1/query_range")
                 .arg(dashboardUid);  // UID
  // QString endpoint =
  //     QString("/api/datasources/uid/%1/api/v1/query").arg(PC67C731E4884519C); // UID
  QUrl url(endpoint);
  QUrlQuery query;
  query.addQueryItem("query", presetName);
  query.addQueryItem("start", from.toString(Qt::ISODate));
  query.addQueryItem("end", to.toString(Qt::ISODate));
  query.addQueryItem("step", "15s"); // 注意：这里应该是"15s"而不是"15"
  url.setQuery(query);

  qDebug() << "\n=== DEBUG: PromQL Query Details ===";
  qDebug() << "Raw PromQL (length:" << presetName.length()
           << "):" << QString("\"%1\"").arg(presetName);
  qDebug() << "Trimmed PromQL:" << QString("\"%1\"").arg(presetName.trimmed());

  QNetworkRequest request = createRequest(url.toString());
  // 客户端的标识符 
  request.setRawHeader("X-QueryName", presetName.toStdString().data());
  request.setRawHeader("X-Operation", "query-prometheus");
  request.setRawHeader("X-From", from.toString(Qt::ISODate).toUtf8());
  request.setRawHeader("X-To", to.toString(Qt::ISODate).toUtf8());
  // Basic Auth认证
  QString username = "admin";
  QString password = "admin123"; // 或从安全存储获取
  QString credentials = QString("%1:%2").arg(username).arg(password);
  QString base64Credentials = credentials.toUtf8().toBase64();
  request.setRawHeader("Authorization",
                       QString("Basic %1").arg(base64Credentials).toUtf8());

  m_networkManager->get(request);
}

QVariant GrafanaClient::queryPrometheusMetric(const QVariantList &args) {
  if (args.size() < 4) {
    emit errorOccurred(
        QString("Insufficient arguments. Expected 4, got %1").arg(args.size()));
    return QVariant().fromValue(QString("size () < 4"));
  }

  // 提取参数
  QString dashboardUid = args[0].toString();
  QString from = args[1].toString();
  QString to = args[2].toString();
  QString presetName = args[3].toString();

  QString endpoint = QString("http://localhost:3001/api/datasources/proxy/uid/%1/api/v1/query_range")
                 .arg(dashboardUid);  // UID
  QUrl url(endpoint);
  QUrlQuery query;
  query.addQueryItem("query", presetName);
  query.addQueryItem("start", from.toUtf8());
  query.addQueryItem("end", to.toUtf8());
  query.addQueryItem("step", "15s"); // 注意：这里应该是"15s"而不是"15"
  url.setQuery(query);

  qDebug() << "\n=== DEBUG: PromQL Query Details ===";
  qDebug() << "Raw PromQL (length:" << presetName.length()
           << "):" << QString("\"%1\"").arg(presetName);
  qDebug() << "Trimmed PromQL:" << QString("\"%1\"").arg(presetName.trimmed());

  QNetworkRequest request = createRequest(url.toString());
  // 客户端的标识符 
  request.setRawHeader("X-QueryName", presetName.toStdString().data());
  request.setRawHeader("X-Operation", "query-prometheus");
  request.setRawHeader("X-From", from.toUtf8());
  request.setRawHeader("X-To", to.toUtf8());
  // Basic Auth认证
  QString username = "admin";
  QString password = "admin123"; // 或从安全存储获取
  QString credentials = QString("%1:%2").arg(username).arg(password);
  QString base64Credentials = credentials.toUtf8().toBase64();
  request.setRawHeader("Authorization",
                       QString("Basic %1").arg(base64Credentials).toUtf8());

  m_networkManager->get(request);
  return QVariant().fromValue(QString("success"));
}

QVariant GrafanaClient::queryGrafanaPreset(const QVariantList &args) {
  if (args.size() < 4) {
    emit errorOccurred(
        QString("Insufficient arguments. Expected 4, got %1").arg(args.size()));
    return QVariant();
  }

  // 提取参数
  QString dashboardUid = args[0].toString();
  QString from = args[1].toString();
  QString to = args[2].toString();
  QString presetName = args[3].toString();
  // 获取Dashboard定义
  QString endpoint = QString("http://localhost:3001/api/dashboards/uid/%1").arg(dashboardUid);
  QNetworkRequest request = createRequest(endpoint);
  // 客户端的标识符设置
  request.setRawHeader("X-Operation", "fetch-grafana-preset");
  request.setRawHeader("X-Preset-Name", presetName.toUtf8());
  request.setRawHeader("X-From", from.toUtf8());
  request.setRawHeader("X-To", to.toUtf8());
  // Basic Auth认证
  QString username = "admin";
  QString password = "admin123"; // 或从安全存储获取
  QString credentials = QString("%1:%2").arg(username).arg(password);
  QString base64Credentials = credentials.toUtf8().toBase64();
  request.setRawHeader("Authorization",
                       QString("Basic %1").arg(base64Credentials).toUtf8());

  m_networkManager->get(request);
  return QVariant();
}

void GrafanaClient::onReplyFinished(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) {
      // 输出错误信息
      qDebug() << "Network request failed:";
      qDebug() << "  Error code:" << reply->error();
      qDebug() << "  Error string:" << reply->errorString();
      qDebug() << "  URL:" << reply->url().toString();

      // 如果需要输出响应内容
      if (reply->isReadable()) {
        QString response = QString::fromUtf8(reply->readAll());
        qDebug() << "  Response:" << response.left(500); // 限制输出长度
      }
      handleDirectPrometheusResponse(reply);
      reply->deleteLater();
      return;
    }

    QString operation = reply->request().rawHeader("X-Operation");

    // 检查是否为面板数据查询
    if (operation.toStdString() == "fetch-grafana-preset") {
        handleDashboardForPreset(reply);
    } else if (operation.toStdString() == "query-prometheus") {
        handleDirectPrometheusResponse(reply);
    }
    
    reply->deleteLater();

    // QNetworkAccessManager::finished 信号
    // ↓ onReplyFinished() // 统一入口
    // ↓ 错误检查 → 错误处理
    // ↓ 响应解析 → 根据头部路由到特定处理器
    // ↓ handleDirectPrometheusResponse() / handleDashboardForPreset()
    // ↓ 信号发射 → 数据传递到业务层
}

void GrafanaClient::handleDashboardForPreset(QNetworkReply *reply)
{
  // 读取客户端自己设置的标记
  QString presetName = reply->request().rawHeader("X-Preset-Name");
  QDateTime from =
      QDateTime::fromString(reply->request().rawHeader("X-From"), Qt::ISODate);
  QDateTime to =
      QDateTime::fromString(reply->request().rawHeader("X-To"), Qt::ISODate);

  // 解析Dashboard JSON
  QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
  QJsonObject dashboard = doc.object()["dashboard"].toObject();

  // 在Dashboard中查找预设
  QString foundPromql = findPresetInDashboard(dashboard, presetName);
  if (!foundPromql.isEmpty()) {
    // 第二步：执行查询
    queryPrometheusMetric(QVariantList()<<"PC67C731E4884519C"<<from<<to<<foundPromql);
  } else {
    emit errorOccurred(QString("Preset '%1' not found").arg(presetName));
  }
}

QString GrafanaClient::findPresetInDashboard(const QJsonObject &dashboard,
                                             const QString &presetName) {
    QString promql;

    // // 完整输出 JSON 结构
    // outputJsonStructure("ROOT", dashboard);

    qDebug() << "Searching for preset:" << presetName;
    
    if (dashboard.contains("templating")) {
        QJsonArray variables = dashboard["templating"].toObject()["list"].toArray();
        for (const QJsonValue &var : variables) {
            QJsonObject varObj = var.toObject();
            if (varObj["name"].toString() == presetName) {
                qDebug() << "Found variable:" << varObj["name"].toString();
                
                // 从definition字段获取查询
                if (varObj.contains("definition") && varObj["definition"].isString()) {
                    QString definition = varObj["definition"].toString();
                    qDebug() << "Raw definition:" << definition;
                    
                    // 提取query_result()内部的查询
                    // 格式：query_result(<actual_query>)
                    if (definition.startsWith("query_result(") && definition.endsWith(")")) {
                        promql = definition.mid(13, definition.length() - 14);
                        qDebug() << "Extracted query:" << promql;
                    } else {
                        promql = definition;
                    }
                }
                
                break;
            }
        }
    }
    
    if (promql.isEmpty()) {
        qDebug() << "Warning: No query found for preset" << presetName;
    }
    
    return promql;
}


void GrafanaClient::handleDirectPrometheusResponse(QNetworkReply *reply)
{
    if (!reply) {
        qDebug()<<"reply is nullptr";
        return;
    }
    
    QByteArray headerData = reply->request().rawHeader("X-Metric-Name");
    QString metricName = headerData.isEmpty() ? QString() : QString::fromUtf8(headerData);
    // 读取响应数据
    QByteArray responseData = reply->readAll();
    QString responseStr = QString::fromUtf8(responseData);
    // 尝试解析 JSON
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        QString errorMsg = QString("JSON parse error at offset %1: %2\nResponse: %3")
                          .arg(parseError.offset)
                          .arg(parseError.errorString())
                          .arg(responseStr.left(500));
        qDebug() << "Parse error:" << errorMsg;
        emit errorOccurred(errorMsg);
        return;
    }
    
    if (!doc.isObject()) {
        emit errorOccurred("Response is not a JSON object");
        return;
    }
    
    QJsonObject result = doc.object();
    
    // 完整输出 JSON 结构
    outputJsonStructure("ROOT", result);
    
    // 检查状态
    QString status = result["status"].toString();
    qDebug() << "\n" << QString(80, '-');
    qDebug() << "STATUS:" << status;
    
    if (status == "success") {
        QJsonObject dataObj = result["data"].toObject();
        QJsonArray dataArray = dataObj["result"].toArray();
        QString resultType = dataObj["resultType"].toString();
        
        qDebug() << "Result Type:" << resultType;
        qDebug() << "ResultArray Size:" << dataArray.size();

        if (dataArray.size() != 0) {
          for (auto it : dataArray) {
            QJsonObject item = it.toObject();
            QJsonObject metric = item["metric"].toObject();
            if (metric.size() == 0) {
              emit JsonData_ready(QJsonObject(),
                                  reply->request().rawHeader("X-QueryName"));
              return;
            } else {
              break;
            }
          }
        }
        else
        {
          emit JsonData_ready(QJsonObject(),
                              reply->request().rawHeader("X-QueryName"));
          return;
        }

        // 处理不同类型的响应
        if (resultType == "vector") {
            processVectorResult(dataObj["result"].toArray(), metricName);
        } else if (resultType == "matrix") {
          emit JsonData_ready(dataObj, reply->request().rawHeader("X-QueryName"));
          // processMatrixResult(dataObj["result"].toArray(), metricName);
        } else if (resultType == "scalar") {
            processScalarResult(dataObj["result"]);
        } else if (resultType == "string") {
            processStringResult(dataObj["result"]);
        } else {
            qDebug() << "Unknown result type:" << resultType;
            // 直接输出结果
            outputJsonValue("result", dataObj["result"]);
        }
        
        // 如果有统计信息，也输出
        if (result.contains("status")) {
            qDebug() << "\n=== QUERY STATISTICS ===";
            outputJsonObject("status", result["stats"].toObject());
        }
        
        
    } else {
        QString errorType = result["errorType"].toString();
        QString error = result["error"].toString();
        
        qDebug() << "ERROR TYPE:" << errorType;
        qDebug() << "ERROR DETAILS:" << error;
        qDebug() << "\nFull error response:";
        outputJsonObject("error_response", result);
        
        emit errorOccurred(QString("%1: %2").arg(errorType).arg(error));
    }
    
    qDebug() << "\n" << QString(80, '=');
    qDebug() << "=== END OF RESPONSE ===";
    qDebug() << QString(80, '=');
}

// 递归输出 JSON 结构的辅助函数
void GrafanaClient::outputJsonStructure(const QString &path, const QJsonValue &value, int depth )
{
    QString indent = QString("  ").repeated(depth);
    
    if (value.isObject()) {
        QJsonObject obj = value.toObject();
        qDebug() << indent << path << "=> Object with" << obj.size() << "keys";
        
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            QString newPath = path.isEmpty() ? it.key() : path + "." + it.key();
            outputJsonStructure(newPath, it.value(), depth + 1);
        }
        
    } else if (value.isArray()) {
        QJsonArray arr = value.toArray();
        qDebug() << indent << path << "=> Array with" << arr.size() << "elements";
        
        if (arr.size() <= 5) {  // 只显示前几个元素的结构
            for (int i = 0; i < qMin(arr.size(), 5); i++) {
                // **修复这里：使用正确的格式化**
                QString newPath = QString("%1[%2]").arg(path).arg(i);
                outputJsonStructure(newPath, arr[i], depth + 1);
            }
            if (arr.size() > 5) {
                qDebug() << indent << "  ... and" << (arr.size() - 5) << "more";
            }
        } else {
            // 对于大数组，只显示类型信息
            for (int i = 0; i < qMin(3, arr.size()); i++) {
                QString newPath = QString("%1[%2]").arg(path).arg(i);
                
                if (arr[i].isObject()) {
                    qDebug() << indent << "  " << newPath << "=> Object";
                } else if (arr[i].isArray()) {
                    qDebug() << indent << "  " << newPath << "=> Array size:" << arr[i].toArray().size();
                } else {
                    qDebug() << indent << "  " << newPath << "=>" 
                             << arr[i].toVariant().toString().left(50);
                }
            }
            if (arr.size() > 3) {
                qDebug() << indent << "  ... and" << (arr.size() - 3) << "more elements";
            }
        }
        
    } else if (value.isString()) {
        QString str = value.toString();
        qDebug() << indent << path << "=> String:" 
                 << (str.length() > 50 ? str.left(50) + "..." : str)
                 << "(" << str.length() << "chars)";
        
    } else if (value.isDouble()) {
        qDebug() << indent << path << "=> Number:" << value.toDouble();
        
    } else if (value.isBool()) {
        qDebug() << indent << path << "=> Bool:" << value.toBool();
        
    } else if (value.isNull()) {
        qDebug() << indent << path << "=> NULL";
    }
}

// 详细输出 JSON 值
void GrafanaClient::outputJsonValue(const QString &name, const QJsonValue &value)
{
    if (value.isObject()) {
        outputJsonObject(name, value.toObject());
    } else if (value.isArray()) {
        outputJsonArray(name, value.toArray());
    } else if (value.isString()) {
        qDebug() << name << ":" << value.toString();
    } else if (value.isDouble()) {
        qDebug() << name << ":" << value.toDouble();
    } else if (value.isBool()) {
        qDebug() << name << ":" << value.toBool();
    } else if (value.isNull()) {
        qDebug() << name << ": NULL";
    }
}

// 详细输出 JSON 对象
void GrafanaClient::outputJsonObject(const QString &name, const QJsonObject &obj)
{
    qDebug() << "\n=== OBJECT:" << name << "===";
    qDebug() << "Keys:" << obj.keys().join(", ");
    qDebug() << "Size:" << obj.size();
    
    
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        qDebug() << "  " << it.key() << ":";
        
        if (it.value().isObject()) {
            qDebug() << "    [Object]";
        } else if (it.value().isArray()) {
            qDebug() << "    [Array] size:" << it.value().toArray().size();
        } else {
            qDebug() << "    " << it.value().toVariant().toString();
        }
    }
}

// 详细输出 JSON 数组
void GrafanaClient::outputJsonArray(const QString &name, const QJsonArray &arr)
{
    qDebug() << "\n=== ARRAY:" << name << "===";
    qDebug() << "Size:" << arr.size();
    
    for (int i = 0; i < arr.size(); i++) {
        if (arr[i].isObject()) {
            qDebug() << QString("  [%1] Object:").arg(i);
            QJsonObject obj = arr[i].toObject();
            qDebug() << "    Keys:" << obj.keys().join(", ");
        } else if (arr[i].isArray()) {
            qDebug() << QString("  [%1] Array size:").arg(i) << arr[i].toArray().size();
        } else {
            qDebug() << QString("  [%1]:").arg(i) << arr[i].toVariant().toString();
        }
    }
}

// 处理 Vector 类型结果（即时查询）
void GrafanaClient::processVectorResult(const QJsonArray &results, const QString &metricName)
{
    qDebug() << "\n" << QString(80, '-');
    qDebug() << "=== PROCESSING VECTOR RESULTS ===";
    qDebug() << "Number of time series:" << results.size();
    
    for (int i = 0; i < results.size(); i++) {
        QJsonObject item = results[i].toObject();
        QJsonObject metric = item["metric"].toObject();
        QJsonValue value = item["value"];
        
        qDebug() << QString("\n[Series %1]:").arg(i + 1);
        qDebug() << "  Metric labels (" << metric.size() << "labels):";
        
        // 输出所有标签
        for (auto it = metric.begin(); it != metric.end(); ++it) {
            qDebug() << QString("    %1 = \"%2\"").arg(it.key(), -15).arg(it.value().toString());
        }
        
        // **修复：正确输出值**
        if (value.isArray()) {
            QJsonArray valArray = value.toArray();
            qDebug() << "  Value array size:" << valArray.size();
            
            if (valArray.size() >= 2) {
                // 处理时间戳
                QJsonValue timestampVal = valArray[0];
                float timestamp = 0;
                
                if (timestampVal.isDouble()) {
                    timestamp = timestampVal.toDouble();
                } else if (timestampVal.isString()) {
                    timestamp = timestampVal.toString().toDouble();
                }
                
                // 处理值
                QJsonValue dataVal = valArray[1];
                float dataValue = 0;
                QString dataStr;
                
                if (dataVal.isDouble()) {
                    dataValue = dataVal.toDouble();
                    dataStr = QString::number(dataValue, 'f', 10);
                } else if (dataVal.isString()) {
                    dataStr = dataVal.toString();
                    bool ok;
                    dataValue = dataStr.toDouble(&ok);
                    if (!ok) {
                        dataValue = 0;
                    }
                }
                
                QDateTime dt = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(timestamp));
                qDebug() << "  Value at" << dt.toString("yyyy-MM-dd HH:mm:ss") 
                         << ":" << dataStr << "(parsed as:" << dataValue << ")";
                
                // 额外调试：显示原始数组
                qDebug() << "  Raw value array: [" 
                         << timestampVal.toVariant().toString() << ", "
                         << dataVal.toVariant().toString() << "]";
            }
        }
        
        // 检查是否有 __name__ 标签
        if (!metric.contains("__name__")) {
            qDebug() << "  NOTE: No __name__ label found in metric";
            
            // 尝试从查询中推断指标名
            if (!metricName.isEmpty()) {
                qDebug() << "  Inferred metric name from header:" << metricName;
            }
        }
    }
    
    // 提取用于 Qt 图表的数据
    extractChartDataFromVector(results);
}


// 处理 Matrix 类型结果（范围查询）
void GrafanaClient::processMatrixResult(const QJsonArray &results, const QString &metricName)
{
    qDebug() << "\n" << QString(80, '-');
    qDebug() << "=== PROCESSING MATRIX RESULTS ===";
    qDebug() << "Number of time series:" << results.size();
    
    int totalDataPoints = 0;
    
    for (int i = 0; i < results.size(); i++) {
       QJsonObject item = results[i].toObject();
        QJsonObject metric = item["metric"].toObject();
        QJsonArray values = item["values"].toArray();
        
        qDebug() << QString("\n[Series %1]:").arg(i + 1);
        qDebug() << "  Metric labels:";
        
        // 输出标签
        QStringList labelStrings;
        for (auto it = metric.begin(); it != metric.end(); ++it) {
            QString labelStr = QString("%1=%2").arg(it.key()).arg(it.value().toString());
            labelStrings << labelStr;
            qDebug() << "    " << labelStr;
        }
        
        QString seriesName = labelStrings.join(", ");
        qDebug() << "  Data points:" << values.size();
        totalDataPoints += values.size();
        
        int pointsToShow = qMin(20, values.size());
        if (values.size() > pointsToShow) {
            qDebug() << "    ... and" << (values.size() - pointsToShow) << "more points";
        }
        
        // 提取统计数据
        if (values.size() > 0) {
            float firstTimestamp = values.first().toArray()[0].toDouble();
            float lastTimestamp = values.last().toArray()[0].toDouble();
            QDateTime firstDt = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(firstTimestamp));
            QDateTime lastDt = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(lastTimestamp));
            
            qDebug() << "  Time range:" << firstDt.toString("HH:mm:ss") 
                     << "to" << lastDt.toString("HH:mm:ss");
        }
    }
    
    qDebug() << "\nTotal data points across all series:" << totalDataPoints;
    
    // 提取用于 Qt 图表的数据
    extractChartDataFromMatrix(results);
}

// 处理 Scalar 类型结果
void GrafanaClient::processScalarResult(const QJsonValue &result)
{
    qDebug() << "\n=== SCALAR RESULT ===";
    if (result.isArray() && result.toArray().size() >= 2) {
        QJsonArray arr = result.toArray();
        float timestamp = arr[0].toDouble();
        float value = arr[1].toDouble();
        QDateTime dt = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(timestamp));
        
        qDebug() << "Timestamp:" << dt.toString("yyyy-MM-dd HH:mm:ss");
        qDebug() << "Value:" << value;
    } else {
        qDebug() << "Unexpected scalar format:" << result;
    }
}

// 处理 String 类型结果
void GrafanaClient::processStringResult(const QJsonValue &result)
{
    qDebug() << "\n=== STRING RESULT ===";
    if (result.isArray() && result.toArray().size() >= 2) {
        QJsonArray arr = result.toArray();
        float timestamp = arr[0].toDouble();
        QString value = arr[1].toString();
        QDateTime dt = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(timestamp));
        
        qDebug() << "Timestamp:" << dt.toString("yyyy-MM-dd HH:mm:ss");
        qDebug() << "String value:" << value;
    } else {
        qDebug() << "Unexpected string format:" << result;
    }
}

// 从 Vector 结果提取图表数据
void GrafanaClient::extractChartDataFromVector(const QJsonArray &results)
{
    QMap<QString, float> seriesData;  // 系列名 -> 值（对于即时查询）
    QMap<QString, QString> seriesLabels;  // 系列名 -> 标签字符串
    
    for (const QJsonValue &item : results) {
        QJsonObject obj = item.toObject();
        QJsonObject metric = obj["metric"].toObject();
        QJsonArray value = obj["value"].toArray();
        
        // 构建系列名称（使用有意义的标签）
        QStringList labelParts;
        QString seriesName;
        
        // 优先使用特定的标签
        if (metric.contains("app")) {
            seriesName = metric["app"].toString();
            labelParts << "app=" + metric["app"].toString();
        } else if (metric.contains("service")) {
            seriesName = metric["service"].toString();
            labelParts << "service=" + metric["service"].toString();
        } else if (metric.contains("job")) {
            seriesName = metric["job"].toString();
            labelParts << "job=" + metric["job"].toString();
        } else if (metric.contains("instance")) {
            seriesName = metric["instance"].toString();
            labelParts << "instance=" + metric["instance"].toString();
        }
        
        // 如果还没有名称，使用所有标签
        if (seriesName.isEmpty()) {
            for (auto it = metric.begin(); it != metric.end(); ++it) {
                if (it.key() != "__name__") {
                    labelParts << it.key() + "=" + it.value().toString();
                }
            }
            seriesName = labelParts.join("_");
        }
        
        // 添加主机或实例作为区分
        if (metric.contains("host") && !metric["host"].toString().isEmpty()) {
            seriesName += " (" + metric["host"].toString() + ")";
        } else if (metric.contains("instance") && !metric["instance"].toString().isEmpty()) {
            seriesName += " (" + metric["instance"].toString() + ")";
        }
        
        // 提取值
        float dataValue = 0;
        if (value.size() >= 2) {
            QJsonValue val = value[1];
            if (val.isDouble()) {
                dataValue = val.toDouble();
            } else if (val.isString()) {
                dataValue = val.toString().toDouble();
            }
        }
        
        seriesData[seriesName] = dataValue;
        seriesLabels[seriesName] = labelParts.join(", ");
        
        qDebug() << "Extracted series:" << seriesName << "=" << dataValue;
    }
    
    if (!seriesData.isEmpty()) {
        // emit vectorDataReady(seriesData);
        
        // 输出摘要
        qDebug() << "\n=== DATA SUMMARY ===";
        qDebug() << "Extracted" << seriesData.size() << "data series:";
        for (auto it = seriesData.begin(); it != seriesData.end(); ++it) {
            qDebug() << QString("  %1: %2 (labels: %3)")
                        .arg(it.key(), -30)
                        .arg(it.value(), 10, 'f', 6)
                        .arg(seriesLabels[it.key()]);
        }
    } else {
        qDebug() << "No data extracted from results";
    }
}

// 从 Matrix 结果提取图表数据
void GrafanaClient::extractChartDataFromMatrix(const QJsonArray &results)
{
    // 数据结构：系列名 -> 时间戳-值对列表
    QMap<QString, QVector<QPair<qint64, float>>> chartData;
    
    for (const QJsonValue &item : results) {
        QJsonObject obj = item.toObject();
        QJsonObject metric = obj["metric"].toObject();
        QJsonArray values = obj["values"].toArray();
        
        // 构建系列名称
        QString seriesName = buildSeriesName(metric);
        
        // 提取所有数据点
        QVector<QPair<qint64, float>> dataPoints;
        for (const QJsonValue &point : values) {
            QJsonArray pointArr = point.toArray();
            if (pointArr.size() >= 2) {
                qint64 timestamp = static_cast<qint64>(pointArr[0].toDouble());
                float value = pointArr[1].toDouble();
                dataPoints.append(qMakePair(timestamp, value));
            }
        }
        
        chartData[seriesName] = dataPoints;
    }
    
    if (!chartData.isEmpty()) {
        // emit matrixDataReady(chartData);
    }
}

// 构建系列名称的辅助函数
QString GrafanaClient::buildSeriesName(const QJsonObject &metric)
{
    QStringList parts;
    
    // 优先使用有意义的标签
    if (metric.contains("job")) parts << metric["job"].toString();
    if (metric.contains("instance")) parts << metric["instance"].toString();
    if (metric.contains("method")) parts << metric["method"].toString();
    if (metric.contains("status")) parts << metric["status"].toString();
    
    // 如果没有有意义的标签，使用指标名
    if (parts.isEmpty() && metric.contains("__name__")) {
        parts << metric["__name__"].toString();
    }
    
    // 如果还是没有，使用所有标签
    if (parts.isEmpty()) {
        for (auto it = metric.begin(); it != metric.end(); ++it) {
            if (it.key() != "__name__") {
                parts << QString("%1=%2").arg(it.key()).arg(it.value().toString());
            }
        }
    }
    
    return parts.join(" - ");
}