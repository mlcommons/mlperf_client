#include "command_parser.h"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "../CLI/version.h"
#include "utils.h"

const int MIN_CONSOLE_WIDTH = 60;

CommandParser::CommandParser(const std::string& program_name,
                             const std::string& program_description)
    : program_name_(program_name),
      program_description_(program_description),
      display_order_({}) {}

void CommandParser::AddOption(const CommandOption& option) {
  if (option.GetShortName() != '\0') {
    short_options_map_[option.GetShortName()] = option.GetLongName();
  }
  long_options_[option.GetLongName()] = option;
}

void CommandParser::AddFlag(CommandOption& option) {
  option.SetFlag(true);
  AddOption(option);
}

void CommandParser::AddBooleanOption(CommandOption& option) {
  option.SetValidValues({"true", "false"});
  AddOption(option);
}

void CommandParser::AddEnumOption(
    CommandOption& option, const std::vector<std::string>& valid_values) {
  option.SetValidValues(valid_values);
  AddOption(option);
}

bool CommandParser::Process(int argc, char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    std::string name;
    if (arg.starts_with("--")) {
      name = arg.substr(2);
    } else if (arg.starts_with("-") && arg.length() == 2) {
      char short_name = arg[1];
      if (short_options_map_.find(short_name) == short_options_map_.end()) {
        unknown_options_.push_back(arg);
        continue;
      }
      name = short_options_map_[short_name];

    } else {
      unknown_options_.push_back(arg);
      continue;
    }
    // Check if the option has a value
    if (!long_options_.contains(name)) {
      unknown_options_.push_back(arg);
      continue;
    }
    std::string value;
    // let's parse the value
    if (i + 1 < argc && argv[i + 1][0] != '-') {
      value = argv[++i];
    }
    if (long_options_[name].ValidateValue(value)) {
      long_values_[name] = value;
      continue;
    }

    auto error_message = long_options_[name].GetCustomErrorMessage();
    if (value.empty() && !long_options_[name].GetDefaultValue().empty()) {
      std::cerr << "No value provided for option \"" << name << "\".\n"
                << error_message << std::endl;
    } else
      std::cerr << "Invalid value for option \"" << name << "\": \"" << value
                << "\".\n"
                << error_message << std::endl;

    return false;
  }
  // Check if there's any required option missing
  for (const auto& [name, option] : long_options_) {
    if (option.IsRequired() && !long_values_.contains(name)) {
      std::cerr << "Option " << name << " is required" << std::endl;
      return false;
    }
  }
  ConfigureDisplayOptionOrder();
  return true;
}

const std::string& CommandParser::GetProgramName() const {
  return program_name_;
}

const std::string& CommandParser::GetProgramDescription() const {
  return program_description_;
}

bool CommandParser::OptionPassed(const std::string& long_name) const {
  return long_values_.contains(long_name);
}

std::string CommandParser::GetOptionValue(const std::string& long_name) const {
  if (!long_values_.contains(long_name)) {
    return long_options_.at(long_name).GetDefaultValue();
  }
  return long_values_.at(long_name);
}

std::optional<std::string> CommandParser::GetOption(
    const std::string& long_name) const {
  if (!long_values_.contains(long_name)) return std::nullopt;
  return long_values_.at(long_name);
}

std::vector<std::string> CommandParser::GetUnknownOptionNames() const {
  return unknown_options_;
}

std::string WrapText(const std::string& text, size_t width, size_t padding) {
  std::istringstream words(text);
  std::string word;
  std::string wrapped;
  std::string line;
  std::string pad(padding, ' ');
  while (words >> word) {
    if (line.size() + word.size() + 1 > width) {
      wrapped += line + "\n" + pad;
      line = word;
    } else {
      if (!line.empty()) {
        line += " ";
      }
      line += word;
    }
  }
  wrapped += line;
  return wrapped;
}

void CommandParser::ShowHelp() const {
  std::cout << "Usage: " << program_name_ << " [options]" << std::endl;
  std::cout << program_description_ << std::endl;
  std::cout << "Options:" << std::endl;
  // col1 -h, --help
  // col2 type hint
  // col3 default value
  // col4 help message
  int console_width = cil::utils::GetConsoleWidth();
  if (console_width < MIN_CONSOLE_WIDTH) {
    console_width = MIN_CONSOLE_WIDTH;
  }
  int option_width = 0;         // width of the option column (col1)
  int expected_args_width = 0;  // width of the expected arguments column (col2)
  int default_value_width = 0;  // width of the default value column (col3)
  // Start with the width of the headers text
  expected_args_width = std::string("Expected Type").length();
  option_width = std::string("Option").length();
  // calculate the width of the columns
  for (const auto& [name, option] : long_options_) {
    int option_length = 4;               // -c,
    option_length += name.length() + 2;  // --name
    option_width = option_width > option_length ? option_width : option_length;
    expected_args_width =
        expected_args_width > option.GetOptionTypeHintDisplay().length()
            ? expected_args_width
            : option.GetOptionTypeHintDisplay().length();
    default_value_width =
        default_value_width > option.GetDefaultValue().length()
            ? default_value_width
            : option.GetDefaultValue().length();
  }
  // Add some padding
  option_width += 2;
  expected_args_width += 2;
  int header_width = option_width + expected_args_width;
  int help_message_width = console_width - header_width - 10;
  // Print the header
  std::cout << std::string(header_width + help_message_width, '-') << std::endl;
  std::cout << std::left << std::setw(option_width) << "Option" << std::left
            << std::setw(expected_args_width) << "Expected Type"
            << "Description" << std::endl;
  std::cout << std::string(header_width + help_message_width, '-') << std::endl;
  std::cout << std::flush;
  for (auto name : display_order_) {
    auto option = long_options_.at(name);
    std::string wrapped_description =
        WrapText(option.GetHelpMessage(), help_message_width, header_width);
    std::string option_text = "";
    std::cout << "  ";
    if (option.GetShortName() != '\0') {
      option_text += "-";
      option_text.push_back(option.GetShortName());
      option_text += ", ";
    } else {
      option_text += "    ";
    }
    option_text += "--" + name;
    std::cout << "\n";
    std::cout << std::left << std::setw(option_width) << option_text
              << std::left << std::setw(expected_args_width)
              << option.GetOptionTypeHintDisplay() << wrapped_description;
  }
  std::cout << std::endl;
  std::cout << std::string(header_width + help_message_width, '-') << std::endl;
  std::cout << "\nExamples:\n\n";

  std::cout << "    " << program_name_
            << " -c Intel_ORTGenAI-DML_GPU.json"
               "\n\n Runs the benchmark using Intel_ORTGenAI-DML_GPU.json as the "
               "configuration "
               "file\n\n";
}

void CommandParser::ShowVersion() const {
  std::clog << "Application Version: " << APP_VERSION_STRING << " " << APP_BUILD_NAME << std::endl;
}

void CommandParser::SetDisplayOptionOrder(
    const std::vector<std::string>& display_order) {
  display_order_ = display_order;
}

void CommandParser::ConfigureDisplayOptionOrder() {
  if (display_order_.empty()) {
    for (const auto& [name, option] : long_options_) {
      display_order_.push_back(name);
    }
  }
}