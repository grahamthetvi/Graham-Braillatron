#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace braillatron::ui {
namespace {

bool service_active()
{
    FILE *pipe = popen("systemctl is-active braillatron-localsend 2>/dev/null", "r");
    if (pipe == nullptr) {
        return false;
    }
    char buf[64] = {};
    const char *got = fgets(buf, sizeof(buf), pipe);
    pclose(pipe);
    if (got == nullptr) {
        return false;
    }
    std::string status(buf);
    while (!status.empty() && (status.back() == '\n' || status.back() == '\r')) {
        status.pop_back();
    }
    return status == "active";
}

std::string primary_ip()
{
    FILE *pipe = popen(
        "hostname -I 2>/dev/null | awk '{print $1}'", "r");
    if (pipe == nullptr) {
        return {};
    }
    char buf[64] = {};
    const char *got = fgets(buf, sizeof(buf), pipe);
    pclose(pipe);
    if (got == nullptr) {
        return {};
    }
    std::string ip(buf);
    while (!ip.empty() && (ip.back() == '\n' || ip.back() == '\r' || ip.back() == ' ')) {
        ip.pop_back();
    }
    return ip;
}

std::vector<std::string> recent_received(size_t limit)
{
    std::vector<std::string> names;
    std::ifstream in("/data/braillatron/localsend/received.jsonl");
    if (!in) {
        return names;
    }
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    const size_t start = lines.size() > limit ? lines.size() - limit : 0;
    for (size_t i = start; i < lines.size(); ++i) {
        const std::string &json = lines[i];
        const size_t key = json.find("\"name\":\"");
        if (key == std::string::npos) {
            continue;
        }
        const size_t begin = key + 8;
        const size_t end = json.find('"', begin);
        if (end == std::string::npos) {
            continue;
        }
        names.push_back(json.substr(begin, end - begin));
    }
    return names;
}

class LocalSendApp final : public AppSession {
public:
    std::string id() const override { return "localsend"; }
    std::string label() const override { return "LocalSend"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        step_ = 0;
        if (!service_active()) {
            announce(ctx,
                     "LocalSend receiver is not running. Ask an admin to enable "
                     "braillatron-localsend.");
            return;
        }
        announce_step(ctx);
    }

    void on_exit(UiContext &ctx) override { announce(ctx, "LocalSend closed"); }
    void on_poll(UiContext &) override {}
    void on_chord(uint8_t, UiContext &) override {}
    void on_text(const std::string &, UiContext &) override {}

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed || !service_active()) {
            return;
        }
        if (key == keyboard::ControlKey::Enter || key == keyboard::ControlKey::DpadDown) {
            step_ = (step_ + 1) % 5;
            announce_step(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadUp) {
            step_ = (step_ + 4) % 5;
            announce_step(ctx);
            return;
        }
        if (key == keyboard::ControlKey::Backspace) {
            announce_step(ctx);
        }
    }

private:
    void announce_step(UiContext &ctx)
    {
        const std::string ip = primary_ip();
        const std::string addr = ip.empty() ? "this device IP" : ip;
        switch (step_) {
        case 0:
            announce(ctx,
                     "LocalSend setup step 1 of 5. Install LocalSend on your phone or computer "
                     "from localsend.org. Enter for next step.");
            break;
        case 1:
            announce(ctx,
                     "Step 2. Connect the phone to the same WiFi as Braillatron, or use the "
                     "Ethernet network. Braillatron IP is " +
                         addr + ". Enter for next.");
            break;
        case 2:
            announce(ctx,
                     "Step 3. In LocalSend settings, turn encryption or HTTPS off so it uses "
                     "HTTP. Or add this device manually as " +
                         addr + " port 53317. Enter for next.");
            break;
        case 3:
            announce(ctx,
                     "Step 4. Send files to the device named Braillatron. Cookies go to "
                     "credentials, books to library import, music to the music folder, OPML to "
                     "podcasts. Enter for next.");
            break;
        case 4: {
            std::string msg = "Step 5. Status: receiver is running on port 53317.";
            const auto recent = recent_received(3);
            if (recent.empty()) {
                msg += " No files received yet. Enter to repeat setup.";
            } else {
                msg += " Recent files: ";
                for (size_t i = 0; i < recent.size(); ++i) {
                    if (i > 0) {
                        msg += ", ";
                    }
                    msg += recent[i];
                }
                msg += ". Enter to repeat setup.";
            }
            announce(ctx, msg);
            break;
        }
        default:
            step_ = 0;
            announce_step(ctx);
            break;
        }
    }

    int step_ = 0;
};

} // namespace

std::unique_ptr<AppSession> make_localsend_app()
{
    return std::make_unique<LocalSendApp>();
}

} // namespace braillatron::ui
