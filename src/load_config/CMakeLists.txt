#记录当前执行的Cmakelist文件所在目录（当前记录的是 Common_Libraries_Struct 的Cmakelist文件所在路径）
set(CurrentFolder ${CMAKE_CURRENT_LIST_DIR})

#获取特定文件夹下的.c  .cpp 源文件
file(GLOB SOURCES "${CurrentFolder}/*.c"  "${CurrentFolder}/*.cpp" )

set(appleName "Common_Libraries_Struct")

#获取特定文件夹下的.c  .cpp 源文件
set(
    SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/load_config.cpp
)

#确保编译器能找到库文件进行 Moc 文件的生成
set (MOC_HEADERS
     ${CMAKE_CURRENT_SOURCE_DIR}/../../include/load_config/load_config.h
   ${CMAKE_CURRENT_SOURCE_DIR}/../../include/load_config/Qt_library.h
)

#创建动态库/静态库文件名，将源文件添加到动态库/静态库文件里
add_library(${appleName} STATIC ${SOURCES} ${MOC_HEADERS})

set_target_properties(${appleName} PROPERTIES AUTOMOC ON)

#确保编译器能找到头文件进行编译,用于 #include 指令
target_include_directories(${appleName} PUBLIC
${CMAKE_SOURCE_DIR}/include

)

#对动态库/静态库文件
target_link_libraries(Common_Libraries_Struct
    PUBLIC 
    third_party_deps
)

#为当前库增加一个带命名空间的别名
add_library(Common_Libraries_Struct::Common_Libraries_Struct ALIAS Common_Libraries_Struct)

# 显式检查目标是否存在
if( NOT TARGET Common_Libraries_Struct::Common_Libraries_Struct)
    message(FATAL_ERROR "Dependency target 'Common_Libraries_Struct' found")
endif() #满足条件则报错，并结束