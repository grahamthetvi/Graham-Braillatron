#include "library_store.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

int failures = 0;

void expect_true(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool write_file(const std::string &path, const std::string &contents)
{
    std::error_code ec;
    const std::filesystem::path file_path(path);
    if (file_path.has_parent_path()) {
        std::filesystem::create_directories(file_path.parent_path(), ec);
    }
    std::ofstream out(path);
    if (!out.is_open()) {
        return false;
    }
    out << contents;
    return true;
}

std::string temp_dir()
{
    const char *env = std::getenv("TMPDIR");
    const std::string base =
        (env != nullptr && env[0] != '\0') ? env : "braillatron-self-test-tmp";
    return base + "/library-" + std::to_string(::getpid());
}

bool test_epub_directory_parse()
{
    const std::string dir = temp_dir() + "-epub";
    expect_true(write_file(dir + "/META-INF/container.xml",
                           "<?xml version=\"1.0\"?>\n"
                           "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
                           "  <rootfiles>\n"
                           "    <rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>\n"
                           "  </rootfiles>\n"
                           "</container>\n"),
                "write container.xml");
    expect_true(write_file(dir + "/OEBPS/content.opf",
                           "<?xml version=\"1.0\"?>\n"
                           "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\">\n"
                           "  <metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
                           "    <dc:title>Test Book</dc:title>\n"
                           "    <dc:creator>Test Author</dc:creator>\n"
                           "  </metadata>\n"
                           "  <manifest>\n"
                           "    <item id=\"nav\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>\n"
                           "    <item id=\"ch1\" href=\"chapter1.xhtml\" media-type=\"application/xhtml+xml\"/>\n"
                           "    <item id=\"ch2\" href=\"chapter2.xhtml\" media-type=\"application/xhtml+xml\"/>\n"
                           "  </manifest>\n"
                           "  <spine toc=\"nav\">\n"
                           "    <itemref idref=\"ch1\"/>\n"
                           "    <itemref idref=\"ch2\"/>\n"
                           "  </spine>\n"
                           "</package>\n"),
                "write content.opf");
    expect_true(write_file(dir + "/OEBPS/toc.ncx",
                           "<?xml version=\"1.0\"?>\n"
                           "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\">\n"
                           "  <navMap>\n"
                           "    <navPoint id=\"np1\"><navLabel><text>Chapter One</text></navLabel>"
                           "<content src=\"chapter1.xhtml\"/></navPoint>\n"
                           "    <navPoint id=\"np2\"><navLabel><text>Chapter Two</text></navLabel>"
                           "<content src=\"chapter2.xhtml\"/></navPoint>\n"
                           "  </navMap>\n"
                           "</ncx>\n"),
                "write toc.ncx");
    expect_true(write_file(dir + "/OEBPS/chapter1.xhtml",
                           "<html><body><h1>Chapter One</h1><p>First chapter body.</p></body></html>"),
                "write chapter1");
    expect_true(write_file(dir + "/OEBPS/chapter2.xhtml",
                           "<html><body><h1>Chapter Two</h1><p>Second chapter body.</p></body></html>"),
                "write chapter2");

    braillatron::documents::EbookDocument doc;
    expect_true(doc.open(dir), "open daisy/epub directory");
    expect_true(doc.title() == "Test Book", "book title");
    expect_true(doc.author() == "Test Author", "book author");
    expect_true(doc.sections().size() == 2, "section count");
    if (doc.sections().size() >= 2) {
        expect_true(doc.sections()[0].title == "Chapter One", "ncx title chapter one");
        expect_true(doc.sections()[0].text.find("First chapter") != std::string::npos,
                    "chapter one text");
        expect_true(doc.sections()[1].title == "Chapter Two", "ncx title chapter two");
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return true;
}

bool test_txt_open()
{
    const std::string dir = temp_dir() + "-txt";
    expect_true(write_file(dir + "/sample.txt", "Plain text book content."), "write txt");
    braillatron::documents::EbookDocument doc;
    expect_true(doc.open(dir + "/sample.txt"), "open txt");
    expect_true(doc.sections().size() == 1, "txt section count");
    if (!doc.sections().empty()) {
        expect_true(doc.sections()[0].text.find("Plain text") != std::string::npos, "txt content");
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return true;
}

bool test_catalog_and_resume_state()
{
    const std::string dir = temp_dir() + "-store";
    std::error_code ec;
    std::filesystem::create_directories(dir + "/books", ec);
    std::filesystem::create_directories(dir + "/state", ec);

    braillatron::documents::LibraryStoreConfig config;
    config.catalog_path = dir + "/catalog.json";
    config.books_dir = dir + "/books";
    config.import_dir = dir + "/import";
    config.state_dir = dir + "/state";

    braillatron::documents::LibraryStore store(config);
    braillatron::documents::LibraryBook book;
    book.title = "Catalog Test";
    book.author = "Tester";
    book.format = "txt";
    book.local_path = dir + "/books/test.txt";
    book.source = "test";
    write_file(book.local_path, "Body text.");
    expect_true(store.register_book(book), "register book");
    expect_true(store.books().size() == 1, "catalog count");

    const auto matches = store.search_local("catalog");
    expect_true(matches.size() == 1, "search finds book");

    braillatron::documents::ReadingState state;
    state.book_id = store.books().front().id;
    state.section_index = 2;
    state.char_offset = 10;
    state.updated_at = 12345;
    expect_true(store.save_reading_state(state), "save reading state");

    const braillatron::documents::ReadingState loaded =
        store.load_reading_state(store.books().front().id);
    expect_true(loaded.section_index == 2, "resume section index");
    expect_true(loaded.char_offset == 10, "resume char offset");
    expect_true(loaded.updated_at == 12345, "resume timestamp");

    std::filesystem::remove_all(dir, ec);
    return true;
}

} // namespace

int main()
{
    test_epub_directory_parse();
    test_txt_open();
    test_catalog_and_resume_state();

    if (failures != 0) {
        std::cerr << failures << " library self-test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "library self-test passed\n";
    return EXIT_SUCCESS;
}
