// ============================================================
// IDataNode.h - 统一数据节点接口
// ============================================================

#pragma once
#include <string>
#include <QVariant>
#include "Rust_error_deal/error_deal.h"
#include "PLC/S7TypeStruct.h"
#include "PLC/WriteRequestAddres.h"

#include <QtCore/qthreadpool.h>
#include <QtCore/qobjectdefs.h>
#include <QtCore/qrunnable.h>
#include <QtCore/qmutex.h>
#include <QtCore/qcache.h>
#include <QtCore/qmimedata.h>

/**
 * @brief 统一数据节点接口
 * 
 * 设计目标：
 * 1. 屏蔽 S7/OPC UA/Modbus 的底层差异
 * 2. 提供统一的 读/写/遍历 能力
 * 3. 支持结构化数据（容器节点）和叶子节点（实际数值）
 */
class IDataNode {
public:
  virtual ~IDataNode() = default;
  IDataNode() = default;

  // ===== 1. 元信息（所有节点必须提供）=====
  virtual std::string getName() const = 0; // 变量名，如 "Temperature"
  virtual S7DataType
  getDataType() const = 0;                // BOOL/INT/REAL/STRING/STRUCT/ARRAY
  virtual int getAccessLevel() const = 0; // 0=只读, 1=读写, 2=只写

  // ===== 2. 数值操作（仅叶子节点有效）=====
  virtual ValueType readValue() const = 0;
  virtual Result<bool, RichError> writeValue(const ValueType &value) = 0;

  // ===== 4. 可选：协议特定信息（调试用）=====
  virtual std::string getProtocolType() const = 0; // "S7" / "OPCUA" / "MODBUS"
  virtual std::string getNodeId() const = 0; // 协议原生ID，如 "ns=3;s=DB111"
  virtual std::string getParentPath() const = 0;
  virtual std::string getFullPath() const = 0;
  virtual std::string getDescrition() const = 0;

  virtual void setRawValue(const ValueType &value) = 0; // 协调层批量读完后回填
  virtual bool isDirty() const = 0; // 协调层检查是否需要写入
  virtual void clearDirty() = 0;    // 协调层写入成功后清除
};

class INodeManager:public IDataNode{
    public:
    virtual void setDataTypeLength(int val) = 0 ;
    virtual void setDataByte(const float &val) = 0 ;
    virtual void setDataBit(int val) = 0 ;
    virtual void setPendingStringLength(int val) = 0 ;

    virtual int getDataTypeLength() = 0 ;
    virtual float getDataByte() = 0 ;
    virtual int getDataBit() = 0 ;
    virtual int getPendingStringLength() = 0 ;
    virtual int getNameSpace() = 0 ;
    virtual std::string getFilter() = 0 ;
};