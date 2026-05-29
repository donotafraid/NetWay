#pragma once

#include "load_config/Qt_library.h"
#include <iostream>
#include <load_config/load_config.h>

enum class Log_Level { DEBUG, INFO, ERROR,UNKNOWN};
enum class Log_OUTPUT_FORMAT {TXT,JSON,UNKNOWN};
enum class Cloud_Platform { AWS, Azure, ThingsBoard,UNKNOWN};
enum class Cloud_Platform_API {Token,UNKNOWN};

static const std::map<Log_Level, QString> LogLevelToQString = {
    {Log_Level::DEBUG, "DEBUG"},
    {Log_Level::UNKNOWN,  "UNKNOWN"},
    {Log_Level::INFO,  "INFO"},
    {Log_Level::ERROR, "ERROR"}
};

static const std::map<QString, Log_Level> QStringToLogLevel = {
    {"DEBUG", Log_Level::DEBUG},
    {"UNKNOWN",  Log_Level::UNKNOWN},
    {"INFO",  Log_Level::INFO},
    {"ERROR", Log_Level::ERROR}
};

// 枚举 → 字符串
static const std::map<Log_OUTPUT_FORMAT, QString> LogOutputFormatToQString = {
    {Log_OUTPUT_FORMAT::UNKNOWN,  "UNKNOWN"},
    {Log_OUTPUT_FORMAT::TXT,  "TXT"},
    {Log_OUTPUT_FORMAT::JSON, "JSON"}
};

// 字符串 → 枚举（用于从 QComboBox 等 UI 元素还原）
static const std::map<QString, Log_OUTPUT_FORMAT> QStringToLogOutputFormat = {
    {"UNKNOWN",  Log_OUTPUT_FORMAT::UNKNOWN},
    {"TXT",  Log_OUTPUT_FORMAT::TXT},
    {"JSON", Log_OUTPUT_FORMAT::JSON}
};

static const std::map<Cloud_Platform, QString> CloudPlatformToQString = {
    {Cloud_Platform::AWS,         "AWS"},
    {Cloud_Platform::Azure,       "Azure"},
    {Cloud_Platform::UNKNOWN,       "UNKNOWN"},
    {Cloud_Platform::ThingsBoard, "ThingsBoard"}
};

static const std::map<QString, Cloud_Platform> QStringToCloudPlatform = {
    {"AWS",         Cloud_Platform::AWS},
    {"Azure",       Cloud_Platform::Azure},
    {"UNKNOWN",       Cloud_Platform::UNKNOWN},
    {"ThingsBoard", Cloud_Platform::ThingsBoard}
};

static const std::map<Cloud_Platform_API, QString> CloudPlatformApiToQString = {
    {Cloud_Platform_API::Token, "Token"},
    {Cloud_Platform_API::UNKNOWN, "UNKNOWN"}
};

static const std::map<QString, Cloud_Platform_API> QStringToCloudPlatformApi = {
    {"Token", Cloud_Platform_API::Token},
    {"UNKNOWN", Cloud_Platform_API::UNKNOWN}
};

// Log_Level → spdlog::level::level_enum
static const std::map<Log_Level, spdlog::level::level_enum> LogLevelToSpdlogLevel = {
    {Log_Level::DEBUG,   spdlog::level::debug},
    {Log_Level::INFO,    spdlog::level::info},
    {Log_Level::ERROR,   spdlog::level::err},
    {Log_Level::UNKNOWN, spdlog::level::info} // fallback
};

struct System_Parameter
{
    bool enable_TLS = false;
    bool enable_SSL = false;
    QString  MQTT_Publish_Frequency = "5";
    QString  MQTT_Connect_Retry_Times = "5";
    QString MQTT_Retry_distance_times = "2000";

    QString PLC_Refresh_Frequency = "5";
    QString PLC_User = "";
    QString PLC_Password = "";
    QString PLC_Client_Certificate = "";

    Log_Level current_level = Log_Level::UNKNOWN;
    Log_OUTPUT_FORMAT current_format = Log_OUTPUT_FORMAT::UNKNOWN;
    bool enable_prometheus = 0;
    QString prometheus_port = ""; 

    Cloud_Platform current_cloud_platform  = Cloud_Platform::UNKNOWN; 
    bool enable_cloud_api = false;
    Cloud_Platform_API cloud_api_certificate_type = Cloud_Platform_API::UNKNOWN;
    QString cloud_api_key = "";
};

struct DeviceTableInfo
{
    std::string ip_Address;
    std::string  dataBlockName;
    std::string connectWay;

    // 确保有正确的移动操作
    DeviceTableInfo() = default;
    DeviceTableInfo(DeviceTableInfo &&) = default;            // 移动构造
    DeviceTableInfo &operator=(DeviceTableInfo &&) = default; // 移动赋值

    // 禁用拷贝（可选）
    DeviceTableInfo(const DeviceTableInfo &) = delete;
    DeviceTableInfo &operator=(const DeviceTableInfo &) = delete;

    ~DeviceTableInfo(){std::cout<<"~DeviceTableInfo call !"<<std::endl;}
};

// 0 = DEVICE , 1 = DATABLOCK
enum class ItemType{
    DEVICE,
    DATABLOCK
};