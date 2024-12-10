#include "benchmark_logger.h"

#include <log4cxx/fileappender.h>
#include <log4cxx/logger.h>

#include "benchmark_result.h"
#include "system_info_provider_macos.h"
#include "system_info_provider_windows.h"
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
    : config_verified_(false),
      filename_(output_dir.empty() ? "Logs/results.json"
                                   : output_dir + "/results.json"),
#ifdef WIN32
      system_info_provider_(std::make_unique<SystemInfoProviderWindows>())
#else
      system_info_provider_(std::make_unique<SystemInfoProviderMacOS>())
#endif

{
  system_info_provider_->FetchInfo();

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
  if (ValidateUTF8inJSONObj(json_obj)) {
    WriteResults(json_obj);
    results_.push_back(result);
  } else {
    LOG4CXX_ERROR(loggerBenchmarkLogger,
                  "Invalid UTF-8 string found in BenchmarkResult");
    return false;
  }
  return true;
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
  if (config_verified_) {
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
            auto it = exec_result.performance_results.find("Overall");
            if (it != exec_result.performance_results.end()) {
              auto overall_perf_results = it->second;
              oss << Indent(3) << "OVERALL:\t"
                  << "Geomean Time to First Token: "
                  << DumpRoundedDouble(
                         overall_perf_results.time_to_first_token_duration, 3)
                  << " s, ";
              if (std::fabs(overall_perf_results.token_generation_rate) >
                  1e-5) {
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
            oss << Indent(3)
                << ", Avg Inference Duration: "
                << DumpRoundedDouble(exec_result.duration, 9) << " s\n";
          }
        } else {
          oss << Indent(3) << "Error: "
              << utils::CleanErrorMessageFromStaticPaths(
                     exec_result.error_message)
              << "\n";
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

nlohmann::ordered_json BenchmarkLogger::BenchmarkResultToJson(
    const BenchmarkResult& result) {
  nlohmann::ordered_json json_obj;
  json_obj["BuildVer"] = application_version_string_;
  if (config_verified_) {
    json_obj["Config"] = "Configuration tested by MLCommons";
  } else {
    json_obj["Config"] = "Configuration NOT tested by MLCommons";
  }
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
  json_obj["Error Message"] = result.error_message;
  json_obj["Device Type"] = result.device_type;

  if (result.is_llm_benchmark) {
    nlohmann::json overall_perf_results_json{};
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
    gpu_memory_sizes.push_back(info.memory_size);
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

}  // namespace cil