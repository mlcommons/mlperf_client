#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "command_option.h"

class CommandParser {
 public:
  CommandParser(const std::string& program_name,
                const std::string& program_description);

  void AddOption(const CommandOption& option);
  void AddFlag(CommandOption& option);
  void AddBooleanOption(CommandOption& option);
  void AddEnumOption(CommandOption& option,
                     const std::vector<std::string>& valid_values);
  bool Process(int argc, char* argv[]);
  const std::string& GetProgramName() const;
  const std::string& GetProgramDescription() const;
  bool OptionPassed(const std::string& long_name) const;
  std::string GetOptionValue(const std::string& long_name) const;
  std::optional<std::string> GetOption(const std::string& long_name) const;
  inline std::optional<std::string> operator[](
      const std::string& long_name) const {
    return GetOption(long_name);
  }

  std::vector<std::string> GetUnknownOptionNames() const;
  void ShowHelp() const;
  void ShowVersion() const;
  void SetDisplayOptionOrder(const std::vector<std::string>& display_order);

 private:
  std::string program_name_;
  std::string program_description_;
  std::unordered_map<char, std::string> short_options_map_;
  std::map<std::string, CommandOption> long_options_;
  std::unordered_map<std::string, std::string> long_values_;
  std::vector<std::string> unknown_options_;
  mutable std::vector<std::string> display_order_;
  void ConfigureDisplayOptionOrder();
};

#endif  // COMMAND_PARSER_H