#pragma once

#include "../Llm/llm_inference.h"
using namespace cil::infer;
namespace cil {
namespace IHV {

namespace infer {
std::string LlmInference::formConfigStringPhi4_SDX_Elite(
    const nlohmann::json& model_config, const std::string& device_type_,
    const std::string& model_folder_, const std::string& model_path_,
    const std::vector<std::string>& npu_bins_) {
  nlohmann::json genieConfig;

  if (device_type_ == "NPU_CPU") {
    // NPU_CPU Config Settings
    genieConfig["dialog"]["type"] = "kv-share";
    auto& npu_engine = genieConfig["dialog"]["engine"][0];
    npu_engine["version"] = 1;
    npu_engine["n-threads"] = 3;
    npu_engine["role"] = "primary";
    npu_engine["backend"]["version"] = 1;
    npu_engine["backend"]["type"] = "QnnHtp";
    npu_engine["backend"]["QnnHtp"] = {
        {"version", 1},      {"spill-fill-bufsize", 0},
        {"use-mmap", false}, {"mmap-budget", 0},
        {"poll", false},     {"cpu-mask", "0xe0"},
        {"kv-dim", 128},      {"allow-async-init", false}};

    npu_engine["backend"]["extensions"] =
        model_folder_ + "/htpBackendExtConfig.json";
    npu_engine["model"]["version"] = 1;
    npu_engine["model"]["type"] = "binary";
    npu_engine["model"]["binary"]["version"] = 1;
    npu_engine["model"]["binary"]["ctx-bins"] = npu_bins_;

    auto& cpu_engine = genieConfig["dialog"]["engine"][1];
    cpu_engine["version"] = 1;
    cpu_engine["n-threads"] = std::thread::hardware_concurrency();
    cpu_engine["role"] = "secondary";
    cpu_engine["backend"]["version"] = 1;
    cpu_engine["backend"]["type"] = "QnnGenAiTransformer";
    cpu_engine["backend"]["QnnGenAiTransformer"] = {
        {"version", 1},
        {"enable-in-memory-kv-share", true},
        {"kv-quantization", true}};
    cpu_engine["model"]["version"] = 1;
    cpu_engine["model"]["type"] = "library";
    cpu_engine["model"]["library"] = {{"version", 1},
                                      {"model-bin", model_path_}};
  } else if (device_type_ == "NPU") {
    // NPU Config Settings
    genieConfig["dialog"]["type"] = "basic";

    auto& engine = genieConfig["dialog"]["engine"];
    engine["version"] = 1;
    engine["n-threads"] = 3;
    engine["backend"]["version"] = 1;
    engine["backend"]["type"] = "QnnHtp";
    engine["backend"]["QnnHtp"] = {{"version", 1},
                                   {"spill-fill-bufsize", 0},
                                   {"use-mmap", false},
                                   {"mmap-budget", 0},
                                   {"poll", false},
                                   {"cpu-mask", "0xe0"},
                                   {"kv-dim", 128},
                                   {"rope-theta", 500000},
                                   {"allow-async-init", false}};
    engine["backend"]["extensions"] =
        model_folder_ + "/htpBackendExtConfig.json";
    engine["model"]["version"] = 1;
    engine["model"]["type"] = "binary";
    engine["model"]["binary"]["version"] = 1;
    engine["model"]["binary"]["ctx-bins"] = npu_bins_;

  } else {
    return "";
  }

  // Common Settings for all
  genieConfig["dialog"]["version"] = 1;
  genieConfig["dialog"]["stop-sequence"] =
      nlohmann::json::basic_json::array({""});
  genieConfig["dialog"]["max-num-tokens"] =
      model_config["search"]["max_length"];

  auto& ctx = genieConfig["dialog"]["context"];
  ctx["version"] = 1;
  ctx["size"] = std::min(4096, (int)model_config["model"]["context_length"]);
  ctx["n-vocab"] = 100352;
  ctx["bos-token"] = 100257;
  ctx["eos-token"] = 100265;

  auto& sampler = genieConfig["dialog"]["sampler"];
  sampler["version"] = 1;
  sampler["seed"] = 100;
  sampler["temp"] = model_config["search"]["temperature"];
  sampler["top-k"] = model_config["search"]["top_k"];
  sampler["top-p"] = model_config["search"].value("top_p", 0.95);
  sampler["greedy"] = true;

  genieConfig["dialog"]["tokenizer"]["version"] = 1;
  genieConfig["dialog"]["tokenizer"]["path"] =
      model_folder_ + "/tokenizer.json";

  return genieConfig.dump();
}
}  // namespace infer
}  // namespace IHV
}  // namespace cil