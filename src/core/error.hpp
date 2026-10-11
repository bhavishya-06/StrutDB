#pragma once

#include <utility>

namespace strutdb::core {

enum class CoreError {
    Ok = 0,
    KeyNotFound,
    NodeNotFound,
    SrcNodeNotFound,
    DstNodeNotFound,
    EdgeNotFound,
    InvalidArgument,
    ParseError,
    QueryTooLarge,
    UnsupportedAst,
};

template <typename T>
struct Result {
    T         value{};
    CoreError error = CoreError::Ok;

    [[nodiscard]] bool ok() const noexcept { return error == CoreError::Ok; }
    explicit operator bool() const noexcept { return ok(); }

    T& operator*() & { return value; }
    const T& operator*() const& { return value; }
    T&& operator*() && { return std::move(value); }

    T* operator->() { return &value; }
    const T* operator->() const { return &value; }
};

}  // namespace strutdb::core
