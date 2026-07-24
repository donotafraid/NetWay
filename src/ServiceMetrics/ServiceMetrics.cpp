#include "ServiceMetrics/serviceMetrics.h"

void ServiceMetrics::setupMetrics()
{
  auto &request_counter = prometheus::BuildCounter()
                              .Name("request_total")
                              .Help("Total request processed")
                              .Register(*m_registry);
  m_request_total_counter = &request_counter.Add({{"service", "myapp"}, {"version", "1.0"}});

  auto &active_rqquests = prometheus::BuildGauge()
                              .Name("myapp_active_requests")
                              .Help("current active request")
                              .Register(*m_registry);
  m_active_request_counter = &active_rqquests.Add({{"service", "myapp"}});

  auto &latency_histogram = prometheus::BuildHistogram()
                                .Name("myapp_request_latency_seconds")
                                .Help("Request latency in seconds")
                                .Register(*m_registry);
  m_histogram = &latency_histogram.Add(
      {{"service", "myapp"}},
      prometheus::Histogram::BucketBoundaries{0.01, 0.05, 0.1, 0.5, 1.0, 2.0});
}

void ServiceMetrics::processRequest()
{
    auto start_time = std::chrono::steady_clock::now();
    m_active_request_counter->Increment();

    m_request_total_counter->Increment();
    //  BUSSINESS FUNCTION CALL
    std::cout<<"Bussiness is dealing"<<std::endl;

    //  RECORD DEALING TIME
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration<float>(end_time-start_time).count();
    m_histogram->Observe(duration);
    m_active_request_counter->Decrement();
}

void ServiceMetrics::processGaugeRequest(const float &data)
{
    m_active_request_counter->Set(data);
}

void ServiceMetrics::graceful_shutdown() {
  // 1. 停止采集、上报等线程...
  // 2. 调用Pushgateway清理API
  std::string delete_url =
      "http://localhost:9092/metrics/job/my_cpp_app/instance/gateway_1";
  CURL *curl = curl_easy_init();
  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, delete_url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    // ... 可添加超时设置，防止卡住
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
  }
  // 3. 正常退出
}

bool ServiceMetrics::pushToPushgateway(const float &data) {
  // 1. 手动构建与你原来 gauge 等价的 Prometheus 文本
  std::ostringstream body;
  body << "# HELP myapp_active_requests current active request\n";
  body << "# TYPE myapp_active_requests gauge\n";
  body << "myapp_active_requests{service=\"myapp\"} "
       << data << "\n";

  std::string body_content = body.str();

  // 2. 发送到 Pushgateway（用 POST，避免覆盖其他指标）
  CURL *curl = curl_easy_init();
  if (!curl)
    return false;

  std::string url =
      "http://localhost:9092/metrics/job/my_cpp_app/instance/gateway_1";
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_content.c_str());

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(
      headers, "Content-Type: text/plain; version=0.0.4; charset=utf-8");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  CURLcode res = curl_easy_perform(curl);

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return res == CURLE_OK;
}

void ServiceMetrics::simulateConcurrentRequests(int requests)
{
    if(m_exposer == nullptr)
    {
        m_exposer = std::make_unique<prometheus::Exposer>("172.28.80.94:9090");
        m_exposer->RegisterCollectable(m_registry);
    }
     std::vector<std::future<void>> futures;
    
    for (int i = 0; i < requests; ++i) {
        futures.push_back(std::async(std::launch::async, [this, i]() {
            // 模拟不同类型的请求
            std::string method = (i % 3 == 0) ? "GET" : 
                                (i % 3 == 1) ? "POST" : "PUT";
            
            try {
                this->processRequest();
            } catch (const std::exception& e) {
                std::cerr << "Request " << i << " failed: " << e.what() << std::endl;
            }
        }));
        
    }
    
    // 等待所有请求完成
    for (auto& future : futures) {
        future.wait();
    }
}