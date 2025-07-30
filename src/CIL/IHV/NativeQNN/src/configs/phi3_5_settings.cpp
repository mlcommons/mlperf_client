#pragma once

#include "../Llm/llm_inference.h"
using namespace cil::infer;
namespace cil {
namespace IHV {

namespace infer {
std::string LlmInference::formConfigStringPhi3_5(
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
                                       {"cpu-mask", "0xe0"},
                                       {"kv-dim", 96},
                                       {"allow-async-init", false}};

    npu_engine["backend"]["extensions"] =
        model_folder_ + "/htpBackendExtConfig.json";
    npu_engine["model"]["version"] = 1;
    npu_engine["model"]["type"] = "binary";
    npu_engine["model"]["binary"]["version"] = 1;
    npu_engine["model"]["binary"]["ctx-bins"] = {
        model_folder_ + "/phi3_5_npu_1_of_4.bin",
        model_folder_ + "/phi3_5_npu_2_of_4.bin",
        model_folder_ + "/phi3_5_npu_3_of_4.bin",
        model_folder_ + "/phi3_5_npu_4_of_4.bin"};
    npu_engine["model"]["positional-encoding"] = {
        {"type", "rope"}, {"rope-dim", 48}, {"rope-theta", 10000}};
    npu_engine["model"]["positional-encoding"]["rope-scaling"] = {
      {"rope-type", "longrope"}, {"factor", 32},
      {"original-max-position-embeddings", 4096},
      {"long-factor",
       nlohmann::json::basic_json::array(
           {1.0800000429153442, 1.1100000143051147, 1.1399999856948853,
            1.340000033378601,  1.5899999141693115, 1.600000023841858,
            1.6200000047683716, 2.620000123977661,  3.2300000190734863,
            3.2300000190734863, 4.789999961853027,  7.400000095367432,
            7.700000286102295,  9.09000015258789,   12.199999809265137,
            17.670000076293945, 24.46000099182129,  28.57000160217285,
            30.420001983642578, 30.840002059936523, 32.590003967285156,
            32.93000411987305,  42.320003509521484, 44.96000289916992,
            50.340003967285156, 50.45000457763672,  57.55000305175781,
            57.93000411987305,  58.21000289916992,  60.1400032043457,
            62.61000442504883,  62.62000274658203,  62.71000289916992,
            63.1400032043457,   63.1400032043457,   63.77000427246094,
            63.93000411987305,  63.96000289916992,  63.970001220703125,
            64.02999877929688,  64.06999969482422,  64.08000183105469,
            64.12000274658203,  64.41000366210938,  64.4800033569336,
            64.51000213623047,  64.52999877929688,  64.83999633789062})},
      {"short-factor",
       nlohmann::json::basic_json::array(
           {1.0,  1.0199999809265137,  1.0299999713897705,
            1.0299999713897705,  1.0499999523162842,  1.0499999523162842,
            1.0499999523162842,  1.0499999523162842,  1.0499999523162842,
            1.0699999332427979,  1.0999999046325684,  1.1099998950958252,
            1.1599998474121094,  1.1599998474121094,  1.1699998378753662,
            1.2899998426437378,  1.339999794960022,  1.679999828338623,
            1.7899998426437378,  1.8199998140335083,  1.8499997854232788,
            1.8799997568130493,  1.9099997282028198,  1.9399996995925903,
            1.9899996519088745,  2.0199997425079346,  2.0199997425079346,
            2.0199997425079346,  2.0199997425079346,  2.0199997425079346,
            2.0199997425079346,  2.0299997329711914,  2.0299997329711914,
            2.0299997329711914,  2.0299997329711914,  2.0299997329711914,
            2.0299997329711914,  2.0299997329711914,  2.0299997329711914,
            2.0299997329711914,  2.0799996852874756,  2.0899996757507324,
            2.189999580383301,  2.2199995517730713,  2.5899994373321533,
            2.729999542236328,  2.749999523162842,  2.8399994373321533})}
    };


    auto& cpu_engine = genieConfig["dialog"]["engine"][1];
    cpu_engine["version"] = 1;
    cpu_engine["n-threads"] = 10;
    cpu_engine["role"] = "secondary";
    cpu_engine["backend"]["version"] = 1;
    cpu_engine["backend"]["type"] = "QnnGenAiTransformer";
    cpu_engine["backend"]["QnnGenAiTransformer"] = {{"version", 1},
                                                    {"n-layer", 32},
                                                    {"n-embd", 3072},
                                                    {"n-heads", 32},
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
  ctx["n-vocab"] = 32064;
  ctx["bos-token"] = 1;
  ctx["eos-token"] = nlohmann::json::basic_json::array({32007, 32001, 32000});

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