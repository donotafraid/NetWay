#pragma once
#include "PLC/Struct.h"
#include "PLC_Collector/IDataNode.h"

class OpcUaDataNode : public INodeManager {
private:
  OPCUAModernDataStruct m_var;                       // 指向当前变量
  bool m_dirty  = false;

public:
    // 构造函数
    explicit OpcUaDataNode(OPCUAModernDataStruct &var);
    explicit OpcUaDataNode(OPCUAModernDataStruct &&var);

    // ===== 元信息接口 =====
    std::string getName() const override;
    S7DataType getDataType() const override;
    int getAccessLevel() const override;
    std::string getProtocolType() const override;
    std::string getNodeId() const override;
    std::string getDescrition() const override;
    std::string getParentPath() const override;
    std::string getFullPath() const override;
    void setRawValue(const ValueType &value) override; // 协调层批量读完后回填
    bool isDirty() const override; // 协调层检查是否需要写入
    void clearDirty() override;    // 协调层写入成功后清除

    void setDataTypeLength(int val)  override;
    void setDataByte(const float &val)  override;
    void setDataBit(int val)  override;
    void setPendingStringLength(int val)  override;

    int getDataTypeLength()  override;
    float getDataByte()  override;
    int getDataBit()  override;
    int getPendingStringLength()  override;
    int getNameSpace() override;
    std::string getFilter() override;
    // ===== 读取操作 =====
    ValueType readValue() const override;
    // ===== 写入操作 =====
    Result<bool, RichError> writeValue(const ValueType& value) override;

    //  =====trim function====
    void emitBatchData();
};
