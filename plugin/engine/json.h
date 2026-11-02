// Minimal JSON reader for engine.json (config only, never on the audio thread).
#pragma once
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gl {

struct Json {
  enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
  double num = 0;
  bool b = false;
  std::string str;
  std::vector<Json> arr;
  std::map<std::string, Json> obj;

  bool has(const std::string& k) const { return type == Obj && obj.count(k); }
  const Json& operator[](const std::string& k) const { static Json none; auto it = obj.find(k); return it == obj.end() ? none : it->second; }
  const Json& operator[](size_t i) const { static Json none; return i < arr.size() ? arr[i] : none; }
  size_t size() const { return type == Arr ? arr.size() : type == Obj ? obj.size() : 0; }
  double d(double def = 0) const { return type == Num ? num : type == Bool ? (b ? 1 : 0) : def; }
  bool isNum() const { return type == Num; }

  static bool parse(const char* s, Json& out) { const char* p = s; ws(p); if (!val(p, out)) return false; ws(p); return *p == 0; }

 private:
  static void ws(const char*& p) { while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') ++p; }
  static bool lit(const char*& p, const char* w) { size_t n = strlen(w); if (strncmp(p, w, n)) return false; p += n; return true; }
  static bool strv(const char*& p, std::string& o) {
    if (*p != '"') return false; ++p;
    while (*p && *p != '"') {
      if (*p == '\\') {
        ++p;
        switch (*p) {
          case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': o += '\r'; break;
          case 'b': o += '\b'; break; case 'f': o += '\f'; break;
          case 'u': {  // BMP code point -> UTF-8
            unsigned cp = (unsigned)strtoul(std::string(p + 1, 4).c_str(), nullptr, 16); p += 4;
            if (cp < 0x80) o += (char)cp;
            else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 63)); }
            else { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
          } break;
          default: o += *p;
        }
        ++p;
      } else o += *p++;
    }
    if (*p != '"') return false; ++p; return true;
  }
  static bool val(const char*& p, Json& o) {
    ws(p);
    if (*p == '{') {
      o.type = Obj; ++p; ws(p);
      if (*p == '}') { ++p; return true; }
      for (;;) {
        std::string k; ws(p); if (!strv(p, k)) return false; ws(p); if (*p != ':') return false; ++p;
        if (!val(p, o.obj[k])) return false; ws(p);
        if (*p == ',') { ++p; continue; } if (*p == '}') { ++p; return true; } return false;
      }
    }
    if (*p == '[') {
      o.type = Arr; ++p; ws(p);
      if (*p == ']') { ++p; return true; }
      for (;;) {
        o.arr.emplace_back(); if (!val(p, o.arr.back())) return false; ws(p);
        if (*p == ',') { ++p; continue; } if (*p == ']') { ++p; return true; } return false;
      }
    }
    if (*p == '"') { o.type = Str; return strv(p, o.str); }
    if (lit(p, "true")) { o.type = Bool; o.b = true; return true; }
    if (lit(p, "false")) { o.type = Bool; o.b = false; return true; }
    if (lit(p, "null")) { o.type = Null; return true; }
    char* e; o.num = strtod(p, &e); if (e == p) return false; p = e; o.type = Num; return true;
  }
};

}  // namespace gl
