#include "connect_async.h"
#include "connect_client.h"
#include "event_writer.h"
#include "json_utils.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
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

void test_async_command_detection()
{
    expect_true(braillatron::connect::is_async_command("signal.start_link"),
                "signal.start_link is async");
    expect_true(braillatron::connect::is_async_command("youtube.search"), "youtube.search is async");
    expect_true(braillatron::connect::is_async_command("music.scan"), "music.scan is async");
    expect_true(braillatron::connect::is_async_command("podcasts.refresh"), "podcasts.refresh is async");
    expect_true(braillatron::connect::is_async_command("podcasts.download"), "podcasts.download is async");
    expect_true(braillatron::connect::is_async_command("radio.search"), "radio.search is async");
    expect_true(!braillatron::connect::is_async_command("ping"), "ping is sync");
    expect_true(!braillatron::connect::is_async_command("signal.link_status"),
                "signal.link_status is sync");
}

void test_pending_response()
{
    const std::string response = braillatron::connect::make_pending_response("req-1");
    expect_true(braillatron::connect::json_get_bool(response, "ok", false), "pending ok");
    expect_true(braillatron::connect::json_get_bool(response, "pending", false), "pending flag");
    expect_true(braillatron::connect::json_get_string(response, "request_id") == "req-1",
                "pending request_id");
}

void test_dispatch_async_response()
{
    braillatron::connect::ConnectClient client("/tmp/unused.sock", "/tmp/unused.events");
    bool called = false;
    client.register_pending_for_test("req-test", [&called](const std::string &response) {
        called = true;
        expect_true(braillatron::connect::json_get_string(response, "request_id") == "req-test",
                    "callback request_id");
        expect_true(braillatron::connect::json_get_bool(response, "ok", false), "callback ok");
    });

    braillatron::connect::ConnectEvent event;
    event.type = "connect.response";
    event.data_json = R"({"request_id":"req-test","ok":true})";
    client.dispatch_async_response(event);
    expect_true(called, "async callback invoked");
}

void test_emit_connect_response()
{
    const std::string path = "/tmp/braillatron-connect-test.events";
    std::remove(path.c_str());

    braillatron::connect::EventWriter writer(path);
    braillatron::connect::emit_connect_response(&writer, "req-42", R"({"ok":true,"value":7})");

    std::ifstream in(path);
    std::string line;
    expect_true(static_cast<bool>(std::getline(in, line)), "event line written");
    expect_true(line.find("\"event\":\"connect.response\"") != std::string::npos,
                "connect.response event type");
    expect_true(line.find("\"request_id\":\"req-42\"") != std::string::npos, "event request_id");
    std::remove(path.c_str());
}

} // namespace

int main()
{
    test_async_command_detection();
    test_pending_response();
    test_dispatch_async_response();
    test_emit_connect_response();

    if (failures != 0) {
        std::cerr << failures << " connect client test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "connect client self-test passed\n";
    return EXIT_SUCCESS;
}
