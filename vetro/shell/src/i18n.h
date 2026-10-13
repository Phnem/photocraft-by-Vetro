#pragma once
// Interface language. Strings in the code stay English and are the lookup keys; a language's
// catalog maps them to what is shown. Russian comes from the egui shell's own catalog (ru.tsv,
// through the bridge) plus the strings only this shell has (lang_ru.inc).
//
// Drawing does the lookup: gfx's Text() and Measure() show a translated string whenever the whole
// string has a translation, so labels, buttons, tooltips and toasts follow the language without
// each call site knowing. Strings built from pieces (formats, names) translate their pieces with
// Tr(); menu items translate by command id with TrId().
#include <string>
#include <vector>

namespace i18n {
struct Language {
  const char* code;     // "en", "ru"
  const wchar_t* name;  // in itself: "English", "Русский"
};
const std::vector<Language>& Languages();

void Load(const std::string& code);  // "en": the source strings
const std::string& Code();

// The translation of a whole string, or the string itself.
const std::wstring& Tr(const std::wstring& english);
// A menu item: its command id's translation first (one English label can read differently per
// command), then the label's.
std::wstring TrId(const std::string& id, const std::wstring& english);

// The language to start in: the saved choice, else the system's UI language if we have it.
std::string Saved();
void Save(const std::string& code);
}  // namespace i18n
