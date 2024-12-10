#include "command_option.h"

CommandOption::CommandOption(const std::string& long_name, char short_name,
                             const std::string& help_message, bool required)
    : long_name_(long_name),
      short_name_(short_name),
      help_message_(help_message),
      required_(required),
      default_value_(std::nullopt),
      valid_values_(),
      is_flag_(false),
      custom_error_message_(""),
      option_type_hint_("") {}

const std::string& CommandOption::GetLongName() const { return long_name_; }

char CommandOption::GetShortName() const { return short_name_; }

const std::string& CommandOption::GetHelpMessage() const {
  return help_message_;
}

bool CommandOption::IsRequired() const { return required_; }

bool CommandOption::HasDefaultValue() const {
  return default_value_.has_value();
}

void CommandOption::SetDefaultValue(const std::string& default_value) {
  default_value_ = default_value;
}
std::string CommandOption::GetDefaultValue() const {
  if (default_value_.has_value()) {
    return default_value_.value();
  }
  return "";
}

void CommandOption::SetValidValues(
    const std::vector<std::string>& valid_values) {
  valid_values_ = valid_values;
}

const std::vector<std::string>& CommandOption::GetValidValues() const {
  return valid_values_;
}

void CommandOption::SetFlag(bool is_flag) { is_flag_ = is_flag; }

bool CommandOption::IsFlag() const { return is_flag_; }

bool CommandOption::ValidateValue(const std::string& value) const {
  if (value.empty() && !is_flag_) {
    return false;
  }
  // Enum based values
  if (valid_values_.empty()) {
    return true;
  }
  return std::find(valid_values_.begin(), valid_values_.end(), value) !=
         valid_values_.end();
}

void CommandOption::SetCustomErrorMessage(const std::string& message) {
  custom_error_message_ = message;
}

const std::string& CommandOption::GetCustomErrorMessage() const {
  return custom_error_message_;
}

void CommandOption::SetOptionTypeHint(const std::string& option_type_hint) {
  option_type_hint_ = option_type_hint;
}

const std::string& CommandOption::GetOptionTypeHint() const {
  return option_type_hint_;
}

std::string CommandOption::GetOptionTypeHintDisplay() const {
  std::string type_hint = option_type_hint_;
  if (type_hint.empty()) {
    if (!valid_values_.empty()) {
      for (const auto& value : valid_values_) {
        type_hint += value + ", ";
      }
      // Remove the last comma and space
      type_hint.pop_back();
      type_hint.pop_back();
    }
  }
  if (!type_hint.empty()) {
    type_hint = "<" + type_hint + ">";
  }
  return type_hint;
}
