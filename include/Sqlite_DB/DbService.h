#pragma once
#include "Sqlite_DB/DBDAO.h"
#include "load_config/load_config.h"

class FileService : public ISignalHandler{
public:
    FileService() = default ;
    
    // Service 回答：在"创建新文件"这个业务场景下，需要做什么
    int createNewFile(const std::string& sourcefile_path);
    bool loadMissingSlicesToMemoryPool(const std::string &file_path);
    
    // 处理信号
    ServiceResult<void> handle_signal(ExternalSignal &signal) override;
    // 获取处理器支持的信号类型
    std::vector<SignalType> get_supported_types() const override;
    // 获取处理器名称（用于调试）
    std::string get_handler_name() const override;

  private:
    FileRecordDAO *file_dao_;
    SliceRecordDAO *slice_dao_;
    memory_pool *memory_pool_pointer = nullptr;
};

class SliceRecordService : public ISignalHandler{
public:
  // 构造函数：注入依赖
  SliceRecordService() = default;
  ~SliceRecordService() = default;

  // ========== 父类接口 ==========
  // 处理信号
  ServiceResult<void> handle_signal(ExternalSignal &signal) override;
  // 获取处理器支持的信号类型
  std::vector<SignalType> get_supported_types() const override;
  // 获取处理器名称（用于调试）
  std::string get_handler_name() const override;

  // ========== 业务接口（对外暴露） ==========
  bool transformMessage(ExternalSignal &signal);

  // 1. 保存切片（带业务校验）
  ServiceResult<void> save_slice();

  // 2. 批量保存切片（带事务和校验）
  ServiceResult<bool> save_slices_batch();

  // 3. 查询文件的所有切片（带缓存和分页）
  ServiceResult<std::vector<TestMsg>>
  get_file_slices(const std::string &file_id, int offset = 0, int limit = 100);

  // 4. 删除文件的所有切片（带清理逻辑）
  ServiceResult<int> delete_file_slices(const std::string &file_id);

  // 5. 获取切片数量（带缓存）
  ServiceResult<int> get_slice_count(const std::string &file_id);

  // 6. 验证切片完整性
  ServiceResult<bool> verify_slices_integrity(const std::string &file_id,
                                              int expected_count);

  // 7. 合并切片为完整文件
  ServiceResult<std::string>
  merge_slices_to_file(const std::string &file_id,
                       const std::string &output_path);

  // 8. 迁移切片数据
  ServiceResult<bool> migrate_slices(const std::string &source_file_id,
                                     const std::string &target_file_id);

  // 9. 获取缺失的切片索引
  ServiceResult<std::vector<int>>
  get_missing_slice_indices(const std::string &file_id,
                            const std::vector<int> &expected_indices);

  // 10. 清理孤立的切片（没有对应文件记录的切片）
  ServiceResult<int> cleanup_orphaned_slices();

  // 11. Decrypt Data
  bool DecryptSharedData(const TestMsg &msg, std::vector<uint8_t> &plaintext);
  std::vector<unsigned char> string_to_bytes(const std::string &str);

private:
  // ========== 内部辅助方法 ==========

  // 业务校验
  bool validate_slice_record(const TestMsg &record, std::string &error_msg);
  bool validate_file_id(const std::string &file_id, std::string &error_msg);

  // 缓存管理（可选）
  void update_cache(const std::string &file_id,
                    const std::vector<SliceRecord> &slices){}
  void invalidate_cache(const std::string &file_id);

  // 加密/解密（示例）
  std::string decrypt_slice(const SliceRecord &record);

private:
  // ========== 依赖注入 ==========
  SliceRecordDAO *dao_;       // DAO层依赖
  ConnectionPool *conn_pool_; // 连接池（用于事务）
  std::queue<TestMsg> m_message_queue;

};

class DownloadFileAPI {
  // 协调多个服务完成复杂操作
  void merge_select_file(request_message &request_message_ref);
  void delete_select_file(request_message &request_message_ref);
};

class DateUtils {
    std::string return_current_date_string();
};

class SequenceUtils {
    static std::vector<int> get_SubConnectiontinuous_sequence(int file_size);
};

class SqlBussinessHandler
{

  FileService *m_FileService = nullptr;
  SliceRecordService  *m_SliceRecordService = nullptr;
};
