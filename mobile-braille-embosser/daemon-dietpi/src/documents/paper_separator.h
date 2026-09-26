#pragma once

#include <cstdint>
#include <functional>

namespace braillatron::documents {

class PaperSeparator {
public:
    using FeedFn = std::function<bool(int32_t line_delta)>;
    using SensorFn = std::function<bool()>;

    void set_feed_handler(FeedFn fn);
    void set_paper_edge_sensor(SensorFn fn);

    /* Reverse to the paper edge, then feed one fresh page (33 lines).
     * With no sensor, only the forward feed runs (dev bench).
     * Returns false when a feed is rejected or the edge is never seen. */
    bool separate_to_fresh_page();

private:
    FeedFn feed_;
    SensorFn paper_edge_;
};

} // namespace braillatron::documents
