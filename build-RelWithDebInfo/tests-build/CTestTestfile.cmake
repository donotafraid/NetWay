# CMake generated Testfile for 
# Source directory: /root/NetWay/opcuaNetWay/tests
# Build directory: /root/NetWay/opcuaNetWay/build-RelWithDebInfo/tests-build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
include("/root/NetWay/opcuaNetWay/build-RelWithDebInfo/tests-build/tests[1]_include.cmake")
add_test(EncapsulationProbe_OnlyUaH "/usr/bin/cmake" "--build" "/root/NetWay/opcuaNetWay/build-RelWithDebInfo" "--target" "only_ua_h" "--config" "RelWithDebInfo")
set_tests_properties(EncapsulationProbe_OnlyUaH PROPERTIES  _BACKTRACE_TRIPLES "/root/NetWay/opcuaNetWay/tests/CMakeLists.txt;107;add_test;/root/NetWay/opcuaNetWay/tests/CMakeLists.txt;0;")
add_test(PublicHeader_DefaultValueFor_Link "/root/NetWay/opcuaNetWay/build-RelWithDebInfo/tests-build/public_header_link_probe")
set_tests_properties(PublicHeader_DefaultValueFor_Link PROPERTIES  _BACKTRACE_TRIPLES "/root/NetWay/opcuaNetWay/tests/CMakeLists.txt;144;add_test;/root/NetWay/opcuaNetWay/tests/CMakeLists.txt;0;")
