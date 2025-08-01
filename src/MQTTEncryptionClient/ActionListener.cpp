#include "MQTTEncryptionClient/ActionListener.h"

void ActionListener::on_success(const mqtt::token &tok)
{
    result_promise->set_value(1);
}

void ActionListener::on_failure(const mqtt::token &tok)
{
    std::cerr<<"on_failure:" <<tok.get_reason_code() <<std::endl;
    result_promise->set_value(-1);
}