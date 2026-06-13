#include "signal_backend.h"

#include "json_utils.h"

#include <dirent.h>
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <cctype>

namespace braillatron::connect {

namespace {

std::string shell_escape(const std::string &value)
{
    std::string out = "'";
    for (char ch : value) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out += ch;
        }
    }
    out += "'";
    return out;
}

std::string trim_link_uri(const std::string &output)
{
    const std::string prefix = "sgnl://linkdevice";
    const size_t pos = output.find(prefix);
    if (pos != std::string::npos) {
        size_t end = output.find_first_of(" \n\r\t", pos);
        if (end == std::string::npos) {
            end = output.size();
        }
        return output.substr(pos, end - pos);
    }
    return {};
}

std::string curl_get(const std::string &base, const std::string &path)
{
    const std::string cmd =
        "curl -sS -N " + shell_escape("http://" + base + path) + " 2>/dev/null";
    return run_command(cmd);
}

} // namespace

SignalBackend::SignalBackend(MessagesConfig config, EventWriter *events)
    : config_(std::move(config))
    , events_(events)
{
    ensure_directory(config_.signal_data_dir);
}

SignalBackend::~SignalBackend()
{
    stop_event_thread();
    stop_daemon();
}

std::string SignalBackend::signal_env() const
{
    return "XDG_DATA_HOME=" + shell_escape(config_.signal_data_dir) + " ";
}

bool SignalBackend::is_linked() const
{
    return !linked_account().empty();
}

std::string SignalBackend::linked_account() const
{
    auto account_from_name = [](const std::string &name) -> std::string {
        if (name.empty() || name == "." || name == "..") {
            return {};
        }
        if (name.find('+') == 0 || std::isdigit(static_cast<unsigned char>(name[0]))) {
            return name;
        }
        return {};
    };

    DIR *dir = opendir(config_.signal_data_dir.c_str());
    if (dir != nullptr) {
        dirent *entry = nullptr;
        while ((entry = readdir(dir)) != nullptr) {
            const std::string account = account_from_name(entry->d_name);
            if (!account.empty()) {
                closedir(dir);
                return account;
            }
        }
        closedir(dir);

        const std::string data_account_dir = config_.signal_data_dir + "/data";
        dir = opendir(data_account_dir.c_str());
        if (dir != nullptr) {
            dirent *nested = nullptr;
            while ((nested = readdir(dir)) != nullptr) {
                const std::string account = account_from_name(nested->d_name);
                if (!account.empty()) {
                    closedir(dir);
                    return account;
                }
            }
            closedir(dir);
        }
    }

    const std::string output =
        run_command(signal_env() + config_.signal_cli_path + " listAccounts 2>/dev/null");
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        const std::string account = account_from_name(line);
        if (!account.empty()) {
            return account;
        }
    }
    return {};
}

std::string SignalBackend::accounts_status() const
{
    const bool linked = is_linked();
    std::ostringstream out;
    out << "{\"signal_linked\":" << (linked ? "true" : "false");
    if (linked) {
        out << ",\"account\":\"" << json_escape(linked_account()) << "\"";
    }
    out << "}";
    return out.str();
}

void SignalBackend::start_daemon_if_linked()
{
    if (!config_.enabled || !is_linked()) {
        return;
    }
    if (daemon_proc_.pid > 0) {
        return;
    }
    const std::string account = linked_account();
    const std::string cmd = signal_env() + config_.signal_cli_path + " -a " + shell_escape(account) +
                            " daemon --http " + config_.signal_http + " >/dev/null 2>&1";
    daemon_proc_ = spawn_background(cmd);
}

void SignalBackend::stop_daemon()
{
    daemon_proc_.stop();
}

std::string SignalBackend::link_status() const
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"signal disabled\"}";
    }
    const bool linked = is_linked();
    const bool pending = link_watch_active_.load();
    std::ostringstream out;
    out << "{\"ok\":true,\"linked\":" << (linked ? "true" : "false") << ",\"link_pending\":"
        << (pending ? "true" : "false");
    if (!pending_link_uri_.empty()) {
        out << ",\"uri\":\"" << json_escape(pending_link_uri_) << "\"";
    }
    out << "}";
    return out.str();
}

std::string SignalBackend::start_link()
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"signal disabled\"}";
    }
    if (link_watch_active_.load()) {
        return link_status();
    }
    return run_link_workflow();
}

std::string SignalBackend::run_link_workflow()
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"signal disabled\"}";
    }
    link_watch_active_ = true;

    const std::string log_path = config_.signal_data_dir + "/link.log";
    const std::string cmd = signal_env() + config_.signal_cli_path + " link -n " +
                            shell_escape(config_.device_name) + " > " + shell_escape(log_path) +
                            " 2>&1";
    spawn_background(cmd);

    for (int i = 0; i < 20; ++i) {
        const std::string output = run_command("head -n 5 " + shell_escape(log_path) + " 2>/dev/null");
        pending_link_uri_ = trim_link_uri(output);
        if (!pending_link_uri_.empty()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    if (pending_link_uri_.empty()) {
        pending_link_uri_ = "Open Signal on your phone, Settings, Linked Devices, Link New Device";
    }
    if (events_ != nullptr) {
        events_->emit("signal.link_pending",
                      "{\"uri\":\"" + json_escape(pending_link_uri_) + "\"}");
    }

    for (uint32_t i = 0; i < config_.link_timeout_sec; ++i) {
        if (is_linked()) {
            start_daemon_if_linked();
            link_watch_active_ = false;
            if (events_ != nullptr) {
                events_->emit("signal.link_completed", "{\"linked\":true}");
            }
            return "{\"ok\":true,\"linked\":true,\"uri\":\"" + json_escape(pending_link_uri_) +
                   "\"}";
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    link_watch_active_ = false;
    if (events_ != nullptr) {
        events_->emit("signal.link_failed", "{\"error\":\"link not completed\"}");
    }
    return "{\"ok\":false,\"error\":\"link not completed\",\"uri\":\"" +
           json_escape(pending_link_uri_) + "\"}";
}

std::string SignalBackend::finish_link()
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"signal disabled\"}";
    }
    if (is_linked()) {
        start_daemon_if_linked();
        return "{\"ok\":true,\"linked\":true}";
    }
    if (link_watch_active_.load()) {
        return link_status();
    }
    return "{\"ok\":false,\"error\":\"no link in progress; use signal.start_link\"}";
}

std::string SignalBackend::list_chats()
{
    if (!is_linked()) {
        return "{\"ok\":false,\"error\":\"signal not linked\"}";
    }
    start_daemon_if_linked();
    const std::string account = linked_account();
    const std::string output =
        run_command(signal_env() + config_.signal_cli_path + " -a " + shell_escape(account) +
                    " listContacts --json 2>/dev/null");
    std::ostringstream out;
    out << "{\"ok\":true,\"chats\":[";
    bool first = true;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] != '{') {
            continue;
        }
        const std::string name = json_get_string(line, "name");
        const std::string number = json_get_string(line, "number");
        if (number.empty()) {
            continue;
        }
        if (!first) {
            out << ',';
        }
        first = false;
        out << "{\"id\":\"" << json_escape(number) << "\",\"name\":\""
            << json_escape(name.empty() ? number : name) << "\"}";
    }
    out << "]}";
    return out.str();
}

std::string SignalBackend::list_messages(const std::string &recipient)
{
    if (!is_linked()) {
        return "{\"ok\":false,\"error\":\"signal not linked\"}";
    }
    start_daemon_if_linked();
    const std::string account = linked_account();
    const std::string output =
        run_command(signal_env() + config_.signal_cli_path + " -a " + shell_escape(account) +
                    " listMessages -n " + shell_escape(recipient) + " --json 2>/dev/null");
    std::ostringstream out;
    out << "{\"ok\":true,\"messages\":[";
    bool first = true;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] != '{') {
            continue;
        }
        const std::string body = json_get_string(line, "message");
        const std::string sender = json_get_string(line, "source");
        if (body.empty()) {
            continue;
        }
        if (!first) {
            out << ',';
        }
        first = false;
        out << "{\"from\":\"" << json_escape(sender) << "\",\"text\":\"" << json_escape(body)
            << "\"}";
    }
    out << "]}";
    return out.str();
}

std::string SignalBackend::send_message(const std::string &recipient, const std::string &text)
{
    if (!is_linked()) {
        return "{\"ok\":false,\"error\":\"signal not linked\"}";
    }
    start_daemon_if_linked();
    const std::string account = linked_account();
    const std::string cmd = signal_env() + config_.signal_cli_path + " -a " + shell_escape(account) +
                            " send -m " + shell_escape(text) + " " + shell_escape(recipient) +
                            " 2>&1";
    const int status = run_command_status(cmd);
    return status == 0 ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"send failed\"}";
}

void SignalBackend::start_event_thread()
{
    if (event_running_.load()) {
        return;
    }
    event_running_ = true;
    event_thread_ = std::thread([this]() {
        while (event_running_.load()) {
            if (!is_linked()) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
            start_daemon_if_linked();
            const std::string chunk = curl_get(config_.signal_http, "/api/v1/events");
            if (chunk.empty()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            std::istringstream stream(chunk);
            std::string line;
            while (std::getline(stream, line)) {
                if (line.rfind("data:", 0) == 0) {
                    const std::string payload = line.substr(5);
                    const std::string body = json_get_string(payload, "message");
                    const std::string from = json_get_string(payload, "source");
                    if (!body.empty() && events_ != nullptr) {
                        events_->emit("message.received",
                                      "{\"from\":\"" + json_escape(from) + "\",\"text\":\"" +
                                          json_escape(body) + "\"}");
                    }
                }
            }
        }
    });
}

void SignalBackend::stop_event_thread()
{
    event_running_ = false;
    if (event_thread_.joinable()) {
        event_thread_.join();
    }
}

} // namespace braillatron::connect
