#pragma once

#include <iostream>
#include "OPCUAPacking/detail/ClientConfig.h"

class ISdk;

//  独立的外置对象，确保整个对象析构时，部分未完成线程还能调用某些对象
struct RecreateSync {
  // pImpl（外层指针）、Impl::m_impl、Impl 内部成员（如 m_config、m_impl）
  mutable std::mutex m_lock;
  // shutdown() 与 recreate() 两个长事务之间的互斥
  mutable std::mutex m_recreateLock;
  std::atomic<bool> m_recreating{false};
  std::atomic<bool> m_terminated{false};
  std::condition_variable m_recreateCv;

  // 该对象的所有事务计数部分，重建部分需要确保该事务计数=0下才能进行
  int m_inFlight = 0; // 在 m_recreatelock 下读写
  std::condition_variable m_inFlightCv;

  // member variable(used for recreare pImpl)
  std::string m_endpointUrl;
  ClientConfig m_config;                  // 保存初始配置用于重建
  std::shared_ptr<ISdk> m_sdkForRecreate; // 保存同一对象，方便Mock测试
};