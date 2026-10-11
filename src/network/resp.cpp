#include "network/resp.hpp"

#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace strutdb::network {

bool RespParser::parse(const std::vector<std::uint8_t>& data, std::size_t& consumed) {
    consumed = 0;

    for (std::size_t i = 0; i < data.size(); ++i) {
        if (parse_byte(data[i])) {
            consumed = i + 1;
            return true;
        }
    }

    return false;
}

bool RespParser::parse_byte(std::uint8_t byte) {
    switch (state_) {
        case ParseState::Start:
            if (byte == '+') {
                state_ = ParseState::SimpleString;
                current_string_.clear();
            } else if (byte == '-') {
                state_ = ParseState::Error;
                current_string_.clear();
            } else if (byte == ':') {
                state_ = ParseState::Integer;
                current_string_.clear();
            } else if (byte == '$') {
                state_ = ParseState::BulkStringLength;
                current_string_.clear();
            } else if (byte == '*') {
                state_ = ParseState::ArrayLength;
                current_string_.clear();
            } else {
                // Invalid RESP
                return false;
            }
            break;

        case ParseState::SimpleString:
            if (parse_simple_string(byte)) {
                current_value_ = current_string_;
                if (in_array_) {
                    array_values_.push_back(std::move(current_value_));
                    current_value_ = RespNull{};
                    if (array_values_.size() >= expected_array_elements_) {
                        if (!array_values_.empty()) {
                            if (std::holds_alternative<std::string>(array_values_[0])) {
                                command_.name = std::get<std::string>(array_values_[0]);
                            } else if (std::holds_alternative<RespBulkString>(array_values_[0])) {
                                auto& bulk = std::get<RespBulkString>(array_values_[0]);
                                command_.name = std::string(bulk.begin(), bulk.end());
                            }
                            for (std::size_t i = 1; i < array_values_.size(); ++i) {
                                command_.args.push_back(std::move(array_values_[i]));
                            }
                        }
                        state_ = ParseState::Complete;
                        return true;
                    } else {
                        state_ = ParseState::Start;
                    }
                } else {
                    command_.name = current_string_;
                    state_ = ParseState::Complete;
                    return true;
                }
            }
            break;

        case ParseState::Error:
            if (parse_error(byte)) {
                current_value_ = current_string_;
                if (in_array_) {
                    array_values_.push_back(std::move(current_value_));
                    current_value_ = RespNull{};
                    if (array_values_.size() >= expected_array_elements_) {
                        state_ = ParseState::Complete;
                        return true;
                    } else {
                        state_ = ParseState::Start;
                    }
                } else {
                    command_.name = current_string_;
                    state_ = ParseState::Complete;
                    return true;
                }
            }
            break;

        case ParseState::Integer:
            if (parse_integer(byte)) {
                current_value_ = current_integer_;
                if (in_array_) {
                    array_values_.push_back(std::move(current_value_));
                    current_value_ = RespNull{};
                    if (array_values_.size() >= expected_array_elements_) {
                        state_ = ParseState::Complete;
                        return true;
                    } else {
                        state_ = ParseState::Start;
                    }
                } else {
                    command_.name = std::to_string(current_integer_);
                    state_ = ParseState::Complete;
                    return true;
                }
            }
            break;

        case ParseState::BulkStringLength:
            if (parse_bulk_string_length(byte)) {
                if (current_integer_ == -1) {
                    current_value_ = RespNull{};
                    if (in_array_) {
                        array_values_.push_back(std::move(current_value_));
                        current_value_ = RespNull{};
                        if (array_values_.size() >= expected_array_elements_) {
                            state_ = ParseState::Complete;
                            return true;
                        } else {
                            state_ = ParseState::Start;
                        }
                    } else {
                        state_ = ParseState::Complete;
                        return true;
                    }
                } else if (current_integer_ < 0) {
                    return false;
                } else {
                    remaining_bytes_ = static_cast<std::size_t>(current_integer_);
                    if (remaining_bytes_ > 1024 * 1024) {  // 1MB limit
                        return false;
                    }
                    state_ = ParseState::BulkStringData;
                    RespBulkString bulk;
                    bulk.reserve(remaining_bytes_);
                    current_value_ = std::move(bulk);
                }
            }
            break;

        case ParseState::BulkStringData:
            if (remaining_bytes_ > 0) {
                std::get<RespBulkString>(current_value_).push_back(byte);
                remaining_bytes_--;
            } else {
                if (byte == '\r') {
                    // Wait for \n
                } else if (byte == '\n') {
                    if (in_array_) {
                        array_values_.push_back(std::move(current_value_));
                        current_value_ = RespNull{};
                        if (array_values_.size() >= expected_array_elements_) {
                            if (!array_values_.empty()) {
                                if (std::holds_alternative<RespBulkString>(array_values_[0])) {
                                    auto& bulk = std::get<RespBulkString>(array_values_[0]);
                                    command_.name = std::string(bulk.begin(), bulk.end());
                                } else if (std::holds_alternative<std::string>(array_values_[0])) {
                                    command_.name = std::get<std::string>(array_values_[0]);
                                }
                                for (std::size_t i = 1; i < array_values_.size(); ++i) {
                                    command_.args.push_back(std::move(array_values_[i]));
                                }
                            }
                            state_ = ParseState::Complete;
                            return true;
                        } else {
                            state_ = ParseState::Start;
                        }
                    } else {
                        auto& bulk = std::get<RespBulkString>(current_value_);
                        command_.name = std::string(bulk.begin(), bulk.end());
                        state_ = ParseState::Complete;
                        return true;
                    }
                } else {
                    return false;
                }
            }
            break;

        case ParseState::ArrayLength:
            if (parse_integer(byte)) {
                if (current_integer_ <= 0) {
                    current_value_ = RespNull{};
                    state_ = ParseState::Complete;
                    return true;
                } else if (current_integer_ > 1024 * 1024) {  // 1MB limit
                    return false;
                } else {
                    expected_array_elements_ = static_cast<std::size_t>(current_integer_);
                    in_array_ = true;
                    array_values_.clear();
                    array_values_.reserve(expected_array_elements_);
                    state_ = ParseState::Start;
                }
            }
            break;

        case ParseState::ArrayElement:
            return false;

        case ParseState::Complete:
            return true;
    }

    return false;
}

bool RespParser::parse_simple_string(std::uint8_t byte) {
    if (byte == '\r') {
        return false;  // Wait for \n
    } else if (byte == '\n') {
        return true;
    } else {
        current_string_.push_back(static_cast<char>(byte));
        return false;
    }
}

bool RespParser::parse_error(std::uint8_t byte) {
    return parse_simple_string(byte);  // Same format as simple string
}

bool RespParser::parse_integer(std::uint8_t byte) {
    if (byte == '\r') {
        return false;  // Wait for \n
    } else if (byte == '\n') {
        // Parse the integer
        auto result = std::from_chars(current_string_.data(),
                                       current_string_.data() + current_string_.size(),
                                       current_integer_);
        if (result.ec != std::errc{}) {
            return false;
        }
        return true;
    } else {
        current_string_.push_back(static_cast<char>(byte));
        return false;
    }
}

bool RespParser::parse_bulk_string_length(std::uint8_t byte) {
    return parse_integer(byte);  // Same format as integer
}

bool RespParser::parse_bulk_string_data(std::uint8_t byte) {
    // Handled in parse_byte
    return false;
}

bool RespParser::parse_array_length(std::uint8_t byte) {
    return parse_integer(byte);  // Same format as integer
}

bool RespParser::parse_array_element(std::uint8_t byte) {
    // Handled in parse_byte
    return false;
}

void RespParser::reset() {
    state_ = ParseState::Start;
    command_ = Command{};
    current_string_.clear();
    current_integer_ = 0;
    remaining_bytes_ = 0;
    array_index_ = 0;
    array_values_.clear();
    current_value_ = RespNull{};
    in_array_ = false;
    expected_array_elements_ = 0;
}

// RESP Formatter implementation

std::vector<std::uint8_t> RespFormatter::format_simple_string(std::string_view s) {
    std::vector<std::uint8_t> result;
    result.reserve(s.size() + 3);
    result.push_back('+');
    result.insert(result.end(), s.begin(), s.end());
    result.push_back('\r');
    result.push_back('\n');
    return result;
}

std::vector<std::uint8_t> RespFormatter::format_error(std::string_view s) {
    std::vector<std::uint8_t> result;
    result.reserve(s.size() + 3);
    result.push_back('-');
    result.insert(result.end(), s.begin(), s.end());
    result.push_back('\r');
    result.push_back('\n');
    return result;
}

std::vector<std::uint8_t> RespFormatter::format_integer(std::int64_t i) {
    std::string s = std::to_string(i);
    std::vector<std::uint8_t> result;
    result.reserve(s.size() + 3);
    result.push_back(':');
    result.insert(result.end(), s.begin(), s.end());
    result.push_back('\r');
    result.push_back('\n');
    return result;
}

std::vector<std::uint8_t> RespFormatter::format_bulk_string(std::string_view s) {
    std::string len = std::to_string(s.size());
    std::vector<std::uint8_t> result;
    result.reserve(len.size() + s.size() + 5);
    result.push_back('$');
    result.insert(result.end(), len.begin(), len.end());
    result.push_back('\r');
    result.push_back('\n');
    result.insert(result.end(), s.begin(), s.end());
    result.push_back('\r');
    result.push_back('\n');
    return result;
}

std::vector<std::uint8_t> RespFormatter::format_null_bulk_string() {
    return {'$', '-', '1', '\r', '\n'};
}

std::vector<std::uint8_t> RespFormatter::format_array(const std::vector<std::uint8_t>* elements,
                                                       std::size_t count) {
    std::string len = std::to_string(count);
    std::vector<std::uint8_t> result;
    result.reserve(len.size() + 3);
    result.push_back('*');
    result.insert(result.end(), len.begin(), len.end());
    result.push_back('\r');
    result.push_back('\n');

    for (std::size_t i = 0; i < count; ++i) {
        result.insert(result.end(), elements[i].begin(), elements[i].end());
    }

    return result;
}

}  // namespace strutdb::network
