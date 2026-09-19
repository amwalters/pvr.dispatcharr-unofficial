#include "EpgRecordingMatch.h"
#include "EpgTagUtil.h"
#include "RecordingParser.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace dispatcharr;

namespace
{
EpgEntry Entry(const char* title, time_t start, time_t end)
{
  EpgEntry entry;
  entry.title = title;
  entry.startTime = start;
  entry.endTime = end;
  return entry;
}
} // namespace

TEST_CASE("Recording EPG match tolerates DVR padding", "[EpgRecordingMatch]")
{
  std::vector<EpgEntry> entries = {Entry("Previous", 1000, 1600), Entry("The Show", 1600, 3400),
                                   Entry("Next", 3400, 4000)};

  const EpgEntry* match = MatchRecordingToEpg(entries, 1300, 3700, "The Show");
  REQUIRE(match != nullptr);
  CHECK(match->startTime == 1600);
}

TEST_CASE("Recording EPG title matching ignores case and punctuation", "[EpgRecordingMatch]")
{
  std::vector<EpgEntry> entries = {Entry("The Show!", 1600, 3400)};

  const EpgEntry* match = MatchRecordingToEpg(entries, 1600, 3400, "THE SHOW");
  REQUIRE(match != nullptr);
  CHECK(match->startTime == 1600);
}

TEST_CASE("Recording an already-airing programme can match after start was clamped", "[EpgRecordingMatch]")
{
  std::vector<EpgEntry> entries = {Entry("The Show", 1600, 3400)};

  // User presses Record late in the programme; Dispatcharr may persist the
  // actual recording start rather than the EPG programme's original start.
  const EpgEntry* match = MatchRecordingToEpg(entries, 3000, 3400, "The Show");
  REQUIRE(match != nullptr);
  CHECK(match->startTime == 1600);
}

TEST_CASE("Repeated adjacent titles choose the occurrence with most overlap", "[EpgRecordingMatch]")
{
  std::vector<EpgEntry> entries = {Entry("The Show", 1600, 3400), Entry("The Show", 3400, 5200)};

  const EpgEntry* match = MatchRecordingToEpg(entries, 1500, 3700, "The Show");
  REQUIRE(match != nullptr);
  CHECK(match->startTime == 1600);
}

TEST_CASE("A different title does not claim an overlapping programme", "[EpgRecordingMatch]")
{
  std::vector<EpgEntry> entries = {Entry("The Show", 1600, 3400)};
  CHECK(MatchRecordingToEpg(entries, 1500, 3500, "Something Else") == nullptr);
}

TEST_CASE("Untitled recording only matches near-identical programme boundaries", "[EpgRecordingMatch]")
{
  std::vector<EpgEntry> entries = {Entry("The Show", 1600, 3400)};

  REQUIRE(MatchRecordingToEpg(entries, 1590, 3410, "") != nullptr);
  CHECK(MatchRecordingToEpg(entries, 1300, 3700, "") == nullptr);
}

TEST_CASE("Broad manual recording without a title does not guess an EPG programme", "[EpgRecordingMatch]")
{
  std::vector<EpgEntry> entries = {Entry("One", 1000, 1600), Entry("Two", 1600, 2200), Entry("Three", 2200, 2800)};
  CHECK(MatchRecordingToEpg(entries, 1000, 2800, "") == nullptr);
}

TEST_CASE("Invalid recording windows never match", "[EpgRecordingMatch]")
{
  std::vector<EpgEntry> entries = {Entry("The Show", 1600, 3400)};
  CHECK(MatchRecordingToEpg(entries, 0, 3400, "The Show") == nullptr);
  CHECK(MatchRecordingToEpg(entries, 3400, 1600, "The Show") == nullptr);
}

TEST_CASE("Equal overlap with adjacent repeats is ambiguous", "[EpgRecordingMatch]")
{
  std::vector<EpgEntry> entries = {Entry("The Show", 1000, 2000), Entry("The Show", 2000, 3000)};
  CHECK(MatchRecordingToEpg(entries, 1500, 2500, "The Show") == nullptr);
}

TEST_CASE("Display placeholder does not block an otherwise exact recording match", "[EpgRecordingMatch]")
{
  Recording rec;
  rec.channelId = 7;
  rec.startTime = 1600;
  rec.endTime = 3400;
  rec.title = "Recording 42";
  rec.titleIsPlaceholder = true;
  std::vector<EpgEntry> entries = {Entry("The Show", 1600, 3400)};
  REQUIRE(MatchRecordingToEpg(entries, rec) != nullptr);
  rec.titleIsPlaceholder = false; // an actual user-entered title is not discarded
  CHECK(MatchRecordingToEpg(entries, rec) == nullptr);
}

TEST_CASE("Saved guide identity survives server clamping, rename and guide expiry", "[EpgRecordingMatch]")
{
  constexpr time_t start = 1767225600;
  constexpr time_t end = start + 3600;
  const uint32_t uid = ComputeBroadcastId(7, start);
  auto response = BuildOneTimeRecordingRequest(7, start, end, {7, uid, start, end});
  response["id"] = 42;
  response["start_time"] = "2026-01-01T00:45:00Z"; // started late from the guide
  response["custom_properties"]["program"]["title"] = "Renamed recording";
  response["custom_properties"]["status"] = "completed";
  // Serializing and re-reading represents a fresh addon instance fetching the
  // recording after restart, with no in-memory guide or title cache left.
  const auto rec = ParseRecordingFields(nlohmann::json::parse(response.dump()), end + 86400);
  CHECK(RecordingEpgUid(rec) == uid);
  CHECK(MatchRecordingToEpg({}, rec) == nullptr);
  std::vector<EpgEntry> entries = {Entry("Original programme title", start, end)};
  REQUIRE(MatchRecordingToEpg(entries, rec) != nullptr);
}

TEST_CASE("Stale saved association cannot claim another channel or time slot", "[EpgRecordingMatch]")
{
  Recording rec;
  rec.channelId = 7;
  rec.startTime = 1600;
  rec.endTime = 3400;
  rec.epgLink = {8, ComputeBroadcastId(8, 1600), 1600, 3400};
  CHECK(RecordingEpgUid(rec) == 0);
  rec.epgLink = {7, ComputeBroadcastId(7, 4000), 4000, 5800};
  CHECK(RecordingEpgUid(rec) == 0);
  rec.title = "The Show";
  std::vector<EpgEntry> entries = {Entry("The Show", 1600, 3400)};
  CHECK(RecordingEpgUid(rec, MatchRecordingToEpg(entries, rec)) == ComputeBroadcastId(7, 1600));
}

TEST_CASE("Existing recording programme timestamps recover identity without XMLTV", "[EpgRecordingMatch]")
{
  nlohmann::json item = {{"channel", 7},
                         {"start_time", "2026-01-01T00:10:00Z"},
                         {"end_time", "2026-01-01T01:05:00Z"},
                         {"custom_properties",
                          {{"program",
                            {{"title", "The Show"},
                             {"id", 99},
                             {"start_time", "2026-01-01T11:00:00+11:00"},
                             {"end_time", "2026-01-01T12:00:00+11:00"}}}}}};
  Recording rec = ParseRecordingFields(item, 1767312000);
  CHECK(RecordingEpgUid(rec) == ComputeBroadcastId(7, 1767225600));
  rec.programEndTime = rec.programStartTime; // malformed programme metadata
  CHECK(RecordingEpgUid(rec) == 0);
  item["custom_properties"]["program"].erase("id");
  CHECK(RecordingEpgUid(ParseRecordingFields(item, 1767312000)) == 0);
}

TEST_CASE("A pre-update completed recording can match historical XMLTV", "[EpgRecordingMatch]")
{
  nlohmann::json item = {{"channel", 7},
                         {"start_time", "2026-01-01T00:45:00Z"},
                         {"end_time", "2026-01-01T01:00:00Z"},
                         {"custom_properties", {{"program", {{"title", "The Show"}}}, {"status", "completed"}}}};
  const Recording rec = ParseRecordingFields(item, 1767312000);
  std::unordered_map<std::string, std::vector<EpgEntry>> guide;
  std::string error;
  REQUIRE(XmlTvParser::Parse(R"(<tv><programme channel="12" start="20260101000000 +0000"
      stop="20260101010000 +0000"><title>The Show</title></programme></tv>)",
                             guide, error));
  const auto* match = MatchRecordingToEpg(guide.at("12"), rec);
  REQUIRE(match != nullptr);
  CHECK(RecordingEpgUid(rec, match) == ComputeBroadcastId(7, 1767225600));
  CHECK(RecordingEpgUid(rec, MatchRecordingToEpg({}, rec)) == 0);
}
