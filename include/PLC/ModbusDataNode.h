#include "PLC_Collector/IDataNode.h"
#include "PLC/Modbus.h"


class ModbusDataNode : public IDataNode {
    Q_OBJECT 
private:
    ModbusDataStruct *var;
    std::vector<ModbusDataStruct> m_varVec;
    std::atomic<bool> m_cacheValid{false}; // 标记缓存是否有效
    std::weak_ptr<ModbusMediator> reader;

  public:
    // 构造函数
    ModbusDataNode();
    // 构造函数
    explicit ModbusDataNode(const std::vector<ModbusDataStruct> &varVec);
    explicit ModbusDataNode(std::shared_ptr<ModbusMediator> &modbusReader,ModbusDataStruct *var);

    // ===== 元信息接口 =====
    std::string getName() const override;
    S7DataType getDataType() const override;
    std::string getProtocolType() const override;
    std::string getNodeId() const override;
    int getAccessLevel() const override;
    std::string getDescrition() const override;

    // ===== 读取操作 =====
    Result<QVariant, RichError> readValue() override;
    Result<bool, RichError> readValueFromPLC() override;
    
    // ===== 写入操作 =====
    Result<bool, RichError> writeValue(const QVariant &value) override;
    Result<bool, RichError> writeValueToPLC() override;

    //  =====trim function====
    void emitBatchData();
};