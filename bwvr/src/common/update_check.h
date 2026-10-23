// The update check of the launcher: it reads the answer of GitHub and
// compares the version of the latest release with the version of the
// launcher. See docs/launcher.md, "The update check". No Windows and no game
// here, so the math test compiles this file.
//
// The answer is the JSON of
// https://api.github.com/repos/BW1-VR/BW1-VR/releases/latest. The launcher
// needs two fields of its top object: "tag_name" (for example
// "v1.2.0-20261020-fd7f198") and "html_url" (the page of the release). The
// objects in the answer ("author", each asset) have an "html_url" too, so
// the parse reads only the keys of the top object.

#pragma once

#include <stddef.h>

namespace update_check {

// The string value of `key` in the top object of `json`, without its
// quotes, in `out`. False when the text is not an object, when the top
// object has no such key, when the value is not a string, or when the value
// does not fit in `size` bytes with its end. A \u escape gives false: a tag
// and a URL of GitHub have none.
inline bool TopString(const char* json, size_t length, const char* key,
                      char* out, size_t size) {
  if (json == nullptr || key == nullptr || out == nullptr || size == 0) {
    return false;
  }
  size_t i = 0;
  while (i < length && (json[i] == ' ' || json[i] == '\t' ||
                        json[i] == '\r' || json[i] == '\n')) {
    ++i;
  }
  if (i >= length || json[i] != '{') return false;
  int depth = 0;
  for (; i < length; ++i) {
    const char c = json[i];
    if (c == '{' || c == '[') {
      ++depth;
      continue;
    }
    if (c == '}' || c == ']') {
      --depth;
      if (depth <= 0) return false;
      continue;
    }
    if (c != '"') continue;
    // A string. At depth 1 it is a key when a colon follows it.
    const size_t start = i + 1;
    size_t end = start;
    while (end < length && json[end] != '"') {
      if (json[end] == '\\') ++end;
      ++end;
    }
    if (end >= length) return false;
    i = end;
    if (depth != 1) continue;
    size_t next = end + 1;
    while (next < length && (json[next] == ' ' || json[next] == '\t' ||
                             json[next] == '\r' || json[next] == '\n')) {
      ++next;
    }
    if (next >= length || json[next] != ':') continue;
    size_t key_length = 0;
    while (key[key_length] != 0) ++key_length;
    bool same = end - start == key_length;
    for (size_t k = 0; same && k < key_length; ++k) {
      same = json[start + k] == key[k];
    }
    if (!same) continue;
    // The value of the key.
    ++next;
    while (next < length && (json[next] == ' ' || json[next] == '\t' ||
                             json[next] == '\r' || json[next] == '\n')) {
      ++next;
    }
    if (next >= length || json[next] != '"') return false;
    size_t n = 0;
    for (size_t v = next + 1; v < length; ++v) {
      char ch = json[v];
      if (ch == '"') {
        out[n] = 0;
        return true;
      }
      if (ch == '\\') {
        if (++v >= length) return false;
        switch (json[v]) {
          case '"': ch = '"'; break;
          case '\\': ch = '\\'; break;
          case '/': ch = '/'; break;
          default: return false;
        }
      }
      if (n + 1 >= size) return false;
      out[n++] = ch;
    }
    return false;
  }
  return false;
}

// The version in a tag of a release: "v1.2.0-20261020-fd7f198" is 1.2.0. The
// "v" is optional. After the third number comes the end or a character that
// is not a digit and not a point. False for any other text.
inline bool TagVersion(const char* tag, int* major, int* minor, int* patch) {
  if (tag == nullptr) return false;
  const char* p = tag;
  if (*p == 'v' || *p == 'V') ++p;
  int part[3] = {0, 0, 0};
  for (int n = 0; n < 3; ++n) {
    if (*p < '0' || *p > '9') return false;
    int value = 0;
    while (*p >= '0' && *p <= '9') {
      value = value * 10 + (*p - '0');
      if (value > 99999) return false;
      ++p;
    }
    part[n] = value;
    if (n < 2) {
      if (*p != '.') return false;
      ++p;
    }
  }
  if (*p == '.' || (*p >= '0' && *p <= '9')) return false;
  *major = part[0];
  *minor = part[1];
  *patch = part[2];
  return true;
}

// Above zero when version a is newer than version b, zero when they are the
// same, below zero when a is older.
inline int CompareVersions(int a_major, int a_minor, int a_patch, int b_major,
                           int b_minor, int b_patch) {
  if (a_major != b_major) return a_major > b_major ? 1 : -1;
  if (a_minor != b_minor) return a_minor > b_minor ? 1 : -1;
  if (a_patch != b_patch) return a_patch > b_patch ? 1 : -1;
  return 0;
}

// The launcher opens only a page of GitHub in the browser of the player: an
// address that starts with https://github.com/ and has only printable ASCII
// characters with no space and no quote.
inline bool IsGitHubPage(const char* url) {
  if (url == nullptr) return false;
  const char kPagePrefix[] = "https://github.com/";
  for (size_t i = 0; i + 1 < sizeof(kPagePrefix); ++i) {
    if (url[i] != kPagePrefix[i]) return false;
  }
  for (const char* p = url; *p != 0; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c <= ' ' || c >= 0x7F || c == '"') return false;
  }
  return url[sizeof(kPagePrefix) - 1] != 0;
}

enum Verdict {
  kNewer = 0,     // the release is newer than the launcher
  kNotNewer = 1,  // the release has the version of the launcher, or older
  kNoTag = 2,     // the answer has no "tag_name"
  kBadTag = 3,    // the tag has no version that the launcher can read
  kNoPage = 4,    // the answer has no "html_url" that is a page of GitHub
};

struct Release {
  char tag[128] = "";
  char page[512] = "";
  int major = 0, minor = 0, patch = 0;
};

// The verdict on one answer of GitHub, for a launcher of version
// own_major.own_minor.own_patch. `release` gets the fields that the parse
// found.
inline Verdict Judge(const char* json, size_t length, int own_major,
                     int own_minor, int own_patch, Release* release) {
  if (!TopString(json, length, "tag_name", release->tag,
                 sizeof(release->tag))) {
    return kNoTag;
  }
  if (!TagVersion(release->tag, &release->major, &release->minor,
                  &release->patch)) {
    return kBadTag;
  }
  if (!TopString(json, length, "html_url", release->page,
                 sizeof(release->page)) ||
      !IsGitHubPage(release->page)) {
    return kNoPage;
  }
  return CompareVersions(release->major, release->minor, release->patch,
                         own_major, own_minor, own_patch) > 0
             ? kNewer
             : kNotNewer;
}

}  // namespace update_check
