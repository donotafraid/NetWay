// include/OPCUAPacking/internal/ClientTestHooks.h
#pragma once
#include "OPCUAPacking/ua.h"


struct ClientTestHooks {
  static void pumpWatchdogForTest(OPC_UA_Client &c) {
    c.pumpWatchdogForTest();
  }
  static bool getRecreatingStatus(OPC_UA_Client &c) {
    return c.getRecreatingStatus();
  }
  static Result<std::unique_ptr<OPC_UA_Client>, RichError>
  createWithSdk(std::shared_ptr<ISdk> sdk, const std::string &endpointUrl,
                ClientConfig config, bool usedefault = false,
                bool enableWatchDog = true) {
    return OPC_UA_Client::createWithSdk(  // ← 加类名限定
        sdk, endpointUrl, config, usedefault, enableWatchDog);
  }
};