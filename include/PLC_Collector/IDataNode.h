// ============================================================
// IDataNode.h - 统一数据节点接口
// ============================================================

#pragma once
#include <string>
#include <QVariant>
#include "Rust_error_deal/error_deal.h"
#include "PLC/Struct.h"
#include "PLC_Collector/data_types.h"

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
class IDataNode:public QObject {
    Q_OBJECT
public:
    virtual ~IDataNode() = default;

    // ===== 1. 元信息（所有节点必须提供）=====
    virtual std::string getName() const = 0;           // 变量名，如 "Temperature"
    virtual S7DataType getDataType() const = 0;        // BOOL/INT/REAL/STRING/STRUCT/ARRAY
    virtual int getAccessLevel() const = 0;            // 0=只读, 1=读写, 2=只写

    // ===== 2. 数值操作（仅叶子节点有效）=====
    virtual Result<QVariant, RichError> readValue() = 0;
    virtual Result<bool, RichError> writeValue(const QVariant& value) = 0;
    virtual Result<bool,RichError> readValueFromPLC() = 0;
    virtual Result<bool,RichError> writeValueToPLC() = 0;

    // ===== 4. 可选：协议特定信息（调试用）=====
    virtual std::string getProtocolType() const = 0;   // "S7" / "OPCUA" / "MODBUS"
    virtual std::string getNodeId() const = 0;         // 协议原生ID，如 "ns=3;s=DB111"
    virtual std::string getDescrition() const = 0;

  signals:
    // ✅ 推荐：批量数据就绪信号，传递的是轻量级、扁平的通用结构
    void batchDataReady(const std::vector<PLCData>& records);
};