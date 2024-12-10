#ifndef COMMAND_OPTION_H
#define COMMAND_OPTION_H

#include <optional>
#include <string>
#include <vector>

class CommandOption {
 public:
  CommandOption() = default;
  CommandOption(const std::string& long_name, char short_name,
                const std::string& help_message, bool required);

  const std::string& GetLongName() const;
  char GetShortName() const;
  const std::string& GetHelpMessage() const;
  bool IsRequired() const;
  void SetDefaultValue(const std::string& default_value);
  bool HasDefaultValue() const;
  std::string GetDefaultValue() const;

  void SetValidValues(const std::vector<std::string>& valid_values);
  const std::vector<std::string>& GetValidValues() const;
  bool ValidateValue(const std::string& value) const;
  void SetCustomErrorMessage(const std::string& message);
  const std::string& GetCustomErrorMessage() const;
  void SetFlag(bool is_flag);
  bool IsFlag() const;
  void SetOptionTypeHint(const std::string& option_type_hint);
  const std::string& GetOptionTypeHint() const;
  std::string GetOptionTypeHintDisplay() const;

 private:
  std::string long_name_;
  char short_name_;
  std::string help_message_;
  bool required_;
  std::optional<std::string> default_value_;
  std::vector<std::string> valid_values_;
  std::string custom_error_message_;
  bool is_flag_;  // does not require a value
  std::string option_type_hint_;
};

#endif  // COMMAND_OPTION_H