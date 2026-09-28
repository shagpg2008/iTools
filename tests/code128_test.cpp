#include <iostream>
#include <vector>
#include "tools/barcode/Code128Encoder.h"

int main()
{
    std::vector<int> symbols, widths; wxString error;
    bool ok = barcode::EncodeCode128B(wxS("123"), symbols, widths, error);
    ok = ok && symbols.size() == 6 && symbols[0] == 104 && symbols[1] == 17 && symbols[2] == 18 && symbols[3] == 19;
    ok = ok && symbols[4] == 8 && symbols[5] == 106 && widths.size() == 37;
    ok = ok && !barcode::EncodeCode128B(wxString::FromUTF8("中文"), symbols, widths, error);
    if (!ok) std::cerr << "Code 128 test failed" << std::endl;
    return ok ? 0 : 1;
}
