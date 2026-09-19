#include "EpgRecordingMatch.h"
#include "EpgTagUtil.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>

namespace dispatcharr
{
namespace
{

bool OverlapsProgramme(const Recording& recording, time_t start, time_t end)
{
  if (recording.startTime <= 0 || recording.endTime <= recording.startTime || start <= 0 || end <= start)
    return false;
  const int64_t overlap = static_cast<int64_t>(std::min(recording.endTime, end)) -
                          static_cast<int64_t>(std::max(recording.startTime, start));
  const int64_t shorter =
      std::min(static_cast<int64_t>(recording.endTime) - recording.startTime, static_cast<int64_t>(end) - start);
  return overlap > 0 && overlap * 2 >= shorter;
}

std::string NormalizeTitle(const std::string& title)
{
  std::string normalized;
  normalized.reserve(title.size());
  bool pendingSpace = false;
  for (const unsigned char character : title)
  {
    if (std::isalnum(character))
    {
      if (pendingSpace && !normalized.empty())
        normalized.push_back(' ');
      normalized.push_back(static_cast<char>(std::tolower(character)));
      pendingSpace = false;
    }
    else
    {
      pendingSpace = !normalized.empty();
    }
  }
  return normalized;
}

} // namespace

const EpgEntry* MatchRecordingToEpg(const std::vector<EpgEntry>& entries, time_t recordingStart, time_t recordingEnd,
                                    const std::string& recordingTitle)
{
  if (recordingStart <= 0 || recordingEnd <= recordingStart)
    return nullptr;

  const std::string normalizedRecordingTitle = NormalizeTitle(recordingTitle);
  const EpgEntry* best = nullptr;
  int64_t bestScore = std::numeric_limits<int64_t>::min();
  bool ambiguous = false;

  for (const auto& entry : entries)
  {
    if (entry.endTime <= entry.startTime)
      continue;

    const time_t overlapStart = std::max(recordingStart, entry.startTime);
    const time_t overlapEnd = std::min(recordingEnd, entry.endTime);
    const int64_t overlap = static_cast<int64_t>(overlapEnd - overlapStart);
    if (overlap <= 0)
      continue;

    const int64_t recordingDuration = static_cast<int64_t>(recordingEnd - recordingStart);
    const int64_t entryDuration = static_cast<int64_t>(entry.endTime - entry.startTime);
    const int64_t shorterDuration = std::min(recordingDuration, entryDuration);
    const bool titleMatches =
        !normalizedRecordingTitle.empty() && normalizedRecordingTitle == NormalizeTitle(entry.title);
    const bool substantialOverlap = overlap * 2 >= shorterDuration;
    const bool closeBoundaries = std::llabs(static_cast<long long>(recordingStart - entry.startTime)) <= 120 &&
                                 std::llabs(static_cast<long long>(recordingEnd - entry.endTime)) <= 120;
    const bool acceptable = normalizedRecordingTitle.empty() ? closeBoundaries : titleMatches && substantialOverlap;
    if (!acceptable)
      continue;

    // The large title bonus makes a title match decisive; overlap then picks
    // the right occurrence when adjacent/repeated programmes share a title.
    const int64_t score = overlap + (titleMatches ? 1000000000LL : 0LL);
    if (score > bestScore)
    {
      bestScore = score;
      best = &entry;
      ambiguous = false;
    }
    else if (score == bestScore && best && entry.startTime != best->startTime)
      ambiguous = true;
  }

  return ambiguous ? nullptr : best;
}

uint32_t RecordingEpgUid(const Recording& recording, const EpgEntry* matchedEntry)
{
  if (recording.channelId <= 0)
    return 0;
  const auto& link = recording.epgLink;
  if (link.channelId == recording.channelId && link.broadcastId != 0 &&
      OverlapsProgramme(recording, link.startTime, link.endTime))
    return link.broadcastId;
  if (OverlapsProgramme(recording, recording.programStartTime, recording.programEndTime))
    return ComputeBroadcastId(recording.channelId, recording.programStartTime);
  return matchedEntry ? ComputeBroadcastId(recording.channelId, matchedEntry->startTime) : 0;
}

const EpgEntry* MatchRecordingToEpg(const std::vector<EpgEntry>& entries, const Recording& recording)
{
  const uint32_t savedUid = RecordingEpgUid(recording);
  if (savedUid != 0)
  {
    for (const auto& entry : entries)
      if (ComputeBroadcastId(recording.channelId, entry.startTime) == savedUid)
        return &entry;
    return nullptr;
  }
  return MatchRecordingToEpg(entries, recording.startTime, recording.endTime,
                             recording.titleIsPlaceholder ? "" : recording.title);
}

} // namespace dispatcharr
