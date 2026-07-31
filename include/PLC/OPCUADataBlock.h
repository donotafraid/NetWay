#pragma once

#include <open62541/nodeids.h>
#include <open62541/client.h>
#include <open62541/config.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_config_default.h>

#include "PLC/Struct.h"


// 职责：只存储数据和变量定义
class OPCUADataBlock {
public:
    // 构造函数
    OPCUADataBlock() = default;
    explicit OPCUADataBlock(const std::string& name, const std::string& ip);
    
    // 基本数据访问
    const std::string& getBlocktName() const;
    std::vector<OPCUAModernDataStruct>& getVariabeDataVector();
    bool hasVariable(const std::string& path) const;
    const int getVariableVectorSize() const;
    std::vector<uint8_t>& getVariableDataBuffer();
    
    // 获取变量映射
    Result<bool, RichError> setOPCUAParseResult(const std::shared_ptr<OPCUAParseResult> &parseResult);
    Result<bool, RichError> setVariableMap(std::vector<OPCUAModernDataStruct>& m_variable_vector);
    std::string getIdentifier();
    
    // set part
    void setName(const std::string& name);
    void setIpAddres(const std::string& ip_Address);
    void setParseResult(const std::shared_ptr<OPCUAParseResult> &parseResult);
    void calculateDataBlockLength(int length) {
      m_whole_data_block_length += length;
      if (dataBuffer.size() == 0) {
        dataBuffer.resize(m_whole_data_block_length);
      }
    };

    // update function
    void updateBufferFromS7ModernStructByLSB(
        std::vector<OPCUAModernDataStruct> &vector);
    void updateBufferFromS7ModernStructByMSB(
        std::vector<OPCUAModernDataStruct> &vector);

    // Model operation   
    // Result<QVariant, RichError> readValue(QModelIndex index) const;
    
    // trait function
    Result<bool, RichError> isArray(OPCUAModernDataStruct& data_var);
    
    // 批量操作
    Result<bool, RichError> batchReadValues(const std::vector<std::string>& paths, 
                                            std::vector<QVariant>& out_values) const;
    Result<bool, RichError> batchWriteValues(const std::vector<std::string>& paths,
                                             const std::vector<QVariant>& values);

  private:
    std::shared_ptr<OPCUAParseResult> m_variable;
    std::string data_block_name;
    std::string ip_address;
    std::vector<uint8_t> dataBuffer;
    int m_whole_data_block_length = 0;
    
    // 辅助方法
    const OPCUAModernDataStruct* findVariableByPath(const std::string& path) const;
    bool validatePath(const std::string& path, std::string& error_msg) const;
};