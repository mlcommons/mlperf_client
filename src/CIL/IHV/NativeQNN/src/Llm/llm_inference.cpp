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
#include <vector>

#include "../native_qnn_config.h"
#include "llm/llama_config.h"
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

    // Regex
    std::regex pattern;
    if (model_name_ == "llama3") {
      pattern = std::regex(R"(^llama3_npu_.*\.bin$)");
    } else if (model_name_ == "phi3.5") {
      pattern = std::regex(R"(^phi3_5_npu_.*\.bin$)");
    } else if (model_name_ == "phi4") {
      pattern = std::regex(R"(^phi4_npu_.*\.bin$)");
    } else {
      logger_(cil::LogLevel::kFatal, "Model name is not recognized");
      error_message_ = "Model name is not recognized";
      return {};
    }
    std::vector<std::string> files;

    for (const auto& entry : fs::directory_iterator(folder)) {
      if (!entry.is_regular_file()) continue;

      const std::string name = entry.path().filename().string();
      if (std::regex_match(name, pattern)) {
        files.push_back(model_folder_ + "/" + name);
      }
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
  npu_bins_ = listNpuFiles(model_folder_, model_name_);
  if (model_name_ == "llama3") {
    configString_ = formConfigStringLlama3_SDX_Elite(
        model_config, device_type_, model_folder_, model_path_, npu_bins_);
  } else if (model_name_ == "phi3.5") {
    configString_ = formConfigStringPhi3_5_SDX_Elite(
        model_config, device_type_, model_folder_, model_path_, npu_bins_);
  } else if (model_name_ == "phi4") {
    configString_ = formConfigStringPhi4_SDX_Elite(
        model_config, device_type_, model_folder_, model_path_, npu_bins_);
  } else {
    configString_ = "";
  }
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
                        "QnnGenAiTransformerCpuOpPkg.dll",
                        "QnnGenAiTransformerModel.dll",
                        "QnnGenAiTransformer.dll"};
    } else if (device_type_ == "NPU") {
      extra_cpu_deps = {"Genie.dll", "QnnHtp.dll", "QnnHtpNetRunExtensions.dll",
                        "QnnSystem.dll"};
    } else {
      extra_cpu_deps = {
          "Genie.dll", "QnnSystem.dll", "QnnGenAiTransformerCpuOpPkg.dll",
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
