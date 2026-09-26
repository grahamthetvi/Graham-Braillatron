#include "paper_separator.h"

#include <iostream>

namespace braillatron::documents {

namespace {

constexpr int32_t kFreshPageLines = 33;
constexpr int32_t kMaxReverseLines = 200;

} // namespace

void PaperSeparator::set_feed_handler(FeedFn fn)
{
    feed_ = std::move(fn);
}

void PaperSeparator::set_paper_edge_sensor(SensorFn fn)
{
    paper_edge_ = std::move(fn);
}

bool PaperSeparator::separate_to_fresh_page()
{
    if (!feed_) {
        return false;
    }

    if (paper_edge_) {
        int32_t reversed = 0;
        while (!paper_edge_() && reversed < kMaxReverseLines) {
            if (!feed_(-1)) {
                std::cerr << "[paper] reverse stopped before the paper edge\n";
                return false;
            }
            ++reversed;
        }
        if (!paper_edge_()) {
            std::cerr << "[paper] paper edge not reached within " << kMaxReverseLines
                      << " lines; fresh-page feed skipped\n";
            return false;
        }
    }

    for (int32_t i = 0; i < kFreshPageLines; ++i) {
        if (!feed_(1)) {
            std::cerr << "[paper] fresh-page feed stopped after " << i << " lines\n";
            return false;
        }
    }
    return true;
}

} // namespace braillatron::documents
