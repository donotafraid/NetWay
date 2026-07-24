#include "PLC/Modbus.h"

// ============ ModbusMediator 实现 ============

ModbusMediator::ModbusMediator() : ctx_(nullptr), connected_(false) {}

ModbusMediator::~ModbusMediator() {
    disconnect();
}

ModbusMediator::ModbusMediator(ModbusMediator&& other) noexcept
    : ctx_(other.ctx_), connected_(other.connected_) {
    other.ctx_ = nullptr;
    other.connected_ = false;
}

ModbusMediator& ModbusMediator::operator=(ModbusMediator&& other) noexcept {
    if (this != &other) {
        disconnect();
        ctx_ = other.ctx_;
        connected_ = other.connected_;
        other.ctx_ = nullptr;
        other.connected_ = false;
    }
    return *this;
}

// ===== 连接管理 =====

Result<bool, RichError> ModbusMediator::connect(const std::string& ip, int port) {
    ctx_ = modbus_new_tcp(ip.c_str(), port);
    if (!ctx_) {
        return Result<bool, RichError>(RichError{"Failed to create TCP context"});
    }
    
    // 设置默认超时
    modbus_set_response_timeout(ctx_, 1, 0);
    
    if (modbus_connect(ctx_) == -1) {
        modbus_free(ctx_);
        ctx_ = nullptr;
        return Result<bool, RichError>(RichError{"Failed to modbus TCP connect"});
    }
    
    connected_ = true;
    return Result<bool, RichError>(true);
}

void ModbusMediator::disconnect() {
    if (ctx_) {
        modbus_close(ctx_);
        modbus_free(ctx_);
        ctx_ = nullptr;
        connected_ = false;
    }
}

// ===== 核心：统一的请求处理入口 =====

ModbusResponse ModbusMediator::executeRequest(const ModbusRequest& request) {
    ModbusResponse response;
    response.status = ModbusResponse::Status::Error;
    
    if (!ctx_ || !connected_) {
        response.error_message = "Not connected";
        return response;
    }
    
    // 设置从站ID
    modbus_set_slave(ctx_, request.slave_id);
    
    // 根据请求类型分发处理
    try {
        switch (request.type) {
            case ModbusRequest::Type::ReadHoldingRegisters:
                response = readHoldingRegisters(request);
                break;
                
            case ModbusRequest::Type::ReadInputRegisters:
                response = readInputRegisters(request);
                break;
                
            case ModbusRequest::Type::ReadCoils:
                response = readCoils(request);
                break;
                
            case ModbusRequest::Type::ReadDiscreteInputs:
                response = readDiscreteInputs(request);
                break;
                
            case ModbusRequest::Type::WriteSingleRegister:
                response = writeSingleRegister(request);
                break;
                
            case ModbusRequest::Type::WriteMultipleRegisters:
                response = writeMultipleRegisters(request);
                break;
                
            case ModbusRequest::Type::WriteSingleCoil:
                response = writeSingleCoil(request);
                break;
                
            case ModbusRequest::Type::WriteMultipleCoils:
                response = writeMultipleCoils(request);
                break;
                
            default:
                response.error_message = "Unsupported request type";
                break;
        }
    } catch (const std::exception& e) {
        response.error_message = e.what();
        response.status = ModbusResponse::Status::Error;
    }
    
    return response;
}

// ===== 便捷方法：同步调用 =====

Result<std::vector<uint16_t>, RichError> 
ModbusMediator::readHoldingRegisters(int slave_id, int start_addr, int count) {
    ModbusRequest req;
    req.type = ModbusRequest::Type::ReadHoldingRegisters;
    req.slave_id = slave_id;
    req.start_addr = start_addr;
    req.count = count;
    
    auto resp = executeRequest(req);
    if (resp.status != ModbusResponse::Status::Success) {
        return Result<std::vector<uint16_t>, RichError>(
            RichError{resp.error_message});
    }
    return Result<std::vector<uint16_t>, RichError>(resp.registers);
}

Result<std::vector<uint16_t>, RichError> 
ModbusMediator::readInputRegisters(int slave_id, int start_addr, int count) {
    ModbusRequest req;
    req.type = ModbusRequest::Type::ReadInputRegisters;
    req.slave_id = slave_id;
    req.start_addr = start_addr;
    req.count = count;
    
    auto resp = executeRequest(req);
    if (resp.status != ModbusResponse::Status::Success) {
        return Result<std::vector<uint16_t>, RichError>(
            RichError{resp.error_message});
    }
    return Result<std::vector<uint16_t>, RichError>(resp.registers);
}

Result<std::vector<uint8_t>, RichError> 
ModbusMediator::readCoils(int slave_id, int start_addr, int count) {
    ModbusRequest req;
    req.type = ModbusRequest::Type::ReadCoils;
    req.slave_id = slave_id;
    req.start_addr = start_addr;
    req.count = count;
    
    auto resp = executeRequest(req);
    if (resp.status != ModbusResponse::Status::Success) {
        return Result<std::vector<uint8_t>, RichError>(
            RichError{resp.error_message});
    }
    return Result<std::vector<uint8_t>, RichError>(resp.bits);
}

Result<std::vector<uint8_t>, RichError> 
ModbusMediator::readDiscreteInputs(int slave_id, int start_addr, int count) {
    ModbusRequest req;
    req.type = ModbusRequest::Type::ReadDiscreteInputs;
    req.slave_id = slave_id;
    req.start_addr = start_addr;
    req.count = count;
    
    auto resp = executeRequest(req);
    if (resp.status != ModbusResponse::Status::Success) {
        return Result<std::vector<uint8_t>, RichError>(
            RichError{resp.error_message});
    }
    return Result<std::vector<uint8_t>, RichError>(resp.bits);
}

Result<bool, RichError> 
ModbusMediator::writeSingleRegister(int slave_id, int start_addr, uint16_t value) {
    ModbusRequest req;
    req.type = ModbusRequest::Type::WriteSingleRegister;
    req.slave_id = slave_id;
    req.start_addr = start_addr;
    req.data.resize(2);
    req.data[0] = value & 0xFF;
    req.data[1] = (value >> 8) & 0xFF;
    
    auto resp = executeRequest(req);
    if (resp.status != ModbusResponse::Status::Success) {
        return Result<bool, RichError>(RichError{resp.error_message});
    }
    return Result<bool, RichError>(true);
}

Result<bool, RichError> 
ModbusMediator::writeMultipleRegisters(int slave_id, int start_addr, 
                                        const std::vector<uint16_t>& data) {
    ModbusRequest req;
    req.type = ModbusRequest::Type::WriteMultipleRegisters;
    req.slave_id = slave_id;
    req.start_addr = start_addr;
    req.count = data.size();
    req.data.resize(data.size() * 2);
    memcpy(req.data.data(), data.data(), data.size() * 2);
    
    auto resp = executeRequest(req);
    if (resp.status != ModbusResponse::Status::Success) {
        return Result<bool, RichError>(RichError{resp.error_message});
    }
    return Result<bool, RichError>(true);
}

Result<bool, RichError> 
ModbusMediator::writeSingleCoil(int slave_id, int start_addr, uint8_t value) {
    ModbusRequest req;
    req.type = ModbusRequest::Type::WriteSingleCoil;
    req.slave_id = slave_id;
    req.start_addr = start_addr;
    req.data.push_back(value ? 0xFF : 0x00);
    
    auto resp = executeRequest(req);
    if (resp.status != ModbusResponse::Status::Success) {
        return Result<bool, RichError>(RichError{resp.error_message});
    }
    return Result<bool, RichError>(true);
}

Result<bool, RichError> 
ModbusMediator::writeMultipleCoils(int slave_id, int start_addr, 
                                    const std::vector<uint8_t>& data) {
    ModbusRequest req;
    req.type = ModbusRequest::Type::WriteMultipleCoils;
    req.slave_id = slave_id;
    req.start_addr = start_addr;
    req.count = data.size();
    req.data = data;
    
    auto resp = executeRequest(req);
    if (resp.status != ModbusResponse::Status::Success) {
        return Result<bool, RichError>(RichError{resp.error_message});
    }
    return Result<bool, RichError>(true);
}

// ===== 配置方法 =====

void ModbusMediator::setResponseTimeout(int seconds, int microseconds) {
    if (ctx_) {
        modbus_set_response_timeout(ctx_, seconds, microseconds);
    }
}

void ModbusMediator::setByteTimeout(int seconds, int microseconds) {
    if (ctx_) {
        modbus_set_byte_timeout(ctx_, seconds, microseconds);
    }
}

bool ModbusMediator::isConnected() const {
    return connected_;
}

// ===== 内部实现：各个具体的读写操作 =====

ModbusResponse ModbusMediator::readHoldingRegisters(const ModbusRequest& req) {
    ModbusResponse resp;
    resp.registers.resize(req.count);
    
    int rc = modbus_read_registers(ctx_, req.start_addr, req.count, 
                                   resp.registers.data());
    if (rc == -1) {
        resp.status = ModbusResponse::Status::Error;
        resp.error_message = modbus_strerror(errno);
        resp.system_error_code = errno;
    } else {
        resp.status = ModbusResponse::Status::Success;
    }
    return resp;
}

ModbusResponse ModbusMediator::readInputRegisters(const ModbusRequest& req) {
    ModbusResponse resp;
    resp.registers.resize(req.count);
    
    int rc = modbus_read_input_registers(ctx_, req.start_addr, req.count,
                                         resp.registers.data());
    if (rc == -1) {
        resp.status = ModbusResponse::Status::Error;
        resp.error_message = modbus_strerror(errno);
        resp.system_error_code = errno;
    } else {
        resp.status = ModbusResponse::Status::Success;
    }
    return resp;
}

ModbusResponse ModbusMediator::readCoils(const ModbusRequest& req) {
    ModbusResponse resp;
    resp.bits.resize(req.count);
    
    int rc = modbus_read_bits(ctx_, req.start_addr, req.count,
                              resp.bits.data());
    if (rc == -1) {
        resp.status = ModbusResponse::Status::Error;
        resp.error_message = modbus_strerror(errno);
        resp.system_error_code = errno;
    } else {
        resp.status = ModbusResponse::Status::Success;
    }
    return resp;
}

ModbusResponse ModbusMediator::readDiscreteInputs(const ModbusRequest& req) {
    ModbusResponse resp;
    resp.bits.resize(req.count);
    
    int rc = modbus_read_input_bits(ctx_, req.start_addr, req.count,
                                    resp.bits.data());
    if (rc == -1) {
        resp.status = ModbusResponse::Status::Error;
        resp.error_message = modbus_strerror(errno);
        resp.system_error_code = errno;
    } else {
        resp.status = ModbusResponse::Status::Success;
    }
    return resp;
}

ModbusResponse ModbusMediator::writeSingleRegister(const ModbusRequest& req) {
    ModbusResponse resp;
    if (req.data.size() < 2) {
        resp.error_message = "Invalid data size for single register";
        return resp;
    }
    
    uint16_t value = req.data[0] | (req.data[1] << 8);
    int rc = modbus_write_register(ctx_, req.start_addr, value);
    
    if (rc == -1) {
        resp.status = ModbusResponse::Status::Error;
        resp.error_message = modbus_strerror(errno);
        resp.system_error_code = errno;
    } else {
        resp.status = ModbusResponse::Status::Success;
    }
    return resp;
}

ModbusResponse ModbusMediator::writeMultipleRegisters(const ModbusRequest& req) {
    ModbusResponse resp;
    if (req.data.size() != static_cast<size_t>(req.count * 2)) {
        resp.error_message = "Data size mismatch";
        return resp;
    }
    
    std::vector<uint16_t> values(req.count);
    memcpy(values.data(), req.data.data(), req.data.size());
    
    int rc = modbus_write_registers(ctx_, req.start_addr, req.count,
                                    values.data());
    if (rc == -1) {
        resp.status = ModbusResponse::Status::Error;
        resp.error_message = modbus_strerror(errno);
        resp.system_error_code = errno;
    } else {
        resp.status = ModbusResponse::Status::Success;
    }
    return resp;
}

ModbusResponse ModbusMediator::writeSingleCoil(const ModbusRequest& req) {
    ModbusResponse resp;
    if (req.data.empty()) {
        resp.error_message = "No coil state provided";
        return resp;
    }
    
    int rc = modbus_write_bit(ctx_, req.start_addr, req.data[0]);
    if (rc == -1) {
        resp.status = ModbusResponse::Status::Error;
        resp.error_message = modbus_strerror(errno);
        resp.system_error_code = errno;
    } else {
        resp.status = ModbusResponse::Status::Success;
    }
    return resp;
}

ModbusResponse ModbusMediator::writeMultipleCoils(const ModbusRequest& req) {
    ModbusResponse resp;
    if (req.data.size() != static_cast<size_t>(req.count)) {
        resp.error_message = "Data size mismatch";
        return resp;
    }
    
    int rc = modbus_write_bits(ctx_, req.start_addr, req.count,
                               req.data.data());
    if (rc == -1) {
        resp.status = ModbusResponse::Status::Error;
        resp.error_message = modbus_strerror(errno);
        resp.system_error_code = errno;
    } else {
        resp.status = ModbusResponse::Status::Success;
    }
    return resp;
}

// ============ TemperatureMonitor 实现 ============

TemperatureMonitor::TemperatureMonitor(ModbusMediator& mediator) 
    : mediator_(mediator) {}

Result<float, RichError> 
TemperatureMonitor::getTemperature(int slave_id, int sensor_addr) {
    auto result = mediator_.readHoldingRegisters(slave_id, sensor_addr, 1);
    if (result.is_fail()) {
        return Result<float, RichError>(RichError{result.unwrap_err()});
    }
    
    float temp = result.unwrap_returnLeftValue()[0] / 10.0f;
    return Result<float, RichError>(temp);
}

Result<bool, RichError> 
TemperatureMonitor::setTemperatureThreshold(int slave_id, 
                                            int threshold_addr,
                                            float threshold) {
    uint16_t raw_value = static_cast<uint16_t>(threshold * 10);
    return mediator_.writeMultipleRegisters(slave_id, threshold_addr, {raw_value});
}

// ============ SwitchMonitor 实现 ============

SwitchMonitor::SwitchMonitor(ModbusMediator& mediator) 
    : mediator_(mediator) {}

Result<uint8_t, RichError> 
SwitchMonitor::getSwitch(int slave_id, int sensor_addr) {
    auto result = mediator_.readCoils(slave_id, sensor_addr, 8);
    if (result.is_fail()) {
        return Result<uint8_t, RichError>(RichError{result.unwrap_err()});
    }
    
    return Result<uint8_t, RichError>(result.unwrap_returnLeftValue()[0]);
}

Result<bool, RichError> 
SwitchMonitor::setSwitchThreshold(int slave_id, 
                                   int threshold_addr,
                                   float threshold) {
    uint16_t raw_value = static_cast<uint16_t>(threshold * 10);
    return mediator_.writeMultipleRegisters(slave_id, threshold_addr, {raw_value});
}