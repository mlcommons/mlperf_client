#include "benchmark_logger.h"

#include <log4cxx/fileappender.h>
#include <log4cxx/logger.h>

#include <fstream>

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

BenchmarkLogger::~BenchmarkLogger() = default;

bool BenchmarkLogger::AppendBenchmarkResult(const BenchmarkResult& result) {
  nlohmann::ordered_json json_obj = BenchmarkResultToJson(result);
  bool validated = ValidateUTF8inJSONObj(json_obj);
  if (validated) {
    WriteResults(json_obj);
    results_.push_back(result);
  } else {
    auto failed_results = result;
    failed_results.benchmark_success = false;
    failed_results.error_message = "Invalid UTF-8 string found in output";
    LOG4CXX_ERROR(loggerBenchmarkLogger,
                  "Invalid UTF-8 string found in BenchmarkResult");
    results_.push_back(failed_results);
  }
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
  if (config_experimental_) {
    oss << "== Configuration is experimental ==\n\n";
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
            oss << Indent(3) << "Error: "
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

void BenchmarkLogger::SetConfigExperimental(bool is_experimental) {
  config_experimental_ = is_experimental;
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

  json_obj["Config Is Experimental"] = config_experimental_;
  json_obj["ConfigFile"] = result.config_file_name;
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
  json_obj["SysInfo_CPUArchitecture"] = cpu_info.architecture;
  json_obj["SysInfo_CPUModel"] = cpu_info.model_name;
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
  json_obj["SysInfo_RAMTotal"] = memory_info.physical_total;
  json_obj["SysInfo_RAMAvailable"] = memory_info.physical_available;

  const auto& os_info = system_info_provider_->GetOsInfo();
  json_obj["SysInfo_OSName"] = os_info.full_name;
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
    result.config_experimental = item.value("Config Is Experimental", false);
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
    result.system_info.os_name = item.value("SysInfo_OSName", "unknown");
    result.system_info.cpu_model = item.value("SysInfo_CPUModel", "unknown");
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
}  // namespace cil
