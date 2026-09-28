#include "tools/time/TimeConverter.h"

#include "core/Localization.h"

#include <cerrno>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace timeconv
{
namespace
{
bool Leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }

int DaysInMonth(int year, int month)
{
    static const int days[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    return month == 2 && Leap(year) ? 29 : days[month - 1];
}

int64_t DaysFromCivil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(dayOfEra) - 719468;
}

void CivilFromDays(int64_t days, int& year, unsigned& month, unsigned& day)
{
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned dayOfEra = static_cast<unsigned>(days - era * 146097);
    const unsigned yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    year = static_cast<int>(yearOfEra) + static_cast<int>(era) * 400;
    const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const unsigned monthPrime = (5 * dayOfYear + 2) / 153;
    day = dayOfYear - (153 * monthPrime + 2) / 5 + 1;
    month = monthPrime + (monthPrime < 10 ? 3 : -9);
    year += month <= 2;
}

bool ReadNumber(const wxString& text, size_t offset, size_t length, int& value)
{
    if (offset + length > text.length()) return false;
    unsigned long parsed = 0;
    if (!text.Mid(offset, length).ToULong(&parsed, 10)) return false;
    value = static_cast<int>(parsed); return true;
}

int64_t FloorDiv(int64_t value, int64_t divisor)
{
    int64_t quotient = value / divisor, remainder = value % divisor;
    if (remainder < 0) --quotient;
    return quotient;
}
}

bool ParseUtc(const wxString& source, int64_t& unixMilliseconds, wxString& error)
{
    wxString text(source); text.Trim(true).Trim(false);
    if (text.EndsWith(wxS("Z")) || text.EndsWith(wxS("z"))) text.RemoveLast();
    if (text.length() < 19 || text[4] != '-' || text[7] != '-' ||
        (text[10] != ' ' && text[10] != 'T') || text[13] != ':' || text[16] != ':')
        { error = ITOOL_TR("The UTC time format should be YYYY-MM-DD HH:MM:SS[.mmm]."); return false; }
    int year, month, day, hour, minute, second;
    if (!ReadNumber(text,0,4,year) || !ReadNumber(text,5,2,month) || !ReadNumber(text,8,2,day) ||
        !ReadNumber(text,11,2,hour) || !ReadNumber(text,14,2,minute) || !ReadNumber(text,17,2,second))
        { error = ITOOL_TR("The UTC time contains an invalid number."); return false; }
    if (year < 1 || year > 9999 || month < 1 || month > 12 || day < 1 || day > DaysInMonth(year, month) ||
        hour > 23 || minute > 59 || second > 59)
        { error = ITOOL_TR("The UTC date or time is outside the valid range."); return false; }
    int milliseconds = 0;
    if (text.length() > 19)
    {
        if (text[19] != '.' || text.length() > 23 || text.length() == 20) { error = ITOOL_TR("Fractional seconds contain up to three digits."); return false; }
        wxString fraction = text.Mid(20);
        while (fraction.length() < 3) fraction += '0';
        if (!ReadNumber(fraction, 0, 3, milliseconds)) { error = ITOOL_TR("Invalid millisecond format."); return false; }
    }
    const int64_t seconds = DaysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 +
                            hour * 3600 + minute * 60 + second;
    unixMilliseconds = seconds * 1000 + milliseconds; return true;
}

wxString FormatUtc(int64_t unixMilliseconds, bool includeMilliseconds)
{
    const int64_t seconds = FloorDiv(unixMilliseconds, 1000);
    const int milliseconds = static_cast<int>(unixMilliseconds - seconds * 1000);
    const int64_t days = FloorDiv(seconds, 86400);
    const int64_t secondsOfDay = seconds - days * 86400;
    int year; unsigned month, day; CivilFromDays(days, year, month, day);
    const int hour = static_cast<int>(secondsOfDay / 3600);
    const int minute = static_cast<int>((secondsOfDay % 3600) / 60);
    const int second = static_cast<int>(secondsOfDay % 60);
    std::ostringstream stream;
    stream << std::setfill('0') << std::setw(4) << year << '-' << std::setw(2) << month << '-' << std::setw(2) << day
           << 'T' << std::setw(2) << hour << ':' << std::setw(2) << minute << ':' << std::setw(2) << second;
    if (includeMilliseconds) stream << '.' << std::setw(3) << milliseconds;
    stream << 'Z'; return wxString::FromUTF8(stream.str().c_str());
}

bool ParseTimestamp(const wxString& source, bool milliseconds, int64_t& unixMilliseconds, wxString& error)
{
    wxString text(source); text.Trim(true).Trim(false);
    if (text.empty()) { error = ITOOL_TR("Unix timestamp cannot be empty."); return false; }
    wxCharBuffer ascii = text.utf8_str(); errno = 0; char* end = NULL;
    const long long value = std::strtoll(ascii.data(), &end, 10);
    if (errno == ERANGE || end == ascii.data() || *end != '\0') { error = ITOOL_TR("Unix timestamp is not a valid 64-bit integer."); return false; }
    if (milliseconds) unixMilliseconds = static_cast<int64_t>(value);
    else
    {
        if (value > INT64_MAX / 1000 || value < INT64_MIN / 1000) { error = ITOOL_TR("Unix seconds are outside the convertible range."); return false; }
        unixMilliseconds = static_cast<int64_t>(value) * 1000;
    }
    return true;
}

wxString FormatTimestamp(int64_t unixMilliseconds, bool milliseconds)
{
    const int64_t value = milliseconds ? unixMilliseconds : FloorDiv(unixMilliseconds, 1000);
    std::ostringstream stream; stream << value; return wxString::FromUTF8(stream.str().c_str());
}
}
