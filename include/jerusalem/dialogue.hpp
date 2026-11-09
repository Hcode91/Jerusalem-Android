#pragma once
#include "jerusalem/navigation.hpp"
namespace jerusalem {
struct SubtitleCue { std::uint32_t time_ms; std::string text,secondary; };
struct DialogueText { std::vector<SubtitleCue> cues; std::uint32_t end_ms; };
// Confirmed integer-millisecond DIA dialect. Comma/timecode dialect is rejected.
DialogueText parse_dia(const std::string& bytes);
std::string subtitle_at(const DialogueText& text,std::uint64_t elapsed_ms);
struct DialogueFiles { std::filesystem::path audio,subtitles;std::optional<std::filesystem::path> animation{}; };
// A unique, case-insensitive WAC/WAS/WAV audio sibling plus the matching DIA.
// File bytes select the decoder; the executable's WAC suffix is not authoritative.
DialogueFiles resolve_dialogue(const std::filesystem::path& root,const DialogueResource& resource);
struct DialogueTopic {std::uint32_t id;std::string text;bool accented;};
struct TopicGroup {std::uint32_t id,root;std::vector<DialogueTopic> topics;};
std::vector<TopicGroup> parse_keywords(const std::string& bytes);
}
