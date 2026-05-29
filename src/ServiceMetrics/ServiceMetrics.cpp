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

void ServiceMetrics::simulateConcurrentRequests(int requests)
{
    if(m_exposer == nullptr)
    {
        m_exposer = std::make_unique<prometheus::Exposer>("0.0.0.0:9090");
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