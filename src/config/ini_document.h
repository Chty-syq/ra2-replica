#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ra2 {
std::string canonicalId(std::string_view text);
std::string trimIni(std::string_view text);
struct IniValue {
  std::string text;
  std::string file;
  std::size_t line = 0;
};

// IDs are ASCII case insensitive. Repeated sections merge, last field wins.
// Source locations survive overlays; no file IO occurs during queries.
class IniDocument {
public:
  using Section = std::map<std::string, IniValue, std::less<>>;
  static IniDocument load(const std::filesystem::path& path);
  static IniDocument parse(std::string_view text, std::string source = "<memory>");
  void overlay(const IniDocument& other);
  const Section& section(std::string_view id) const;
  const IniValue* find(std::string_view section, std::string_view key) const;
  bool contains(std::string_view section) const;
  std::string string(std::string_view section, std::string_view key, std::string fallback = {}) const;
  int integer(std::string_view section, std::string_view key, int fallback = 0) const;
  double number(std::string_view section, std::string_view key, double fallback = 0) const;
  bool boolean(std::string_view section, std::string_view key, bool fallback = false) const;
  std::vector<std::string> list(std::string_view section, std::string_view key) const;
  [[noreturn]] void invalid(std::string_view section, std::string_view key, std::string_view reason) const;
  std::string fingerprint() const;
private:
  std::map<std::string, Section, std::less<>> sections_;
};
}
