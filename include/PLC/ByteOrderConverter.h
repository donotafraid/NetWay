#pragma once

#include <cstdint>
#include <vector>
#include <cstring>
#include <type_traits>

class ByteOrderConverter {
public:
    // ==================== 核心转换函数 ====================
    
    /**
     * @brief 将值转换为大端序字节序列
     * @tparam T 数值类型（必须为整数类型）
     * @param value 要转换的值
     * @return std::vector<uint8_t> 大端序字节序列
     */
    template <typename T>
    static std::vector<uint8_t> toBigEndian(T value) {
        static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>,
                      "T must be integral or floating point type");
        
        std::vector<uint8_t> byte_array(sizeof(T), 0);
        toBigEndian(value, byte_array.data(), sizeof(value));
        return byte_array;
    }

    /**
     * @brief 将值转换为小端序字节序列
     * @tparam T 数值类型（必须为整数类型）
     * @param value 要转换的值
     * @return std::vector<uint8_t> 小端序字节序列
     */
    template <typename T>
    static std::vector<uint8_t> toLittleEndian(T value) {
        static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>,
                      "T must be integral or floating point type");
        
        std::vector<uint8_t> byte_array(sizeof(T), 0);
        toLittleEndian(value, byte_array.data(), sizeof(value));
        return byte_array;
    }

    /**
     * @brief 将值转换为大端序并写入缓冲区
     * @tparam T 数值类型
     * @param value 要转换的值
     * @param buffer 输出缓冲区
     * @param size 缓冲区大小（应为 sizeof(T)）
     */
    template <typename T>
    static void toBigEndian(T value, void* buffer, size_t size) {
        static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>,
                      "T must be integral or floating point type");
        
        uint8_t* ptr = reinterpret_cast<uint8_t*>(buffer);
        
        // 对于浮点数，先转换为整数类型（按位拷贝）
        if constexpr (std::is_floating_point_v<T>) {
            // 使用 memcpy 将浮点数的位模式复制到整数
            using IntType = typename std::conditional<
                std::is_same_v<T, float>, uint32_t,
                typename std::conditional<
                    std::is_same_v<T, double>, uint64_t,
                    void
                >::type
            >::type;
            
            static_assert(!std::is_same_v<IntType, void>, "Unsupported floating point type");
            
            IntType intValue;
            std::memcpy(&intValue, &value, sizeof(T));
            toBigEndianInteger(intValue, ptr, size);
        } else {
            // 整数类型直接处理
            toBigEndianInteger(value, ptr, size);
        }
    }

    /**
     * @brief 将值转换为小端序并写入缓冲区
     * @tparam T 数值类型
     * @param value 要转换的值
     * @param buffer 输出缓冲区
     * @param size 缓冲区大小（应为 sizeof(T)）
     */
    template <typename T>
    static void toLittleEndian(T value, void* buffer, size_t size) {
        static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>,
                      "T must be integral or floating point type");
        
        uint8_t* ptr = reinterpret_cast<uint8_t*>(buffer);
        
        // 对于浮点数，先转换为整数类型（按位拷贝）
        if constexpr (std::is_floating_point_v<T>) {
            using IntType = typename std::conditional<
                std::is_same_v<T, float>, uint32_t,
                typename std::conditional<
                    std::is_same_v<T, double>, uint64_t,
                    void
                >::type
            >::type;
            
            static_assert(!std::is_same_v<IntType, void>, "Unsupported floating point type");
            
            IntType intValue;
            std::memcpy(&intValue, &value, sizeof(T));
            toLittleEndianInteger(intValue, ptr, size);
        } else {
            // 整数类型直接处理
            toLittleEndianInteger(value, ptr, size);
        }
    }

    // ==================== 便捷转换函数（返回 std::vector<uint8_t>） ====================
    
    static std::vector<uint8_t> uint16ToBigEndian(uint16_t value) {
        return toBigEndian(value);
    }

    static std::vector<uint8_t> uint32ToBigEndian(uint32_t value) {
        return toBigEndian(value);
    }

    static std::vector<uint8_t> uint64ToBigEndian(uint64_t value) {
        return toBigEndian(value);
    }

    static std::vector<uint8_t> int16ToBigEndian(int16_t value) {
        return toBigEndian(static_cast<uint16_t>(value));
    }

    static std::vector<uint8_t> int32ToBigEndian(int32_t value) {
        return toBigEndian(static_cast<uint32_t>(value));
    }

    static std::vector<uint8_t> int64ToBigEndian(int64_t value) {
        return toBigEndian(static_cast<uint64_t>(value));
    }

    static std::vector<uint8_t> floatToBigEndian(float value) {
        return toBigEndian(value);
    }

    static std::vector<uint8_t> doubleToBigEndian(double value) {
        return toBigEndian(value);
    }

    // ==================== 反向转换：从字节序列恢复数值 ====================
    
    /**
     * @brief 从大端序字节序列恢复数值
     * @tparam T 目标数值类型
     * @param bytes 大端序字节序列
     * @param offset 起始偏移量
     * @return T 恢复的数值
     */
    template <typename T>
    static T fromBigEndian(const std::vector<uint8_t>& bytes, size_t offset = 0) {
        static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>,
                      "T must be integral or floating point type");
        
        if (offset + sizeof(T) > bytes.size()) {
            throw std::out_of_range("Byte vector too small for type");
        }
        
        T value;
        fromBigEndian(bytes.data() + offset, sizeof(T), value);
        return value;
    }

    /**
     * @brief 从大端序字节缓冲区恢复数值
     * @tparam T 目标数值类型
     * @param buffer 大端序字节缓冲区
     * @param size 缓冲区大小
     * @param value 输出参数：恢复的数值
     */
    template <typename T>
    static void fromBigEndian(const void* buffer, size_t size, T& value) {
        static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>,
                      "T must be integral or floating point type");
        
        if (size != sizeof(T)) {
            throw std::invalid_argument("Buffer size must match type size");
        }
        
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(buffer);
        
        if constexpr (std::is_floating_point_v<T>) {
            // 浮点数：先还原为整数位模式，再转换为浮点
            using IntType = typename std::conditional<
                std::is_same_v<T, float>, uint32_t,
                typename std::conditional<
                    std::is_same_v<T, double>, uint64_t,
                    void
                >::type
            >::type;
            
            static_assert(!std::is_same_v<IntType, void>, "Unsupported floating point type");
            
            IntType intValue = 0;
            fromBigEndianInteger(ptr, size, intValue);
            std::memcpy(&value, &intValue, sizeof(T));
        } else {
            // 整数类型直接还原
            fromBigEndianInteger(ptr, size, value);
        }
    }

private:
    // ==================== 内部辅助函数 ====================
    
    /**
     * @brief 整数类型的大端序转换（内部实现）
     */
    template <typename IntType>
    static void toBigEndianInteger(IntType value, uint8_t* buffer, size_t size) {
        static_assert(std::is_integral_v<IntType>, "IntType must be integral");
        
        for (size_t i = 0; i < size; i++) {
            // 大端序：高位字节在前
            buffer[i] = static_cast<uint8_t>((value >> (8 * (size - i - 1))) & 0xFF);
        }
    }

    /**
     * @brief 整数类型的小端序转换（内部实现）
     */
    template <typename IntType>
    static void toLittleEndianInteger(IntType value, uint8_t* buffer, size_t size) {
        static_assert(std::is_integral_v<IntType>, "IntType must be integral");
        
        for (size_t i = 0; i < size; i++) {
            // 小端序：低位字节在前
            buffer[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
        }
    }

    /**
     * @brief 从大端序字节还原整数（内部实现）
     */
    template <typename IntType>
    static void fromBigEndianInteger(const uint8_t* buffer, size_t size, IntType& value) {
        static_assert(std::is_integral_v<IntType>, "IntType must be integral");
        
        value = 0;
        for (size_t i = 0; i < size; i++) {
            value |= static_cast<IntType>(buffer[i]) << (8 * (size - i - 1));
        }
    }

    /**
     * @brief 从小端序字节还原整数（内部实现）
     */
    template <typename IntType>
    static void fromLittleEndianInteger(const uint8_t* buffer, size_t size, IntType& value) {
        static_assert(std::is_integral_v<IntType>, "IntType must be integral");
        
        value = 0;
        for (size_t i = 0; i < size; i++) {
            value |= static_cast<IntType>(buffer[i]) << (8 * i);
        }
    }
};