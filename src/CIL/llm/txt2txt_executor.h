/**
 * @file txt2txt_executor.h
 *
 * @brief Concrete LLM executor for the "txt2txt" scenario: a batch of
 * independent single-turn prompts, one completion each.
 *
 * All LLM-shared plumbing (tokenizer, EP dispatch, logging, progress,
 * BenchmarkResult) lives in LLMExecutor. This class only adds the
 * scenario-specific inference loop. Future single-in / single-out LLM
 * benchmarks can also be expressed as Txt2Txt; richer interactions (e.g.
 * chat with history) belong in a sibling subclass of LLMExecutor.
 */
#ifndef TXT2TXT_EXECUTOR_H_
#define TXT2TXT_EXECUTOR_H_

#include "llm_executor.h"

namespace cil::infer {

class Txt2TxtExecutor : public LLMExecutor {
 public:
  using LLMExecutor::LLMExecutor;

 protected:
  void RunScenario(EP ep, const nlohmann::json& settings) override;
};

}  // namespace cil::infer

#endif  // TXT2TXT_EXECUTOR_H_
