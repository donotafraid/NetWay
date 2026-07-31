#include "PLC_Collector/IDataNode.h"
#include "PLC/Modbus.h"

enum class ModbusNodeType {
    Coil,                 // 0x 线圈
    DiscreteInput,        // 1x 离散输入（只读）
    HoldingRegister,      // 4x 保持寄存器（可读写）
    InputRegister         // 3x 输入寄存器（只读）
};

class ModbusDataNode : public IDataNode {
private:
    // ① 节点属性（构造时传入）
    std::string m_name;
    std::string m_path;
    
    // ② Modbus 通信参数
    int m_slaveId;
    int m_address;
    ModbusNodeType m_nodeType;   // Coil / HoldingRegister / InputRegister
    S7DataType m_dataType;       // BOOL / INT / REAL / UINT16 等
    QVariant m_cachedValue;
    // ✅ 新增：统一的数据指针（与 OPC UA 保持完全一致）
     std::atomic<bool> m_cacheValid{false}; // 标记缓存是否有效

  public:
    // 构造函数：传入所有必要信息
    ModbusDataNode(const std::string& name, 
                   const std::string& path,
                   int slave, 
                   int addr, 
                   ModbusNodeType type, 
                   S7DataType dataType,
                   std::shared_ptr<ModbusMediator> mediator)
        : m_name(name)
        , m_path(path)
        , m_slaveId(slave)
        , m_address(addr)
        , m_nodeType(type)
        , m_dataType(dataType)
        {
        }

    // ===== 元信息接口 =====
    std::string getName() const override { return m_name; }
    std::string getPath() const override { return m_path; }
    S7DataType getDataType() const override { return m_dataType; }
    std::string getProtocolType() const override { return "MODBUS"; }
    std::string getNodeId() const override { return std::to_string(m_address); }
    int getAccessLevel() const override { return 1; } // Modbus 默认可读写

    // ===== 读取操作（核心修正）=====
    Result<QVariant, RichError> readValue() override {
      if (!m_cacheValid) {
        return Result<QVariant, RichError> (RichError{"error cache"});
      } else {
        return Result<QVariant, RichError>(QVariant(m_cachedValue));
        }
    }

    // ===== 写入操作 =====
    Result<bool, RichError> writeValue(const QVariant &value) override {
      switch (m_dataType) {
      case S7DataType::BOOL: {
        if (!value.canConvert<bool>()) {
          m_cacheValid = false;
          return Result<bool, RichError>(RichError{"Invalid type for BOOL"});
        }
        bool boolValue = value.toBool();
        m_cachedValue = boolValue;
        m_cacheValid = true;
        break;
      }

      case S7DataType::BYTE: {
        if (!value.canConvert<int>()) {
          m_cacheValid = false;
          return  Result<bool, RichError>(RichError{"Invalid type for BYTE"});
        }
        int intValue = value.toInt();
        if (intValue < 0 || intValue > 255) {
          m_cacheValid = false;
          return Result<bool, RichError>(
              RichError{"BYTE value out of range (0-255)"});
        }
        m_cachedValue = static_cast<uint8_t>(intValue);
        m_cacheValid = true;
        break;
      }

      case S7DataType::INT: {
        if (!value.canConvert<int>()) {
          m_cacheValid = false;
          return Result<bool, RichError>(RichError{"Invalid type for INT"});
        }
        int intValue = value.toInt();
        if (intValue < -32768 || intValue > 32767) {
          m_cacheValid = false;
          return Result<bool, RichError>(
              RichError{"INT value out of range (-32768 to 32767)"});
        }
        m_cachedValue = static_cast<int16_t>(intValue);
        m_cacheValid = true;
        break;
      }

      case S7DataType::WORD: {
        if (!value.canConvert<int>()) {
          m_cacheValid = false;
          return  Result<bool, RichError>(RichError{"Invalid type for WORD"});
        }
        int intValue = value.toInt();
        if (intValue < 0 || intValue > 65535) {
          m_cacheValid = false;
          return Result<bool, RichError>(
              RichError{"WORD value out of range (0-65535)"});
        }
        m_cachedValue = static_cast<uint16_t>(intValue);
        m_cacheValid = true;
        break;
      }

      case S7DataType::DWORD:
      case S7DataType::UDINT: {
        if (!value.canConvert<quint32>()) {
          m_cacheValid = false;
          return  Result<bool, RichError>(RichError{"Invalid type for DWORD/UDINT"});
        }
        quint32 uintValue = value.toUInt();
        // DWORD 和 UDINT 都是 32 位无符号，范围 0 ~ 4294967295，无需额外校验
        m_cachedValue = static_cast<uint32_t>(uintValue);
        m_cacheValid = true;
        break;
      }

      case S7DataType::REAL: {
        if (!value.canConvert<float>()) {
          m_cacheValid = false;
          return  Result<bool, RichError>(RichError{"Invalid type for REAL"});
        }
        float floatValue = value.toFloat();
        // REAL（32位浮点）无需范围校验，任何 float 都合法
        m_cachedValue = floatValue;
        m_cacheValid = true;
        break;
      }

      default: {
        m_cacheValid = false;
        return  Result<bool, RichError>(RichError{"Unsupported S7 data type for writing"});
      }

      }
        return  Result<bool, RichError>(RichError{"Unsupported S7 data type for writing"});
    }
};