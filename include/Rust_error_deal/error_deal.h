#ifndef ERROR_DEALl_H
#define ERROR_DEALl_H 

#include <variant>
#include <string>
#include <system_error>
#include <functional>
#include <iostream>

struct RichError {
// std::error_code code;
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
        //  initialization function
        Result(T success):result(std::move(success)){}
        Result(E fail):result(std::move(fail)){}

        //  check result type function
        bool is_success() const{
            return std::holds_alternative<T>(result);
        }

        bool is_fail() const{
            return std::holds_alternative<E>(result);
        }

        //  unpacking function
        T& unwrap()  &{
            if(is_success()){
                return std::get<T>(result);
            }
            else{
                std::cerr<<"Error: Failed to unwrap"<<std::endl;
                std::terminate();
            }
        }

        T&& unwrap() && {
            if(is_success()){
                return std::get<T>(std::move(result));
            }
            else{
                std::cerr<<"Error: Failed to unwrap"<<std::endl;
                std::terminate();
            }
        }

        E& unwrap_err()  &{
            if(is_fail()){
                return std::get<E>(result);
            }
            else{
                std::cerr<<"Error: Failed to unwrap"<<std::endl;
                std::terminate();
            }
        }

        E&& unwrap_err()  &&{
            if(is_fail()){
                return std::get<E>(std::move(result));
            }
            else{
                std::cerr<<"Error: Failed to unwrap"<<std::endl;
                std::terminate();
            }
        }

        //  template result transform
        template<typename callable>
        auto transform_func(callable&& transform_callable)->
            typename std::remove_reference <decltype(transform_callable( std::declval<T&>()   ))> ::type
        {
            if(is_success())
            {
                auto&& arg = unwrap();
                return transform_callable(  std::forward<decltype(arg)  > (arg));
            }
            else {
                using Return_type = typename std::remove_reference <decltype(transform_callable( std::declval<T&>()   ))> ::type;
                return Return_type(std::forward<  decltype(unwrap_err()) > (unwrap_err()));
            }
        }

        //  result recover
        template<typename callable>
       auto recover(callable&& recover_callable)->
            typename std::remove_reference <decltype(recover_callable( std::declval<E>()   ))> ::type
        {
            {
                auto&& arg = unwrap_err();
                return recover_callable( std::forward<  decltype(arg) > (arg));
            }
        }   
};

#endif