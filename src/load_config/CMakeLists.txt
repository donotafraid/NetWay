#记录当前执行的Cmakelist文件所在目录（当前记录的是 Common_Libraries_Struct 的Cmakelist文件所在路径）
set(CurrentFolder ${CMAKE_CURRENT_LIST_DIR})

#获取特定文件夹下的.c  .cpp 源文件
file(GLOB SOURCES "${CurrentFolder}/*.c"  "${CurrentFolder}/*.cpp" )

#创建动态库/静态库文件
add_library(Common_Libraries_Struct STATIC ${SOURCES})


#将源头文件的路径传递给建立的动态库/静态库文件里
target_include_directories(Common_Libraries_Struct PUBLIC
$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
$<INSTALL_INTERFACE:include>
)

#设置动态库/静态库对应的Target.cmake文件路径,该部分在父文件里设置


#对动态库/静态库文件
target_link_libraries(Common_Libraries_Struct
    PUBLIC 
    nlohmann_json::nlohmann_json
    eclipse-paho-mqtt-c::paho-mqtt3as-static
    openssl::openssl
    PahoMqttCpp::paho-mqttpp3-static
    spdlog::spdlog
    SQLite::SQLite3 
    Backward::Backward
    boost::boost
    protobuf::libprotobuf
)

#为当前库增加一个带命名空间的别名
add_library(Common_Libraries_Struct::Common_Libraries_Struct ALIAS Common_Libraries_Struct)

# 显式检查目标是否存在
if( NOT TARGET Common_Libraries_Struct::Common_Libraries_Struct)
    message(FATAL_ERROR "Dependency target 'Common_Libraries_Struct' found")
endif() #满足条件则报错，并结束