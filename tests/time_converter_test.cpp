#include <iostream>
#include "tools/time/TimeConverter.h"

int main()
{
    bool ok = true; wxString error; int64_t value = 0;
    ok = ok && timeconv::ParseUtc(wxS("1970-01-01 00:00:00"), value, error) && value == 0;
    ok = ok && timeconv::FormatUtc(-1, true) == wxS("1969-12-31T23:59:59.999Z");
    ok = ok && timeconv::ParseUtc(wxS("2000-02-29T12:34:56.789Z"), value, error);
    ok = ok && timeconv::FormatUtc(value, true) == wxS("2000-02-29T12:34:56.789Z");
    ok = ok && !timeconv::ParseUtc(wxS("2100-02-29 00:00:00"), value, error);
    ok = ok && timeconv::ParseUtc(wxS("9999-12-31 23:59:59"), value, error);
    ok = ok && timeconv::FormatUtc(value, false) == wxS("9999-12-31T23:59:59Z");
    if (!ok) std::cerr << "time converter test failed" << std::endl;
    return ok ? 0 : 1;
}
