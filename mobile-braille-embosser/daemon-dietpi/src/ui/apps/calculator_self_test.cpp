#include "calculator_eval.h"
#include "ui/display/remote_frame_publisher.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

namespace {

int failures = 0;

void expect_true(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void expect_double(double actual, double expected, const char *message)
{
    if (std::abs(actual - expected) > 1e-9) {
        std::cerr << "FAIL: " << message << " (expected " << expected << ", got " << actual
                  << ")\n";
        ++failures;
    }
}

void expect_eval(const std::string &expr, double expected)
{
    const auto value = braillatron::ui::evaluate_calculator_expression(expr);
    expect_true(value.has_value(), ("missing result for " + expr).c_str());
    if (value.has_value()) {
        expect_double(*value, expected, expr.c_str());
    }
}

void expect_invalid(const std::string &expr)
{
    expect_true(!braillatron::ui::evaluate_calculator_expression(expr).has_value(),
                ("expected invalid expression: " + expr).c_str());
}

} // namespace

int main()
{
    expect_eval("2+2", 4);
    expect_eval("10-3", 7);
    expect_eval("3*4", 12);
    expect_eval("15/4", 3.75);
    expect_eval("2*(3+4)", 14);
    expect_eval("(2+3)*5", 25);
    expect_eval("-5+2", -3);
    expect_eval("2 + 2", 4);
    expect_eval("3.5*2", 7);

    expect_invalid("");
    expect_invalid("abc");
    expect_invalid("2+");
    expect_invalid("(2+3");
    expect_invalid("1/0");
    expect_invalid("2 & 3");

    expect_true(braillatron::ui::format_calculator_result(42.0) == "42",
                "integer result formatting");
    expect_true(braillatron::ui::format_calculator_result(3.75) == "3.75",
                "decimal result formatting");

    expect_true(!braillatron::ui::should_publish_remote_frame(100, 100),
                "identical crc should not publish");
    expect_true(braillatron::ui::should_publish_remote_frame(101, 100),
                "different crc should publish");

    if (failures != 0) {
        std::cerr << failures << " calculator self-test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "calculator self-test passed\n";
    return EXIT_SUCCESS;
}
