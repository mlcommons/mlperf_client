#pragma once

#include "../Llm/llm_inference.h"
using namespace cil::infer;
namespace cil {
namespace IHV {

namespace infer {
std::string LlmInference::formConfigStringLlama3(
    const nlohmann::json& model_config, const std::string& device_type_,
    const std::string& model_folder_, const std::string& model_path_) {
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
    npu_engine["backend"]["QnnHtp"] = {{"version", 1},
                                       {"spill-fill-bufsize", 0},
                                       {"use-mmap", false},   
                                       {"mmap-budget", 0},
                                       {"poll", false},       
                                       {"pos-id-dim", 64},
                                       {"cpu-mask", "0xe0"},  
                                       {"kv-dim", 128},
                                       {"rope-theta", 500000}, 
                                       {"enable-graph-switching", false}};

    npu_engine["backend"]["extensions"] =
        model_folder_ + "/htpBackendExtConfig.json";
    npu_engine["model"]["version"] = 1;
    npu_engine["model"]["type"] = "binary";
    npu_engine["model"]["binary"]["version"] = 1;
    npu_engine["model"]["binary"]["ctx-bins"] = {
        model_folder_ + "/llama3_npu_1_of_5.bin",
        model_folder_ + "/llama3_npu_2_of_5.bin",
        model_folder_ + "/llama3_npu_3_of_5.bin",
        model_folder_ + "/llama3_npu_4_of_5.bin",
        model_folder_ + "/llama3_npu_5_of_5.bin"};

    auto& cpu_engine = genieConfig["dialog"]["engine"][1];
    cpu_engine["version"] = 1;
    cpu_engine["n-threads"] = 10;
    cpu_engine["role"] = "secondary";
    cpu_engine["backend"]["version"] = 1;
    cpu_engine["backend"]["type"] = "QnnGenAiTransformer";
    cpu_engine["backend"]["QnnGenAiTransformer"] = {{"version", 1},
                                                    {"n-kv-heads", 8},
                                                    {"use-in-memory", true},
                                                    {"kv-quantization", true}};
    cpu_engine["model"]["version"] = 1;
    cpu_engine["model"]["type"] = "library";
    cpu_engine["model"]["library"] = {{"version", 1},
                                      {"model-bin", model_path_}};

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
  ctx["n-vocab"] = 128256;
  ctx["bos-token"] = 128000;
  ctx["eos-token"] =
    nlohmann::json::basic_json::array({128001, 128008, 128009});

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