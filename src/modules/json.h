// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
//
// A tiny read-only JSON reader, just big enough to walk a module catalog.
// Deliberately not a general-purpose library: numbers go through strtod, and a
// duplicate key keeps the first one. Header-only; used only by src/modules.
#ifndef LED_MATRIX_SERVER_MODULES_JSON_H
#define LED_MATRIX_SERVER_MODULES_JSON_H

#include <stdlib.h>
#include <string>
#include <utility>
#include <vector>

class Json {
public:
  enum Type { NUL, BOOL, NUM, STR, ARR, OBJ };
  Type type = NUL;
  bool boolean = false;
  double num = 0;
  std::string str;
  std::vector<Json> arr;
  std::vector<std::pair<std::string, Json> > obj;

  // Member lookup; null when absent or when this isn't an object.
  const Json *Get(const std::string &key) const {
    if (type != OBJ) return NULL;
    for (size_t i = 0; i < obj.size(); ++i)
      if (obj[i].first == key) return &obj[i].second;
    return NULL;
  }
  // A string member, or `dflt` if missing / not a string.
  std::string Str(const std::string &key, const char *dflt = "") const {
    const Json *v = Get(key);
    return (v && v->type == STR) ? v->str : std::string(dflt);
  }

  // Parse `text`. On failure returns false and sets *err.
  static bool Parse(const std::string &text, Json *out, std::string *err) {
    size_t i = 0;
    Json v;
    if (!ParseValue(text, &i, &v, err)) return false;
    SkipWs(text, &i);
    *out = v;
    return true;
  }

private:
  static void SkipWs(const std::string &s, size_t *i) {
    while (*i < s.size() && (s[*i] == ' ' || s[*i] == '\t' || s[*i] == '\n' || s[*i] == '\r')) ++*i;
  }
  static bool Fail(std::string *err, const char *msg, size_t at) {
    char b[96];
    snprintf(b, sizeof b, "%s at offset %zu", msg, at);
    *err = b;
    return false;
  }
  static bool ParseString(const std::string &s, size_t *i, std::string *out, std::string *err) {
    if (*i >= s.size() || s[*i] != '"') return Fail(err, "expected string", *i);
    ++*i;
    out->clear();
    while (*i < s.size() && s[*i] != '"') {
      char c = s[*i];
      if (c == '\\') {
        if (++*i >= s.size()) break;
        switch (s[*i]) {
        case 'n': *out += '\n'; break;
        case 't': *out += '\t'; break;
        case 'r': *out += '\r'; break;
        case 'b': *out += '\b'; break;
        case 'f': *out += '\f'; break;
        case 'u': {                       // \uXXXX -> UTF-8 (no surrogate pairs)
          if (*i + 4 >= s.size()) return Fail(err, "bad \\u escape", *i);
          const unsigned cp = (unsigned)strtoul(s.substr(*i + 1, 4).c_str(), NULL, 16);
          *i += 4;
          if (cp < 0x80) { *out += (char)cp; }
          else if (cp < 0x800) { *out += (char)(0xC0 | (cp >> 6)); *out += (char)(0x80 | (cp & 0x3F)); }
          else { *out += (char)(0xE0 | (cp >> 12)); *out += (char)(0x80 | ((cp >> 6) & 0x3F));
                 *out += (char)(0x80 | (cp & 0x3F)); }
          break; }
        default: *out += s[*i]; break;    // covers \" \\ \/
        }
        ++*i;
      } else {
        *out += c;
        ++*i;
      }
    }
    if (*i >= s.size()) return Fail(err, "unterminated string", *i);
    ++*i;                                  // closing quote
    return true;
  }
  static bool ParseValue(const std::string &s, size_t *i, Json *out, std::string *err) {
    SkipWs(s, i);
    if (*i >= s.size()) return Fail(err, "unexpected end", *i);
    const char c = s[*i];
    if (c == '{') {
      out->type = OBJ;
      ++*i;
      SkipWs(s, i);
      if (*i < s.size() && s[*i] == '}') { ++*i; return true; }
      for (;;) {
        SkipWs(s, i);
        std::string key;
        if (!ParseString(s, i, &key, err)) return false;
        SkipWs(s, i);
        if (*i >= s.size() || s[*i] != ':') return Fail(err, "expected ':'", *i);
        ++*i;
        Json v;
        if (!ParseValue(s, i, &v, err)) return false;
        out->obj.push_back(std::make_pair(key, v));
        SkipWs(s, i);
        if (*i < s.size() && s[*i] == ',') { ++*i; continue; }
        if (*i < s.size() && s[*i] == '}') { ++*i; return true; }
        return Fail(err, "expected ',' or '}'", *i);
      }
    }
    if (c == '[') {
      out->type = ARR;
      ++*i;
      SkipWs(s, i);
      if (*i < s.size() && s[*i] == ']') { ++*i; return true; }
      for (;;) {
        Json v;
        if (!ParseValue(s, i, &v, err)) return false;
        out->arr.push_back(v);
        SkipWs(s, i);
        if (*i < s.size() && s[*i] == ',') { ++*i; continue; }
        if (*i < s.size() && s[*i] == ']') { ++*i; return true; }
        return Fail(err, "expected ',' or ']'", *i);
      }
    }
    if (c == '"') { out->type = STR; return ParseString(s, i, &out->str, err); }
    if (s.compare(*i, 4, "true") == 0)  { out->type = BOOL; out->boolean = true;  *i += 4; return true; }
    if (s.compare(*i, 5, "false") == 0) { out->type = BOOL; out->boolean = false; *i += 5; return true; }
    if (s.compare(*i, 4, "null") == 0)  { out->type = NUL; *i += 4; return true; }
    { char *end = NULL;
      const double d = strtod(s.c_str() + *i, &end);
      if (end && end != s.c_str() + *i) {
        out->type = NUM; out->num = d; *i = (size_t)(end - s.c_str());
        return true;
      } }
    return Fail(err, "unexpected character", *i);
  }
};

#endif  // LED_MATRIX_SERVER_MODULES_JSON_H
