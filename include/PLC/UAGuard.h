#pragma once


#include <open62541/client_highlevel.h>

class UAVariantGuard {
public:
  UAVariantGuard(UA_Variant *var) : m_access(var) {}

  ~UAVariantGuard() {
    if (m_access) {
      UA_Variant_clear(m_access);
    }
  }

  // 禁止拷贝
  UAVariantGuard(const UAVariantGuard &) = delete;
  UAVariantGuard &operator=(const UAVariantGuard &) = delete;

  // 允许移动
  UAVariantGuard(UAVariantGuard &&other) noexcept : m_access(other.m_access) {
    other.m_access = nullptr;
  }

  UAVariantGuard &operator=(UAVariantGuard &&other) noexcept {
    if (this != &other) {
      if (m_access)
        UA_Variant_clear(m_access);
      m_access = other.m_access;
      other.m_access = nullptr;
    }
    return *this;
  }

  UA_Variant &get() const 
  {
    return *m_access;
  }

private:
  UA_Variant *m_access;
};

