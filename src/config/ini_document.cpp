#include "ini_document.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace ra2 {
std::string trimIni(const std::string_view text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) return {};
  return std::string(text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1));
}
std::string canonicalId(const std::string_view text) {
  auto result = trimIni(text);
  for (auto& ch : result) if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
  return result;
}
IniDocument IniDocument::load(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Cannot open INI: " + path.string());
  const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  return parse(bytes, path.string());
}
IniDocument IniDocument::parse(std::string_view text, std::string source) {
  if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
  IniDocument result;
  std::istringstream input{std::string(text)};
  std::string line, current;
  std::size_t lineNumber = 0;
  while (std::getline(input, line)) {
    ++lineNumber;
    // Shipped RA2 files also contain whitespace-prefixed // comments.
    auto comment = line.find(';');
    const auto slash = line.find("//");
    if (slash != std::string::npos && (slash == 0 || line[slash - 1] == ' ' || line[slash - 1] == '\t')) comment = std::min(comment, slash);
    auto value = trimIni(std::string_view(line).substr(0, comment));
    if (value.empty()) continue;
    if (value.front() == '[') {
      if (value.back() != ']') throw std::runtime_error(source + ":" + std::to_string(lineNumber) + ": malformed INI section");
      current = canonicalId(std::string_view(value).substr(1, value.size() - 2));
      result.sections_[current];
    } else if (const auto equals = value.find('='); equals != std::string::npos && !current.empty()) {
      result.sections_[current][canonicalId(std::string_view(value).substr(0, equals))] =
        {trimIni(std::string_view(value).substr(equals + 1)), source, lineNumber};
    }
  }
  return result;
}
void IniDocument::overlay(const IniDocument& other) {
  for (const auto& [id, fields] : other.sections_) for (const auto& [key, value] : fields) sections_[id][key] = value;
}
const IniDocument::Section& IniDocument::section(const std::string_view id) const {
  static const Section empty;
  const auto it = sections_.find(canonicalId(id));
  return it == sections_.end() ? empty : it->second;
}
const IniValue* IniDocument::find(const std::string_view id, const std::string_view key) const {
  const auto& fields = section(id);
  const auto it = fields.find(canonicalId(key));
  return it == fields.end() ? nullptr : &it->second;
}
bool IniDocument::contains(const std::string_view id) const { return sections_.contains(canonicalId(id)); }
std::string IniDocument::string(const std::string_view id, const std::string_view key, std::string fallback) const {
  const auto* value = find(id, key);
  return value ? value->text : std::move(fallback);
}
int IniDocument::integer(const std::string_view id, const std::string_view key, const int fallback) const {
  const auto* value = find(id, key);
  if (!value) return fallback;
  int result = 0;
  auto text = std::string_view(value->text);
  if (text.starts_with('+')) text.remove_prefix(1);
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
  if (error != std::errc{} || end != text.data() + text.size()) invalid(id, key, "expected an integer");
  return result;
}
double IniDocument::number(const std::string_view id, const std::string_view key, const double fallback) const {
  const auto* value = find(id, key);
  if (!value) return fallback;
  double result = 0;
  auto text = std::string_view(value->text);
  if (text.starts_with('+')) text.remove_prefix(1);
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
  if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(result)) invalid(id, key, "expected a finite number");
  return result;
}
bool IniDocument::boolean(const std::string_view id, const std::string_view key, const bool fallback) const {
  const auto* value = find(id, key);
  if (!value) return fallback;
  const auto text = canonicalId(value->text);
  if (text == "YES" || text == "TRUE" || text == "1") return true;
  if (text == "NO" || text == "FALSE" || text == "0") return false;
  invalid(id, key, "expected yes/no, true/false or 1/0");
}
std::vector<std::string> IniDocument::list(const std::string_view id, const std::string_view key) const {
  std::vector<std::string> result;
  std::istringstream input(string(id, key));
  std::string part;
  while (std::getline(input, part, ',')) if (auto trimmed = trimIni(part); !trimmed.empty()) result.push_back(std::move(trimmed));
  return result;
}
void IniDocument::invalid(const std::string_view id, const std::string_view key, const std::string_view reason) const {
  const auto* value = find(id, key);
  throw std::runtime_error((value ? value->file + ":" + std::to_string(value->line) : "<configuration>") +
                          " [" + std::string(id) + "] " + std::string(key) + ": " + std::string(reason));
}
std::string IniDocument::fingerprint() const {
  // FNV-1a over canonical, merged content; diagnostic/replay identity, not a security hash.
  std::uint64_t hash = 14695981039346656037ULL;
  const auto append = [&hash](std::string_view value) {
    for (const unsigned char ch : value) { hash ^= ch; hash *= 1099511628211ULL; }
    hash ^= 0; hash *= 1099511628211ULL;
  };
  for (const auto& [id, fields] : sections_) {
    append(id);
    for (const auto& [key, value] : fields) { append(key); append(value.text); }
  }
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << hash;
  return out.str();
}
}
