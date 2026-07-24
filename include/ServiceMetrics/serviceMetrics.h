#pragma once
#include <memory>
#include <string>
#include <prometheus/counter.h>
#include <prometheus/registry.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/exposer.h>

#include <curl/curl.h>

#include <iostream>
#include <future>
class ServiceMetrics{
    public:
    ServiceMetrics(std::shared_ptr<prometheus::Registry> Registry):m_registry(Registry){setupMetrics();
      if (m_exposer == nullptr) {
        m_exposer = std::make_unique<prometheus::Exposer>("172.28.80.94:9090");
        m_exposer->RegisterCollectable(m_registry);
      }
    };
    ~ServiceMetrics() = default;

    void setupMetrics();
    void processRequest();
    void processGaugeRequest(const float &data);

    bool pushToPushgateway(const float &data);
    void graceful_shutdown(); 
    void simulateConcurrentRequests(int requests);
    private:
    std::shared_ptr<prometheus::Registry> m_registry;
    std::unique_ptr<prometheus::Exposer> m_exposer = nullptr;


    prometheus::Counter *m_request_total_counter;
    prometheus::Gauge *m_active_request_counter;
    prometheus::Histogram *m_histogram;
} ;
