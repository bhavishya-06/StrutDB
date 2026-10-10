#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace strutdb::core {

using NodeId  = std::uint64_t;
using EdgeId  = std::uint64_t;
using LabelId = std::uint32_t;
using TypeId  = std::uint32_t;

inline constexpr NodeId kInvalidNodeId = 0;
inline constexpr EdgeId kInvalidEdgeId = 0;

// Scalar property payload stored by value. No heap graph pointers here, so
// property maps cannot form ownership cycles with Node/Edge objects.
using PropertyValue = std::variant<
    std::monostate,
    bool,
    std::int64_t,
    double,
    std::string,
    std::vector<std::uint8_t>>;

struct Property {
    std::string  key;
    PropertyValue value;
};

}  // namespace strutdb::core
