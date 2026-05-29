#include "Sqlite_DB/DbService.h"

//DataUnits----------------------------
std::string DateUtils::return_current_date_string()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d"); // 格式化为 "20240520"
    return oss.str();
}

//  FileService------------------------------------
int FileService::createNewFile(const std::string& sourcefile_path)
{
    // 1. 业务：计算分片数量
    int file_size = fs::file_size(sourcefile_path);
    int slice_count = (file_size + SLICE_SIZE - 1) / SLICE_SIZE;
    if(slice_count <= 0) {
        std::cerr << "File size invalid: " << sourcefile_path << std::endl;
        return false;
    }
    
    // 2. 业务：生成文件ID
    std::array<uint8_t,16> file_id;
    {
        boost::uuids::random_generator gen;
        boost::uuids::uuid id = gen();
        std::copy(id.begin(), id.end(), file_id.begin());
    }
    
    // 3. 业务：计算缺失的切片索引
    std::vector<int> missing_indices;
    missing_indices.reserve(slice_count); // 预分配空间，提高性能
    for (int i = 1; i <= slice_count; ++i) {
      missing_indices.push_back(i);
    }

    // 4. 业务：转换为JSON格式
    std::string json_string = nlohmann::json(missing_indices).dump();
    
    // 5. 调用 DAO：把数据存到数据库
    int rc = file_dao_->insertFileRecord(file_id, sourcefile_path, json_string);
    if(rc != 0) {
        std::cerr << "Failed to insert file record" << std::endl;
        return false;
    }
    
    return true;
}

bool FileService::loadMissingSlicesToMemoryPool(const std::string& file_path)
{
    // 1. 调用 DAO 获取原始数据（DAO 不关心内存池）
    std::vector<FileRecordDAO::FileRecordData> records = 
        file_dao_->queryFileRecordsByPath(file_path);
    
    if (records.empty()) {
        return false;
    }
    
    // 2. 业务逻辑：遍历数据，过滤、转换、填充到内存池
    for (const auto& record : records) {
        // 业务判断：只处理有效数据
        if (!record.is_valid) {
            continue;
        }
        
        // 业务操作：从内存池获取对象
        auto request_ptr = memory_pool_pointer->return_ptr();
        if (!request_ptr) {
            continue;  // 内存池无可用对象
        }
        
        // 业务操作：填充数据
        request_ptr->set_is_download(false);
        request_ptr->set_file_id(record.file_id.data());
        request_ptr->set_input_file_path(record.input_file_path);
        request_ptr->set_missing_slices_index_json(record.missing_slices_json);
        
        // 业务操作：放回队列
        memory_pool_pointer->push(std::move(request_ptr));
    }
    
    return true;
}

ServiceResult<void> FileService::handle_signal(ExternalSignal &signal) {return ServiceResult<void>{};}

std::vector<SignalType> FileService::get_supported_types() const {return std::vector<SignalType>{};}

std::string FileService::get_handler_name() const {return {};} 

// SliceRecordService--------------------------------
bool SliceRecordService::transformMessage(ExternalSignal &signal) {
  TestMsg data_information;
  while (signal.m_tmp_received_messages_queue.size()) {
    auto msg = signal.m_tmp_received_messages_queue.front()->to_string();
    data_information.ParseFromString(msg);
    signal.m_tmp_received_messages_queue.pop();
    m_message_queue.push(data_information);
    data_information.Clear();
  }
  if (m_message_queue.size()) {
    return true;
  } else {
    return false;
  }
}

ServiceResult<void> SliceRecordService::handle_signal(ExternalSignal &signal) {

  if (!transformMessage(signal)) {
    return ServiceResult<void>::error("transformMessage exist error !");
  }
  ServiceResult<void> result;

  switch (signal.type) {
  case SignalType::SLICE_SAVE:
    // 假设从 signal.data 中解析 SliceRecord
    {
      auto save_result = save_slice();
      if (!save_result.success) {
        result.success = false;
        result.error_message = save_result.error_message;
      }
    }
    break;

  case SignalType::SLICE_BATCH_SAVE: {
    auto save_result = save_slice();
    if (!save_result.success) {
      result.success = false;
      result.error_message = save_result.error_message;
    }
  } break;

  case SignalType::SLICE_QUERY: {
    // 可以从 signal.command 解析 offset/limit，或使用默认值
    auto query_result = get_file_slices("", 0, 100);
    if (query_result.successStatus) {
    //   result.data = query_result.data;
    } else {
      result.success = false;
      result.error_message = query_result.error_message;
    }
  } break;

  case SignalType::SLICE_DELETE: {
    auto delete_result = delete_file_slices("");
    if (delete_result.successStatus) {
    //   result.data = delete_result.data; // 返回删除数量
    } else {
      result.success = false;
      result.error_message = delete_result.error_message;
    }
  } break;

  case SignalType::SLICE_MERGE: {
    auto merge_result =
        merge_slices_to_file("","");
    if (merge_result.successStatus) {
    //   result.data = merge_result.data; // 返回输出文件路径
    } else {
      result.success = false;
      result.error_message = merge_result.error_message;
    }
  } break;

  case SignalType::SLICE_MIGRATE: {
    auto migrate_result =
        migrate_slices("","");
    if (!migrate_result.successStatus) {
      result.success = false;
      result.error_message = migrate_result.error_message;
    }
  } break;

  case SignalType::SLICE_CLEANUP: {
    auto cleanup_result = cleanup_orphaned_slices();
    if (cleanup_result.successStatus) {
    //   result.data = cleanup_result.data; // 返回清理数量
    } else {
      result.success = false;
      result.error_message = cleanup_result.error_message;
    }
  } break;

  case SignalType::SLICE_VERIFY:
    // 可以从 signal.command 解析参数，如 "file_id:expected_count"
    {
      auto verify_result =
          verify_slices_integrity("",-1);
      if (!verify_result.successStatus) {
        result.success = false;
        result.error_message = verify_result.error_message;
      }
    }
    break;

  case SignalType::SLICE_REGISTER:
    // 注册切片相关操作
    result.success = true;
    result.error_message = "Slice service registered";
    break;

  default:
    result.success = false;
    result.error_message = "Unsupported signal type: " +
                           std::to_string(static_cast<int>(signal.type));
    break;
  }

  return result;
}

std::vector<SignalType> SliceRecordService::get_supported_types() const {
  return std::vector<SignalType>{
      SignalType::SLICE_SAVE,       // 保存单个切片
      SignalType::SLICE_BATCH_SAVE, // 批量保存切片
      SignalType::SLICE_QUERY,      // 查询切片
      SignalType::SLICE_DELETE,     // 删除切片
      SignalType::SLICE_MERGE,      // 合并切片为文件
      SignalType::SLICE_MIGRATE,    // 迁移切片
      SignalType::SLICE_CLEANUP,    // 清理孤立切片
      SignalType::SLICE_VERIFY,     // 验证切片完整性
      SignalType::SLICE_REGISTER,
  };
}

std::string SliceRecordService::get_handler_name() const { return{}; }

// 1. 保存切片（带业务校验）
ServiceResult<void> SliceRecordService::save_slice() {
  // 1. 业务校验
  std::string error_msg;
  const TestMsg msg = m_message_queue.front();
  if (!validate_slice_record(msg, error_msg)) {
    return ServiceResult<void>::error(error_msg, 1001);
  }

  // 2. 检查是否已存在（可选，取决于业务需求）
  auto existing = dao_->select_all_by_file_id(msg.file_id());
  for (const auto &slice : existing) {
    if (slice.slice_index() == msg.slice_index()) {
      return ServiceResult<void>::error("Slice already exists: index=" +
                                            std::to_string(msg.slice_index()),
                                        1002);
    }
  }

  // 3. 执行插入
  bool result = dao_->insert_slice(msg);
  if (!result) {
    return ServiceResult<void>::error("Failed to insert slice", 1003);
  }

  // 5. 审计日志（可选）
  // logger_->log("Slice saved", record.file_id, record.slice_index);

  // 6. queue pop element
  m_message_queue.pop();
  return ServiceResult<void>::successStatus();
}

// 2. 批量保存切片（带事务和校验）
ServiceResult<bool>
SliceRecordService::save_slices_batch() {

  TestMsg msg{};
  std::unordered_set<int> indices;
  bool all_success = true;
  //    start transaction 
  auto conn_guard = conn_pool_->get_SubConnection();
  ConnectionWrapper *conn = conn_guard.get();
  if (!conn || !conn->begin_transaction()) {
    return ServiceResult<bool>::error("Failed to begin transaction", 2006);
  }

  while (m_message_queue.size()) {
    msg = m_message_queue.front();
    // 1. 校验
    std::string error_msg;
    if (!validate_file_id(msg.file_id(), error_msg)) {
      return ServiceResult<bool>::error(error_msg, 2001);
    }

    // 2. 检查索引是否重复
    indices.insert(msg.slice_index());
    if (indices.count(msg.slice_index())) {
      return ServiceResult<bool>::error(
          "Duplicate slice index: " + std::to_string(msg.slice_index()), 2005);
    }

    // 3. 执行批量插入
    if (!dao_->insert_slice(msg)) {
      all_success = false;
      break;
    }

    m_message_queue.pop();
    msg.Clear();
  }

  //    release pointer
  conn_pool_->release_connectionWrapper_ptr(std::move(conn_guard));

  // 4. 提交或回滚
  if (all_success) {
    if (!conn->commit_transaction()) {
      return ServiceResult<bool>::error("Failed to commit transaction", 2007);
    }
    return ServiceResult<bool>::success(true);
  } else {
    conn->rollback_transaction();
    return ServiceResult<bool>::error("Batch insert failed, rolled back", 2008);
  }
}

// 3. 查询文件的所有切片（带分页）
ServiceResult<std::vector<TestMsg>> SliceRecordService::get_file_slices(
    const std::string& file_id, int offset, int limit) {
    
    // 1. 校验
    std::string error_msg;
    if (!validate_file_id(file_id, error_msg)) {
        return ServiceResult<std::vector<TestMsg>>::error(error_msg, 3001);
    }
    
    if (offset < 0 || limit <= 0 || limit > 1000) {
        return ServiceResult<std::vector<TestMsg>>::error(
            "Invalid pagination parameters", 3002);
    }
    
    // 2. 查询数据
    auto all_slices = dao_->select_all_by_file_id(file_id);
    
    // 3. 分页处理
    if (offset >= static_cast<int>(all_slices.size())) {
        return ServiceResult<std::vector<TestMsg>>::success({});
    }
    
    int end = std::min(offset + limit, static_cast<int>(all_slices.size()));
    std::vector<TestMsg> paged_slices(
        all_slices.begin() + offset,
        all_slices.begin() + end
    );
    
    return ServiceResult<std::vector<TestMsg>>::success(std::move(paged_slices));
}

// 4. 删除文件的所有切片（带清理逻辑）
ServiceResult<int> SliceRecordService::delete_file_slices(const std::string& file_id) {
    // 1. 校验
    std::string error_msg;
    if (!validate_file_id(file_id, error_msg)) {
        return ServiceResult<int>::error(error_msg, 4001);
    }
    
    // 2. 先获取数量（用于返回）
    int count = dao_->count_by_file_id(file_id);
    if (count <= 0) {
        return ServiceResult<int>::success(0);  // 没有需要删除的
    }
    
    // 3. 备份数据（可选，用于恢复）
    // auto backup = dao_->select_all_by_file_id(file_id);
    // backup_service_->save_backup(file_id, backup);
    
    // 4. 执行删除
    bool result = dao_->delete_by_file_id(file_id);
    if (!result) {
        return ServiceResult<int>::error("Failed to delete slices", 4002);
    }
    
    // 5. 清理缓存
    invalidate_cache(file_id);
    
    // 6. 记录审计日志
    // logger_->log("Slices deleted", file_id, count);
    
    return ServiceResult<int>::success(count);
}

// 5. 获取切片数量（带缓存）
ServiceResult<int> SliceRecordService::get_slice_count(const std::string& file_id) {
    // 1. 校验
    std::string error_msg;
    if (!validate_file_id(file_id, error_msg)) {
        return ServiceResult<int>::error(error_msg, 5001);
    }
    
    // 2. 检查缓存（可选）
    // auto cached_count = cache_->get_count(file_id);
    // if (cached_count) return ServiceResult::success(*cached_count);
    
    // 3. 查询数量
    int count = dao_->count_by_file_id(file_id);
    if (count < 0) {
        return ServiceResult<int>::error("Failed to get slice count", 5002);
    }
    
    // 4. 更新缓存
    // cache_->set_count(file_id, count);
    
    return ServiceResult<int>::success(count);
}

// 6. 验证切片完整性
ServiceResult<bool> SliceRecordService::verify_slices_integrity(
    const std::string& file_id, int expected_count) {
    
    // 1. 获取实际数量
    auto count_result = get_slice_count(file_id);
    if (!count_result.successStatus) {
        return ServiceResult<bool>::error(count_result.error_message, 6001);
    }
    
    // 2. 验证数量
    if (count_result.data != expected_count) {
        return ServiceResult<bool>::error(
            "Slice count mismatch: expected=" + std::to_string(expected_count) +
            ", actual=" + std::to_string(count_result.data),
            6002
        );
    }
    
    // 3. 获取所有索引，验证连续性
    auto slices = dao_->select_all_by_file_id(file_id);
    for (size_t i = 0; i < slices.size(); ++i) {
        if (slices[i].slice_index() != static_cast<int>(i)) {
            return ServiceResult<bool>::error(
                "Slice index not sequential at position " + std::to_string(i),
                6003
            );
        }
    }
    
    // 4. 验证每个切片的加密数据（可选）
    for (const auto& slice : slices) {
        if (slice.aes_key().size() != 32) {
            return ServiceResult<bool>::error(
                "Invalid AES key length for slice " + std::to_string(slice.slice_index()),
                6004
            );
        }
        if (slice.iv().size() != 16) {
            return ServiceResult<bool>::error(
                "Invalid IV length for slice " + std::to_string(slice.slice_index()),
                6005
            );
        }
    }
    
    return ServiceResult<bool>::success(true);
}

// 7. 合并切片为完整文件（核心业务逻辑）
ServiceResult<std::string> SliceRecordService::merge_slices_to_file(
    const std::string& file_id, const std::string& output_path) {
    
    // 1. 查询所有切片
    auto slices_result = get_file_slices(file_id, 0, 10000);
    if (!slices_result.successStatus) {
        return ServiceResult<std::string>::error(
            slices_result.error_message, 7001);
    }
    
    auto& slices = slices_result.data;
    if (slices.empty()) {
        return ServiceResult<std::string>::error("No slices found", 7002);
    }
    
    // 2. 验证完整性（检查索引是否连续）
    for (size_t i = 0; i < slices.size(); ++i) {
        if (slices[i].slice_index() != static_cast<int>(i)) {
            return ServiceResult<std::string>::error(
                "Missing slice at index " + std::to_string(i), 7003);
        }
    }
    
    // 3. 打开输出文件
    std::ofstream output_file(output_path, std::ios::binary);
    if (!output_file.is_open()) {
        return ServiceResult<std::string>::error(
            "Cannot open output file: " + output_path, 7004);
    }
    
    std::vector<uint8_t> dest_vector{};
    // 4. 解密并写入每个切片
    for (const auto& slice : slices) {
        // 解密切片（示例：假设需要解密）
        std::string decrypted_data = decrypt_slice({});
        DecryptSharedData(slice, dest_vector);

        // 写入文件
        output_file.write(reinterpret_cast<const char *>(dest_vector.data()),
                          dest_vector.size());
        if (!output_file.good()) {
            return ServiceResult<std::string>::error(
                "Write failed at slice " + std::to_string(slice.slice_index()), 7005);
        }
    }
    
    output_file.close();
    
    // 5. 验证输出文件大小
    // auto file_size = std::filesystem::file_size(output_path);
    // if (file_size == 0) { ... }
    
    return ServiceResult<std::string>::success(output_path);
}

// 8. 迁移切片数据
ServiceResult<bool> SliceRecordService::migrate_slices(
    const std::string& source_file_id,
    const std::string& target_file_id) {
    
    // 1. 校验
    if (source_file_id == target_file_id) {
        return ServiceResult<bool>::error("Source and target are the same", 8001);
    }
    
    // 2. 查询源数据
    auto slices = dao_->select_all_by_file_id(source_file_id);
    if (slices.empty()) {
        return ServiceResult<bool>::error("No slices to migrate", 8002);
    }
    
    // // 3. 修改file_id
    // for (auto& slice : slices) {
    //     slice.file_id() = target_file_id;
    // }
    
    // 4. 开启事务
    auto conn_guard = conn_pool_->get_SubConnection();
    ConnectionWrapper* conn = conn_guard.get();
    if (!conn || !conn->begin_transaction()) {
        return ServiceResult<bool>::error("Failed to begin transaction", 8003);
    }
    
    // 5. 插入到目标
    bool success = true;
    for (const auto& slice : slices) {
        if (!dao_->insert_slice(slice)) {
            success = false;
            break;
        }
    }
    
    // 6. 删除源数据
    if (success) {
        success = dao_->delete_by_file_id(source_file_id);
    }
    
    // 7. 提交或回滚
    if (success) {
        conn->commit_transaction();
        invalidate_cache(source_file_id);
        invalidate_cache(target_file_id);
        return ServiceResult<bool>::success(true);
    } else {
        conn->rollback_transaction();
        return ServiceResult<bool>::error("Migration failed, rolled back", 8004);
    }
}

// 9. 获取缺失的切片索引
ServiceResult<std::vector<int>> SliceRecordService::get_missing_slice_indices(
    const std::string &file_id, const std::vector<int> &expected_indices) {

  // 1. 获取实际存在的索引
  auto actual_indices = dao_->get_slice_indices(file_id);

  // 2. 使用set进行差集计算
  std::unordered_set<int> actual_set(actual_indices.begin(),
                                     actual_indices.end());
  std::vector<int> missing_indices;

  for (int idx : expected_indices) {
    if (actual_set.find(idx) == actual_set.end()) {
      missing_indices.push_back(idx);
    }
  }

  return ServiceResult<std::vector<int>>::success(std::move(missing_indices));
}

// 10. 清理孤立的切片
ServiceResult<int> SliceRecordService::cleanup_orphaned_slices() {
    // 这个需要配合 FileRecordService 使用
    // 找出所有没有对应文件记录的切片并删除
    
    // 示例实现（简化版）
    int total_deleted = 0;
    
    // 获取所有唯一的file_id
    // auto all_file_ids = dao_->get_all_distinct_file_ids();
    
    // for (const auto& file_id : all_file_ids) {
    //     auto file_exists = file_service_->exists(file_id);
    //     if (!file_exists) {
    //         int deleted = delete_file_slices(file_id).data;
    //         total_deleted += deleted;
    //     }
    // }
    
    return ServiceResult<int>::success(total_deleted);
}

bool SliceRecordService::validate_slice_record(const TestMsg &record,
                                               std::string &error_msg) {
  if (record.file_id().empty()) {
    error_msg = "File ID cannot be empty";
    return false;
  }

  if (record.slice_index() < 0) {
    error_msg = "Slice index must be non-negative";
    return false;
  }

  if (record.aes_key().size() != 32) {
    error_msg = "AES key must be 32 bytes";
    return false;
  }

  if (record.iv().size() != 16) {
    error_msg = "IV must be 16 bytes";
    return false;
  }

  if (record.ciphertext().empty()) {
    error_msg = "Plaintext cannot be empty";
    return false;
  }

  return true;
}

bool SliceRecordService::validate_file_id(const std::string& file_id, 
                                           std::string& error_msg) {
    if (file_id.empty()) {
        error_msg = "File ID cannot be empty";
        return false;
    }
    
    // 可以添加更多校验，如长度限制、格式要求等
    if (file_id.length() > 256) {
        error_msg = "File ID too long (max 256)";
        return false;
    }
    
    return true;
}

void SliceRecordService::invalidate_cache(const std::string& file_id) {
    // 实现缓存失效逻辑
    // if (cache_) cache_->invalidate(file_id);
}

std::string SliceRecordService::decrypt_slice(const SliceRecord& record) {
    // 实现解密逻辑
    // 这里简化处理，直接返回plaintext
    return record.plaintext;
}

bool SliceRecordService::DecryptSharedData(const TestMsg &msg, std::vector<uint8_t> &plaintext)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if(!ctx)
    {
        return false;
    }
    std::vector<unsigned char> iv{string_to_bytes(msg.iv())};
    std::vector<unsigned char> aes_key{string_to_bytes(msg.aes_key())};
    std::vector<unsigned char> ciphertext{string_to_bytes(msg.ciphertext())};

    const EVP_CIPHER* cipherType = EVP_aes_256_cbc();
    if(EVP_DecryptInit_ex(ctx,cipherType,nullptr,aes_key.data(),iv.data()) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    plaintext.resize(msg.plaintext_size() + EVP_CIPHER_CTX_block_size(ctx));
    int len , plainLen = 0 ; 
    if ( EVP_DecryptUpdate(ctx , plaintext.data() , &len , ciphertext.data() , ciphertext.size()) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    plainLen = len ;

    if( EVP_DecryptFinal_ex(ctx , plaintext.data() + len , &len) != 1)
    {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        std::cerr<<"EVP_EncryptInit_ex failed and error is : "<<err_buf<<std::endl;
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    plainLen += len ;
    plaintext.resize(plainLen);

    EVP_CIPHER_CTX_free(ctx);
    return true;
}

std::vector<unsigned char>
SliceRecordService::string_to_bytes(const std::string &str) {
  std::vector<unsigned char> bytes(str.size());
  std::memcpy(bytes.data(), str.data(), str.size());
  return bytes;
}