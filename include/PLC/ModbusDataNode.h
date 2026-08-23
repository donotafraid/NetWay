#include "PLC_Collector/IDataNode.h"
#include "PLC/ModbusDataStruct.h"


class ModbusDataNode : public IDataNode {
    Q_OBJECT 
private:
    ModbusDataStruct *var;
    std::atomic<bool> m_dirty {false}; // 标记缓存是否有效

  public:
    // 构造函数
    ModbusDataNode();
    // 构造函数
    explicit ModbusDataNode(ModbusDataStruct *var);

    // ===== 元信息接口 =====
    std::string getName() const override;
    S7DataType getDataType() const override;
    std::string getProtocolType() const override;
    std::string getNodeId() const override;
    int getAccessLevel() const override;
    std::string getDescrition() const override;

    // ===== 读取操作 =====
    ValueType readValue() const override;
    // Result<bool, RichError> readValueFromPLC() override;
    
    // ===== 写入操作 =====
    Result<bool, RichError> writeValue(const ValueType &value) override;
    // Result<bool, RichError> writeValueToPLC() override;

    //  =====trim function====
    void emitBatchData();
};