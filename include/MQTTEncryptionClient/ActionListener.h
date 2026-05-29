#pragma once

#include <functional>
#include <string>
#include "load_config/load_config.h"
#include "mqtt/async_client.h"


class ActionListener : public mqtt::iaction_listener
{
    std::shared_ptr<std::promise<int>> result_promise;
    public:
        ActionListener(std::shared_ptr<std::promise<int>> result_promise) : result_promise(result_promise) {
        }

        void on_success(const mqtt::token& tok) override;
        void on_failure(const mqtt::token& tok) override;
};