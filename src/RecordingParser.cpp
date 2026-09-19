#include "RecordingParser.h"

#include "DateTimeFormat.h"
#include "JsonFieldUtil.h"

#include <nlohmann/json.hpp>
#include <limits>

namespace dispatcharr
{

nlohmann::json BuildOneTimeRecordingRequest(int channelId, time_t start, time_t end, const RecordingEpgLink& epgLink)
{
  nlohmann::json body = {{"channel", channelId}, {"start_time", IsoFromTime(start)}, {"end_time", IsoFromTime(end)}};
  if (epgLink.channelId == channelId && channelId > 0 && epgLink.broadcastId != 0 && epgLink.startTime > 0 &&
      epgLink.endTime > epgLink.startTime && start < epgLink.endTime && end > epgLink.startTime)
    body["custom_properties"]["pvr_dispatcharr_unofficial"] = {{"version", 1},
                                                               {"channel_id", channelId},
                                                               {"broadcast_id", epgLink.broadcastId},
                                                               {"start_time", IsoFromTime(epgLink.startTime)},
                                                               {"end_time", IsoFromTime(epgLink.endTime)}};
  return body;
}

Recording ParseRecordingFields(const nlohmann::json& item, time_t now)
{
  Recording r;
  r.id = FieldOr(item, "id", 0);
  r.channelId = FieldOr(item, "channel", FieldOr(item, "channel_id", 0));
  r.startTime = TimeFromIso(FieldOr<std::string>(item, "start_time", ""));
  r.endTime = TimeFromIso(FieldOr<std::string>(item, "end_time", ""));
  r.durationSeconds = (r.endTime > r.startTime) ? static_cast<int>(r.endTime - r.startTime) : 0;
  r.isInProgress = r.startTime > 0 && r.startTime <= now && now < r.endTime;
  r.isUpcoming = r.startTime > now;

  const nlohmann::json& custom = item.contains("custom_properties") ? item["custom_properties"] : nlohmann::json();
  if (custom.is_object())
  {
    // custom_properties.status is a more authoritative signal than the
    // start/end time window above when present: a recording stopped
    // early (see DispatcharrClient::StopRecording()) keeps its
    // originally-scheduled end_time untouched, so the time-window check
    // alone would keep reporting it as in-progress for the rest of that
    // original duration even though it finished the moment it was
    // stopped. Confirmed values: "recording" (still active), "completed"/
    // "stopped"/"interrupted" (all finished, one way or another) --
    // exact enum not documented, so only treat "recording" as
    // authoritative for in-progress and fall back to the time window for
    // anything else/absent, rather than assuming a closed list.
    std::string status = FieldOr<std::string>(custom, "status", "");
    if (status == "recording")
      r.isInProgress = true;
    else if (!status.empty())
      r.isInProgress = false;

    // See Recording::hlsDirStillPresent's own comment: present for the
    // whole window between "user stopped it" and "concat + viewer-wait
    // actually finished," regardless of what status already says.
    r.hlsDirStillPresent = custom.contains("_hls_dir") && !custom["_hls_dir"].is_null();

    // custom_properties.bytes_written is only written by Dispatcharr's
    // recording task at finalization (confirmed against its source,
    // apps/channels/tasks.py: summed from HLS segment file sizes and
    // stored into custom_properties only once the task reaches its
    // post-processing step) -- absent while genuinely still recording,
    // hence the 0 default here rather than treating absence as an error.
    r.bytesWritten = FieldOr<int64_t>(custom, "bytes_written", 0);
    r.iconPath = FieldOr<std::string>(custom, "poster_url", "");

    const auto linkIt = custom.find("pvr_dispatcharr_unofficial");
    if (linkIt != custom.end() && linkIt->is_object())
    {
      const auto& link = *linkIt;
      const auto uidIt = link.find("broadcast_id");
      const auto versionIt = link.find("version");
      const auto channelIt = link.find("channel_id");
      if (versionIt != link.end() && versionIt->is_number_integer() && FieldOr<int64_t>(link, "version", 0) == 1 &&
          channelIt != link.end() && channelIt->is_number_integer() && uidIt != link.end() &&
          uidIt->is_number_integer())
      {
        const int64_t uid = FieldOr<int64_t>(link, "broadcast_id", 0);
        const int64_t channel = FieldOr<int64_t>(link, "channel_id", 0);
        if (uid > 0 && uid <= std::numeric_limits<uint32_t>::max() && channel > 0 &&
            channel <= std::numeric_limits<int>::max())
        {
          r.epgLink.channelId = static_cast<int>(channel);
          r.epgLink.broadcastId = static_cast<uint32_t>(uid);
          r.epgLink.startTime = TimeFromIso(FieldOr<std::string>(link, "start_time", ""));
          r.epgLink.endTime = TimeFromIso(FieldOr<std::string>(link, "end_time", ""));
        }
      }
    }

    const nlohmann::json& program = custom.contains("program") ? custom["program"] : nlohmann::json();
    if (program.is_object())
    {
      r.title = FieldOr<std::string>(program, "title", "");
      r.subtitle = FieldOr<std::string>(program, "sub_title", "");
      r.description = FieldOr<std::string>(program, "description", "");
      // Manual recurring schedules can also carry a program dictionary with
      // their own window. Only trust original timestamps for an identified
      // backend programme; otherwise let the guide matcher decide.
      if (FieldOr<int64_t>(program, "id", 0) > 0)
      {
        r.programStartTime = TimeFromIso(FieldOr<std::string>(program, "start_time", ""));
        r.programEndTime = TimeFromIso(FieldOr<std::string>(program, "end_time", ""));
      }
    }
    if (r.title.empty())
      r.title = FieldOr<std::string>(custom, "title", "");
    if (r.subtitle.empty())
      r.subtitle = FieldOr<std::string>(custom, "sub_title", "");
    if (r.description.empty())
      r.description = FieldOr<std::string>(custom, "description", "");

    // Tagged by Dispatcharr's own recurring-rule scheduler (confirmed
    // against its source: custom_properties.rule =
    // {"type": "recurring", "id": <rule id>, ...}) -- see RecurringRule's
    // own comment for how this links back to its parent rule as a Kodi
    // timer.
    const nlohmann::json& rule = custom.contains("rule") ? custom["rule"] : nlohmann::json();
    if (rule.is_object() && FieldOr<std::string>(rule, "type", "") == "recurring")
      r.recurringRuleId = FieldOr(rule, "id", 0);
  }
  return r;
}

bool ParseRecordingEdlEntryJson(const nlohmann::json& item, RecordingEdlEntry& out)
{
  RecordingEdlEntry entry;
  entry.startMs = FieldOr<int64_t>(item, "start", 0);
  entry.endMs = FieldOr<int64_t>(item, "end", 0);
  entry.type = FieldOr(item, "type", 3);
  if (entry.endMs <= entry.startMs)
    return false;
  out = entry;
  return true;
}

} // namespace dispatcharr
