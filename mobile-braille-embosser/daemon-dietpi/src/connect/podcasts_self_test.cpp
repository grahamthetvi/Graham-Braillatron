#include "rss_backend.h"

#include <cstdlib>
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

void test_rss_episode_parsing()
{
    const std::string rss = R"(<?xml version="1.0"?>
<rss version="2.0">
  <channel>
    <title>Test Podcast</title>
    <item>
      <title>Episode One</title>
      <pubDate>Mon, 01 Jan 2024 00:00:00 GMT</pubDate>
      <enclosure url="https://example.com/ep1.mp3" type="audio/mpeg"/>
    </item>
    <item>
      <title>Episode Two</title>
      <enclosure url="https://example.com/ep2.mp3" type="audio/mpeg"/>
    </item>
  </channel>
</rss>)";

    const std::vector<braillatron::connect::PodcastEpisode> episodes =
        braillatron::connect::parse_rss_episodes(rss, "feed-0", 10);
    expect_true(episodes.size() == 2, "rss episode count");
    expect_true(episodes[0].title == "Episode One", "first episode title");
    expect_true(episodes[0].enclosure_url == "https://example.com/ep1.mp3", "enclosure url");
    expect_true(episodes[0].id == "feed-0-0", "episode id");
    expect_true(episodes[1].title == "Episode Two", "second episode title");
}

void test_atom_episode_parsing()
{
    const std::string atom = R"(<?xml version="1.0"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <title>Atom Podcast</title>
  <entry>
    <title>Atom Episode</title>
    <published>2024-01-01T00:00:00Z</published>
    <link href="https://example.com/atom1.mp3" rel="enclosure" type="audio/mpeg"/>
  </entry>
</feed>)";

    const std::vector<braillatron::connect::PodcastEpisode> episodes =
        braillatron::connect::parse_rss_episodes(atom, "feed-1", 10);
    expect_true(episodes.size() == 1, "atom episode count");
    expect_true(episodes[0].title == "Atom Episode", "atom episode title");
    expect_true(episodes[0].enclosure_url == "https://example.com/atom1.mp3", "atom enclosure");
}

void test_opml_import()
{
    const std::string opml = R"(<?xml version="1.0"?>
<opml version="2.0">
  <body>
    <outline text="News" title="News">
      <outline text="Daily" title="Daily News" xmlUrl="https://example.com/feed.rss"/>
    </outline>
    <outline text="Tech" title="Tech Talk" xmlUrl="https://example.com/tech.rss"/>
  </body>
</opml>)";

    const std::vector<braillatron::connect::PodcastFeed> feeds =
        braillatron::connect::parse_opml(opml);
    expect_true(feeds.size() == 2, "opml feed count");
    expect_true(feeds[0].url == "https://example.com/feed.rss", "nested opml url");
    expect_true(feeds[0].title == "Daily News", "nested opml title");
    expect_true(feeds[1].url == "https://example.com/tech.rss", "flat opml url");
}

void test_xml_helpers()
{
    const std::string xml = "<item><title>Hello World</title></item>";
    expect_true(braillatron::connect::xml_tag_content(xml, "title") == "Hello World", "xml tag content");
    const std::string enclosure = R"(<enclosure url="https://x.com/a.mp3" length="123"/>)";
    expect_true(braillatron::connect::xml_attr_value(enclosure, "enclosure", "url") ==
                    "https://x.com/a.mp3",
                "xml attr value");
}

} // namespace

int main()
{
    test_xml_helpers();
    test_rss_episode_parsing();
    test_atom_episode_parsing();
    test_opml_import();

    if (failures > 0) {
        std::cerr << failures << " podcasts self-test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "podcasts self-test passed\n";
    return EXIT_SUCCESS;
}
