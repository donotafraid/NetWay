#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

#include "OPCUAPacking/Types.h"   // ClientConfig 在 Types.h

class ISdk;

//  独立的外置对象，确保整个对象析构时，部分未完成线程还能调用某些对象
namespace OPCUAPacking::internal {
struct RecreateSync {
  mutable std::mutex m_lock;
  mutable std::mutex m_recreateLock;
  std::atomic<bool> m_recreating{false};
  std::atomic<bool> m_terminated{false};
  std::condition_variable m_recreateCv;
  int m_inFlight = 0;
  std::condition_variable m_inFlightCv;
  std::string m_endpointUrl;
  ClientConfig m_config;
  std::shared_ptr<ISdk> m_sdkForRecreate;
};
}