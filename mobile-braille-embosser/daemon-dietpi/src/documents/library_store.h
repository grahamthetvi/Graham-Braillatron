#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "liblouis_bridge.h"

namespace braillatron::documents {

struct LibraryBook {
    std::string id;
    std::string title;
    std::string author;
    std::string format;
    std::string local_path;
    std::string source;
    int gutenberg_id = 0;
};

struct BookSection {
    std::string id;
    std::string href;
    std::string title;
    /** Print text. For a BRF book this is the back-translation. */
    std::string text;
    /**
     * Original North American BRF when this section was imported from Braille.
     * Empty for print books. Emboss this instead of forward-translating `text`.
     */
    std::string emboss_brf;
    int spine_index = 0;
};

struct ReadingState {
    std::string book_id;
    int section_index = 0;
    int char_offset = 0;
    uint64_t updated_at = 0;
};

struct LibraryStoreConfig {
    std::string catalog_path = "/data/braillatron/library/catalog.json";
    std::string books_dir = "/data/braillatron/library/books";
    std::string import_dir = "/data/braillatron/library/import";
    std::string state_dir = "/data/braillatron/library/state";
    bool epub_enabled = true;
    bool daisy_enabled = true;
    int max_local_results = 20;
};

LibraryStoreConfig load_library_store_config(const std::string &path);

class EbookDocument {
public:
    bool open(const std::string &path);
    /** Re-run BRF back-translation with the caller's grade. No-op for print books. */
    void apply_back_translation(const BrailleTranslationService &braille);
    const std::vector<BookSection> &sections() const { return sections_; }
    const std::string &title() const { return title_; }
    const std::string &author() const { return author_; }
    const std::string &format() const { return format_; }

private:
    bool open_epub_archive(const std::string &path);
    bool open_extracted_dir(const std::string &dir, const std::string &format);
    bool parse_opf(const std::string &opf_path, const std::string &base_dir);
    void load_spine_sections(const std::string &opf_content, const std::string &base_dir);
    void apply_ncx_titles(const std::string &ncx_path, const std::string &base_dir);
    std::string find_opf_path(const std::string &container_xml) const;
    std::string html_to_text(const std::string &html) const;
    std::string read_file(const std::string &path) const;
    bool open_brf(const std::string &path);

    std::string title_;
    std::string author_;
    std::string format_;
    std::vector<BookSection> sections_;
};

class LibraryStore {
public:
    explicit LibraryStore(LibraryStoreConfig config = {});

    bool load();
    bool save() const;
    void refresh();

    const std::vector<LibraryBook> &books() const { return books_; }
    std::vector<LibraryBook> search_local(const std::string &query) const;
    const LibraryBook *find_by_id(const std::string &id) const;

    bool register_book(LibraryBook book);
    bool remove_book(const std::string &id);
    bool rename_book(const std::string &id, const std::string &new_title);
    bool import_file(const std::string &src_path);
    /** Write a BRF document into the library and register it. Returns the stored path. */
    std::optional<std::string> import_brf_text(const std::string &filename, const std::string &brf);
    std::vector<std::string> list_removable_mounts() const;
    bool save_document_text(const std::string &text, const std::string &title_hint = {});
    bool register_media_file(const std::string &path, const std::string &title,
                             const std::string &source);
    bool process_import_dir();

    ReadingState load_reading_state(const std::string &book_id) const;
    bool save_reading_state(const ReadingState &state) const;

    std::string detect_format(const std::string &path) const;

private:
    bool load_catalog();
    std::string next_id() const;

    LibraryStoreConfig config_;
    std::vector<LibraryBook> books_;
    mutable int id_counter_ = 0;
};

} // namespace braillatron::documents
