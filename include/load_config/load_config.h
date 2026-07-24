#ifndef Common_Libraries_Struct_H
#define Common_Libraries_Struct_H

#include <array>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <iostream>
#include <vector>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/conf.h>
#include <zlib.h>
#include <unordered_map>
#include <queue>
#include <future>
#include <memory>
#include <regex>
#include <functional>
#include <unordered_set> 
#include <boost/lockfree/queue.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <backward.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <sqlite3.h>
#include <mqtt/async_client.h>
#include <nlohmann/json.hpp>
#include "spdlog/spdlog.h"
#include <cassert>
#include "proto/message_struct.pb.h"

const uint32_t MAGIC = 0xDEADBEEF;
constexpr size_t SLICE_SIZE = static_cast<size_t>(1024*10);
constexpr size_t MAXIMUM_SLICE_SIZE = static_cast<size_t>(1024* 1.5);
constexpr size_t MAX_DB_FILE_LIMIT = 1*1024*1024*1024;
constexpr size_t THREAD_NUM = 4; 
constexpr int magic = 0xDEADBEEF;
constexpr bool DEBUG_TEST = true;

namespace fs = std::filesystem;

struct download_path_manager
{
    std::string download_folder_path;
};

struct buffer_administrator
{
    std::atomic<int> m_buffer_size = 0;
    std::atomic<int> m_inflight_size = 0;
    int max_buffer_size = 1000;
    int max_inflight_size = 1000;
};

struct ProtocolHeader {
    std::array<uint8_t, 16> file_id {} ;  // 文件唯一ID（UUID，16字节
    uint32_t magic;         // 魔数（如 0xDEADBEEF，4字节）
    uint32_t slice_index = UINT32_MAX;   // 分片索引（从0开始，4字节）
    uint32_t total_slices;  // 总分片数（4字节）
    uint32_t plaintext_size=0;  // 明文数据长度（4字节）
    std::array<uint8_t,32> AES_KEY;    // AES_KEY（32字节）
    std::array<uint8_t, 16> iv;  // 16 字节的二进制数组
    uint32_t ciphertext_len=0;      // 加密后数据长度（4字节）
    bool is_control = false;
};

struct ArrayHash {
    ArrayHash() = default;
    ArrayHash(const ArrayHash &) = default;
    size_t operator()(const std::array<uint8_t, 16> &arr) const noexcept
    {
        // 直接哈希二进制数据（例如使用 FNV-1a 算法）
        constexpr size_t FNV_prime = 0x100000001b3;
        size_t hash = 0xcbf29ce484222325;
        for (uint8_t byte : arr) {
            hash ^= byte;
            hash *= FNV_prime;
        }
        return hash;
    }
};

struct FileInfo
{
    std::string input_file_path;
    std::string output_folder_path;
    std::unordered_map<uint32_t, ProtocolHeader> slice_info_map;
    std::unordered_map<uint32_t,std::vector<uint8_t>> slice_plaintext_Map;
};

struct DB_Info
{
    bool used_status = false;
    ProtocolHeader header_info;
    std::string input_file_path;
    std::string output_file_name;
    std::string db_file_path;
    std::vector<int> slice_index_list;
    std::vector<uint8_t> slice_data_info;
};

struct DB_Info_raw_ptr
{
    std::string* db_file_path = nullptr;
    std::string* input_file_path = nullptr;
    std::string* output_file_path = nullptr;
    ProtocolHeader* header_info = nullptr;
    std::vector<uint8_t>* slice_data_info = nullptr;
    int file_size = 0;
    
    // 添加资源管理辅助函数
    void clear() {
        delete db_file_path;
        delete input_file_path;
        delete output_file_path;
        delete header_info;
        delete slice_data_info;
    }
};

// struct MainWindows_Intermediate_Struct
// {
//     std::string stored_DB_folder_path;
//     std::string input_file_path;
//     std::string output_file_path;
//     std::string db_file_path;
//     int file_size = 0;
//     std::map<std::string,std::pair<int,bool>> terminate_symbol_to_file_map; 
// };

enum class SignalType {
  // 切片相关操作（细粒度）
  SLICE_SAVE,       // 保存单个切片
  SLICE_BATCH_SAVE, // 批量保存切片
  SLICE_QUERY,      // 查询切片
  SLICE_DELETE,     // 删除切片
  SLICE_MERGE,      // 合并切片为文件
  SLICE_MIGRATE,    // 迁移切片
  SLICE_CLEANUP,    // 清理孤立切片
  SLICE_VERIFY,     // 验证切片完整性
  SLICE_REGISTER,

  // 文件相关操作
  FILE_REGISTER,
  FILE_OPERATION,
  FILE_UPLOAD,
  FILE_DOWNLOAD,

  // 系统操作
  SYSTEM_CONTROL,
  SYSTEM_MONITOR,

  // 其他业务模块...
};

// 信号优先级（用于处理顺序）
enum class SignalPriority {
    CRITICAL = 0,    // 关键操作（如数据保存）
    HIGH = 1,        // 高优先级
    NORMAL = 2,      // 普通优先级
    LOW = 3,         // 低优先级（如查询统计）
    BACKGROUND = 4   // 后台任务（如清理）
};

// 信号具体内容
struct ExternalSignal {
  SignalType type;
  SignalPriority priority = SignalPriority::NORMAL;
  std::string command; // 具体命令（可选，用于更细粒度）
  uint64_t timestamp;
  std::string source_id;
  std::string request_id; // 用于追踪链路
  std::queue<mqtt::const_message_ptr> m_tmp_received_messages_queue;

  // 回调函数（用于异步响应）
  std::function<void(bool, const std::string &)> callback;

  ExternalSignal() : timestamp(0) {}

  ExternalSignal(SignalType t, const std::string &cmd,
                 SignalPriority pri = SignalPriority::NORMAL,
                 const std::string &src = "")
      : type(t), priority(pri), command(cmd),
        timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count()),
        source_id(src), request_id(generate_request_id()) {}

private:
  static std::string generate_request_id() {
    // 生成唯一请求ID
    static std::atomic<uint64_t> counter{0};
    return std::to_string(
               std::chrono::steady_clock::now().time_since_epoch().count()) +
           "_" + std::to_string(counter++);
  }
};


#endif