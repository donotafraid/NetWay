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

const uint32_t MAGIC = 0xDEADBEEF;
constexpr size_t SLICE_SIZE = static_cast<size_t>(1024 *10*1);
constexpr size_t MAXIMUM_SLICE_SIZE = static_cast<size_t>(1024 * 1024 * 1.5);
constexpr size_t THREAD_NUM = 4; 
constexpr int magic = 0xDEADBEEF;

namespace fs = std::filesystem;

struct ProtocolHeader {
    std::array<uint8_t, 16> file_id {} ;  // 文件唯一ID（UUID，16字节）// 文件唯一ID（UUID，16字节）
    uint32_t magic;         // 魔数（如 0xDEADBEEF，4字节）
    uint32_t slice_index = UINT32_MAX;   // 分片索引（从0开始，4字节）
    uint32_t total_slices;  // 总分片数（4字节）
    uint32_t plaintext_size=0;  // 明文数据长度（4字节）
    uint32_t ciphertext_len=0;      // 加密后数据长度（4字节）
    std::array<uint8_t,32> AES_KEY;    // AES_KEY（32字节）
    std::array<uint8_t, 16> iv;  // 16 字节的二进制数组
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
    std::string db_file_path;
    std::string input_file_path;
    std::string output_folder_path;
    int file_size = 0;
    ProtocolHeader header_info;
    std::vector<uint8_t> slice_data_info;
};

struct DB_Info_raw_ptr
{
    std::string* db_file_path = nullptr;
    std::string* input_file_path = nullptr;
    std::string* output_folder_path = nullptr;
    ProtocolHeader* header_info = nullptr;
    std::vector<uint8_t>* slice_data_info = nullptr;
    int file_size = 0;
    
    // 添加资源管理辅助函数
    void clear() {
        delete db_file_path;
        delete input_file_path;
        delete output_folder_path;
        delete header_info;
        delete slice_data_info;
    }
};

struct MainWindows_Intermediate_Struct
{
    std::string stored_DB_folder_path;
    std::string source_file_path;
    std::string output_folder_path;
    std::string db_file_path;
    std::string output_file_path;
    int file_size = 0;
    std::map<std::string,std::pair<int,bool>> terminate_symbol_to_file_map; 
};
#endif