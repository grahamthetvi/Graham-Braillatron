#include "paper_separator.h"

namespace braillatron::documents {

void PaperSeparator::set_feed_handler(FeedFn fn)
{
    feed_ = std::move(fn);
}

void PaperSeparator::set_paper_edge_sensor(SensorFn fn)
{
    paper_edge_ = std::move(fn);
}

void PaperSeparator::separate_to_fresh_page()
{
    if (!feed_) {
        return;
    }

    int32_t reverse_steps = 0;
    if (paper_edge_) {
        while (!paper_edge_() && reverse_steps > -200) {
            feed_(-1);
            --reverse_steps;
        }
    }

    constexpr int32_t kFreshPageLines = 30;
    for (int32_t i = 0; i < kFreshPageLines; ++i) {
        feed_(1);
    }
}

} // namespace braillatron::documents
