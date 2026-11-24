#include "gdlvl.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <miniz.h>

namespace pathfinder_cli {
namespace {

// Errors are returned in the `error` field; an empty field means success.
struct BytesResult {
  std::vector<uint8_t> bytes;
  std::string error;
};

struct TextResult {
  std::string text;
  std::string error;
};

struct HeaderResult {
  size_t position = 0;
  std::string error;
};

struct InputResult {
  std::string xml;
  std::string level_string;
  std::string error;
};

std::string Trim(std::string_view text) {
  auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  auto first = std::find_if_not(text.begin(), text.end(), is_space);
  auto last = std::find_if_not(text.rbegin(), text.rend(), is_space).base();
  if (first >= last) {
    return "";
  }
  return std::string(first, last);
}

// GD stores base64 with a URL-safe alphabet; accept the standard one too.
int Base64Value(char ch) {
  if (ch >= 'A' && ch <= 'Z') {
    return ch - 'A';
  }
  if (ch >= 'a' && ch <= 'z') {
    return ch - 'a' + 26;
  }
  if (ch >= '0' && ch <= '9') {
    return ch - '0' + 52;
  }
  if (ch == '-' || ch == '+') {
    return 62;
  }
  if (ch == '_' || ch == '/') {
    return 63;
  }
  return -1;
}

BytesResult DecodeBase64(std::string_view text) {
  BytesResult result;
  result.bytes.reserve(text.size() * 3 / 4);
  uint32_t acc = 0;
  int bits = 0;
  for (char ch : text) {
    if (ch == '=' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
      continue;
    }
    int value = Base64Value(ch);
    if (value < 0) {
      result.error = "invalid base64 character in level data";
      return result;
    }
    acc = (acc << 6) | static_cast<uint32_t>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      result.bytes.push_back(static_cast<uint8_t>((acc >> bits) & 0xff));
      acc &= (1u << bits) - 1;
    }
  }
  return result;
}

// Returns the offset just after a NUL-terminated header field.
std::optional<size_t> SkipHeaderString(std::vector<uint8_t> const& data,
                                       size_t start) {
  size_t position = start;
  while (position < data.size() && data[position] != 0) {
    ++position;
  }
  if (position >= data.size()) {
    return std::nullopt;
  }
  return position + 1;
}

// Advances past the gzip header so tinfl sees raw deflate data.
HeaderResult SkipGzipHeader(std::vector<uint8_t> const& data) {
  HeaderResult result;
  if (data.size() < 18 || data[0] != 0x1f || data[1] != 0x8b ||
      data[2] != 8) {
    result.error = "level data is not gzip-compressed";
    return result;
  }
  result.position = 10;
  uint8_t flags = data[3];
  auto has = [&](size_t count) {
    return result.position + count <= data.size();
  };
  if ((flags & 0x04) != 0) {  // FEXTRA
    if (!has(2)) {
      result.error = "truncated gzip header in level data";
      return result;
    }
    size_t xlen = static_cast<size_t>(data[result.position]) |
                  (static_cast<size_t>(data[result.position + 1]) << 8);
    if (!has(2 + xlen)) {
      result.error = "truncated gzip header in level data";
      return result;
    }
    result.position += 2 + xlen;
  }
  for (int flag : {0x08, 0x10}) {  // FNAME, FCOMMENT
    if ((flags & flag) == 0) {
      continue;
    }
    std::optional<size_t> next = SkipHeaderString(data, result.position);
    if (!next.has_value()) {
      result.error = "truncated gzip header in level data";
      return result;
    }
    result.position = *next;
  }
  if ((flags & 0x02) != 0 && has(2)) {  // FHCRC
    result.position += 2;
  }
  if (!has(1)) {
    result.error = "truncated gzip header in level data";
  }
  return result;
}

BytesResult InflateGzip(std::vector<uint8_t> const& data) {
  BytesResult result;
  HeaderResult header = SkipGzipHeader(data);
  if (!header.error.empty()) {
    result.error = header.error;
    return result;
  }
  constexpr size_t kMaxOutput = 64 * 1024 * 1024;
  result.bytes.resize(kMaxOutput);
  size_t written = tinfl_decompress_mem_to_mem(
      result.bytes.data(), result.bytes.size(), data.data() + header.position,
      data.size() - header.position,
      TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  if (written == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
    result.bytes.clear();
    result.error = "failed to inflate level data";
    return result;
  }
  result.bytes.resize(written);
  return result;
}

TextResult DecodeGdString(std::string const& encoded) {
  TextResult result;
  BytesResult bytes = DecodeBase64(encoded);
  if (!bytes.error.empty()) {
    result.error = bytes.error;
    return result;
  }
  BytesResult inflated = InflateGzip(bytes.bytes);
  if (!inflated.error.empty()) {
    result.error = inflated.error;
    return result;
  }
  result.text.assign(inflated.bytes.begin(), inflated.bytes.end());
  return result;
}

// Parses a numeric XML entity without throwing.
std::optional<char> ParseEntity(std::string_view entity, int base) {
  uint32_t value = 0;
  auto [end, ec] = std::from_chars(
      entity.data(), entity.data() + entity.size(), value, base);
  if (ec != std::errc{} || end != entity.data() + entity.size() ||
      value > 0xff) {
    return std::nullopt;
  }
  return static_cast<char>(value);
}

std::optional<char> DecodeEntity(std::string_view entity) {
  if (entity == "amp") {
    return '&';
  }
  if (entity == "lt") {
    return '<';
  }
  if (entity == "gt") {
    return '>';
  }
  if (entity == "quot") {
    return '"';
  }
  if (entity == "apos") {
    return '\'';
  }
  if (entity.starts_with("#x") || entity.starts_with("#X")) {
    return ParseEntity(entity.substr(2), 16);
  }
  if (entity.starts_with('#')) {
    return ParseEntity(entity.substr(1), 10);
  }
  return std::nullopt;
}

std::string XmlUnescape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '&') {
      out.push_back(text[i]);
      continue;
    }
    size_t entity_end = text.find(';', i);
    if (entity_end == std::string_view::npos || entity_end - i > 12) {
      out.push_back(text[i]);
      continue;
    }
    std::string_view entity = text.substr(i + 1, entity_end - i - 1);
    std::optional<char> decoded = DecodeEntity(entity);
    out.push_back(decoded.has_value() ? *decoded : text[i]);
    if (decoded.has_value()) {
      i = entity_end;
    }
  }
  return out;
}

std::optional<std::string> FindTagValue(std::string_view xml,
                                       std::string_view key) {
  std::string needle = "<k>" + std::string(key) + "</k>";
  size_t key_pos = xml.find(needle);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }
  size_t value_pos = xml.find("<s>", key_pos + needle.size());
  if (value_pos == std::string_view::npos) {
    return std::nullopt;
  }
  size_t value_end = xml.find("</s>", value_pos + 3);
  if (value_end == std::string_view::npos) {
    return std::nullopt;
  }
  return XmlUnescape(xml.substr(value_pos + 3, value_end - value_pos - 3));
}

// Returns the file name without its extension, using either separator style.
std::string BaseName(std::string const& path) {
  size_t slash = path.find_last_of("/\\");
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  size_t dot = name.find_last_of('.');
  return dot == std::string::npos ? name : name.substr(0, dot);
}

InputResult ParseInput(std::string const& path) {
  InputResult result;
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    result.error = "could not open " + path;
    return result;
  }
  std::string raw((std::istreambuf_iterator<char>(file)),
                  std::istreambuf_iterator<char>());
  std::string text = Trim(raw);
  if (text.empty()) {
    result.error = "empty level file: " + path;
    return result;
  }
  if (text.find("<plist") != std::string::npos) {
    result.xml = text;
    return result;
  }

  TextResult decoded = DecodeGdString(text);
  if (decoded.error.empty()) {
    if (decoded.text.find("<plist") != std::string::npos) {
      result.xml = decoded.text;
      return result;
    }
    if (decoded.text.find(';') != std::string::npos) {
      result.level_string = decoded.text;
      return result;
    }
  }
  if (text.find(';') != std::string::npos) {
    result.level_string = text;
    return result;
  }
  result.error = decoded.error.empty() ? "unrecognized level data in " + path
                                       : decoded.error;
  return result;
}

}  // namespace

LoadResult LoadLevelFile(std::string const& path) {
  LoadResult result;
  result.level.name = BaseName(path);
  InputResult input = ParseInput(path);
  if (!input.error.empty()) {
    result.error = input.error;
    return result;
  }
  if (!input.level_string.empty()) {
    result.level.level_string = input.level_string;
    return result;
  }

  std::optional<std::string> encoded = FindTagValue(input.xml, "k4");
  if (!encoded.has_value()) {
    result.error = "level plist has no k4 level string";
    return result;
  }
  std::optional<std::string> name = FindTagValue(input.xml, "k2");
  if (name.has_value() && !name->empty()) {
    result.level.name = *name;
  }
  TextResult level = DecodeGdString(*encoded);
  if (!level.error.empty()) {
    result.error = level.error;
    return result;
  }
  result.level.level_string = level.text;
  return result;
}

}  // namespace pathfinder_cli
