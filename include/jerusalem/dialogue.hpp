#pragma once
#include "jerusalem/navigation.hpp"
namespace jerusalem {
struct SubtitleCue { std::uint32_t time_ms; std::string text,secondary; };
struct DialogueText { std::vector<SubtitleCue> cues; std::uint32_t end_ms; };
// Confirmed integer-millisecond DIA dialect. Comma/timecode dialect is rejected.
DialogueText parse_dia(const std::string& bytes);
std::string subtitle_at(const DialogueText& text,std::uint64_t elapsed_ms);
struct DialogueFiles { std::filesystem::path audio,subtitles; };
DialogueFiles resolve_dialogue(const std::filesystem::path& root,const DialogueResource& resource);
}
