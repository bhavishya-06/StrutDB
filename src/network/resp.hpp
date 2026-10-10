#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace strutdb::network {

// RESP data types
using RespInteger = std::int64_t;
using RespBulkString = std::vector<std::uint8_t>;
using RespArray = std::vector<RespValue>;

struct RespNull {};

// RESP value variant
using RespValue = std::variant<
    RespNull,
    std::string,         // Simple string or error
    RespInteger,
    RespBulkString,
    RespArray>;

// Parsed command with arguments
struct Command {
    std::string            name;
    std::vector<RespValue> args;
};

// RESP parser state
enum class ParseState {
    Start,
    SimpleString,
    Error,
    Integer,
    BulkStringLength,
    BulkStringData,
    ArrayLength,
    ArrayElement,
    Complete
};

class RespParser {
public:
    RespParser() = default;

    // Parse data from buffer, returns true if a complete command is ready
    [[nodiscard]] bool parse(const std::vector<std::uint8_t>& data, std::size_t& consumed);

    // Get the parsed command (valid only when parse returns true)
    [[nodiscard]] Command get_command() const { return command_; }

    // Reset parser for next command
    void reset();

private:
    [[nodiscard]] bool parse_byte(std::uint8_t byte);
    [[nodiscard]] bool parse_simple_string(std::uint8_t byte);
    [[nodiscard]] bool parse_error(std::uint8_t byte);
    [[nodiscard]] bool parse_integer(std::uint8_t byte);
    [[nodiscard]] bool parse_bulk_string_length(std::uint8_t byte);
    [[nodiscard]] bool parse_bulk_string_data(std::uint8_t byte);
    [[nodiscard]] bool parse_array_length(std::uint8_t byte);
    [[nodiscard]] bool parse_array_element(std::uint8_t byte);

    ParseState               state_ = ParseState::Start;
    Command                  command_;
    std::string              current_string_;
    std::int64_t             current_integer_ = 0;
    std::size_t              remaining_bytes_ = 0;
    std::size_t              array_index_ = 0;
    std::vector<RespValue>  array_values_;
    RespValue                current_value_;
};

// RESP response formatter
class RespFormatter {
public:
    // Format values to RESP
    [[nodiscard]] static std::vector<std::uint8_t> format_simple_string(std::string_view s);
    [[nodiscard]] static std::vector<std::uint8_t> format_error(std::string_view s);
    [[nodiscard]] static std::vector<std::uint8_t> format_integer(std::int64_t i);
    [[nodiscard]] static std::vector<std::uint8_t> format_bulk_string(std::string_view s);
    [[nodiscard]] static std::vector<std::uint8_t> format_null_bulk_string();
    [[nodiscard]] static std::vector<std::uint8_t> format_array(const std::vector<std::uint8_t>* elements,
                                                                 std::size_t count);
};

}  // namespace strutdb::network
