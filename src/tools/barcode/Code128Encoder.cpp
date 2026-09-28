#include "tools/barcode/Code128Encoder.h"

#include "core/Localization.h"

namespace barcode
{
namespace
{
const char* const kPatterns[] = {
    "212222","222122","222221","121223","121322","131222","122213","122312","132212","221213",
    "221312","231212","112232","122132","122231","113222","123122","123221","223211","221132",
    "221231","213212","223112","312131","311222","321122","321221","312212","322112","322211",
    "212123","212321","232121","111323","131123","131321","112313","132113","132311","211313",
    "231113","231311","112133","112331","132131","113123","113321","133121","313121","211331",
    "231131","213113","213311","213131","311123","311321","331121","312113","312311","332111",
    "314111","221411","431111","111224","111422","121124","121421","141122","141221","112214",
    "112412","122114","122411","142112","142211","241211","221114","413111","241112","134111",
    "111242","121142","121241","114212","124112","124211","411212","421112","421211","212141",
    "214121","412121","111143","111341","131141","114113","114311","411113","411311","113141",
    "114131","311141","411131","211412","211214","211232","2331112"
};
}

bool EncodeCode128B(const wxString& text, std::vector<int>& symbolValues,
                    std::vector<int>& barWidths, wxString& error)
{
    symbolValues.clear(); barWidths.clear();
    if (text.empty()) { error = ITOOL_TR("The barcode content cannot be empty."); return false; }
    if (text.length() > 200) { error = ITOOL_TR("Code 128-B content cannot exceed 200 characters."); return false; }
    symbolValues.push_back(104); // Start Code B
    int checksum = 104;
    for (size_t i = 0; i < text.length(); ++i)
    {
        const unsigned long character = static_cast<unsigned long>(text[i]);
        if (character < 32 || character > 126)
            { error = ITOOL_TR("Code 128-B only supports ASCII 32~126 characters."); return false; }
        const int value = static_cast<int>(character - 32);
        symbolValues.push_back(value); checksum += value * static_cast<int>(i + 1);
    }
    symbolValues.push_back(checksum % 103); symbolValues.push_back(106);
    for (size_t i = 0; i < symbolValues.size(); ++i)
    {
        const char* pattern = kPatterns[symbolValues[i]];
        for (const char* position = pattern; *position; ++position) barWidths.push_back(*position - '0');
    }
    return true;
}
}
