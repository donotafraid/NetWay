#pragma  once

#include <variant>
#include <string>
#include <system_error>
#include <functional>
#include <iostream>

struct RichError {
std::string context; // 额外的上下文信息

RichError(const std::string& context):context(std::move(context)){}
const char* what() const {
    return context.c_str();
}
};


template <typename T, typename E>
class Result{
    private:
        //  member variable
        std::variant<T,E> result;
    public:
      using ValueType = T;
      using ErrorType = E;
      //  initialization function
      Result(T success) : result(std::move(success)) {}
      Result(E fail) : result(std::move(fail)) {}

      template <typename U> struct is_result_type : std::false_type {};

      template <typename U, typename F>
      struct is_result_type<Result<U, F>> : std::true_type {};

      //  Copy constructor
      Result(const Result<T, E> &other) : result(other.result) {}
      //  Copy assignment operator
      Result &operator=(const Result<T, E> &other) {
        result = other.result;
        return *this;
      }

        //  Move constructor
        Result(Result<T, E> &&other) noexcept
            : result(std::move(other.result)) {}
        //  Move assignment operator
        Result &operator=(Result<T, E> &&other) noexcept {
          result = std::move(other.result);
          return *this;
        }

        //  check result type function
        bool is_success() const{
            /*
            在const成员函数内部：

            不能修改类的成员变量（除非是mutable的）

            只能调用其他const成员函数
            */
            return std::holds_alternative<T>(result);
        }

        bool is_fail() const{
            return std::holds_alternative<E>(result);
        }

        //  unpacking function
        T& unwrap_returnLeftValue()  &{
            if(is_success()){
                return std::get<T>(result);
            }
            else{
                std::cerr<<"Error: Failed to unwrap_returnLeftValue"<<std::endl;
                std::terminate();
            }
        }

        T&& unwrap_returnRightValue() &&{
            if(is_success()){
                return std::get<T>(std::move(result));
            }
            else{
                std::cerr<<"Error: Failed to unwrap_returnRightValue"<<std::endl;
                std::terminate();
            }
        }

        E& unwrap_err()  &{
            if(is_fail()){
                return std::get<E>(result);
            }
            else{
                std::cerr<<"Error: Failed to unwrap_err_left"<<std::endl;
                std::terminate();
            }
        }

        E&& unwrap_err()  &&{
            if(is_fail()){
                return std::get<E>(std::move(result));
            }
            else{
                std::cerr<<"Error: Failed to unwrap_err_right"<<std::endl;
                std::terminate();
            }
        }

        //  template result transform
        //  NEED ENSURE CALLABLE RETURN NON-RESULT 
        template<typename callable>
        auto transform_func(callable&& transform_callable)->
             Result<std::decay_t<decltype( transform_callable( std::declval<T&>() ))>,RichError>
        {
            //  BUILD RETURN TYPE OF CALLABLE 
            using R = std::decay_t< decltype(transform_callable(std::declval<T&>())) >;
            if(is_success())
            {
                auto&& arg = unwrap_returnLeftValue();
                return Result<R,RichError>(transform_callable(arg));
            }
            else {
                return Result<R,RichError>(unwrap_err());
            }
        }

        //  NEED ENSURE CALLABLE RETURN RESULT
        template <typename callable>
        auto and_then(callable &&transform_callable) {
          // 1. 获取 callable 的返回类型
          using RawReturnType = std::invoke_result_t<callable, T &>;
          using ReturnType = std::decay_t<RawReturnType>; // 去掉 cv 和引用

          // 直接使用类型 trait，编译期常量
          static_assert(is_result_type<ReturnType>::value,
                        "and_then's callable must return a Result type");

          // 3. 提取 callable 返回的 Result 的成功和错误类型
          using ReturnValueType = typename ReturnType::ValueType;
          using ReturnErrorType = typename ReturnType::ErrorType;

          // 4. 可选：检查错误类型兼容性
          static_assert(
              std::is_same_v<E, ReturnErrorType>,
              "Error type mismatch: callable must return Result<U, same E>");

          if (is_success()) {
            // 成功路径：直接返回 callable 的结果
            return transform_callable(unwrap_returnLeftValue());
          } else {
            // 错误路径：构造失败状态的 ReturnType
            // 关键：用当前错误值构造 ReturnType，而不是直接返回错误值
            return ReturnType(unwrap_err());
          }
        }

        //  result recover
        template<typename callable>
       auto recover(callable&& recover_callable)->
             decltype(recover_callable( std::declval<E>()   ))
        {
            {
                auto&& arg = unwrap_err();
                return recover_callable( std::forward<  decltype(arg) > (arg));
            }
        }

       
};

