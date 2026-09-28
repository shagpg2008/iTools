#include "core/Localization.h"

#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/intl.h>
#include <wx/stdpaths.h>

#include "core/AppPaths.h"

namespace localization
{
namespace
{
const wxChar* kLanguageConfigKey = wxS("/application/language");
wxLocale* g_activeLocale = NULL;

int WxLanguage(const wxString& language)
{
    const std::vector<LanguageInfo>& languages = SupportedLanguages();
    for (size_t i = 0; i < languages.size(); ++i)
        if (languages[i].code == language) return languages[i].wxLanguage;
    return wxLANGUAGE_ENGLISH;
}

wxString CatalogDirectory()
{
    const wxString resources = wxStandardPaths::Get().GetResourcesDir();
    const wxString resourceLocale = wxFileName(resources, wxS("locale")).GetFullPath();
    if (wxFileName::DirExists(resourceLocale)) return resourceLocale;
    return wxFileName(
        wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath(),
        wxS("locale")).GetFullPath();
}
}

const std::vector<LanguageInfo>& SupportedLanguages()
{
    // Single language manifest. The UI enumerates this list instead of
    // maintaining another hard-coded set of menu choices.
    static const std::vector<LanguageInfo> languages = {
        LanguageInfo(wxS("en"), wxS("English"), wxLANGUAGE_ENGLISH),
        LanguageInfo(wxS("zh_CN"), wxS("简体中文"), wxLANGUAGE_CHINESE_SIMPLIFIED),
        LanguageInfo(wxS("zh_TW"), wxS("繁體中文"), wxLANGUAGE_CHINESE_TRADITIONAL),
        LanguageInfo(wxS("bg"), wxS("Български"), wxLANGUAGE_BULGARIAN),
        LanguageInfo(wxS("ja"), wxS("日本語"), wxLANGUAGE_JAPANESE),
        LanguageInfo(wxS("cs"), wxS("Čeština"), wxLANGUAGE_CZECH),
        LanguageInfo(wxS("es"), wxS("Español"), wxLANGUAGE_SPANISH),
        LanguageInfo(wxS("et"), wxS("Eesti"), wxLANGUAGE_ESTONIAN),
        LanguageInfo(wxS("hr"), wxS("Hrvatski"), wxLANGUAGE_CROATIAN),
        LanguageInfo(wxS("it"), wxS("Italiano"), wxLANGUAGE_ITALIAN),
        LanguageInfo(wxS("pl"), wxS("Polski"), wxLANGUAGE_POLISH),
        LanguageInfo(wxS("pt_BR"), wxS("Português (Brasil)"), wxLANGUAGE_PORTUGUESE_BRAZILIAN),
        LanguageInfo(wxS("ru"), wxS("Русский"), wxLANGUAGE_RUSSIAN),
        LanguageInfo(wxS("sl"), wxS("Slovenščina"), wxLANGUAGE_SLOVENIAN),
        LanguageInfo(wxS("tr"), wxS("Türkçe"), wxLANGUAGE_TURKISH),
        LanguageInfo(wxS("vi"), wxS("Tiếng Việt"), wxLANGUAGE_VIETNAMESE),
        LanguageInfo(wxS("ko"), wxS("한국어"), wxLANGUAGE_KOREAN)
    };
    return languages;
}

wxString NormalizeLocale(const wxString& input)
{
    wxString locale = input;
    locale.Replace(wxS("_"), wxS("-"));
    const int dot = locale.Find(wxS('.'));
    if (dot != wxNOT_FOUND) locale = locale.Left(dot);
    const int at = locale.Find(wxS('@'));
    if (at != wxNOT_FOUND) locale = locale.Left(at);
    locale.MakeLower();
    if (locale == wxS("zh") || locale.StartsWith(wxS("zh-cn")) ||
        locale.StartsWith(wxS("zh-sg")) || locale.StartsWith(wxS("zh-hans")))
        return wxS("zh_CN");
    if (locale.StartsWith(wxS("zh-tw")) || locale.StartsWith(wxS("zh-hk")) ||
        locale.StartsWith(wxS("zh-mo")) || locale.StartsWith(wxS("zh-hant")))
        return wxS("zh_TW");
    if (locale == wxS("pt-br") || locale.StartsWith(wxS("pt-br-")))
        return wxS("pt_BR");
    const int dash = locale.Find(wxS('-'));
    return dash == wxNOT_FOUND ? locale : locale.Left(dash);
}

wxString EffectiveLanguage(const wxString& language)
{
    wxString effective = language;
    if (effective == wxS("system"))
        effective = wxLocale::GetLanguageCanonicalName(wxLocale::GetSystemLanguage());
    effective = NormalizeLocale(effective);
    const std::vector<LanguageInfo>& languages = SupportedLanguages();
    for (size_t i = 0; i < languages.size(); ++i)
        if (languages[i].code == effective) return effective;
    return wxS("en");
}

wxLocale* Initialize(const wxString& language)
{
    delete g_activeLocale;
    g_activeLocale = NULL;
    const wxString effective = EffectiveLanguage(language);
    wxLocale* locale = new wxLocale;
    if (!locale->Init(WxLanguage(effective), wxLOCALE_DONT_LOAD_DEFAULT))
    {
        delete locale;
        locale = new wxLocale(wxLANGUAGE_ENGLISH, wxLOCALE_DONT_LOAD_DEFAULT);
    }
    wxLocale::AddCatalogLookupPathPrefix(CatalogDirectory());
    locale->AddCatalog(wxS("iTool"));
    g_activeLocale = locale;
    return g_activeLocale;
}

void Shutdown()
{
    delete g_activeLocale;
    g_activeLocale = NULL;
}

wxString LoadConfiguredLanguage()
{
    const wxString path = apppaths::ConfigurationFile();
    if (!wxFileName::FileExists(path)) return wxS("system");
    wxFileConfig config(wxEmptyString, wxEmptyString, path, wxEmptyString,
                        wxCONFIG_USE_LOCAL_FILE);
    wxString code;
    if (!config.Read(kLanguageConfigKey, &code))
        return wxS("system");
    if (code == wxS("system")) return code;
    code = NormalizeLocale(code);
    const std::vector<LanguageInfo>& languages = SupportedLanguages();
    for (size_t i = 0; i < languages.size(); ++i)
        if (languages[i].code == code) return code;
    return wxS("system");
}

bool SaveConfiguredLanguage(const wxString& language)
{
    if (!apppaths::EnsureUserDataDirectory()) return false;
    const wxString path = apppaths::ConfigurationFile();
    wxFileConfig config(wxEmptyString, wxEmptyString, path, wxEmptyString,
                        wxCONFIG_USE_LOCAL_FILE);
    config.Write(kLanguageConfigKey, language);
    return config.Flush();
}

wxString Translate(const char* message)
{
    return wxGetTranslation(wxString::FromUTF8(message), wxS("iTool"));
}
}
