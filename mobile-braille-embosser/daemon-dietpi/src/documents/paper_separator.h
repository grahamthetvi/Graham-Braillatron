#pragma once

#include <cstdint>
#include <functional>

namespace braillatron::documents {

class PaperSeparator {
public:
    using FeedFn = std::function<void(int32_t line_delta)>;
    using SensorFn = std::function<bool()>;

    void set_feed_handler(FeedFn fn);
    void set_paper_edge_sensor(SensorFn fn);

    void separate_to_fresh_page();

private:
    FeedFn feed_;
    SensorFn paper_edge_;
};

} // namespace braillatron::documents
