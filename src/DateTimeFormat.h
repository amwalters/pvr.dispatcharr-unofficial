#pragma once

#include <ctime>
#include <string>

namespace dispatcharr
{

// "YYYY-MM-DDTHH:MM:SSZ" for a time_t, matching Dispatcharr's own
// UTC-normalized date-time fields.
std::string IsoFromTime(time_t t);

// Parses an ISO date-time, honoring Z or a numeric UTC offset (with or
// without a colon). Fractional seconds are discarded; absent zones mean
// UTC. Programme metadata can use a different offset from recording fields.
// Returns 0 on invalid input.
time_t TimeFromIso(const std::string& isoStr);

// "HH:MM:SS" for a plain seconds-since-midnight value, wrapping into
// [0, 86400) first -- callers may have shifted a UTC time-of-day by
// recurring_rule_utc_offset_minutes, which can push it negative or past
// 24h before this is called.
std::string TimeOfDayString(int secondsSinceMidnight);

// Inverse of TimeOfDayString(): parses "HH:MM:SS" (or "HH:MM") into
// seconds since midnight. Returns 0 on anything unparseable.
int SecondsSinceMidnightFromString(const std::string& hms);

// "YYYY-MM-DD" for the UTC calendar date of a time_t (this addon only
// ever stores a rule's start_date/end_date as UTC midnight of the
// intended local calendar date -- see RecurringRule's own comment).
std::string DateStringFromTime(time_t t);

// Inverse of DateStringFromTime(): parses "YYYY-MM-DD" into a UTC
// midnight time_t. Returns 0 on anything unparseable.
time_t TimeFromDateString(const std::string& dateStr);

} // namespace dispatcharr
