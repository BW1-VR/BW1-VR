// THE UPDATE CHECK OF THE LAUNCHER (docs/launcher.md, "The update check").
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include <string.h>

#include "../../src/common/update_check.h"

namespace update_checks {

inline void UpdateChecks() {
  using namespace update_check;
  printf("\nThe update check of the launcher\n");

  // THE TWO FIELDS OF THE TOP OBJECT. The start of a real answer of GitHub:
  // "author" has an "html_url" of its own before "tag_name".
  {
    const char json[] =
        "{\"url\":\"https://api.github.com/repos/BW1-VR/BW1-VR/releases/1\","
        "\"html_url\":\"https:\\/\\/github.com\\/BW1-VR\\/BW1-VR\\/releases"
        "\\/tag\\/v1.2.0-20261020-fd7f198\","
        "\"id\":418801387,"
        "\"author\":{\"login\":\"github-actions[bot]\","
        "\"html_url\":\"https://github.com/apps/github-actions\"},"
        "\"tag_name\":\"v1.2.0-20261020-fd7f198\","
        "\"assets\":[{\"name\":\"a\",\"uploader\":{\"html_url\":\"x\"}}],"
        "\"body\":\"text with \\\"tag_name\\\": and {braces} [and] more\"}";
    char out[256];
    Check(TopString(json, strlen(json), "tag_name", out, sizeof(out)) &&
              strcmp(out, "v1.2.0-20261020-fd7f198") == 0,
          "the tag of the release", 1, 1);
    Check(TopString(json, strlen(json), "html_url", out, sizeof(out)) &&
              strcmp(out,
                     "https://github.com/BW1-VR/BW1-VR/releases/tag/"
                     "v1.2.0-20261020-fd7f198") == 0,
          "the page of the release, with the escapes of the slashes", 1, 1);
    Check(!TopString(json, strlen(json), "login", out, sizeof(out)),
          "a key of an inner object is not a key of the top object", 0, 0);
    Check(!TopString(json, strlen(json), "id", out, sizeof(out)),
          "a value that is not a string gives false", 0, 0);
    Check(!TopString(json, strlen(json), "tag_name", out, 8),
          "a value that does not fit gives false", 0, 0);
    // The order of the keys has no effect.
    const char later[] =
        "{\"author\":{\"html_url\":\"https://github.com/someone\"},"
        "\"tag_name\":\"v2.0.0\","
        "\"html_url\":\"https://github.com/BW1-VR/BW1-VR/releases/tag/v2.0.0\"}";
    Check(TopString(later, strlen(later), "html_url", out, sizeof(out)) &&
              strcmp(out,
                     "https://github.com/BW1-VR/BW1-VR/releases/tag/v2.0.0") ==
                  0,
          "the html_url of the author is not the page of the release", 1, 1);
  }

  // ANSWERS THAT ARE NOT AS EXPECTED.
  {
    char out[64];
    const char* const kBad[] = {
        "",
        "Not Found",
        "[{\"tag_name\":\"v9.9.9\"}]",
        "{\"message\":\"API rate limit exceeded\"}",
        "{\"tag_name\":\"v9.9.9",
        "{\"tag_name\":null}",
        "{\"tag_name\":\"\\u0076\"}",
    };
    int refused = 0;
    for (const char* text : kBad) {
      if (!TopString(text, strlen(text), "tag_name", out, sizeof(out))) {
        ++refused;
      }
    }
    Check(refused == 7, "7 answers that are not as expected have no tag",
          refused, 7);
    // The length counts, not the end of the text.
    const char cut[] = "{\"tag_name\":\"v9.9.9\"}";
    Check(!TopString(cut, 12, "tag_name", out, sizeof(out)),
          "a text that stops early has no tag", 0, 0);
  }

  // THE VERSION IN THE TAG.
  {
    int a = -1, b = -1, c = -1;
    Check(TagVersion("v1.2.0-20261020-fd7f198", &a, &b, &c) && a == 1 &&
              b == 2 && c == 0,
          "v1.2.0-20261020-fd7f198 is 1.2.0", 1, 1);
    Check(TagVersion("1.10.3", &a, &b, &c) && a == 1 && b == 10 && c == 3,
          "1.10.3 with no v", 1, 1);
    Check(TagVersion("V2.0.1+beta", &a, &b, &c) && a == 2 && c == 1,
          "a capital V and a word after the numbers", 1, 1);
    const char* const kBad[] = {"", "v", "v1", "v1.2", "v1.2.", "v1..2",
                                "v1.2.3.4", "latest", "vx.1.2",
                                "v1.2.3456789"};
    int refused = 0;
    for (const char* tag : kBad) {
      if (!TagVersion(tag, &a, &b, &c)) ++refused;
    }
    Check(refused == 10, "10 tags with no version that the launcher reads",
          refused, 10);
  }

  // THE COMPARISON. 1.10.0 is newer than 1.9.0: numbers, not text.
  {
    Check(CompareVersions(1, 3, 0, 1, 2, 0) > 0, "1.3.0 is newer than 1.2.0",
          1, 1);
    Check(CompareVersions(1, 10, 0, 1, 9, 0) > 0,
          "1.10.0 is newer than 1.9.0", 1, 1);
    Check(CompareVersions(2, 0, 0, 1, 99, 99) > 0,
          "2.0.0 is newer than 1.99.99", 1, 1);
    Check(CompareVersions(1, 2, 1, 1, 2, 0) > 0, "1.2.1 is newer than 1.2.0",
          1, 1);
    Check(CompareVersions(1, 2, 0, 1, 2, 0) == 0, "1.2.0 is 1.2.0", 0, 0);
    Check(CompareVersions(1, 1, 9, 1, 2, 0) < 0, "1.1.9 is older than 1.2.0",
          -1, -1);
  }

  // THE PAGE THAT THE LAUNCHER OPENS.
  {
    Check(IsGitHubPage("https://github.com/BW1-VR/BW1-VR/releases/tag/v1.3.0"),
          "a page of a release is a page of GitHub", 1, 1);
    const char* const kBad[] = {"", "https://github.com/",
                                "http://github.com/BW1-VR",
                                "https://github.com.example.org/x",
                                "https://example.org/https://github.com/",
                                "https://github.com/a b", "file:///C:/x.exe",
                                "https://github.com/\xC3\xA9"};
    int refused = 0;
    for (const char* url : kBad) {
      if (!IsGitHubPage(url)) ++refused;
    }
    Check(refused == 8, "8 texts that the launcher does not open", refused, 8);
  }

  // THE VERDICT, for a launcher of version 1.2.0.
  {
    Release release;
    const char newer[] =
        "{\"html_url\":\"https://github.com/BW1-VR/BW1-VR/releases/tag/"
        "v1.3.0-20261101-abc1234\",\"tag_name\":\"v1.3.0-20261101-abc1234\"}";
    Check(Judge(newer, strlen(newer), 1, 2, 0, &release) == kNewer &&
              release.major == 1 && release.minor == 3 && release.patch == 0,
          "a release of 1.3.0 is newer", 1, 1);
    const char same[] =
        "{\"html_url\":\"https://github.com/BW1-VR/BW1-VR/releases/tag/"
        "v1.2.0-20261020-fd7f198\",\"tag_name\":\"v1.2.0-20261020-fd7f198\"}";
    Check(Judge(same, strlen(same), 1, 2, 0, &release) == kNotNewer,
          "a release of 1.2.0 is not newer", 1, 1);
    Check(Judge(same, strlen(same), 1, 3, 0, &release) == kNotNewer,
          "a release older than the launcher is not newer", 1, 1);
    const char no_tag[] = "{\"message\":\"Not Found\"}";
    Check(Judge(no_tag, strlen(no_tag), 1, 2, 0, &release) == kNoTag,
          "an answer of 'Not Found' has no tag", kNoTag, kNoTag);
    const char bad_tag[] =
        "{\"tag_name\":\"nightly\",\"html_url\":\"https://github.com/x\"}";
    Check(Judge(bad_tag, strlen(bad_tag), 1, 2, 0, &release) == kBadTag,
          "a tag with no version", kBadTag, kBadTag);
    const char bad_page[] =
        "{\"tag_name\":\"v9.0.0\",\"html_url\":\"https://example.org/\"}";
    Check(Judge(bad_page, strlen(bad_page), 1, 2, 0, &release) == kNoPage,
          "a page that is not on GitHub", kNoPage, kNoPage);
  }
}

}  // namespace update_checks
