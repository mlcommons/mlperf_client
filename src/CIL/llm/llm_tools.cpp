#include "llm_tools.h"

#include <log4cxx/logger.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "json_schema.h"

namespace fs = std::filesystem;
using json = nlohmann::json;
using MillisecDuration = cil::infer::LLMInference::MillisecDuration;

namespace {

constexpr std::string_view kSandboxDirectory = "tools_sandbox";

const std::unordered_map<std::string, json> kToolInputSchemas = {
    {"read", json::parse(R"({
    "type": "object",
    "properties": {
      "path": {"type": "string"}
    },
    "required": ["path"],
    "additionalProperties": false
  })")},
    {"write", json::parse(R"({
    "type": "object",
    "properties": {
      "path": {"type": "string"},
      "content": {"type": "string"}
    },
    "required": ["path", "content"],
    "additionalProperties": false
  })")},
    {"patch", json::parse(R"({
    "type": "object",
    "properties": {
      "path": {"type": "string"},
      "diff": {"type": "string"}
    },
    "required": ["path", "diff"],
    "additionalProperties": false
  })")},
    {"execute", json::parse(R"({
    "type": "object",
    "properties": {
      "command": {"type": "string"},
      "cwd": {"type": "string"}
    },
    "required": ["command"],
    "additionalProperties": false
  })")},
    {"empty", json::object()}};

const json& GetToolUseBlockSchema() {
  static const json schema = json::parse(R"({
    "type": "object",
    "properties": {
      "type": {"type": "string", "enum": ["tool_use"]},
      "id": {"type": "string"},
      "name": {"type": "string", "enum": ["read", "write", "patch", "execute"]},
      "input": {"type": "object"}
    },
    "required": ["type", "name", "input"]
  })");
  return schema;
}

std::string StripMarkdownFences(std::string_view text) {
  // Standalone fence line: optional whitespace, ```, optional language tag,
  // nothing else
  static const std::regex kStandaloneFence(R"(^[ \t]*```[a-zA-Z]*[ \t]*$)",
                                           std::regex::optimize);
  std::string result;
  result.reserve(text.size());
  std::istringstream stream{std::string(text)};
  std::string line;
  while (std::getline(stream, line)) {
    if (std::regex_match(line, kStandaloneFence)) continue;
    // Strip inline triple-backtick wrappers: ```content``` -> content
    size_t pos = 0;
    while ((pos = line.find("```", pos)) != std::string::npos) {
      line.erase(pos, 3);
    }
    result += line;
    result += '\n';
  }
  return result;
}

// Collect tool_use blocks from a parsed JSON value (object, array, or
// Anthropic message with a "content" array).
void CollectToolUseBlocks(const json& j, std::vector<json>& out) {
  if (j.is_object()) {
    if (j.value("type", "") == "tool_use") {
      out.push_back(j);
    } else if (j.contains("content") && j["content"].is_array()) {
      for (const auto& block : j["content"]) {
        if (block.is_object() && block.value("type", "") == "tool_use")
          out.push_back(block);
      }
    }
  } else if (j.is_array()) {
    for (const auto& elem : j) {
      if (elem.is_object() && elem.value("type", "") == "tool_use")
        out.push_back(elem);
    }
  }
}

// Find the closing '}' for an opening '{' at position start, respecting
// nesting and JSON string literals. Returns npos if unbalanced.
size_t FindMatchingBrace(const std::string& text, size_t start) {
  int depth = 0;
  bool in_string = false;
  bool escape_next = false;
  for (size_t i = start; i < text.size(); ++i) {
    char c = text[i];
    if (escape_next) {
      escape_next = false;
      continue;
    }
    if (c == '\\' && in_string) {
      escape_next = true;
      continue;
    }
    if (c == '"') {
      in_string = !in_string;
      continue;
    }
    if (in_string) continue;
    if (c == '{')
      ++depth;
    else if (c == '}') {
      --depth;
      if (depth == 0) return i;
    }
  }
  return std::string::npos;
}

// Scan text for brace-balanced JSON object substrings, parse each one.
std::vector<json> ExtractJsonObjects(const std::string& text) {
  std::vector<json> objects;
  size_t pos = 0;
  while (pos < text.size()) {
    pos = text.find('{', pos);
    if (pos == std::string::npos) break;
    size_t end = FindMatchingBrace(text, pos);
    if (end == std::string::npos) {
      ++pos;
      continue;
    }
    auto candidate = text.substr(pos, end - pos + 1);
    try {
      auto j = json::parse(candidate);
      if (j.is_object() || j.is_array()) {
        objects.push_back(std::move(j));
        pos = end + 1;
        continue;
      }
    } catch (...) {
    }
    ++pos;
  }
  return objects;
}

std::vector<json> ExtractToolUseBlocks(std::string_view model_output) {
  // Quick check: if "tool_use" doesn't appear anywhere, skip all parsing
  if (model_output.find("tool_use") == std::string_view::npos) {
    return {};
  }

  std::string cleaned = StripMarkdownFences(model_output);
  std::vector<json> tool_blocks;

  // Try parsing the whole string as a single JSON value first
  try {
    json parsed = json::parse(cleaned);
    CollectToolUseBlocks(parsed, tool_blocks);
  } catch (...) {
  }

  // Always scan for embedded JSON objects if the whole-string parse
  // didn't find tool blocks (prompt is typically natural language with
  // JSON fragments inside)
  if (tool_blocks.empty()) {
    auto objects = ExtractJsonObjects(cleaned);
    for (const auto& obj : objects) {
      CollectToolUseBlocks(obj, tool_blocks);
    }
  }

  return tool_blocks;
}

std::string ValidateToolBlock(const json& block) {
  if (std::string err = cil::ValidateJSONSchema(GetToolUseBlockSchema(), block);
      !err.empty())
    return err;

  const std::string name = block["name"].get<std::string>();
  const json& input_schema = kToolInputSchemas.at(name);
  if (input_schema.empty()) return "Unknown tool: " + name;

  return cil::ValidateJSONSchema(input_schema, block["input"]);
}

using cil::infer::ToolDispatchResult;
using cil::infer::ToolResult;

std::string DumpSanitized(const json& j) {
  return j.dump(-1, ' ', /*ensure_ascii=*/false,
                json::error_handler_t::replace);
}

ToolResult MakeErrorResult(std::string message) {
  json j;
  j["error"] = std::move(message);
  return ToolResult{false, DumpSanitized(j)};
}

struct ShellResult {
  int exit_code;
  std::string output;  // combined stdout+stderr (commands append " 2>&1")
};

// Runs a fully-formed shell command, capturing its combined output. Returns
// nullopt only if the pipe could not be opened.
std::optional<ShellResult> RunShell(const std::string& full_cmd) {
  std::array<char, 256> buffer;
#ifdef _WIN32
  FILE* pipe = _popen(full_cmd.c_str(), "r");
#else
  FILE* pipe = popen(full_cmd.c_str(), "r");
#endif
  if (!pipe) return std::nullopt;

  std::string output;
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) !=
         nullptr) {
    output += buffer.data();
  }
#ifdef _WIN32
  int rc = _pclose(pipe);
#else
  int rc = pclose(pipe);
#endif
  return ShellResult{rc, std::move(output)};
}

ToolResult ToolRead(const json& input, const fs::path& sandbox_dir,
                    const log4cxx::LoggerPtr& log) {
  const std::string path = input["path"].get<std::string>();
  const fs::path file_path = sandbox_dir / path;
  std::ifstream file(file_path, std::ios::binary);
  if (!file.is_open()) {
    std::string msg = "Failed to open file: " + file_path.string();
    LOG4CXX_ERROR(log, msg);
    return MakeErrorResult(std::move(msg));
  }
  std::ostringstream ss;
  ss << file.rdbuf();
  json j;
  j["content"] = ss.str();
  return ToolResult{true, DumpSanitized(j)};
}

ToolResult ToolWrite(const json& input, const fs::path& sandbox_dir,
                     const log4cxx::LoggerPtr& log) {
  const std::string path = input["path"].get<std::string>();
  const std::string content = input["content"].get<std::string>();

  const fs::path file_path = sandbox_dir / path;
  if (file_path.has_parent_path()) {
    std::error_code ec;
    fs::create_directories(file_path.parent_path(), ec);
    if (ec) {
      std::string msg = "Failed to create directories: " + ec.message();
      LOG4CXX_ERROR(log, msg);
      return MakeErrorResult(std::move(msg));
    }
  }

  std::ofstream file(file_path, std::ios::binary);
  if (!file.is_open()) {
    std::string msg = "Failed to open file for writing: " + file_path.string();
    LOG4CXX_ERROR(log, msg);
    return MakeErrorResult(std::move(msg));
  }
  file.write(content.data(), content.size());
  return ToolResult{true, R"({"success":true})"};
}

ToolResult ToolPatch(const json& input, const fs::path& sandbox_dir,
                     const log4cxx::LoggerPtr& log) {
  const std::string path = input["path"].get<std::string>();
  const std::string diff = input["diff"].get<std::string>();

  fs::path patch_file = sandbox_dir / path;
  patch_file = patch_file.replace_extension(".diff");

  {
    std::ofstream file(patch_file, std::ios::binary);
    if (!file.is_open()) {
      std::string msg = "Failed to create patch file: " + patch_file.string();
      LOG4CXX_ERROR(log, msg);
      return MakeErrorResult(std::move(msg));
    }
    file.write(diff.data(), diff.size());
  }

#ifdef _WIN32
  std::string cmd =
      "git apply --unsafe-paths \"" + patch_file.string() + "\" 2>&1";
#else
  std::string cmd = "patch -p0 < \"" + patch_file.string() + "\" 2>&1";
#endif

  const std::optional<ShellResult> shell = RunShell(cmd);
  if (!shell.has_value()) {
    fs::remove(patch_file);
    std::string msg = "Failed to execute patch command: " + cmd;
    LOG4CXX_ERROR(log, msg);
    return MakeErrorResult(std::move(msg));
  }
  const std::string& output = shell->output;
  const int rc = shell->exit_code;

  fs::remove(patch_file);

  json j;
  j["output"] = output;
  if (rc != 0) {
    j["error"] = "Patch failed";
    return ToolResult{false, DumpSanitized(j)};
  }
  j["success"] = true;
  return ToolResult{true, DumpSanitized(j)};
}

ToolResult ToolExecute(const json& input, const fs::path& sandbox_dir,
                       const log4cxx::LoggerPtr& log) {
  if (static bool warned = false; !warned) {
    LOG4CXX_WARN(log, "Tool 'execute' runs arbitrary shell commands");
    warned = true;
  }

  const std::string command = input["command"].get<std::string>();
  const std::string cwd =
      input.value("cwd", sandbox_dir.parent_path().string());

  std::string full_cmd;
  if (!cwd.empty()) {
#ifdef _WIN32
    full_cmd = "cd /d \"" + cwd + "\" && " + command;
#else
    full_cmd = "cd \"" + cwd + "\" && " + command;
#endif
  } else {
    full_cmd = command;
  }
  full_cmd += " 2>&1";

  const std::optional<ShellResult> shell = RunShell(full_cmd);
  if (!shell.has_value()) {
    return MakeErrorResult("Failed to execute command");
  }

  json result;
  result["exit_code"] = shell->exit_code;
  result["output"] = shell->output;
  return ToolResult{shell->exit_code == 0, DumpSanitized(result)};
}

ToolDispatchResult DispatchTool(const json& block, const fs::path& sandbox_dir,
                                const log4cxx::LoggerPtr& log) {
  const std::string name = block["name"].get<std::string>();
  const json& input = block["input"];

  auto start = std::chrono::high_resolution_clock::now();
  ToolResult result;
  if (name == "read") {
    result = ToolRead(input, sandbox_dir, log);
  } else if (name == "write") {
    result = ToolWrite(input, sandbox_dir, log);
  } else if (name == "patch") {
    result = ToolPatch(input, sandbox_dir, log);
  } else if (name == "execute") {
    result = ToolExecute(input, sandbox_dir, log);
  } else {
    result = MakeErrorResult("Unknown tool: " + name);
  }
  auto end = std::chrono::high_resolution_clock::now();
  return ToolDispatchResult{
      std::move(result),
      std::chrono::duration_cast<MillisecDuration>(end - start)};
}

}  // namespace

std::vector<cil::infer::ToolDispatchResult> cil::infer::RunTools(
    std::string_view decoded_model_output, std::string_view assets_dir,
    const log4cxx::LoggerPtr& log) {
  LOG4CXX_DEBUG(
      log, "Running tools on output: " + std::string(decoded_model_output));
  auto tool_blocks = ExtractToolUseBlocks(decoded_model_output);
  std::vector<ToolDispatchResult> results;
  if (tool_blocks.empty()) {
    return results;
  }

  fs::path sandbox_dir = fs::path(assets_dir) / kSandboxDirectory;
  if (!fs::is_directory(sandbox_dir)) {
    sandbox_dir = fs::path(kSandboxDirectory);
  }

  results.reserve(tool_blocks.size());

  for (const auto& block : tool_blocks) {
    std::string id = block.value("id", "<no-id>");
    std::string name = block.value("name", "<unknown>");

    LOG4CXX_DEBUG(log, "Tool call: " << name << " (id=" << id << ")");

    if (std::string validation_error = ValidateToolBlock(block);
        !validation_error.empty()) {
      LOG4CXX_WARN(log, "Tool validation failed for "
                            << name << " (id=" << id
                            << "): " << validation_error);
      results.emplace_back(
          MakeErrorResult("Validation failed: " + validation_error),
          MillisecDuration{0});
      break;
    }

    ToolDispatchResult dispatch =
        DispatchTool(block, sandbox_dir, log);
    LOG4CXX_DEBUG(
        log, "Tool " << name << " result: " << dispatch.result.json_result);
    const bool failed = !dispatch.result.success;
    if (failed) {
      LOG4CXX_ERROR(log, "Tool " << name << " (id=" << id << ") failed: "
                                 << dispatch.result.json_result);
    }
    results.emplace_back(std::move(dispatch));
    if (failed) {
      break;
    }
  }

  return results;
}
