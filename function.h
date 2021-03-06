#ifndef FUNCTION_H
#define FUNCTION_H

#include <memory>
#include <type_traits>

#include "badfunctionexception.h"


template <typename>
class function;

template<typename R, typename... Args>
class function<R (Args...)> {
public:
    function(std::nullptr_t) noexcept : small_object(false), big_pointer(nullptr) {}

    function() noexcept : function(nullptr) {}

    function(const function& other) {
        if (other.small_object) {
            reinterpret_cast<const wrapper*>(other.buffer)->clone_small(buffer);
        } else {
            new (buffer) std::unique_ptr((other.big_pointer)->clone());
        }
        small_object = other.small_object;
    }

    function(function&& other) noexcept {
        if (other.small_object) {
            reinterpret_cast<wrapper*>(other.buffer)->move_by_pointer(buffer);
            new (other.buffer) std::unique_ptr<wrapper>(nullptr);
        } else {
            new (buffer) std::unique_ptr<wrapper>(std::move(other.big_pointer));
        }
        small_object = other.small_object;
        other.small_object = false;
    }

    template <typename F>
    function(F f) {
        if constexpr (std::is_nothrow_move_constructible<F>::value && sizeof(f) > MAX_SIZE) {
            small_object = false;
            new (buffer) std::unique_ptr<wrapper_function<F>>(
                        std::make_unique<wrapper_function<F>>(std::move(f)));
        } else {
            small_object = true;
            new (buffer) wrapper_function<F>(std::move(f));
        }
    }

    template <typename F, typename Class>
    function(F Class::* member) {
        small_object = true;
        new (buffer) wrapper_member<F, Class>(member);
    }

    ~function() {
        destruction();
    }

    function& operator=(const function& other) {
        destruction();

        function tmp(other);
        swap(tmp);
        return *this;
    }

    function& operator=(function&& other) noexcept {
        destruction();

        if (other.small_object) {
            reinterpret_cast<wrapper*>(other.buffer)->move_by_pointer(buffer);
            new (other.buffer) std::unique_ptr<wrapper>(nullptr);
        } else {
            new (buffer) std::unique_ptr<wrapper>(std::move(other.big_pointer));
        }

        small_object = other.small_object;
        other.small_object = false;

        return *this;
    }

    void swap(function& other) noexcept {
        function tmp(std::move(other));
        other = std::move(*this);
        *this = std::move(tmp);
    }

    explicit operator bool() const noexcept {
        return small_object || static_cast<bool>(big_pointer);
    }

    R operator()(Args... args) {
        if (!static_cast<bool>(*this)) {
            throw bad_function_call();
        }
        return small_object ? reinterpret_cast<wrapper*>(buffer)->call(std::forward<Args>(args)...) :
                                                     big_pointer->call(std::forward<Args>(args)...);
    }

private:
    void destruction() {
        if (small_object) {
            reinterpret_cast<wrapper*>(buffer)->~wrapper();
        } else {
            big_pointer.~unique_ptr();
        }
    }

    class wrapper {
    public:
        wrapper() = default;

        virtual ~wrapper() = default;

        virtual R call(Args&&...) = 0;

        virtual void move_by_pointer(void* to) = 0;

        virtual void clone_small(void* to) const = 0;

        virtual std::unique_ptr<wrapper> clone() const = 0;
    };

    template <typename T>
    class wrapper_function : public wrapper {
    public:
        wrapper_function(const T& callable) : wrapper(), callable(callable) {}

        wrapper_function(T&& callable) : wrapper(), callable(std::move(callable)) {}

        ~wrapper_function() = default;

        R call(Args&&... args) {
            return callable(std::forward<Args>(args)...);
        }

        void move_by_pointer(void* to) {
            new (to) wrapper_function<T>(std::move(callable));
        }

        virtual void clone_small(void* to) const {
            new (to) wrapper_function<T>(callable);
        }

        std::unique_ptr<wrapper> clone() const {
            return std::make_unique<wrapper_function<T>>(callable);
        }

    private:
        T callable;
    };

    template <typename F, typename Class, typename... FunArgs>
    class wrapper_member : public wrapper {
    public:
        using member = F Class::*;

        wrapper_member(member callable) : wrapper(), callable(std::move(callable)) {}

        ~wrapper_member() = default;

        R call(Class&& object, FunArgs&&... fun_args) {
            return (object.*callable)(std::forward<FunArgs>(fun_args)...);
        }

        void move_by_pointer(void* to) {
            new (to) wrapper_member<F, Class>(std::move(callable));
        }

        virtual void clone_small(void* to) const {
            new (to) wrapper_member<F, Class>(callable);
        }

        std::unique_ptr<wrapper> clone() const {
            return std::make_unique<wrapper_member>(callable);
        }

    private:
        member callable;
    };

    static constexpr int MAX_SIZE = 128;
    bool small_object;

    union {
        std::unique_ptr<wrapper> big_pointer;
        char buffer[MAX_SIZE];
    };
};

#endif // FUNCTION_H
