/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "llm_inference.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <filesystem>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../native_qnn_config.h"
#include "llm/llm_config.h"
#include "math_utils.h"
#include "windows.h"

using namespace cil::infer;
namespace fs = std::filesystem;

std::vector<uint32_t> g_output_data_;

namespace cil {
namespace IHV {
namespace infer {

std::vector<std::string_view> output_texts;
std::vector<std::vector<uint32_t>> token_ids;

namespace fs = std::filesystem;

namespace {

// Translate a shell glob ("name_*.bin") to a fully anchored regex pattern.
// Only `*` is treated as a wildcard; all other regex specials are escaped.
std::string GlobToRegex(const std::string& glob) {
  std::string regex = "^";
  for (char c : glob) {
    switch (c) {
      case '*': regex += ".*"; break;
      case '.': case '+': case '(': case ')': case '[': case ']':
      case '{': case '}': case '?': case '|': case '^': case '$':
      case '\\':
        regex += '\\';
        regex += c;
        break;
      default: regex += c;
    }
  }
  regex += '$';
  return regex;
}

// Walk a JSON value; for every string scalar, apply `subst`. If `subst`
// returns a non-null JSON value, the scalar is replaced wholesale. Supports
// both interpolation (returning a string) and array/number replacement.
void WalkAndSubstitute(
    nlohmann::json& node,
    const std::function<nlohmann::json(const std::string&)>& subst) {
  if (node.is_object()) {
    for (auto& [_, v] : node.items()) WalkAndSubstitute(v, subst);
  } else if (node.is_array()) {
    for (auto& v : node) WalkAndSubstitute(v, subst);
  } else if (node.is_string()) {
    auto replaced = subst(node.get<std::string>());
    if (!replaced.is_null()) node = std::move(replaced);
  }
}

}  // namespace

std::vector<std::string> LlmInference::listNpuFiles(
    const std::string& model_folder_, const std::string& model_name_) {
  try {
    const fs::path folder = model_folder_;

    if (!fs::exists(folder) || !fs::is_directory(folder)) {
      error_message_ = "Folder not found or not a directory: ";
      error_message_ += folder.string();
      logger_(cil::LogLevel::kFatal, error_message_);
      return {};
    }

    if (npu_bin_pattern_.empty()) {
      error_message_ =
          "npu_bin_pattern not set; cannot enumerate NPU bins in " +
          model_folder_;
      logger_(cil::LogLevel::kFatal, error_message_);
      return {};
    }

    const std::regex pattern(GlobToRegex(npu_bin_pattern_));
    std::vector<std::string> files;
    for (const auto& entry : fs::directory_iterator(folder)) {
      if (!entry.is_regular_file()) continue;
      const std::string name = entry.path().filename().string();
      if (std::regex_match(name, pattern)) {
        files.push_back(model_folder_ + "/" + name);
      }
    }

    if (files.empty()) {
      error_message_ = "No NPU bins matched glob '" + npu_bin_pattern_ +
                       "' in " + model_folder_;
      logger_(cil::LogLevel::kFatal, error_message_);
      return {};
    }

    std::sort(files.begin(), files.end());
    return files;

  } catch (const std::exception& ex) {
    logger_(cil::LogLevel::kFatal, ex.what());
    error_message_ = ex.what();
  }
  return {};
}

unsigned long long checkDeviceRAM() {
  MEMORYSTATUSEX statex;
  statex.dwLength = sizeof(statex);
  if (GlobalMemoryStatusEx(&statex)) {
    return (statex.ullTotalPhys / (1024 * 1024 * 1024));
  } else {
    return 0;
  }
}

LlmInference::LlmInference(
    const std::string& model_path, const std::string& model_name,
    const NativeQnnExecutionProviderSettings& ep_settings, cil::Logger logger)
    : BaseInference(model_path, model_name, ep_settings, logger),
      model_path_(std::filesystem::absolute(model_path).string()),
      model_name_(model_name) {
  const auto model_parent_path =
      std::filesystem::absolute(model_path).parent_path();
  model_folder_ =
      fs::current_path().append(model_parent_path.string()).string();
  device_type_ = ep_settings.GetDeviceType();
  is_agentic_ = ep_settings.GetIsAgentic();

  unsigned long long ramSize = checkDeviceRAM();

  // The system size should be of size 32GB.
  if (ramSize < 30) {
    logger_(cil::LogLevel::kFatal,
            "Failed to create dialog. Use at least 32GB RAM device.");
    error_message_ = "Failed to create dialog. Use at least 32GB RAM device.";
    return;
  }
}

void LlmInference::FillConfigString(const nlohmann::json& model_config) {
  // 1. Pick the per-device config file.
  std::string config_filename;
  if (device_type_ == "NPU") {
    config_filename = "native_qnn_config_npu.json";
  } else if (device_type_ == "NPU_CPU") {
    config_filename = "native_qnn_config_npu_cpu.json";
  } else {
    error_message_ = "Unsupported device_type '" + device_type_ +
                     "'; expected NPU or NPU_CPU";
    logger_(cil::LogLevel::kFatal, error_message_);
    configString_ = "";
    return;
  }

  const fs::path config_path =
      fs::path(model_folder_) / config_filename;
  std::ifstream in(config_path);
  if (!in) {
    error_message_ = config_filename + " not found in " + model_folder_;
    logger_(cil::LogLevel::kFatal, error_message_);
    configString_ = "";
    return;
  }

  // 2. Parse it.
  nlohmann::json file_doc;
  try {
    in >> file_doc;
  } catch (const std::exception& e) {
    error_message_ = std::string("Failed to parse ") + config_filename +
                     ": " + e.what();
    logger_(cil::LogLevel::kFatal, error_message_);
    configString_ = "";
    return;
  }

  if (!file_doc.contains("npu_bin_pattern") ||
      !file_doc["npu_bin_pattern"].is_string() ||
      !file_doc.contains("dialog") || !file_doc["dialog"].is_object()) {
    error_message_ = config_filename +
                     " is missing required 'npu_bin_pattern' or 'dialog'";
    logger_(cil::LogLevel::kFatal, error_message_);
    configString_ = "";
    return;
  }

  npu_bin_pattern_ = file_doc["npu_bin_pattern"].get<std::string>();

  // 3. Glob the NPU bin files now that we know the pattern.
  npu_bins_ = listNpuFiles(model_folder_, model_name_);
  if (npu_bins_.empty()) {
    configString_ = "";
    return;
  }

  // 4. Pull out the dialog and substitute placeholders.
  nlohmann::json dialog = file_doc["dialog"];
  const auto npu_bins_json = nlohmann::json(npu_bins_);

  auto subst = [&](const std::string& s) -> nlohmann::json {
    if (s == "${NPU_BINS}") return npu_bins_json;
    std::string out = s;
    auto replace_all = [&](const std::string& key, const std::string& val) {
      size_t pos = 0;
      while ((pos = out.find(key, pos)) != std::string::npos) {
        out.replace(pos, key.size(), val);
        pos += val.size();
      }
    };
    replace_all("${MODEL_FOLDER}", model_folder_);
    replace_all("${MODEL_PATH}", model_path_);
    if (out == s) return nullptr;
    return out;
  };
  WalkAndSubstitute(dialog, subst);

  // 5. Inject per-prompt fields from the prompt JSON's model_config.
  try {
    dialog["max-num-tokens"] = model_config["search"]["max_length"];
    // Agentic conversations need the full configured context to hold the
    // accumulated multi-turn history, so they run uncapped. Non-agentic runs
    // keep the 4096-token cap.
    const int context_length =
        cil::infer::LlmConfig::GetContextLength(model_config);
    dialog["context"]["size"] =
        is_agentic_ ? context_length : std::min(4096, context_length);
    auto& sampler = dialog["sampler"];
    // Skip the fixed sampler for greedy (temp==0) runs like MMLU.
    if (model_name_ == "phi4-mini" &&
        model_config["search"].value("temperature", 0.0) != 0.0) {
      // Phi-4-mini requires fixed sampler settings for correct generation.
      sampler["temp"] = 0.8;
      sampler["top-k"] = 40;
      sampler["top-p"] = 0.95;
    } else {
      sampler["temp"] = model_config["search"]["temperature"];
      sampler["top-k"] = model_config["search"]["top_k"];
      sampler["top-p"] = model_config["search"].value("top_p", 0.95);
    }

    // Set the CPU (QnnGenAiTransformer) engine's thread count to the machine's
    // logical core count. In NPU_CPU mode "engine" is an array holding both the
    // HTP (NPU) engine and the CPU engine; pick the QnnGenAiTransformer one.
    const unsigned int n_threads = std::thread::hardware_concurrency();
    if (dialog.contains("engine") && dialog["engine"].is_array()) {
      for (auto& cpu_engine : dialog["engine"]) {
        if (cpu_engine.value("backend", nlohmann::json::object())
                .value("type", "") == "QnnGenAiTransformer") {
          cpu_engine["n-threads"] = n_threads;
        }
      }
    }
  } catch (const std::exception& e) {
    error_message_ = std::string("Failed to apply prompt model_config to ") +
                     config_filename + ": " + e.what();
    logger_(cil::LogLevel::kFatal, error_message_);
    configString_ = "";
    return;
  }

  // 6. Wrap and emit. Keep the diagnostic dump.
  nlohmann::json full;
  full["dialog"] = dialog;
  {
    std::ofstream debug_f(model_folder_ + "/generated_genie_config.json");
    if (debug_f) debug_f << full.dump(2);
  }
  configString_ = full.dump();
}

void LlmInference::Init(const nlohmann::json& model_config) {
  BaseInference::Init();
  if (error_message_ != "") return;
  if (dialogHandle == nullptr) {
    FillConfigString(model_config);
    if (configString_ == "") {
      logger_(cil::LogLevel::kFatal, "Device Type Or Model Not Supported");
    }

    GenieDialogConfig_Handle_t configHandle = nullptr;

    // Create DialogConfig
    Genie_Status_t config_status =
        GenieDialogConfig_createFromJson(configString_.c_str(), &configHandle);
    if (GENIE_STATUS_SUCCESS != config_status || configHandle == nullptr) {
      logger_(cil::LogLevel::kFatal, "Failed to create config");
      error_message_ = "Failed to create config";
      return;
    }

    // This is a quick fix for Genie, which somehow cannot locate some
    // libraries, even if it is in the same directory

    std::vector<std::string> extra_cpu_deps;
    if (device_type_ == "NPU_CPU") {
      extra_cpu_deps = {"Genie.dll",
                        "QnnHtp.dll",
                        "QnnHtpNetRunExtensions.dll",
                        "QnnSystem.dll",
                        "QnnGenAiTransformerModel.dll",
                        "QnnGenAiTransformer.dll"};
    } else if (device_type_ == "NPU") {
      extra_cpu_deps = {"Genie.dll", "QnnHtp.dll", "QnnHtpNetRunExtensions.dll",
                        "QnnSystem.dll"};
    } else {
      extra_cpu_deps = {
          "Genie.dll", "QnnSystem.dll",
          "QnnGenAiTransformerModel.dll", "QnnGenAiTransformer.dll"};
    }

    for (auto& dep : extra_cpu_deps) {
      if (genie_extra_modules_.contains(dep)) {
        continue;
      }
      HMODULE dep_module = LoadLibraryA(dep.c_str());
      if (dep_module == nullptr) {
        logger_(cil::LogLevel::kFatal, "Failed to load " + dep);
        error_message_ = "Failed to load " + dep;
        return;
      }
      genie_extra_modules_[dep] = dep_module;
    }

    // Create GenieDialog
    Genie_Status_t create_status =
        GenieDialog_create(configHandle, &dialogHandle);
    if (GENIE_STATUS_SUCCESS != create_status) {
      logger_(cil::LogLevel::kFatal, "Failed to create dialog");
      error_message_ = "Failed to create dialog";
      return;
    }

    Genie_Status_t free_config_status = GenieDialogConfig_free(configHandle);
    if (GENIE_STATUS_SUCCESS != free_config_status) {
      logger_(cil::LogLevel::kFatal, "Failed to free config");
      error_message_ = "Failed to free config";
      return;
    }
  }
}

void LlmInference::tokenToTokenCallback(
    const uint32_t* token, const uint32_t tokensLength,
    const GenieDialog_SentenceCode_t sentenceCode, const void*) {
  switch (sentenceCode) {
    case GENIE_DIALOG_SENTENCE_COMPLETE: {
      break;
    }
    case GENIE_DIALOG_SENTENCE_BEGIN: {
      break;
    }
    case GENIE_DIALOG_SENTENCE_CONTINUE: {
      break;
    }
    case GENIE_DIALOG_SENTENCE_END: {
      break;
    }
    case GENIE_DIALOG_SENTENCE_ABORT: {
      break;
    }
    default: {
      break;
    }
  }
  if (token) {
    for (uint32_t i = 0; i < tokensLength; i++) {
      g_output_data_.push_back(token[i]);
      token_callback_global(token[i]);
    }
  }
}

void LlmInference::Run(API_IHV_Infer_t& api,
                       std::function<void(uint32_t)> token_callback) {
  GenieDialog_TokenQueryCallback_t tokenCallback{nullptr};
  uint32_t tokensSize = (uint32_t)api.io_data->input_size;
  if (tokensSize > 0) {
    tokenCallback = tokenToTokenCallback;
  }
  token_callback_global = token_callback;
  Genie_Status_t query_status = GenieDialog_tokenQuery(
      dialogHandle, (uint32_t*)api.io_data->input,
      (uint32_t)api.io_data->input_size,
      GenieDialog_SentenceCode_t::GENIE_DIALOG_SENTENCE_COMPLETE, tokenCallback,
      nullptr);
  if (GENIE_STATUS_SUCCESS != query_status) {
    logger_(cil::LogLevel::kFatal, "Failed to query dialog");
    error_message_ = "Failed to query dialog";
    return;
  }
  api.io_data->output = g_output_data_.data();
  api.io_data->output_size = g_output_data_.size();
}

void LlmInference::Reset() {
  Genie_Status_t reset_status = GenieDialog_reset(dialogHandle);

  if (GENIE_STATUS_SUCCESS != reset_status) {
    logger_(cil::LogLevel::kFatal, "Failed to reset Genie Dialog");
    error_message_ = "Failed to reset Genie Dialog";
    return;
  }
  g_output_data_.clear();
  BaseInference::Reset();
}

void LlmInference::Deinit() {
  if (GENIE_STATUS_SUCCESS != GenieDialog_free(dialogHandle)) {
    logger_(cil::LogLevel::kFatal, "Failed to free Genie");
    error_message_ = "Failed to free Genie";
    return;
  } else {
    dialogHandle = nullptr;
  }

  for (auto& [dep, dep_module] : genie_extra_modules_) {
    if (FreeLibrary(dep_module) == 0) {
      logger_(cil::LogLevel::kFatal, "Failed to free " + dep);
      error_message_ = "Failed to free " + dep;
      return;
    }
  }
  genie_extra_modules_.clear();
  g_output_data_.clear();
  BaseInference::Deinit();
}
}  // namespace infer
}  // namespace IHV
}  // namespace cil
