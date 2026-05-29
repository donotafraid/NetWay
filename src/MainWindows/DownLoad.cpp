#include "MainWindows/DownLoad.h"

//  DownLoadRouter------------------------------------------
DownloadSignalRouter::DownloadSignalRouter(DownloadArchitectureCoordinator* manager)
    : m_manager(manager)
    , m_running(true)
    , m_stats(std::make_unique<StatisticsData>())
{
    if (!m_manager) {
        throw std::invalid_argument("DownloadArchitectureCoordinator cannot be null");
    }
    
    // 启动处理线程
    m_worker = std::thread(&DownloadSignalRouter::processLoop, this);
}

// 析构函数
DownloadSignalRouter::~DownloadSignalRouter() {
    stop();
}

// 移动构造函数
DownloadSignalRouter::DownloadSignalRouter(DownloadSignalRouter&& other) noexcept
    : m_manager(other.m_manager)
    , m_running(other.m_running.load())
    , m_stats(std::move(other.m_stats))
{
    // 移动队列
    std::lock_guard<std::mutex> lock(other.m_queueMutex);
    m_queue = std::move(other.m_queue);
    
    // 移动线程
    if (other.m_worker.joinable()) {
        m_worker = std::move(other.m_worker);
    }
}

// 移动赋值运算符
DownloadSignalRouter& DownloadSignalRouter::operator=(DownloadSignalRouter&& other) noexcept {
    if (this != &other) {
        stop();
        
        m_manager = other.m_manager;
        m_running = other.m_running.load();
        
        {
            std::lock_guard<std::mutex> lock(other.m_queueMutex);
            m_queue = std::move(other.m_queue);
        }
        
        if (other.m_worker.joinable()) {
            m_worker = std::move(other.m_worker);
        }
        
        m_stats = std::move(other.m_stats);
    }
    return *this;
}

// 接收信号
void DownloadSignalRouter::routeSignal(const DownloadSignal& signal) {
    if (!m_running) {
        qWarning() << "DownloadSignalRouter is not running, dropping signal";
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.push(signal);
    }
    m_cv.notify_one();
}

// 批量接收信号
void DownloadSignalRouter::routeSignals(const std::vector<DownloadSignal>& signals_vector) {
    if (!m_running) {
        qWarning() << "DownloadSignalRouter is not running, dropping signals";
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        for (const auto& signal : signals_vector) {
            m_queue.push(signal);
        }
    }
    m_cv.notify_one();
}

// 处理循环
void DownloadSignalRouter::processLoop() {
    qDebug() << "DownloadSignalRouter processing thread started";
    
    while (m_running) {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        
        // 等待信号或停止信号
        m_cv.wait(lock, [this] { 
            return !m_queue.empty() || !m_running; 
        });
        
        // 处理队列中的所有信号
        while (!m_queue.empty() && m_running) {
            auto signal = m_queue.front();
            m_queue.pop();
            lock.unlock();
            
            // 处理信号
            processSignal(signal);
            
            lock.lock();
        }
    }
    
    // 清理：处理剩余的信号
    if (!m_queue.empty()) {
        qWarning() << "DownloadSignalRouter stopping, processing remaining signals:" 
                   << m_queue.size();
        
        std::unique_lock<std::mutex> lock(m_queueMutex);
        while (!m_queue.empty()) {
            auto signal = m_queue.front();
            m_queue.pop();
            lock.unlock();
            
            processSignal(signal);
            
            lock.lock();
        }
    }
    
    qDebug() << "DownloadSignalRouter processing thread stopped";
}

// 处理单个信号
void DownloadSignalRouter::processSignal(const DownloadSignal& signal) {
    bool success = false;
    
    try {
        switch (signal.type) {
            case DownloadSignalType::ADD_FILE: {
                auto* fileInfo = signal.data.value<FileProgressItem*>();
                if (fileInfo) {
                    success = m_manager->addFile(fileInfo);
                } else {
                    qWarning() << "Invalid file info in ADD_FILE signal";
                }
                break;
            }
            
            case DownloadSignalType::DELETE_FILE: {
                auto* fileInfo = signal.data.value<FileProgressItem*>();
                if (fileInfo) {
                    success = m_manager->deleteFile(fileInfo);
                } else {
                    qWarning() << "Invalid file info in DELETE_FILE signal";
                }
                break;
            }
            
            case DownloadSignalType::PAUSE_DOWNLOAD:
                m_manager->setPause(true);
                success = true;
                break;
                
            case DownloadSignalType::RESUME_DOWNLOAD:
                m_manager->setPause(false);
                success = true;
                break;
                
            case DownloadSignalType::QUERY_PROGRESS: {
                float progress = m_manager->getProgress();
                if (signal.callback) {
                    signal.callback(true, QString::number(progress));
                }
                success = true;
                break;
            }
            
            case DownloadSignalType::MERGE_COMPLETE:
                // TODO: 实现合并完成逻辑
                qDebug() << "Merge complete for task:" << signal.taskId;
                success = true;
                break;
                
            case DownloadSignalType::SLICE_DOWNLOADED:
                // TODO: 实现切片下载完成逻辑
                qDebug() << "Slice downloaded for task:" << signal.taskId;
                success = true;
                break;
                
            default:
                qWarning() << "Unknown signal type:" << static_cast<int>(signal.type);
                success = false;
                break;
        }
    } catch (const std::exception& e) {
        handleError(signal, e);
        success = false;
    } catch (...) {
        qCritical() << "Unknown exception in processSignal";
        success = false;
    }
    
    // 执行回调（如果还未执行）
    if (signal.callback && signal.type != DownloadSignalType::QUERY_PROGRESS) {
        signal.callback(success, success ? QString() : "Operation failed");
    }
    
    // 更新统计
    updateStatistics(signal, success);
}

// 处理错误
void DownloadSignalRouter::handleError(const DownloadSignal& signal, const std::exception& e) {
    QString errorMsg = QString("Error processing signal type %1: %2")
                       .arg(static_cast<int>(signal.type))
                       .arg(e.what());
    qCritical() << errorMsg;
    
    if (signal.callback) {
        signal.callback(false, errorMsg);
    }
}

// 更新统计信息
void DownloadSignalRouter::updateStatistics(const DownloadSignal& signal, bool success) {
    if (!m_stats) return;
    
    m_stats->totalProcessed++;
    
    if (success) {
        m_stats->totalSucceeded++;
    } else {
        m_stats->totalFailed++;
    }
    
    std::lock_guard<std::mutex> lock(m_stats->statsMutex);
    m_stats->byType[signal.type]++;
}

// 获取统计信息
DownloadSignalRouter::Statistics DownloadSignalRouter::getStatistics() const {
    Statistics stats;
    
    if (m_stats) {
        stats.totalProcessed = m_stats->totalProcessed.load();
        stats.totalSucceeded = m_stats->totalSucceeded.load();
        stats.totalFailed = m_stats->totalFailed.load();
        stats.queueSize = getQueueSize();
        
        std::lock_guard<std::mutex> lock(m_stats->statsMutex);
        stats.processedByType = m_stats->byType;
    }
    
    return stats;
}

// 重置统计信息
void DownloadSignalRouter::resetStatistics() {
    if (m_stats) {
        m_stats->totalProcessed = 0;
        m_stats->totalSucceeded = 0;
        m_stats->totalFailed = 0;
        
        std::lock_guard<std::mutex> lock(m_stats->statsMutex);
        m_stats->byType.clear();
    }
}

// 清空队列
void DownloadSignalRouter::clearQueue() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    while (!m_queue.empty()) {
        auto signal = m_queue.front();
        m_queue.pop();
        
        // 通知回调队列已被清空
        if (signal.callback) {
            signal.callback(false, "Signal cleared from queue");
        }
    }
}

// 获取队列大小
size_t DownloadSignalRouter::getQueueSize() const {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_queue.size();
}

// 停止路由器
void DownloadSignalRouter::stop() {
    if (!m_running) return;
    
    qDebug() << "Stopping DownloadSignalRouter...";
    m_running = false;
    m_cv.notify_all();
    
    if (m_worker.joinable()) {
        // 等待线程结束，最多5秒
        auto startTime = std::chrono::steady_clock::now();
        const auto timeout = std::chrono::seconds(5);
        
        while (m_worker.joinable() && 
               (std::chrono::steady_clock::now() - startTime) < timeout) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (m_worker.joinable()) {
            qWarning() << "DownloadSignalRouter thread did not stop gracefully, detaching";
            m_worker.detach();
        } else {
            m_worker.join();
        }
    }
    
    qDebug() << "DownloadSignalRouter stopped";
}

// 重启路由器
bool DownloadSignalRouter::restart() {
    if (m_running) {
        qWarning() << "Router is already running";
        return false;
    }
    
    if (m_worker.joinable()) {
        stop();
    }
    
    // 重置状态
    m_running = true;
    clearQueue();
    resetStatistics();
    
    // 启动新线程
    try {
        m_worker = std::thread(&DownloadSignalRouter::processLoop, this);
        qDebug() << "DownloadSignalRouter restarted";
        return true;
    } catch (const std::exception& e) {
        qCritical() << "Failed to restart router:" << e.what();
        m_running = false;
        return false;
    }
}