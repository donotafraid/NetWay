#pragma once

#include <modbus/modbus-tcp.h>
#include "Rust_error_deal/error_deal.h"

#include <string>
#include "PLC/WriteRequestAddres.h"
#include "PLC/ModbusDataStruct.h"

// ============ 请求/响应模型 ============
struct ModbusRequest {
    enum class Type {
        ReadCoils,
        ReadDiscreteInputs,
        ReadHoldingRegisters,
        ReadInputRegisters,
        WriteSingleCoil,
        WriteSingleRegister,
        WriteMultipleCoils,
        WriteMultipleRegisters
    };
    
    Type type;
    int slave_id;
    int start_addr;
    int count;  // 对写操作，表示数据长度
    std::vector<uint8_t> data;  // 对写操作，存储要写入的数据
};

struct ModbusResponse {
    enum class Status {
        Success,
        Error,
        Timeout,
        DeviceNotResponding,
        InvalidCRC,
        InvalidParameter
    };
    
    Status status;
    std::string error_message;
    std::vector<uint16_t> registers;  // 读操作返回的数据
    std::vector<uint8_t> bits;        // 线圈/离散输入数据
    int system_error_code;  // errno
};

// ============ 通信中介者类 ============

class ModbusMediator {
public:
    ModbusMediator();
    ~ModbusMediator();
    
    // 禁止拷贝，支持移动
    ModbusMediator(const ModbusMediator&) = delete;
    ModbusMediator& operator=(const ModbusMediator&) = delete;
    ModbusMediator(ModbusMediator&& other) noexcept;
    ModbusMediator& operator=(ModbusMediator&& other) noexcept;

    // ===== 连接管理 =====
    Result<bool, RichError> connect(const std::string& ip, int port = 502);
    void disconnect();
    Result<bool, RichError> reconnect(const std::string& ip, int port = 502);
    
    // ===== 核心：统一的请求处理入口 =====
    ModbusResponse executeRequest(const ModbusRequest& request);
    
    // ===== 便捷方法：同步调用 =====
    Result<std::vector<uint16_t>, RichError> readHoldingRegisters(
        int slave_id, int start_addr, int count);
    
    Result<std::vector<uint16_t>, RichError> readInputRegisters(
        int slave_id, int start_addr, int count);
    
    Result<std::vector<uint8_t>, RichError> readCoils(
        int slave_id, int start_addr, int count);
    
    Result<std::vector<uint8_t>, RichError> readDiscreteInputs(
        int slave_id, int start_addr, int count);
    
    Result<bool, RichError> writeSingleRegister(
        int slave_id, int start_addr, uint16_t value);
    
    Result<bool, RichError> writeMultipleRegisters(
        int slave_id, int start_addr, const std::vector<uint16_t>& data);
    
    Result<bool, RichError> writeSingleCoil(
        int slave_id, int start_addr, uint8_t value);
    
    Result<bool, RichError> writeMultipleCoils(
        int slave_id, int start_addr, const std::vector<uint8_t>& data);

    // ===== 新增：基于业务数据结构的便捷读写接口 =====
    // ✅ 根据 ModbusDataStruct 读取一个变量（返回原始
    // uint16_t，缩放由上层处理）
    Result<uint16_t, RichError> readNode(const ModbusDataStruct &config);
    // ✅ 根据 ModbusDataStruct 写入一个值（自动判断 Coil 还是 Register）
    Result<bool, RichError> writeNode(const ModbusDataStruct &config,
                                      const ValueType &value);

    Result<bool, RichError> batchWriteNode(const std::vector<ModbusDataStruct> &dataVec);
    Result<bool, RichError> batchReadNode( std::vector<ModbusDataStruct> &dataVec);

    // ===== 配置方法 =====
    void setResponseTimeout(int seconds, int microseconds = 0);
    void setByteTimeout(int seconds, int microseconds = 0);
    bool isConnected() const;
    
private:
    // ===== 内部实现：各个具体的读写操作 =====
    ModbusResponse readHoldingRegisters(const ModbusRequest& req);
    ModbusResponse readInputRegisters(const ModbusRequest& req);
    ModbusResponse readCoils(const ModbusRequest& req);
    ModbusResponse readDiscreteInputs(const ModbusRequest& req);
    ModbusResponse writeSingleRegister(const ModbusRequest& req);
    ModbusResponse writeMultipleRegisters(const ModbusRequest& req);
    ModbusResponse writeSingleCoil(const ModbusRequest& req);
    ModbusResponse writeMultipleCoils(const ModbusRequest& req);
    
private:
    modbus_t* ctx_;
    std::string address;
    int port; 
    bool connected_;
};
