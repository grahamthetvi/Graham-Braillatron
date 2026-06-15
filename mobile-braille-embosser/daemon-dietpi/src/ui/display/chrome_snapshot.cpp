#include "chrome_snapshot.h"

#include "connect/json_utils.h"

#include <sstream>

namespace braillatron::ui {

namespace {

using braillatron::connect::json_escape;
using braillatron::connect::json_get_array_body;
using braillatron::connect::json_get_bool;
using braillatron::connect::json_get_string;

} // namespace

std::string serialize_chrome_snapshot(const RenderedChrome &frame, uint64_t sequence)
{
    std::ostringstream stream;
    stream << "{\"sequence\":" << sequence
           << ",\"header\":\"" << json_escape(frame.header) << "\""
           << ",\"breadcrumb\":\"" << json_escape(frame.breadcrumb) << "\""
           << ",\"focus_row\":" << frame.focus_row
           << ",\"at_top_boundary\":" << (frame.at_top_boundary ? "true" : "false")
           << ",\"at_bottom_boundary\":" << (frame.at_bottom_boundary ? "true" : "false")
           << ",\"toast\":\"" << json_escape(frame.toast) << "\""
           << ",\"tts_paused\":" << (frame.tts_paused ? "true" : "false")
           << ",\"dictation_active\":" << (frame.dictation_active ? "true" : "false")
           << ",\"rows\":[";
    for (size_t i = 0; i < frame.rows.size(); ++i) {
        if (i > 0) {
            stream << ',';
        }
        stream << '"' << json_escape(frame.rows[i]) << '"';
    }
    stream << "]}";
    return stream.str();
}

bool parse_chrome_snapshot(const std::string &json, RenderedChrome &frame, uint64_t *sequence)
{
    if (sequence != nullptr) {
        const std::string seq_text = json_get_string(json, "sequence");
        if (seq_text.empty()) {
            *sequence = 0;
        } else {
            try {
                *sequence = static_cast<uint64_t>(std::stoull(seq_text));
            } catch (...) {
                *sequence = 0;
            }
        }
    }

    frame.header = json_get_string(json, "header");
    frame.breadcrumb = json_get_string(json, "breadcrumb");
    frame.toast = json_get_string(json, "toast");
    frame.tts_paused = json_get_bool(json, "tts_paused", false);
    frame.dictation_active = json_get_bool(json, "dictation_active", false);
    frame.at_top_boundary = json_get_bool(json, "at_top_boundary", false);
    frame.at_bottom_boundary = json_get_bool(json, "at_bottom_boundary", false);

    const std::string focus_text = json_get_string(json, "focus_row");
    if (focus_text.empty()) {
        frame.focus_row = static_cast<size_t>(-1);
    } else {
        try {
            frame.focus_row = static_cast<size_t>(std::stoull(focus_text));
        } catch (...) {
            frame.focus_row = static_cast<size_t>(-1);
        }
    }

    frame.rows.clear();
    const std::string rows_body = json_get_array_body(json, "rows");
    if (!rows_body.empty()) {
        size_t i = 0;
        while (i < rows_body.size()) {
            while (i < rows_body.size() && rows_body[i] != '"') {
                ++i;
            }
            if (i >= rows_body.size()) {
                break;
            }
            ++i;
            std::string row;
            bool escape = false;
            for (; i < rows_body.size(); ++i) {
                const char ch = rows_body[i];
                if (escape) {
                    row += ch;
                    escape = false;
                    continue;
                }
                if (ch == '\\') {
                    escape = true;
                    continue;
                }
                if (ch == '"') {
                    ++i;
                    break;
                }
                row += ch;
            }
            frame.rows.push_back(row);
        }
    }

    return true;
}

} // namespace braillatron::ui
