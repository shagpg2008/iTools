#pragma once

#include <vector>
#include <wx/string.h>

class wxLocale;

namespace localization
{
struct LanguageInfo
{
    LanguageInfo(const wxString& languageCode, const wxString& nativeName, int wxLanguageId)
        : code(languageCode), displayName(nativeName), wxLanguage(wxLanguageId) {}
    wxString code;
    wxString displayName;
    int wxLanguage;
};

wxLocale* Initialize(const wxString& language);
void Shutdown();
wxString LoadConfiguredLanguage();
bool SaveConfiguredLanguage(const wxString& language);
wxString NormalizeLocale(const wxString& locale);
wxString EffectiveLanguage(const wxString& language);
const std::vector<LanguageInfo>& SupportedLanguages();
wxString Translate(const char* message);
}

#define ITOOL_TR(message) localization::Translate(message)
