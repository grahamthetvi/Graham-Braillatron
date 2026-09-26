#include "paper_separator.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

constexpr int32_t kFreshPageLines = 33;
constexpr int32_t kMaxReverseLines = 200;

int failures = 0;

void expect_true(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct FeedScript {
    bool edge = false;
    int edge_after_reverse = -1;
    bool fail_next_negative = false;
    int fail_forward_at = -1;
    int reverse_count = 0;
    int forward_count = 0;
    std::vector<int32_t> deltas;
};

bool scripted_feed(FeedScript &script, int32_t delta)
{
    script.deltas.push_back(delta);
    if (delta < 0) {
        if (script.fail_next_negative) {
            return false;
        }
        ++script.reverse_count;
        if (script.edge_after_reverse >= 0 &&
            script.reverse_count >= script.edge_after_reverse) {
            script.edge = true;
        }
        return true;
    }
    if (delta > 0) {
        if (script.fail_forward_at >= 0 && script.forward_count == script.fail_forward_at) {
            return false;
        }
        ++script.forward_count;
        return true;
    }
    return true;
}

int count_delta(const std::vector<int32_t> &deltas, int32_t want)
{
    int count = 0;
    for (int32_t delta : deltas) {
        if (delta == want) {
            ++count;
        }
    }
    return count;
}

void expect_only_forward(const FeedScript &script, const char *message)
{
    expect_true(count_delta(script.deltas, 1) == kFreshPageLines, message);
    expect_true(count_delta(script.deltas, -1) == 0, message);
    expect_true(static_cast<int>(script.deltas.size()) == kFreshPageLines, message);
}

} // namespace

int main()
{
    {
        braillatron::documents::PaperSeparator separator;
        expect_true(!separator.separate_to_fresh_page(),
                    "missing feed handler refuses the separate");
    }

    {
        braillatron::documents::PaperSeparator separator;
        FeedScript script;
        separator.set_feed_handler([&script](int32_t delta) {
            return scripted_feed(script, delta);
        });
        expect_true(separator.separate_to_fresh_page(),
                    "no sensor still completes the forward page");
        expect_only_forward(script, "no sensor feeds exactly 33 lines forward");
    }

    {
        braillatron::documents::PaperSeparator separator;
        FeedScript script;
        script.edge = true;
        separator.set_feed_handler([&script](int32_t delta) {
            return scripted_feed(script, delta);
        });
        separator.set_paper_edge_sensor([&script]() { return script.edge; });
        expect_true(separator.separate_to_fresh_page(),
                    "edge already present skips the reverse");
        expect_only_forward(script, "edge already present feeds only the fresh page");
    }

    {
        braillatron::documents::PaperSeparator separator;
        FeedScript script;
        script.edge_after_reverse = 2;
        separator.set_feed_handler([&script](int32_t delta) {
            return scripted_feed(script, delta);
        });
        separator.set_paper_edge_sensor([&script]() { return script.edge; });
        expect_true(separator.separate_to_fresh_page(),
                    "reverse until the edge then feed a fresh page");
        expect_true(count_delta(script.deltas, -1) == 2, "two reverse lines before the edge");
        expect_true(count_delta(script.deltas, 1) == kFreshPageLines,
                    "fresh page follows the edge");
        expect_true(script.deltas.size() >= 2 && script.deltas[0] == -1 && script.deltas[1] == -1,
                    "reverse feeds come first");
    }

    {
        braillatron::documents::PaperSeparator separator;
        FeedScript script;
        separator.set_feed_handler([&script](int32_t delta) {
            return scripted_feed(script, delta);
        });
        separator.set_paper_edge_sensor([&script]() { return script.edge; });
        expect_true(!separator.separate_to_fresh_page(),
                    "unseen edge refuses the separate");
        expect_true(count_delta(script.deltas, -1) == kMaxReverseLines,
                    "reverse stops at the line cap");
        expect_true(count_delta(script.deltas, 1) == 0,
                    "unseen edge does not command the fresh-page feed");
    }

    {
        braillatron::documents::PaperSeparator separator;
        FeedScript script;
        script.fail_next_negative = true;
        separator.set_feed_handler([&script](int32_t delta) {
            return scripted_feed(script, delta);
        });
        separator.set_paper_edge_sensor([&script]() { return script.edge; });
        expect_true(!separator.separate_to_fresh_page(),
                    "rejected reverse refuses the separate");
        expect_true(script.deltas.size() == 1 && script.deltas[0] == -1,
                    "rejected reverse is a single backward line");
        expect_true(count_delta(script.deltas, 1) == 0,
                    "rejected reverse does not command the fresh-page feed");
    }

    {
        braillatron::documents::PaperSeparator separator;
        FeedScript script;
        script.edge = true;
        script.fail_forward_at = 4;
        separator.set_feed_handler([&script](int32_t delta) {
            return scripted_feed(script, delta);
        });
        separator.set_paper_edge_sensor([&script]() { return script.edge; });
        expect_true(!separator.separate_to_fresh_page(),
                    "rejected forward feed refuses the separate");
        expect_true(count_delta(script.deltas, 1) == 5,
                    "forward feed stops on the first rejection");
        expect_true(count_delta(script.deltas, -1) == 0,
                    "an edge already present does not reverse");
    }

    if (failures != 0) {
        std::cerr << failures << " paper separator self-test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "paper separator self-test passed\n";
    return EXIT_SUCCESS;
}
