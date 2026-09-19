#include "DateTimeFormat.h"

#include "TimeUtil.h"

#include <cstdio>
#include <cctype>
#include <stdexcept>

namespace dispatcharr
{

std::string IsoFromTime(time_t t)
{
  char buf[32];
  tm tmVal{};
  GmTimeUtc(t, &tmVal);
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmVal);
  return std::string(buf);
}

time_t TimeFromIso(const std::string& isoStr)
{
  if (isoStr.size() < 19 || isoStr[4] != '-' || isoStr[7] != '-' || (isoStr[10] != 'T' && isoStr[10] != ' ') ||
      isoStr[13] != ':' || isoStr[16] != ':')
    return 0;

  for (size_t i = 0; i < 19; ++i)
    if (i != 4 && i != 7 && i != 10 && i != 13 && i != 16 && !std::isdigit(static_cast<unsigned char>(isoStr[i])))
      return 0;

  tm tmVal{};
  try
  {
    tmVal.tm_year = std::stoi(isoStr.substr(0, 4)) - 1900;
    tmVal.tm_mon = std::stoi(isoStr.substr(5, 2)) - 1;
    tmVal.tm_mday = std::stoi(isoStr.substr(8, 2));
    tmVal.tm_hour = std::stoi(isoStr.substr(11, 2));
    tmVal.tm_min = std::stoi(isoStr.substr(14, 2));
    tmVal.tm_sec = std::stoi(isoStr.substr(17, 2));
  }
  catch (const std::exception&)
  {
    return 0;
  }
  const tm expected = tmVal;
  const time_t utc = PortableTimeGm(&tmVal);
  if (tmVal.tm_year != expected.tm_year || tmVal.tm_mon != expected.tm_mon || tmVal.tm_mday != expected.tm_mday ||
      tmVal.tm_hour != expected.tm_hour || tmVal.tm_min != expected.tm_min || tmVal.tm_sec != expected.tm_sec)
    return 0;

  size_t pos = 19;
  if (pos < isoStr.size() && isoStr[pos] == '.')
  {
    const size_t firstDigit = ++pos;
    while (pos < isoStr.size() && std::isdigit(static_cast<unsigned char>(isoStr[pos])))
      ++pos;
    if (pos == firstDigit)
      return 0;
  }
  if (pos == isoStr.size() || isoStr.substr(pos) == "Z")
    return utc;
  const char sign = isoStr[pos++];
  if (sign != '+' && sign != '-')
    return 0;
  std::string offset = isoStr.substr(pos);
  if (offset.size() == 5 && offset[2] == ':')
    offset.erase(2, 1);
  if (offset.size() != 4)
    return 0;
  for (unsigned char c : offset)
    if (!std::isdigit(c))
      return 0;
  const int hours = std::stoi(offset.substr(0, 2));
  const int minutes = std::stoi(offset.substr(2, 2));
  if (hours > 23 || minutes > 59)
    return 0;
  return utc - (sign == '+' ? 1 : -1) * (hours * 3600 + minutes * 60);
}

std::string TimeOfDayString(int secondsSinceMidnight)
{
  int s = secondsSinceMidnight % 86400;
  if (s < 0)
    s += 86400;
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
  return std::string(buf);
}

int SecondsSinceMidnightFromString(const std::string& hms)
{
  int h = 0, m = 0, s = 0;
  if (std::sscanf(hms.c_str(), "%d:%d:%d", &h, &m, &s) < 2)
    return 0;
  return h * 3600 + m * 60 + s;
}

std::string DateStringFromTime(time_t t)
{
  char buf[16];
  tm tmVal{};
  GmTimeUtc(t, &tmVal);
  std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tmVal);
  return std::string(buf);
}

time_t TimeFromDateString(const std::string& dateStr)
{
  if (dateStr.size() < 10)
    return 0;
  tm tmVal{};
  try
  {
    tmVal.tm_year = std::stoi(dateStr.substr(0, 4)) - 1900;
    tmVal.tm_mon = std::stoi(dateStr.substr(5, 2)) - 1;
    tmVal.tm_mday = std::stoi(dateStr.substr(8, 2));
  }
  catch (const std::exception&)
  {
    return 0;
  }
  return PortableTimeGm(&tmVal);
}

} // namespace dispatcharr
