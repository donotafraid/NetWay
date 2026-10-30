if(EXISTS "/root/NetWay/opcuaNetWay/build-RelWithDebInfo/tests-build/tests")
  if(NOT EXISTS "/root/NetWay/opcuaNetWay/build-RelWithDebInfo/tests-build/tests[1]_tests.cmake" OR
     NOT "/root/NetWay/opcuaNetWay/build-RelWithDebInfo/tests-build/tests[1]_tests.cmake" IS_NEWER_THAN "/root/NetWay/opcuaNetWay/build-RelWithDebInfo/tests-build/tests" OR
     NOT "/root/NetWay/opcuaNetWay/build-RelWithDebInfo/tests-build/tests[1]_tests.cmake" IS_NEWER_THAN "${CMAKE_CURRENT_LIST_FILE}")
    include("/usr/share/cmake-3.22/Modules/GoogleTestAddTests.cmake")
    gtest_discover_tests_impl(
      TEST_EXECUTABLE [==[/root/NetWay/opcuaNetWay/build-RelWithDebInfo/tests-build/tests]==]
      TEST_EXECUTOR [==[]==]
      TEST_WORKING_DIR [==[/root/NetWay/opcuaNetWay/tests]==]
      TEST_EXTRA_ARGS [==[]==]
      TEST_PROPERTIES [==[]==]
      TEST_PREFIX [==[]==]
      TEST_SUFFIX [==[]==]
      TEST_FILTER [==[]==]
      NO_PRETTY_TYPES [==[FALSE]==]
      NO_PRETTY_VALUES [==[FALSE]==]
      TEST_LIST [==[tests_TESTS]==]
      CTEST_FILE [==[/root/NetWay/opcuaNetWay/build-RelWithDebInfo/tests-build/tests[1]_tests.cmake]==]
      TEST_DISCOVERY_TIMEOUT [==[5]==]
      TEST_XML_OUTPUT_DIR [==[]==]
    )
  endif()
  include("/root/NetWay/opcuaNetWay/build-RelWithDebInfo/tests-build/tests[1]_tests.cmake")
else()
  add_test(tests_NOT_BUILT tests_NOT_BUILT)
endif()
