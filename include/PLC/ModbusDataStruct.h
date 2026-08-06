#pragma once

#include <string>
#include <QVariant>
#include "PLC/Struct.h"

enum class ModbusNodeType {
    Coil,                 // 0x 线圈
    DiscreteInput,        // 1x 离散输入（只读）
    HoldingRegister,      // 4x 保持寄存器（可读写）
    InputRegister         // 3x 输入寄存器（只读）
};

// Modbus 专有的数据结构（扁平、无嵌套、无位偏移）
struct ModbusDataStruct {
    // ========== 1. 基础元信息（与 OPCUAModernDataStruct 对齐的公约数） ==========
    std::string variable_name;      // 变量名，如 "Temperature_1"
    std::string variable_full_path; // 完整路径，如 "Device1.HoldingRegister.40001"
    std::string description;        // 描述，如 "Room temperature sensor"
    S7DataType data_type_enum;      // 统一使用 S7DataType (BOOL, INT, REAL...)
    
    // ========== 2. Modbus 寻址三要素（这是核心差异） ==========
    int slave_id;                   // 从站 ID (1-247)
    int address;                    // 寄存器地址 (0x0000 - 0xFFFF)
    ModbusNodeType node_type;       // Coil / DiscreteInput / InputRegister / HoldingRegister
    
    // ========== 3. 数据转换与存储 ==========
    QVariant cached_value;          // 当前缓存值（经过缩放后的实际值，如 123.4）
    double scale_factor;            // ✅ 缩放因子，如 0.1 表示原始值除以10
    int register_count;             // 占用的寄存器数量 (1 或 2，用于 32 位数据)
    
    // ========== 4. 过滤/状态标记（与 OPCUAModernDataStruct 保持一致） ==========
    std::string filter_reason;      // 如果被过滤掉，记录原因
    
    // ========== 构造函数 ==========
    ModbusDataStruct()
        : slave_id(1)
        , address(0)
        , node_type(ModbusNodeType::HoldingRegister)
        , data_type_enum(S7DataType::INT)
        , scale_factor(1.0)
        , register_count(1) {}
    
    // 辅助方法：从原始寄存器值计算出实际值（带缩放）
    void setRawValue(uint16_t raw) {
        double scaled = static_cast<double>(raw) * scale_factor;
        switch (data_type_enum) {
            case S7DataType::BOOL:
                cached_value = QVariant(raw != 0);
                break;
            case S7DataType::INT:
                cached_value = QVariant(static_cast<int16_t>(raw));
                break;
            case S7DataType::REAL:
                cached_value = QVariant(static_cast<float>(scaled));
                break;
            default:
                cached_value = QVariant(raw);
                break;
        }
    }
};