#pragma once

#include "XmlTvParser.h"
#include "DispatcharrClient.h"

#include <ctime>
#include <string>
#include <vector>

namespace dispatcharr
{

// Best-effort association between a Dispatcharr Recording's scheduled/actual
// time window and one programme from the already channel-scoped XMLTV EPG.
//
// Recording start/end times cannot be compared for exact equality: global DVR
// pre/post padding expands scheduled recordings, and starting a recording of an
// already-airing programme can clamp its stored start time to "now". Prefer an
// exact normalized-title match plus substantial temporal overlap; when no title
// is available, only accept near-identical boundaries so a broad manual timer
// cannot accidentally claim an arbitrary programme.
//
// Returns a pointer into `entries`, or nullptr if no sufficiently reliable match
// exists. The caller must not retain the pointer beyond the lifetime of entries.
const EpgEntry* MatchRecordingToEpg(const std::vector<EpgEntry>& entries, time_t recordingStart, time_t recordingEnd,
                                    const std::string& recordingTitle);

// Resolves saved identities even when the guide no longer contains the event.
// Rejects stale links after a recording moves to another channel/time slot.
uint32_t RecordingEpgUid(const Recording& recording, const EpgEntry* matchedEntry = nullptr);
const EpgEntry* MatchRecordingToEpg(const std::vector<EpgEntry>& entries, const Recording& recording);

} // namespace dispatcharr
