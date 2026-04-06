#include "benchmark_logger.h"

#include <log4cxx/fileappender.h>
#include <log4cxx/logger.h>

#include <fstream>
#include <set>

#include "benchmark/runner.h"
#include "benchmark_result.h"
#include "system_info_provider.h"
#include "utils.h"

using namespace log4cxx;

LoggerPtr loggerResults(Logger::getLogger("Results"));
LoggerPtr loggerDisplayResults(Logger::getLogger("DisplayRecentResults"));
LoggerPtr loggerBenchmarkLogger(Logger::getLogger("BenchmarkValidationLogger"));

namespace cil {

static std::string DumpDouble(double value) {
  nlohmann::json j = value;
  return j.dump();
}

static double RoundDouble(double value, int places) {
  double factor = 1.0;
  for (int i = 0; i < places; ++i) factor *= 10.0;
  return std::round(value * factor) / factor;
}

static std::string DumpRoundedDouble(double value, int places) {
  return DumpDouble(RoundDouble(value, places));
}

static std::string Indent(int n) {
  const int indent_size = 4;
  return std::string(n * indent_size, ' ');
}

static std::vector<double> RoundedVector(const std::vector<double>& input,
                                         int places) {
  std::vector<double> rounded;
  for (double value : input) {
    rounded.push_back(RoundDouble(value, places));
  }
  return rounded;
}

namespace {
struct DataFileResults {
  std::string scenario_name;
  std::string model_name;
  std::string data_file_name;
  int iterations = 0;
  int iterations_warmup = 0;

  bool operator==(const DataFileResults& other) const {
    return data_file_name == other.data_file_name;
  }
};
}  // namespace

BenchmarkLogger::BenchmarkLogger(const std::string& output_dir)
    : filename_(output_dir.empty() ? "Logs/results.json"
                                   : output_dir + "/results.json"),
      system_info_provider_(SystemInfoProvider::Instance()) {
  if (!output_dir.empty()) {
    auto appender = log4cxx::cast<log4cxx::FileAppender>(
        loggerResults->getAppender("ResultsFileAppender"));
    appender->setFile(filename_);
    log4cxx::helpers::Pool pool;
    appender->activateOptions(pool);
  }
}

bool ValidateUTF8inJSONObj(const nlohmann::json& json_obj) {
  // this will be a recursive function to check all the strings in the json_obj
  if (json_obj.is_string()) {
    std::string str = json_obj.get<std::string>();
    if (!utils::IsUtf8(str)) {
      LOG4CXX_ERROR(loggerBenchmarkLogger,
                    "Invalid UTF-8 string found in JSON object: " << str);
      return false;
    }
  } else if (json_obj.is_object()) {
    for (auto it = json_obj.begin(); it != json_obj.end(); ++it) {
      if (!ValidateUTF8inJSONObj(it.value())) {
        return false;
      }
    }
  } else if (json_obj.is_array()) {
    for (const auto& element : json_obj) {
      if (!ValidateUTF8inJSONObj(element)) {
        return false;
      }
    }
  }
  return true;
}

static std::string EscapeCsvField(const std::string& field) {
  bool needs_quotes = false;
  for (char c : field) {
    if (c == ',' || c == '"' || c == '\n' || c == '\r') {
      needs_quotes = true;
      break;
    }
  }

  if (!needs_quotes) return field;

  std::string out;
  out.reserve(field.size() + 2);
  out.push_back('"');
  for (char c : field) {
    if (c == '"') out.push_back('"');
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

static inline void WriteCsvRow(std::ofstream& out,
                               const std::vector<std::string>& row) {
  for (std::size_t i = 0; i < row.size(); ++i) {
    if (i) out << ',';
    out << EscapeCsvField(row[i]);
  }
  out << "\n";
}

static std::string FormatSystemInfo(const SystemInfo& si) {
  std::vector<std::string> parts;
  auto AddInfoFn = [&](const std::string& label, const std::string& value) {
    if (!value.empty()) parts.push_back(label + ": " + value);
  };

  AddInfoFn("CPU", cil::utils::FormatCPU(si.cpu_model, si.cpu_architecture));
  AddInfoFn("RAM", cil::utils::FormatMemory(si.ram));
  AddInfoFn("GPU", si.gpu_name);
  AddInfoFn("VRAM", cil::utils::FormatMemory(si.gpu_ram));
  AddInfoFn("OS", si.os_name);

  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) out += ";";
    out += parts[i];
  }
  return out;
}

BenchmarkLogger::~BenchmarkLogger() = default;

bool BenchmarkLogger::AppendBenchmarkResult(const BenchmarkResult& result) {
  BenchmarkResult res = result;

  const auto& cpu_info = system_info_provider_->GetCpuInfo();
  res.system_info.cpu_architecture = cpu_info.architecture;
  res.system_info.cpu_model = utils::CleanAndTrimString(cpu_info.model_name);

  const auto& gpu_info = system_info_provider_->GetGpuInfo();
#ifdef __APPLE__
  if (!gpu_info.empty()) {
    const auto& gpu = gpu_info.front();
    res.system_info.gpu_name = gpu.name;
    res.system_info.gpu_ram = gpu.dedicated_memory_size;
  }
#else
  std::string gpu_name = result.ep_configuration.value("device_name", "");
  if (auto it = std::ranges::find_if(
          gpu_info, [&](const auto& info) { return info.name == gpu_name; });
      it != gpu_info.end()) {
    res.system_info.gpu_name = it->name;
    res.system_info.gpu_ram = it->dedicated_memory_size;
  }
#endif

  res.system_info.ram = system_info_provider_->GetMemoryInfo().physical_total;
  res.system_info.os_name =
      utils::CleanAndTrimString(system_info_provider_->GetOsInfo().full_name);

  res.config_verified = config_verified_;

  nlohmann::ordered_json json_obj = BenchmarkResultToJson(res);
  bool validated = ValidateUTF8inJSONObj(json_obj);
  if (validated) {
    WriteResults(json_obj);
  } else {
    res.benchmark_success = false;
    res.error_message = "Invalid UTF-8 string found in output";
    LOG4CXX_ERROR(loggerBenchmarkLogger,
                  "Invalid UTF-8 string found in BenchmarkResult");
  }
  results_.push_back(res);
  return validated;
}

void BenchmarkLogger::DisplayAllResults() {
  if (results_.empty()) {
    LOG4CXX_INFO(loggerDisplayResults, "No benchmark results to display.");
    return;
  }

  std::vector<std::string> unique_models;
  std::vector<std::string> unique_assets;
  std::vector<DataFileResults> results_per_data_file;
  std::map<std::string, std::string> file_name_to_indexed_name;

  using BenchmarksPerEPConfig = std::map<std::string, BenchmarkResult>;
  using BenchmarksPerDeviceType = std::map<std::string, BenchmarksPerEPConfig>;
  using BenchmarksPerModel = std::map<std::string, BenchmarksPerDeviceType>;
  BenchmarksPerModel grouped_results;

  for (const auto& result : results_) {
    std::string model_key = result.model_name + " (" + result.model_path + ")";
    if (std::find(unique_models.begin(), unique_models.end(), model_key) ==
        unique_models.end()) {
      unique_models.push_back(model_key);
      file_name_to_indexed_name[model_key] =
          result.model_name + " (#" + std::to_string(unique_models.size()) +
          ")";
    }

    for (const auto& asset_file : result.asset_paths) {
      if (std::find(unique_assets.begin(), unique_assets.end(), asset_file) ==
          unique_assets.end()) {
        unique_assets.push_back(asset_file);
      }
    }

    for (size_t i = 0; i < result.data_file_names.size(); ++i) {
      DataFileResults data_result;
      data_result.scenario_name = result.scenario_name;
      data_result.model_name = result.model_name;

      data_result.data_file_name = result.data_file_names[i];
      data_result.iterations = result.iterations;
      data_result.iterations_warmup = result.iterations_warmup;

      auto it = std::find(results_per_data_file.begin(),
                          results_per_data_file.end(), data_result);
      if (it == results_per_data_file.end()) {
        it = results_per_data_file.insert(results_per_data_file.end(),
                                          data_result);
      }
    }
    std::string ep_key =
        result.execution_provider_name + result.ep_configuration.dump();
    auto mapped_name = file_name_to_indexed_name[model_key];
    grouped_results[mapped_name][result.device_type][ep_key] = result;
  }

  std::ostringstream oss;
  oss << "\nBenchmark Results " << utils::GetCurrentDateTimeString() << ":\n\n";

  oss << "== Application Version: " << application_version_string_ << " ==\n";
  if (!config_category_.empty()) {
    oss << "== Configuration is " << config_category_ << " ==\n\n";
  } else if (config_verified_) {
    oss << "== Configuration tested by MLCommons ==\n\n";
  } else {
    oss << "== Configuration NOT tested by MLCommons ==\n\n";
  }

  oss << "Model Variants:\n\n";
  for (int i = 0; i < unique_models.size(); i++) {
    oss << i + 1 << ". " << unique_models[i] << "\n";
  }

  oss << "\nShared Information:\n\n";
  oss << Indent(1) << "Assets:\n";
  for (const auto& asset : unique_assets) {
    oss << Indent(2) << asset << std::endl;
  }

  oss << Indent(1) << "Input Files:\n";
  for (const auto& results_data : results_per_data_file) {
    oss << Indent(2) << results_data.data_file_name << std::endl;
  }

  oss << Indent(1) << "Iterations: "
      << (results_per_data_file.empty()
              ? -1
              : results_per_data_file.front().iterations)
      << "\n";

  oss << Indent(1) << "WarmUp Iterations: "
      << (results_per_data_file.empty()
              ? -1
              : results_per_data_file.front().iterations_warmup)
      << "\n";

  oss << "\nExecution Summary:\n\n";
  for (const auto& res : grouped_results) {
    oss << "Model: " << res.first << ":\n";

    for (const auto res_of_device_type : res.second) {
      oss << Indent(1) << res_of_device_type.first << ":\n";
      std::vector<BenchmarkResult> res_vector_of_device_type =
          GetResults(res_of_device_type.second);
      for (const auto res_of_ep_config : res_vector_of_device_type) {
        const auto& exec_result = res_of_ep_config;
        oss << Indent(2) << exec_result.execution_provider_name << " "
            << exec_result.ep_configuration << ":\n";
        if (exec_result.benchmark_success) {
          if (exec_result.is_llm_benchmark) {
            const bool show_overall = config_verified_;
            if (show_overall) {
              auto it = exec_result.performance_results.find(
                  BenchmarkResult::kLLMOverallCategory);
              if (it != exec_result.performance_results.end()) {
                auto overall_perf_results = it->second;
                oss << Indent(3) << "OVERALL:\t"
                    << "Geomean Time to First Token: "
                    << DumpRoundedDouble(
                           overall_perf_results.time_to_first_token_duration, 3)
                    << " s, ";
                if (std::fabs(overall_perf_results.token_generation_rate) >
                    std::numeric_limits<float>::epsilon()) {
                  oss << "Geomean 2nd+ Token Generation Rate: "
                      << DumpRoundedDouble(
                             overall_perf_results.token_generation_rate, 2)
                      << " tokens/s, ";
                } else {
                  oss << "Token Generation Rate: N/A, ";
                }
                oss << "Avg Input Tokens: "
                    << overall_perf_results.average_input_tokens
                    << ", Avg Generated Tokens: "
                    << overall_perf_results.average_generated_tokens << "\n";
              }
            }
            for (const auto& [category, perf_result] :
                 exec_result.performance_results) {
              if (category == "Overall") {
                continue;
              }
              oss << Indent(3) << category << ":\tAvg Time to First Token: "
                  << DumpRoundedDouble(perf_result.time_to_first_token_duration,
                                       3)
                  << " s, ";

              if (std::fabs(perf_result.token_generation_rate) > 1e-5) {
                oss << "Avg 2nd+ Token Generation Rate: "
                    << DumpRoundedDouble(perf_result.token_generation_rate, 2)
                    << " tokens/s, ";
              } else {
                oss << "Token Generation Rate: N/A, ";
              }
              oss << "Avg Input Tokens: " << perf_result.average_input_tokens
                  << ", Avg Generated Tokens: "
                  << perf_result.average_generated_tokens << "\n";
            }
          } else {
            oss << Indent(3) << ", Avg Inference Duration: "
                << DumpRoundedDouble(exec_result.duration, 9) << " s\n";
          }
        }
        if (!exec_result.error_message.empty() ||
            !exec_result.ep_error_messages.empty()) {
          if (!exec_result.error_message.empty()) {
            oss << "\n"
                << Indent(3) << "Error: "
                << utils::CleanErrorMessageFromStaticPaths(
                       exec_result.error_message);
            if (!exec_result.ep_error_messages.empty()) {
              oss << ",\n" << Indent(3) << exec_result.ep_error_messages;
            } else {
              oss << ".\n";
            }
          } else {
            oss << Indent(3) << "Error: " << exec_result.ep_error_messages;
          }
          std::string scenario_logs_location;
          if (exec_result.scenario_name == "Llama2") {
            scenario_logs_location = "Logs/llama2_executor.log";
          } else if (exec_result.scenario_name == "Llama3") {
            scenario_logs_location = "Logs/llama3_executor.log";
          } else if (exec_result.scenario_name == "Phi3.5") {
            scenario_logs_location = "Logs/phi3_5_executor.log";
          }
          oss << Indent(3) << "Check Logs/error.log"
              << (!scenario_logs_location.empty()
                      ? (" and " + scenario_logs_location)
                      : "")
              << " for more details.\n";
        }
      }
    }
    oss << std::endl;
  }

  LOG4CXX_INFO(loggerDisplayResults, oss.str());
}

void BenchmarkLogger::Clear() { results_.clear(); }

void BenchmarkLogger::SetApplicationVersionString(
    std::string application_version_string) {
  application_version_string_ = application_version_string;
}

void BenchmarkLogger::WriteResults(const nlohmann::ordered_json& json_obj) {
  LOG4CXX_INFO(loggerResults, json_obj.dump());
}

void BenchmarkLogger::SetConfigVerified(bool is_verified) {
  config_verified_ = is_verified;
}

void BenchmarkLogger::SetConfigCategory(std::string_view category) {
  config_category_ = category;
}

const std::vector<BenchmarkResult>& BenchmarkLogger::GetResults() const {
  return results_;
}

nlohmann::ordered_json BenchmarkLogger::BenchmarkResultToJson(
    const BenchmarkResult& result) {
  nlohmann::ordered_json json_obj;
  json_obj["BuildVer"] = application_version_string_;
  if (config_verified_) {
    json_obj["Config"] = "Configuration tested by MLCommons";
  } else {
    json_obj["Config"] = "Configuration NOT tested by MLCommons";
  }

  json_obj["Config Category"] = config_category_;
  json_obj["ConfigFile"] = result.config_file_name;
  json_obj["ConfigComment"] = result.config_file_comment;
  json_obj["ConfigHash"] = result.config_file_hash;
  json_obj["Scenario Name"] = result.scenario_name;
  json_obj["Model Name"] = result.model_name;
  json_obj["Model Path"] = result.model_path;
  json_obj["Asset Paths"] = result.asset_paths;
  json_obj["Data Paths"] = result.data_paths;
  json_obj["Results Verification File"] = result.results_file;
  json_obj["Model File Name"] = result.model_file_name;
  json_obj["Assets File Names"] = result.asset_file_names;
  json_obj["Data File Names"] = result.data_file_names;
  json_obj["Execution Provider Name"] = result.execution_provider_name;
  json_obj["EP Configuration"] = result.ep_configuration;
  json_obj["Iterations"] = result.iterations;
  json_obj["WarmUp"] = result.iterations_warmup;
  json_obj["Benchmark Success"] = result.benchmark_success;
  json_obj["Benchmark Start Time"] = result.benchmark_start_time;
  json_obj["Avg Inference Duration"] = RoundDouble(result.duration, 3);
  json_obj["Benchmark Duration"] = result.benchmark_duration;
  json_obj["Results Verified"] = result.results_verified;
  json_obj["Skipped Prompts"] = result.skipped_prompts;
  json_obj["Has Non-base Prompts"] = result.has_non_base_prompts;
  if (!result.ep_error_messages.empty()) {
    if (result.error_message.empty()) {
      json_obj["Error Message"] = result.ep_error_messages;
    } else {
      json_obj["Error Message"] =
          result.error_message + "\n" + result.ep_error_messages;
    }
    json_obj["EP Error Messages"] = result.ep_error_messages;
  } else
    json_obj["Error Message"] = result.error_message;
  json_obj["Device Type"] = result.device_type;

  if (result.is_llm_benchmark) {
    nlohmann::json overall_perf_results_json{};
    const bool show_overall = config_verified_;
    if (show_overall) {
      auto it = result.performance_results.find("Overall");
      if (it != result.performance_results.end()) {
        auto overall_perf_results = it->second;
        overall_perf_results_json["Geomean Time to First Token"] =
            RoundDouble(overall_perf_results.time_to_first_token_duration, 3);
        overall_perf_results_json["Geomean 2nd+ Token Generation Rate"] =
            RoundDouble(overall_perf_results.token_generation_rate, 3);
        overall_perf_results_json["Avg Input Tokens"] =
            overall_perf_results.average_input_tokens;
        overall_perf_results_json["Avg Generated Tokens"] =
            overall_perf_results.average_generated_tokens;
      }
    }
    json_obj["overall_results"] = overall_perf_results_json;

    nlohmann::json category_results_json{};
    for (const auto& [category, perf_result] : result.performance_results) {
      if (category == "Overall") {
        continue;
      }
      nlohmann::json per_category_results_json{};

      per_category_results_json["Avg Time to First Token"] =
          RoundDouble(perf_result.time_to_first_token_duration, 3);
      per_category_results_json["Avg 2nd+ Token Generation Rate"] =
          RoundDouble(perf_result.token_generation_rate, 3);
      per_category_results_json["Avg Input Tokens"] =
          perf_result.average_input_tokens;
      per_category_results_json["Avg Generated Tokens"] =
          perf_result.average_generated_tokens;

      category_results_json[category] = per_category_results_json;
    }
    json_obj["category_results"] = category_results_json;
    std::vector<std::string> sanitized_output;

    for (size_t i = 0; i < result.output.size(); i++) {
      if (!utils::IsUtf8(result.output[i])) {
        sanitized_output.push_back(utils::SanitizeToUtf8(result.output[i]));
      } else
        sanitized_output.push_back(result.output[i]);
    }

    json_obj["Output"] = sanitized_output;
  }

  if (!system_info_provider_) return json_obj;

  const auto& cpu_info = system_info_provider_->GetCpuInfo();
  json_obj["SysInfo_CPUArchitecture"] = result.system_info.cpu_architecture;
  json_obj["SysInfo_CPUModel"] = result.system_info.cpu_model;
  json_obj["SysInfo_CPUCores"] = cpu_info.physical_cpus;
  json_obj["SysInfo_CPULogicalCores"] = cpu_info.logical_cpus;
  json_obj["SysInfo_CPUClockSpeed"] = cpu_info.frequency;

  const auto& gpu_info = system_info_provider_->GetGpuInfo();
  std::vector<std::string> gpu_names;
  std::vector<std::string> gpu_vendors;
  std::vector<std::string> gpu_driver_versions;
  std::vector<size_t> gpu_memory_sizes;
  for (const auto& info : gpu_info) {
    gpu_names.push_back(info.name);
    gpu_vendors.push_back(info.vendor);
    gpu_driver_versions.push_back(info.driver_version);
    gpu_memory_sizes.push_back(info.dedicated_memory_size);
  }
  json_obj["SysInfo_GPUCount"] = gpu_info.size();
  json_obj["SysInfo_GPUNames"] = gpu_names;
  json_obj["SysInfo_GPUVendors"] = gpu_vendors;
  json_obj["SysInfo_GPUDriverVersions"] = gpu_driver_versions;
  json_obj["SysInfo_GPUMemorySizes"] = gpu_memory_sizes;

  const auto& memory_info = system_info_provider_->GetMemoryInfo();
  json_obj["SysInfo_RAMTotal"] = result.system_info.ram;
  json_obj["SysInfo_RAMAvailable"] = memory_info.physical_available;

  const auto& os_info = system_info_provider_->GetOsInfo();
  json_obj["SysInfo_OSName"] = result.system_info.os_name;
  json_obj["SysInfo_OSVersion"] = os_info.version;

  return json_obj;
}

std::vector<BenchmarkResult> BenchmarkLogger::GetResults(
    const std::map<std::string, BenchmarkResult>& results) {
  std::vector<BenchmarkResult> res_vector;
  std::transform(results.begin(), results.end(), std::back_inserter(res_vector),
                 [](const std::pair<std::string, BenchmarkResult>& pair) {
                   return pair.second;
                 });
  return res_vector;
}

std::vector<BenchmarkResult> BenchmarkLogger::ReadResultsFromFile(
    const std::string& filename) {
  std::ifstream results_file(filename);
  if (!results_file.is_open()) {
    LOG4CXX_ERROR(loggerBenchmarkLogger, "Failed to open " << filename);
    return {};
  }

  std::vector<BenchmarkResult> results;
  std::string line;
  while (std::getline(results_file, line)) {
    if (line.empty()) {
      continue;
    }

    nlohmann::json item;
    try {
      item = nlohmann::json::parse(line);
    } catch (const std::exception& e) {
      LOG4CXX_ERROR(loggerBenchmarkLogger,
                    "Failed to parse results file line: " << line << "as json, "
                                                          << e.what());
      continue;
    }
    BenchmarkResult result;
    result.scenario_name = item.value("Scenario Name", "");
    result.execution_provider_name = item.value("Execution Provider Name", "");
    result.benchmark_success = item.value("Benchmark Success", false);
    result.benchmark_start_time = item.value("Benchmark Start Time", "");
    result.results_verified = item.value("Results Verified", false);
    result.config_verified =
        item.value("Config", "") == "Configuration tested by MLCommons";
    result.has_non_base_prompts = item.value("Has Non-base Prompts", false);

    if (item.contains("Config Category")) {
      result.config_category = item.value("Config Category", "");
    } else if (item.contains("Config Is Experimental") &&
               item.value("Config Is Experimental", false)) {
      result.config_category = "experimental";
    }

    result.config_file_comment = item.value("ConfigComment", "");
    result.device_type = item.value("Device Type", "");
    result.error_message = item.value("Error Message", "");
    result.config_file_name = item.value("ConfigFile", "");
    if (item.contains("Data File Names"))
      result.data_file_names =
          item["Data File Names"].get<std::vector<std::string>>();

    std::string device_name;
    if (item.contains("EP Configuration")) {
      if (result.device_type.empty())
        result.device_type = item["EP Configuration"].value("device_type", "");
      device_name = item["EP Configuration"].value("device_name", "");
    }

    if (item.contains("overall_results") &&
        item["overall_results"].contains(
            "Geomean 2nd+ Token Generation Rate") &&
        item["overall_results"].contains("Geomean Time to First Token")) {
      result.performance_results["Overall"].token_generation_rate =
          item["overall_results"].value("Geomean 2nd+ Token Generation Rate",
                                        0.0);
      result.performance_results["Overall"].time_to_first_token_duration =
          item["overall_results"].value("Geomean Time to First Token", 0.0);
    }

    for (const auto& [category, value] : item["category_results"].items()) {
      PerformanceResult category_result;
      category_result.token_generation_rate =
          value.value("Avg 2nd+ Token Generation Rate", 0.0);
      category_result.time_to_first_token_duration =
          value.value("Avg Time to First Token", 0.0);
      result.performance_results[category] = category_result;
    }
    result.system_info.os_name =
        utils::CleanAndTrimString(item.value("SysInfo_OSName", "unknown"));
    result.system_info.cpu_model =
        utils::CleanAndTrimString(item.value("SysInfo_CPUModel", "unknown"));
    result.system_info.cpu_architecture =
        item.value("SysInfo_CPUArchitecture", "unknown");
    result.system_info.ram =
        item.value("SysInfo_RAMTotal", static_cast<size_t>(0));
    // Get GPU data
    if (!device_name.empty()) {
      std::vector<std::string> gpu_names =
          item.value("SysInfo_GPUNames", std::vector<std::string>{});
      for (size_t i = 0; i < gpu_names.size(); i++) {
#ifndef __APPLE__
        if (device_name.find(gpu_names[i]) == std::string::npos) continue;
#endif
        result.system_info.gpu_name = gpu_names[i];
        std::vector<size_t> gpu_memories =
            item.value("SysInfo_GPUMemorySizes", std::vector<size_t>{});
        if (gpu_memories.size() > i)
          result.system_info.gpu_ram = gpu_memories[i];
        break;
      }
    }
    results.push_back(result);
  }

  return results;
}

void BenchmarkLogger::RemoveResultsFromFile(
    const std::string& filename,
    const std::unordered_set<int>& rows_to_remove) {
  std::ifstream results_in_file(filename);
  if (!results_in_file.is_open()) {
    LOG4CXX_ERROR(loggerBenchmarkLogger, "Failed to open " << filename);
    return;
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(results_in_file, line)) {
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  results_in_file.close();

  std::ofstream results_out_file(filename, std::ios::trunc);
  if (!results_out_file.is_open()) {
    LOG4CXX_ERROR(loggerBenchmarkLogger,
                  "Failed to open " << filename << " for writing");
    return;
  }
  for (size_t i = 0; i < lines.size(); ++i) {
    if (rows_to_remove.find(i) == rows_to_remove.end()) {
      results_out_file << lines[i] << "\n";
    }
  }
}

std::vector<std::string> BenchmarkLogger::GetOrderedCategories(
    const std::vector<BenchmarkResult>& results) {
  const auto& required = BenchmarkResult::kLLMRequiredCategories;
  const auto& extended = BenchmarkResult::kLLMExtendedCategories;

  std::map<std::string, int> order;
  order["Overall"] = -1;
  for (int i = 0; i < required.size(); ++i) order[required[i]] = i;
  for (int i = 0; i < extended.size(); ++i)
    order[extended[i]] = static_cast<int>(required.size()) + i;

  std::set<std::string> key_set;
  for (const auto& r : results)
    for (const auto& [key, _] : r.performance_results) key_set.insert(key);

  std::vector<std::string> keys(key_set.begin(), key_set.end());
  std::ranges::sort(keys, [&](const std::string& a, const std::string& b) {
    auto it_a = order.find(a);
    auto it_b = order.find(b);

    const bool a_in = it_a != order.end();
    const bool b_in = it_b != order.end();

    if (a_in && b_in) return it_a->second < it_b->second;
    if (a_in) return true;
    if (b_in) return false;
    return a < b;
  });

  return keys;
}

bool BenchmarkLogger::ExportResultsToCSV(
    const std::vector<BenchmarkResult>& results, const std::string& filename) {
  std::ofstream out(filename, std::ios::binary);
  if (!out.is_open()) return false;

  using CsvRow = std::vector<std::string>;
  CsvRow ep_headers = {"EP", "", ""};
  CsvRow device_headers = {"Device", "", ""};
  CsvRow scenario_headers = {"Scenario", "", ""};
  CsvRow time_headers = {"Date", "", ""};
  CsvRow is_tested_headers = {"Tested By MLCommons", "", ""};
  CsvRow config_category_headers = {"Config Category", "", ""};
  CsvRow system_info_headers = {"System Info", "", ""};

  for (const auto& r : results) {
    ep_headers.push_back(r.execution_provider_name);
    device_headers.push_back(r.device_type);

    auto model_full_name = cil::BenchmarkRunner::GetModelFullName(
        cil::utils::StringToLowerCase(r.scenario_name));
    scenario_headers.push_back(model_full_name.value_or(r.scenario_name));

    time_headers.push_back(r.benchmark_start_time);
    is_tested_headers.emplace_back(r.config_verified ? "Yes" : "No");
    config_category_headers.push_back(
        r.config_category.empty() ? "base" : r.config_category);
    system_info_headers.push_back(FormatSystemInfo(r.system_info));
  }

  WriteCsvRow(out, ep_headers);
  WriteCsvRow(out, device_headers);
  WriteCsvRow(out, scenario_headers);
  WriteCsvRow(out, time_headers);
  WriteCsvRow(out, is_tested_headers);
  WriteCsvRow(out, config_category_headers);
  WriteCsvRow(out, system_info_headers);

  const auto ordered_categories = GetOrderedCategories(results);

  for (const auto& category : ordered_categories) {
    WriteCsvRow(out, CsvRow{category});

    CsvRow TTFT_row = {"", "", "TTFT (seconds)"};
    CsvRow TPS_row = {"", "", "TPS"};

    for (const auto& r : results) {
      auto it = r.performance_results.find(category);
      if (it != r.performance_results.end()) {
        TTFT_row.push_back(utils::DoubleToFixedString(
            it->second.time_to_first_token_duration, 2));
        TPS_row.push_back(
            utils::DoubleToFixedString(it->second.token_generation_rate, 1));
      } else {
        TTFT_row.emplace_back("");
        TPS_row.emplace_back("");
      }
    }

    WriteCsvRow(out, TTFT_row);
    WriteCsvRow(out, TPS_row);
  }
  return true;
}

}  // namespace cil
