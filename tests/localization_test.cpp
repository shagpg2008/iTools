#include <cassert>

#include "core/Localization.h"

int main()
{
    assert(localization::NormalizeLocale(wxS("zh_CN.UTF-8")) == wxS("zh_CN"));
    assert(localization::NormalizeLocale(wxS("zh-SG")) == wxS("zh_CN"));
    assert(localization::NormalizeLocale(wxS("zh-Hans")) == wxS("zh_CN"));
    assert(localization::NormalizeLocale(wxS("zh_TW.Big5")) == wxS("zh_TW"));
    assert(localization::NormalizeLocale(wxS("zh-HK")) == wxS("zh_TW"));
    assert(localization::NormalizeLocale(wxS("zh-MO")) == wxS("zh_TW"));
    assert(localization::NormalizeLocale(wxS("zh-Hant")) == wxS("zh_TW"));
    assert(localization::NormalizeLocale(wxS("pt-BR.UTF-8")) == wxS("pt_BR"));
    assert(localization::NormalizeLocale(wxS("ja_JP.UTF-8")) == wxS("ja"));
    assert(localization::EffectiveLanguage(wxS("ko_KR")) == wxS("ko"));
    assert(localization::NormalizeLocale(wxS("en-US")) == wxS("en"));
    assert(localization::NormalizeLocale(wxS("fr_FR@euro")) == wxS("fr"));
    return 0;
}
