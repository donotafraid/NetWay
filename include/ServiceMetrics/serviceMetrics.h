#pragma once
#include <memory>
#include <string>
#include <prometheus/counter.h>
#include <prometheus/registry.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/exposer.h>

#include <iostream>
#include <future>
class ServiceMetrics{
    public:
    ServiceMetrics(std::shared_ptr<prometheus::Registry> Registry):m_registry(Registry){setupMetrics();};
    ~ServiceMetrics() = default;

    void setupMetrics();
    void processRequest();
    void simulateConcurrentRequests(int requests);
    private:
    std::shared_ptr<prometheus::Registry> m_registry;
    std::unique_ptr<prometheus::Exposer> m_exposer = nullptr;


    prometheus::Counter *m_request_total_counter;
    prometheus::Gauge *m_active_request_counter;
    prometheus::Histogram *m_histogram;
} ;
