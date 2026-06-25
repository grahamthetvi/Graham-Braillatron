#pragma once

#include <cctype>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace braillatron::ui {

inline bool is_valid_calculator_char(char ch)
{
    return std::isdigit(static_cast<unsigned char>(ch)) || ch == '+' || ch == '-' ||
           ch == '*' || ch == '/' || ch == '(' || ch == ')' || ch == '.' || ch == ' ';
}

inline std::string format_calculator_result(double value)
{
    const double rounded = std::round(value);
    if (std::abs(value - rounded) < 1e-9) {
        return std::to_string(static_cast<long long>(rounded));
    }

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", value);
    return buf;
}

namespace detail {

class CalculatorExpressionParser {
public:
    explicit CalculatorExpressionParser(std::string_view input)
        : input_(input)
    {
    }

    std::optional<double> parse()
    {
        skip_spaces();
        const auto value = parse_expression();
        skip_spaces();
        if (!value.has_value() || pos_ != input_.size()) {
            return std::nullopt;
        }
        return value;
    }

private:
    void skip_spaces()
    {
        while (pos_ < input_.size() && input_[pos_] == ' ') {
            ++pos_;
        }
    }

    bool consume(char ch)
    {
        skip_spaces();
        if (pos_ >= input_.size() || input_[pos_] != ch) {
            return false;
        }
        ++pos_;
        return true;
    }

    std::optional<double> parse_expression()
    {
        auto value = parse_term();
        if (!value.has_value()) {
            return std::nullopt;
        }

        while (true) {
            skip_spaces();
            if (pos_ >= input_.size()) {
                break;
            }
            const char op = input_[pos_];
            if (op != '+' && op != '-') {
                break;
            }
            ++pos_;
            const auto rhs = parse_term();
            if (!rhs.has_value()) {
                return std::nullopt;
            }
            value = op == '+' ? *value + *rhs : *value - *rhs;
        }
        return value;
    }

    std::optional<double> parse_term()
    {
        auto value = parse_factor();
        if (!value.has_value()) {
            return std::nullopt;
        }

        while (true) {
            skip_spaces();
            if (pos_ >= input_.size()) {
                break;
            }
            const char op = input_[pos_];
            if (op != '*' && op != '/') {
                break;
            }
            ++pos_;
            const auto rhs = parse_factor();
            if (!rhs.has_value()) {
                return std::nullopt;
            }
            if (op == '/') {
                if (std::abs(*rhs) < 1e-12) {
                    return std::nullopt;
                }
                value = *value / *rhs;
            } else {
                value = *value * *rhs;
            }
        }
        return value;
    }

    std::optional<double> parse_factor()
    {
        skip_spaces();
        if (pos_ >= input_.size()) {
            return std::nullopt;
        }

        bool negative = false;
        if (input_[pos_] == '+') {
            ++pos_;
            skip_spaces();
        } else if (input_[pos_] == '-') {
            negative = true;
            ++pos_;
            skip_spaces();
        }

        if (pos_ < input_.size() && input_[pos_] == '(') {
            ++pos_;
            const auto value = parse_expression();
            if (!value.has_value() || !consume(')')) {
                return std::nullopt;
            }
            return negative ? -*value : *value;
        }

        const auto value = parse_number();
        if (!value.has_value()) {
            return std::nullopt;
        }
        return negative ? -*value : *value;
    }

    std::optional<double> parse_number()
    {
        skip_spaces();
        if (pos_ >= input_.size()) {
            return std::nullopt;
        }

        size_t start = pos_;
        bool saw_digit = false;
        bool saw_decimal = false;
        while (pos_ < input_.size()) {
            const char ch = input_[pos_];
            if (std::isdigit(static_cast<unsigned char>(ch))) {
                saw_digit = true;
                ++pos_;
                continue;
            }
            if (ch == '.' && !saw_decimal) {
                saw_decimal = true;
                ++pos_;
                continue;
            }
            break;
        }

        if (!saw_digit) {
            return std::nullopt;
        }

        try {
            return std::stod(std::string(input_.substr(start, pos_ - start)));
        } catch (...) {
            return std::nullopt;
        }
    }

    std::string_view input_;
    size_t pos_ = 0;
};

} // namespace detail

inline std::optional<double> evaluate_calculator_expression(const std::string &input)
{
    detail::CalculatorExpressionParser parser(input);
    return parser.parse();
}

} // namespace braillatron::ui
