#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace jerusalem {
constexpr std::size_t max_resource_bytes = 64 * 1024 * 1024;
class ParseError : public std::runtime_error { using std::runtime_error::runtime_error; };
class Bytes {
 public:
  Bytes(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}
  explicit Bytes(const std::vector<std::uint8_t>& v) : Bytes(v.data(), v.size()) {}
  std::size_t size() const { return size_; }
  void require(std::size_t offset, std::size_t count) const;
  std::uint8_t u8(std::size_t offset) const;
  std::uint16_t u16(std::size_t offset) const;
  std::uint32_t u32(std::size_t offset) const;
  std::int32_t i32(std::size_t offset) const;
  bool is(std::size_t offset, const std::string& text) const;
  Bytes slice(std::size_t offset, std::size_t count) const;
  const std::uint8_t* data() const { return data_; }
 private:
  const std::uint8_t* data_;
  std::size_t size_;
};
struct Wave {
  std::uint16_t channels, bits;
  std::uint32_t rate;
  std::size_t data_offset, data_size, file_size;
};
struct AudioBlock { std::size_t payload_offset, stored_size, decoded_size; };
struct GameAudio {
  std::uint16_t channels;
  std::uint32_t rate, pcm_size, padded_wave_size;
  std::vector<AudioBlock> blocks;
};
struct Bitmap {
  std::uint32_t width, height;
  std::vector<std::uint8_t> rgba;
};
struct AqcInfo {
  std::uint32_t width, height;
  std::size_t indices_offset, indices_size, palette_offset, palette_size, static_end, tail_size;
};
struct Cue { std::string binary_name; std::uint32_t sector_size = 2352; };
struct Embedded { std::string kind; std::size_t offset, size; };
struct IniEntry { std::string section, key, value; std::size_t line; };
struct ArtEntry { std::string name; std::uint32_t offset, stored_size, auxiliary_size; bool payload_available; };
struct ArtDirectory {
  std::uint32_t declared_count;
  bool directory_complete;
  std::vector<ArtEntry> entries;
};
struct LocalizedEntry { std::string key, language, title, text; };
struct DanTriple { std::uint32_t first, second, third; };
struct Dan { std::vector<DanTriple> levels, intervals; std::uint32_t end; };
enum class ResourceKind { Cinema, Subtitles, Unsupported, Bitmap, AqcImage, Wave, CompressedAudio, Archive, LocalizedText, DialogueAnimation, Text };
struct ResourceDescription {
  ResourceKind kind;
  std::string summary;
  std::string details;
};
ResourceDescription describe_resource(Bytes bytes, const std::string& filename = {});
ArtDirectory parse_at3(Bytes bytes, bool allow_prefix = false);
std::vector<LocalizedEntry> parse_localized(const std::string& text);
Dan parse_dan(const std::string& text);
std::vector<std::uint8_t> read_file(const std::filesystem::path& path);
Wave parse_wave(Bytes bytes);
GameAudio parse_game_audio(Bytes bytes);
std::vector<std::uint8_t> decode_game_audio(Bytes bytes);
Bitmap decode_bitmap(Bytes bytes);
AqcInfo parse_aqc(Bytes bytes);
Bitmap decode_aqc(Bytes bytes);
Cue parse_cue(const std::string& text);
std::vector<IniEntry> parse_ini(const std::string& text);
std::vector<Embedded> scan_media(Bytes bytes);
std::string inspect(Bytes bytes);
}  // namespace jerusalem
