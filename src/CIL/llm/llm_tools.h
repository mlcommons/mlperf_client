#pragma once

#include <log4cxx/logger.h>

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include "llm_inference.h"

namespace cil::infer {

// Result of executing a single tool: a success flag and a JSON-encoded
// payload (either the tool's normal output or an error description).
struct ToolResult {
  bool success = false;
  std::string json_result;
};

// Result of dispatching a single tool: the tool's ToolResult plus the
// wall-clock duration of just that tool's execution.
struct ToolDispatchResult {
  ToolResult result;
  LLMInference::MillisecDuration duration{0};
};

// Parse model output for Anthropic-style tool_use JSON blocks, validate
// each against the tool schemas, and execute them in order.
//
// Execution stops at the first failing tool (validation or runtime
// failure); subsequent tool blocks are not dispatched. The returned
// vector contains one ToolDispatchResult per tool that was actually
// dispatched (including the failing one, if any). An empty vector means
// no tool_use blocks were found in the model output.
//
// Sandbox files resolve to `<assets_dir>/tools_sandbox/`, falling back to
// an exe-adjacent `tools_sandbox/` if that subdir is absent.
std::vector<ToolDispatchResult> RunTools(std::string_view decoded_model_output,
                                         std::string_view assets_dir,
                                         const log4cxx::LoggerPtr& log);

}  // namespace cil::infer
