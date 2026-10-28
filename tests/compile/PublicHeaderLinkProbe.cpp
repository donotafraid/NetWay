// tests/compile/PublicHeaderLinkProbe.cpp
//
// Link-level probe for the public S7TypeStruct.h. EncapsulationProbe only
// compiles headers; it cannot catch a declaration whose definition lives in a
// different library. This target links OPCUAPacking only and calls
// default_value_for(), so an out-of-line definition in the PLC library (the
// pre-fix state) fails at link time with "undefined reference".

#include "OPCUAPacking/S7TypeStruct.h"

#include <gtest/gtest.h>

#include <variant>

TEST(PublicHeaderLink, DefaultValueForIsProvidedByOPCUAPacking) {
  auto v = default_value_for(S7DataType::SINT);
  EXPECT_TRUE(std::holds_alternative<int8_t>(v));
}
