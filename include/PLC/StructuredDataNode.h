#pragma once
#include "PLC/Struct.h"
#include "PLC_Collector/IDataNode.h"
#include "PLC/OPCUAManager.h"

class StructuredDataNode : public IDataNode {
private:
    OPCUAModernDataStruct* m_var;                // 指向当前变量
    std::vector<OPCUAModernDataStruct> m_varVec; // 存储数据块的所有变量
    bool m_cacheValid = false;
    std::shared_ptr<OPCUADeviceReader> m_reader;

public:
    // 构造函数
    explicit StructuredDataNode(const std::vector<OPCUAModernDataStruct> &varVec);
    explicit StructuredDataNode(OPCUAModernDataStruct *var);

    // ===== 元信息接口 =====
    std::string getName() const override;
    S7DataType getDataType() const override;
    int getAccessLevel() const override;
    std::string getProtocolType() const override;
    std::string getNodeId() const override;
    std::string getDescrition() const override;

    // ===== 读取操作 =====
    Result<QVariant, RichError> readValue() override;
    Result<bool, RichError> readValueFromPLC() override;

    // ===== 写入操作 =====
    Result<bool, RichError> writeValue(const QVariant& value) override;
    Result<bool, RichError> writeValueToPLC() override;

    //  =====trim function====
    void emitBatchData();
};
