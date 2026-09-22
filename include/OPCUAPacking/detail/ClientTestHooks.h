#pragma once
#include "OPCUAPacking/ua.h"

struct ClientTestHooks {
  static void pumpWatchdogForTest(OPC_UA_Client &c) {
    c.pumpWatchdogForTest();
  }
  static bool getRecreatingStatus(OPC_UA_Client &c) {
    return c.getRecreatingStatus();
  }
};