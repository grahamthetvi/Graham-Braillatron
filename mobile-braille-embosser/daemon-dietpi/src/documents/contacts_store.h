#pragma once

#include <string>
#include <vector>

namespace braillatron::documents {

struct Contact {
    std::string id;
    std::string name;
    std::vector<std::string> phones;
    std::vector<std::string> emails;
    std::string organization;
    std::string notes;
};

struct ContactsConfig {
    std::string store_path = "/data/braillatron/contacts/contacts.json";
    std::string import_dir = "/data/braillatron/contacts/import";
    bool emboss_enabled = true;
    int max_search_results = 20;
};

ContactsConfig load_contacts_config(const std::string &path);

class ContactsStore {
public:
    explicit ContactsStore(ContactsConfig config = {});

    bool load();
    bool save() const;
    void refresh();

    const std::vector<Contact> &contacts() const { return contacts_; }
    std::vector<Contact> search(const std::string &query) const;
    const Contact *find_by_id(const std::string &id) const;

    std::string format_card(const Contact &contact) const;

    bool import_csv_file(const std::string &path);
    bool import_vcard_file(const std::string &path);

private:
    bool load_json_store();
    bool process_import_dir();
    void upsert_contact(Contact contact);
    std::string dedupe_key(const Contact &contact) const;
    std::string next_id() const;

    ContactsConfig config_;
    std::vector<Contact> contacts_;
    mutable int id_counter_ = 0;
};

} // namespace braillatron::documents
