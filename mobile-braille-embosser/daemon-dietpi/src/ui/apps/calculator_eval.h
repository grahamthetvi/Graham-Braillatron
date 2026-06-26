#pragma once

#include <cctype>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace braillatron::ui {

enum class CalculatorEvalError {
    None,
    Invalid,
    DivideByZero,
};

struct CalculatorEvalOutcome {
    std::optional<double> value;
    CalculatorEvalError error = CalculatorEvalError::None;
};

inline std::string calculator_char_spoken(char ch)
{
    switch (ch) {
    case '+':
        return "plus";
    case '-':
        return "minus";
    case '*':
        return "times";
    case '/':
        return "divided by";
    case '(':
        return "open parenthesis";
    case ')':
        return "close parenthesis";
    case '.':
        return "point";
    default:
        return std::string(1, ch);
    }
}

inline std::string calculator_eval_error_message(CalculatorEvalError error)
{
    switch (error) {
    case CalculatorEvalError::DivideByZero:
        return "Divide by zero";
    case CalculatorEvalError::Invalid:
        return "Invalid equation";
    case CalculatorEvalError::None:
        return {};
    }
    return "Invalid equation";
}

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

    CalculatorEvalOutcome parse_outcome()
    {
        skip_spaces();
        const auto value = parse_expression();
        skip_spaces();
        if (!value.has_value()) {
            CalculatorEvalOutcome outcome;
            outcome.value = std::nullopt;
            outcome.error = divide_by_zero_ ? CalculatorEvalError::DivideByZero
                                            : CalculatorEvalError::Invalid;
            return outcome;
        }
        if (pos_ != input_.size()) {
            CalculatorEvalOutcome outcome;
            outcome.value = std::nullopt;
            outcome.error = CalculatorEvalError::Invalid;
            return outcome;
        }
        CalculatorEvalOutcome outcome;
        outcome.value = value;
        outcome.error = CalculatorEvalError::None;
        return outcome;
    }

    std::optional<double> parse()
    {
        const CalculatorEvalOutcome outcome = parse_outcome();
        return outcome.value;
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

    bool starts_implicit_factor() const
    {
        if (pos_ >= input_.size()) {
            return false;
        }
        const char ch = input_[pos_];
        return ch == '(' || ch == '.' ||
               std::isdigit(static_cast<unsigned char>(ch));
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
            if (op == '*' || op == '/') {
                ++pos_;
                const auto rhs = parse_factor();
                if (!rhs.has_value()) {
                    return std::nullopt;
                }
                if (op == '/') {
                    if (std::abs(*rhs) < 1e-12) {
                        divide_by_zero_ = true;
                        return std::nullopt;
                    }
                    value = *value / *rhs;
                } else {
                    value = *value * *rhs;
                }
                continue;
            }
            if (!starts_implicit_factor()) {
                break;
            }
            const auto rhs = parse_factor();
            if (!rhs.has_value()) {
                return std::nullopt;
            }
            value = *value * *rhs;
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
    bool divide_by_zero_ = false;
};

} // namespace detail

inline CalculatorEvalOutcome evaluate_calculator_expression_outcome(const std::string &input)
{
    detail::CalculatorExpressionParser parser(input);
    return parser.parse_outcome();
}

inline std::optional<double> evaluate_calculator_expression(const std::string &input)
{
    return evaluate_calculator_expression_outcome(input).value;
}

} // namespace braillatron::ui
