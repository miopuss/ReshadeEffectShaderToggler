///////////////////////////////////////////////////////////////////////
//
// Localisation support for the REST user interface.
//
// The English text is the translation key: UI code passes its normal English string to
// Tr("...") and the Chinese build looks the string up in a table. Anything missing from
// the table stays English, so a partially translated build is still usable.
//
// The language is selected at compile time, so the English build neither carries Chinese
// text nor pays for a lookup:
//
//   msbuild /p:RestLanguage=zh      -> Chinese add-on
//   msbuild                         -> English add-on (default)
//
///////////////////////////////////////////////////////////////////////

#pragma once

#include <format>
#include <string>
#include <string_view>

#if defined(REST_LANGUAGE_ZH) && REST_LANGUAGE_ZH
#define REST_LOCALIZED_UI 1
#else
#define REST_LOCALIZED_UI 0
#endif

namespace RestLocalization {

// True when this build was compiled with the Chinese translation table.
inline constexpr bool Chinese = REST_LOCALIZED_UI != 0;

// Returns the translation of an English UI string, or the same pointer when the string has
// no translation (or this is the English build). Null stays null so optional arguments such
// as tooltips can be passed through unchanged.
const char* Translate(const char* english);

// Same lookup for views, used by the std::format based helpers below.
std::string_view TranslateView(std::string_view english);

} // namespace RestLocalization

// Short name used by the UI code, e.g. ImGui::TextUnformatted(Tr("Settings")).
inline const char* Tr(const char* english) { return RestLocalization::Translate(english); }

// Translates the visible part of an ImGui label such as "Clear##ClearKbGroup" and keeps the
// "##id" suffix, so the widget keeps its stable identifier.
std::string TrLabel(const char* label);

// Translates a std::format pattern and then formats it: TrFmt("Group settings ({})", name).
template <typename... Args>
std::string TrFmt(std::string_view englishFormat, Args&&... args) {
    return std::vformat(RestLocalization::TranslateView(englishFormat), std::make_format_args(args...));
}
