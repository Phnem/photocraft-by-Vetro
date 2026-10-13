// PhotoCraft by Vetro -- interface language (see i18n.h).
#include "i18n.h"
#include "bridge.h"
#include "core.h"
#include <unordered_map>
#include <windows.h>

namespace i18n {
namespace {
// What only this shell says, in Russian (same TSV rows as ru.tsv).
const char* const VetroRu =
#include "lang_ru.inc"
    ;

std::string code = "en";
std::unordered_map<std::wstring, std::wstring> plain;
std::unordered_map<std::string, std::wstring> byId;

std::string Unescape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char n = s[++i];
      out += n == 'n' ? '\n' : n == 't' ? '\t' : n;
    } else {
      out += s[i];
    }
  }
  return out;
}

// Rows of `context<TAB>source<TAB>translation`; plain strings and command ids (other contexts
// disambiguate the egui shell's own call sites and plural forms, which this shell doesn't use).
void Add(const std::string& tsv, bool overwrite) {
  size_t start = 0;
  while (start < tsv.size()) {
    size_t end = tsv.find('\n', start);
    if (end == std::string::npos) end = tsv.size();
    std::string line = tsv.substr(start, end - start);
    start = end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    size_t a = line.find('\t');
    if (a == std::string::npos) continue;
    size_t b = line.find('\t', a + 1);
    if (b == std::string::npos) continue;
    std::string ctx = line.substr(0, a), src = Unescape(line.substr(a + 1, b - a - 1)), tr = Unescape(line.substr(b + 1));
    if (tr.empty()) continue;
    if (ctx.empty()) {
      auto key = Widen(src);
      if (overwrite || !plain.count(key)) plain[key] = Widen(tr);
    } else if (ctx[0] == '@' && ctx != "@plural") {
      auto id = ctx.substr(1);
      if (overwrite || !byId.count(id)) byId[id] = Widen(tr);
    }
  }
}

constexpr const wchar_t* RegKey = L"Software\\PhotoCraft by Vetro";
}  // namespace

const std::vector<Language>& Languages() {
  static const std::vector<Language> all = {{"en", L"English"}, {"ru", L"Русский"}};
  return all;
}

void Load(const std::string& c) {
  plain.clear();
  byId.clear();
  code = "en";
  if (c != "ru") return;
  code = "ru";
  Add(TakeString(pc_ui_strings("ru")), true);
  Add(VetroRu, true);  // this shell's own wording wins
}

const std::string& Code() { return code; }

const std::wstring& Tr(const std::wstring& english) {
  if (plain.empty()) return english;
  auto it = plain.find(english);
  return it == plain.end() ? english : it->second;
}

std::wstring TrId(const std::string& id, const std::wstring& english) {
  if (code == "en") return english;
  auto it = byId.find(id);
  if (it != byId.end()) return it->second;
  return Tr(english);
}

std::string Saved() {
  wchar_t buf[16] = {};
  DWORD size = sizeof buf;
  if (RegGetValueW(HKEY_CURRENT_USER, RegKey, L"Language", RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS) {
    std::string c = Narrow(buf);
    for (auto& l : Languages())
      if (c == l.code) return c;
  }
  return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN ? "ru" : "en";
}

void Save(const std::string& c) {
  std::wstring w = Widen(c);
  RegSetKeyValueW(HKEY_CURRENT_USER, RegKey, L"Language", REG_SZ, w.c_str(), DWORD((w.size() + 1) * sizeof(wchar_t)));
}
}  // namespace i18n
