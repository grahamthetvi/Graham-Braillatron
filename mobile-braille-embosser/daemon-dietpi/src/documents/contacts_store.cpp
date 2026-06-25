#include "contacts_store.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace braillatron::documents {

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string &value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string lower_copy(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

bool parse_bool(const std::string &value)
{
    const std::string lower = trim(value);
    return lower == "1" || lower == "true" || lower == "yes" || lower == "on";
}

std::string json_escape(const std::string &value)
{
    std::string out;
    out.reserve(value.size() + 4);
    for (char ch : value) {
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

std::string parse_json_string(const std::string &json, const std::string &key)
{
    const std::string needle = "\"" + key + "\":\"";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    const size_t start = pos + needle.size();
    const size_t end = json.find('"', start);
    if (end == std::string::npos) {
        return {};
    }
    return json.substr(start, end - start);
}

std::vector<std::string> parse_json_string_array(const std::string &json, const std::string &key)
{
    std::vector<std::string> values;
    const std::string needle = "\"" + key + "\":[";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return values;
    }
    size_t i = pos + needle.size();
    while (i < json.size()) {
        while (i < json.size() && json[i] != '"' && json[i] != ']') {
            ++i;
        }
        if (i >= json.size() || json[i] == ']') {
            break;
        }
        ++i;
        const size_t start = i;
        while (i < json.size() && json[i] != '"') {
            if (json[i] == '\\' && i + 1 < json.size()) {
                i += 2;
                continue;
            }
            ++i;
        }
        values.push_back(json.substr(start, i - start));
        ++i;
    }
    return values;
}

std::vector<std::string> split_csv_line(const std::string &line)
{
    std::vector<std::string> fields;
    std::string current;
    bool in_quotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            in_quotes = !in_quotes;
            continue;
        }
        if (ch == ',' && !in_quotes) {
            fields.push_back(trim(current));
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    fields.push_back(trim(current));
    return fields;
}

std::string vcard_field_value(const std::string &line)
{
    const size_t colon = line.find(':');
    if (colon == std::string::npos) {
        return {};
    }
    return trim(line.substr(colon + 1));
}

std::string vcard_property_name(const std::string &line)
{
    const size_t colon = line.find(':');
    if (colon == std::string::npos) {
        return {};
    }
    std::string property = line.substr(0, colon);
    const size_t semi = property.find(';');
    if (semi != std::string::npos) {
        property = property.substr(0, semi);
    }
    return property;
}

std::vector<std::string> unfold_vcard_lines(const std::string &content)
{
    std::vector<std::string> lines;
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!lines.empty() && !line.empty() &&
            (line[0] == ' ' || line[0] == '\t')) {
            lines.back() += trim(line);
            continue;
        }
        lines.push_back(line);
    }
    return lines;
}

Contact contact_from_vcard_block(const std::vector<std::string> &lines)
{
    Contact contact;
    for (const std::string &line : lines) {
        const std::string property = vcard_property_name(line);
        const std::string value = vcard_field_value(line);
        if (value.empty()) {
            continue;
        }
        if (property == "FN") {
            contact.name = value;
        } else if (property == "N" && contact.name.empty()) {
            const auto parts = split_csv_line(value);
            if (parts.size() >= 2) {
                contact.name = trim(parts[1] + " " + parts[0]);
            } else if (!parts.empty()) {
                contact.name = parts[0];
            }
        } else if (property == "TEL") {
            contact.phones.push_back(value);
        } else if (property == "EMAIL") {
            contact.emails.push_back(value);
        } else if (property == "ORG") {
            contact.organization = value;
        } else if (property == "NOTE") {
            contact.notes = value;
        }
    }
    return contact;
}

} // namespace

ContactsConfig load_contacts_config(const std::string &path)
{
    ContactsConfig config;
    std::ifstream file(path);
    if (!file.is_open()) {
        return config;
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key == "store_path") {
            config.store_path = value;
        } else if (key == "import_dir") {
            config.import_dir = value;
        } else if (key == "emboss_enabled") {
            config.emboss_enabled = parse_bool(value);
        } else if (key == "max_search_results") {
            config.max_search_results = std::max(1, std::atoi(value.c_str()));
        }
    }
    return config;
}

ContactsStore::ContactsStore(ContactsConfig config)
    : config_(std::move(config))
{
}

bool ContactsStore::load()
{
    contacts_.clear();
    if (!load_json_store()) {
        contacts_.clear();
    }
    process_import_dir();
    return true;
}

void ContactsStore::refresh()
{
    load();
}

bool ContactsStore::load_json_store()
{
    std::ifstream in(config_.store_path);
    if (!in.is_open()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string json = buffer.str();
    if (json.find("\"contacts\"") == std::string::npos) {
        return false;
    }

    size_t pos = 0;
    while (true) {
        const size_t start = json.find('{', pos);
        if (start == std::string::npos) {
            break;
        }
        const size_t end = json.find('}', start);
        if (end == std::string::npos) {
            break;
        }
        const std::string object = json.substr(start, end - start + 1);
        if (object.find("\"id\"") == std::string::npos) {
            pos = end + 1;
            continue;
        }

        Contact contact;
        contact.id = parse_json_string(object, "id");
        contact.name = parse_json_string(object, "name");
        contact.phones = parse_json_string_array(object, "phones");
        contact.emails = parse_json_string_array(object, "emails");
        contact.organization = parse_json_string(object, "organization");
        contact.notes = parse_json_string(object, "notes");
        if (!contact.id.empty() && !contact.name.empty()) {
            contacts_.push_back(std::move(contact));
        }
        pos = end + 1;
    }
    return true;
}

bool ContactsStore::save() const
{
    std::error_code ec;
    const fs::path path(config_.store_path);
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
    }

    const std::string temp_path = config_.store_path + ".tmp";
    std::ofstream out(temp_path);
    if (!out.is_open()) {
        return false;
    }

    out << "{\n  \"contacts\":[\n";
    for (size_t i = 0; i < contacts_.size(); ++i) {
        const Contact &contact = contacts_[i];
        if (i > 0) {
            out << ",\n";
        }
        out << "    {\n"
            << "      \"id\":\"" << json_escape(contact.id) << "\",\n"
            << "      \"name\":\"" << json_escape(contact.name) << "\",\n"
            << "      \"phones\":[";
        for (size_t p = 0; p < contact.phones.size(); ++p) {
            if (p > 0) {
                out << ',';
            }
            out << "\"" << json_escape(contact.phones[p]) << "\"";
        }
        out << "],\n      \"emails\":[";
        for (size_t e = 0; e < contact.emails.size(); ++e) {
            if (e > 0) {
                out << ',';
            }
            out << "\"" << json_escape(contact.emails[e]) << "\"";
        }
        out << "],\n"
            << "      \"organization\":\"" << json_escape(contact.organization) << "\",\n"
            << "      \"notes\":\"" << json_escape(contact.notes) << "\"\n"
            << "    }";
    }
    out << "\n  ]\n}\n";
    out.flush();
    if (!out.good()) {
        return false;
    }

    fs::rename(temp_path, config_.store_path, ec);
    return !ec;
}

std::string ContactsStore::next_id() const
{
    return "contact-" + std::to_string(static_cast<long long>(std::time(nullptr))) + "-" +
           std::to_string(++id_counter_);
}

std::string ContactsStore::dedupe_key(const Contact &contact) const
{
    std::string key = lower_copy(contact.name);
    if (!contact.phones.empty()) {
        key += "|" + lower_copy(contact.phones.front());
    } else if (!contact.emails.empty()) {
        key += "|" + lower_copy(contact.emails.front());
    }
    return key;
}

void ContactsStore::upsert_contact(Contact contact)
{
    if (contact.name.empty()) {
        return;
    }
    if (contact.id.empty()) {
        contact.id = next_id();
    }

    const std::string key = dedupe_key(contact);
    for (auto &existing : contacts_) {
        if (dedupe_key(existing) == key) {
            existing = std::move(contact);
            return;
        }
    }
    contacts_.push_back(std::move(contact));
}

bool ContactsStore::add_contact(const std::string &name, const std::string &phone,
                                const std::string &email, const std::string &organization,
                                const std::string &notes)
{
    if (name.empty()) {
        return false;
    }

    Contact contact;
    contact.name = name;
    if (!phone.empty()) {
        contact.phones.push_back(phone);
    }
    if (!email.empty()) {
        contact.emails.push_back(email);
    }
    contact.organization = organization;
    contact.notes = notes;
    upsert_contact(std::move(contact));
    if (!save()) {
        return false;
    }
    refresh();
    return true;
}

bool ContactsStore::import_csv_file(const std::string &path)
{
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }

    bool imported = false;
    std::string line;
    std::vector<std::string> headers;
    bool first_row = true;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const auto fields = split_csv_line(line);
        if (fields.empty()) {
            continue;
        }

        if (first_row) {
            first_row = false;
            const std::string header_line = lower_copy(line);
            if (header_line.find("name") != std::string::npos) {
                headers = fields;
                for (auto &header : headers) {
                    header = lower_copy(header);
                }
                continue;
            }
        }

        Contact contact;
        if (headers.empty()) {
            contact.name = fields[0];
            if (fields.size() > 1 && !fields[1].empty()) {
                contact.phones.push_back(fields[1]);
            }
            if (fields.size() > 2 && !fields[2].empty()) {
                contact.emails.push_back(fields[2]);
            }
            if (fields.size() > 3) {
                contact.organization = fields[3];
            }
            if (fields.size() > 4) {
                contact.notes = fields[4];
            }
        } else {
            for (size_t i = 0; i < headers.size() && i < fields.size(); ++i) {
                const std::string &header = headers[i];
                const std::string &value = fields[i];
                if (value.empty()) {
                    continue;
                }
                if (header == "name") {
                    contact.name = value;
                } else if (header == "phone" || header == "phones") {
                    contact.phones.push_back(value);
                } else if (header == "email" || header == "emails") {
                    contact.emails.push_back(value);
                } else if (header == "organization" || header == "org" || header == "company") {
                    contact.organization = value;
                } else if (header == "notes" || header == "note") {
                    contact.notes = value;
                }
            }
        }

        if (!contact.name.empty()) {
            upsert_contact(std::move(contact));
            imported = true;
        }
    }
    return imported;
}

bool ContactsStore::import_vcard_file(const std::string &path)
{
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::vector<std::string> lines = unfold_vcard_lines(buffer.str());

    bool imported = false;
    std::vector<std::string> block;
    for (const std::string &line : lines) {
        const std::string upper = trim(line);
        if (upper == "BEGIN:VCARD") {
            block.clear();
            continue;
        }
        if (upper == "END:VCARD") {
            Contact contact = contact_from_vcard_block(block);
            if (!contact.name.empty()) {
                upsert_contact(std::move(contact));
                imported = true;
            }
            block.clear();
            continue;
        }
        if (!block.empty() || !line.empty()) {
            block.push_back(line);
        }
    }
    return imported;
}

bool ContactsStore::process_import_dir()
{
    if (config_.import_dir.empty()) {
        return false;
    }

    std::error_code ec;
    if (!fs::exists(config_.import_dir, ec)) {
        return false;
    }

    const std::string processed_dir = config_.import_dir + "/processed";
    fs::create_directories(processed_dir, ec);

    bool changed = false;
    for (const auto &entry : fs::directory_iterator(config_.import_dir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string path = entry.path().string();
        const std::string ext = lower_copy(entry.path().extension().string());
        bool imported = false;
        if (ext == ".csv") {
            imported = import_csv_file(path);
        } else if (ext == ".vcf" || ext == ".vcard") {
            imported = import_vcard_file(path);
        }
        if (!imported) {
            continue;
        }
        changed = true;
        const fs::path dest = fs::path(processed_dir) / entry.path().filename();
        fs::rename(path, dest, ec);
        if (ec) {
            ec.clear();
            fs::copy_file(path, dest, fs::copy_options::overwrite_existing, ec);
            if (!ec) {
                fs::remove(path, ec);
            }
        }
    }

    if (changed) {
        save();
    }
    return changed;
}

std::vector<Contact> ContactsStore::search(const std::string &query) const
{
    std::vector<Contact> matches;
    const std::string needle = lower_copy(trim(query));
    for (const Contact &contact : contacts_) {
        if (needle.empty()) {
            matches.push_back(contact);
        } else if (lower_copy(contact.name).find(needle) != std::string::npos) {
            matches.push_back(contact);
        } else if (!contact.organization.empty() &&
                   lower_copy(contact.organization).find(needle) != std::string::npos) {
            matches.push_back(contact);
        } else {
            bool field_match = false;
            for (const std::string &phone : contact.phones) {
                if (lower_copy(phone).find(needle) != std::string::npos) {
                    field_match = true;
                    break;
                }
            }
            if (!field_match) {
                for (const std::string &email : contact.emails) {
                    if (lower_copy(email).find(needle) != std::string::npos) {
                        field_match = true;
                        break;
                    }
                }
            }
            if (field_match) {
                matches.push_back(contact);
            }
        }
        if (matches.size() >= static_cast<size_t>(config_.max_search_results)) {
            break;
        }
    }
    return matches;
}

const Contact *ContactsStore::find_by_id(const std::string &id) const
{
    for (const Contact &contact : contacts_) {
        if (contact.id == id) {
            return &contact;
        }
    }
    return nullptr;
}

std::string ContactsStore::format_card(const Contact &contact) const
{
    std::ostringstream card;
    card << contact.name;
    if (!contact.organization.empty()) {
        card << "\n" << contact.organization;
    }
    for (const std::string &phone : contact.phones) {
        card << "\nPhone: " << phone;
    }
    for (const std::string &email : contact.emails) {
        card << "\nEmail: " << email;
    }
    if (!contact.notes.empty()) {
        card << "\n" << contact.notes;
    }
    return card.str();
}

} // namespace braillatron::documents
