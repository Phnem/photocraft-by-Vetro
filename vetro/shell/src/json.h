#pragma once
// A small JSON reader for what the engine answers (document views, the menu catalog, command
// results) and a writer for the params the shell sends. Malformed input yields null values, never
// an exception: the engine's answers are trusted, but a shell must not die on a surprise.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

struct Json {
  enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<Json> a;
  std::vector<std::pair<std::string, Json>> o;

  static const Json& None() {
    static const Json none;
    return none;
  }
  bool IsNull() const { return kind == Null; }
  const Json& operator[](const char* key) const {
    if (kind == Object)
      for (auto& kv : o)
        if (kv.first == key) return kv.second;
    return None();
  }
  const Json& operator[](size_t i) const { return kind == Array && i < a.size() ? a[i] : None(); }
  // A literal 0 would otherwise be as good a null `const char*` as an index.
  const Json& operator[](int i) const { return i < 0 ? None() : (*this)[size_t(i)]; }
  size_t size() const { return kind == Array ? a.size() : kind == Object ? o.size() : 0; }
  double Num(double fallback = 0) const { return kind == Number ? n : kind == Bool ? (b ? 1 : 0) : fallback; }
  int64_t Int(int64_t fallback = 0) const { return kind == Number ? int64_t(std::llround(n)) : fallback; }
  bool Truthy(bool fallback = false) const { return kind == Bool ? b : kind == Number ? n != 0 : fallback; }
  const std::string& Str() const {
    static const std::string empty;
    return kind == String ? s : empty;
  }

  static Json Parse(const std::string& text) {
    size_t i = 0;
    return ParseValue(text, i, 0);
  }

 private:
  static void Skip(const std::string& t, size_t& i) {
    while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\r' || t[i] == '\t')) i++;
  }
  static void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
  }
  static std::string ParseString(const std::string& t, size_t& i) {
    std::string out;
    if (i >= t.size() || t[i] != '"') return out;
    i++;
    while (i < t.size() && t[i] != '"') {
      char c = t[i++];
      if (c != '\\') { out += c; continue; }
      if (i >= t.size()) break;
      char e = t[i++];
      switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': {
          if (i + 4 > t.size()) return out;
          uint32_t cp = uint32_t(strtoul(t.substr(i, 4).c_str(), nullptr, 16));
          i += 4;
          if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= t.size() && t[i] == '\\' && t[i + 1] == 'u') {
            uint32_t lo = uint32_t(strtoul(t.substr(i + 2, 4).c_str(), nullptr, 16));
            i += 6;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          AppendUtf8(out, cp);
          break;
        }
        default: out += e;
      }
    }
    if (i < t.size()) i++;
    return out;
  }
  static Json ParseValue(const std::string& t, size_t& i, int depth) {
    Json v;
    Skip(t, i);
    if (i >= t.size() || depth > 256) return v;
    char c = t[i];
    if (c == '{') {
      v.kind = Object;
      i++;
      Skip(t, i);
      if (i < t.size() && t[i] == '}') { i++; return v; }
      while (i < t.size()) {
        Skip(t, i);
        std::string key = ParseString(t, i);
        Skip(t, i);
        if (i < t.size() && t[i] == ':') i++;
        v.o.emplace_back(std::move(key), ParseValue(t, i, depth + 1));
        Skip(t, i);
        if (i < t.size() && t[i] == ',') { i++; continue; }
        if (i < t.size() && t[i] == '}') i++;
        break;
      }
    } else if (c == '[') {
      v.kind = Array;
      i++;
      Skip(t, i);
      if (i < t.size() && t[i] == ']') { i++; return v; }
      while (i < t.size()) {
        v.a.push_back(ParseValue(t, i, depth + 1));
        Skip(t, i);
        if (i < t.size() && t[i] == ',') { i++; continue; }
        if (i < t.size() && t[i] == ']') i++;
        break;
      }
    } else if (c == '"') {
      v.kind = String;
      v.s = ParseString(t, i);
    } else if (t.compare(i, 4, "true") == 0) { v.kind = Bool; v.b = true; i += 4; }
    else if (t.compare(i, 5, "false") == 0) { v.kind = Bool; i += 5; }
    else if (t.compare(i, 4, "null") == 0) { i += 4; }
    else {
      char* end = nullptr;
      v.n = strtod(t.c_str() + i, &end);
      if (end == t.c_str() + i) { i++; return Json(); }
      v.kind = Number;
      i = size_t(end - t.c_str());
    }
    return v;
  }
};

// Writing params: small helpers rather than a document model, because the shell only ever sends
// flat objects of numbers, strings and booleans.
inline std::string JsonQuote(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) { char buf[8]; snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
        else out += char(c);
    }
  }
  return out + "\"";
}
inline std::string JsonNum(double v) {
  if (!std::isfinite(v)) return "0";
  char buf[48];
  if (std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e15) snprintf(buf, sizeof buf, "%lld", (long long)std::llround(v));
  else snprintf(buf, sizeof buf, "%.6g", v);
  return buf;
}
inline std::string JsonText(const Json& v) {
  if (v.kind == Json::String) return JsonQuote(v.s);
  if (v.kind == Json::Number) return JsonNum(v.n);
  if (v.kind == Json::Bool) return v.b ? "true" : "false";
  if (v.kind != Json::Array && v.kind != Json::Object) return "null";
  std::string out = v.kind == Json::Array ? "[" : "{";
  if (v.kind == Json::Array) {
    for (auto& item : v.a) out += (out.size() > 1 ? "," : "") + JsonText(item);
  } else {
    for (auto& item : v.o) out += (out.size() > 1 ? "," : "") + JsonQuote(item.first) + ":" + JsonText(item.second);
  }
  return out + (v.kind == Json::Array ? "]" : "}");
}

struct JsonObject {
  std::string body;
  JsonObject& Add(const char* key, const std::string& raw) {
    if (!body.empty()) body += ',';
    body += JsonQuote(key) + ':' + raw;
    return *this;
  }
  JsonObject& Num(const char* key, double v) { return Add(key, JsonNum(v)); }
  JsonObject& Str(const char* key, const std::string& v) { return Add(key, JsonQuote(v)); }
  JsonObject& Bool(const char* key, bool v) { return Add(key, v ? "true" : "false"); }
  std::string Text() const { return "{" + body + "}"; }
};
