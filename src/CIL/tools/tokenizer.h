#ifndef CIL_TOOLS_TOKENIZER_H_
#define CIL_TOOLS_TOKENIZER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/expected.h"
#include "tools/itokenizer.h"

namespace cil::tools {

class Tokenizer {
 public:
  Tokenizer(const std::string& tokenizer_dir, std::string& error);
  ~Tokenizer();

  Tokenizer(const Tokenizer&) = delete;
  Tokenizer& operator=(const Tokenizer&) = delete;
  Tokenizer(Tokenizer&&) = delete;
  Tokenizer& operator=(Tokenizer&&) = delete;

  cil::Expected<std::vector<std::vector<uint32_t>>> Encode(
      const std::vector<std::string_view>& inputs,
      bool add_special_tokens = true) const;

  cil::Expected<std::vector<std::string>> Decode(
      const std::vector<std::span<const uint32_t>>& token_ids) const;

  bool HasChatTemplate() const;

  std::string ApplyChatTemplate(
      const std::string& messages_json, bool add_generation_prompt = true,
      std::optional<bool> enable_thinking = std::nullopt) const;

  bool DetectChatTemplateInText(const std::string& text) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cil::tools

#endif  // CIL_TOOLS_TOKENIZER_H_
