#ifndef CIL_TOOLS_ITOKENIZER_H_
#define CIL_TOOLS_ITOKENIZER_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/expected.h"

namespace cil::tools {

class ITokenizer {
 public:
  virtual ~ITokenizer() = default;

  virtual cil::Expected<std::vector<std::vector<uint32_t>>> Encode(
      const std::vector<std::string_view>& inputs,
      bool add_special_tokens = true) const = 0;

  virtual cil::Expected<std::vector<std::string>> Decode(
      const std::vector<std::span<const uint32_t>>& token_ids) const = 0;

  virtual bool HasChatTemplate() const = 0;

  virtual std::string ApplyChatTemplate(
      const std::string& messages_json, bool add_generation_prompt = true,
      std::optional<bool> enable_thinking = std::nullopt) const = 0;

  virtual bool DetectChatTemplateInText(const std::string& text) const = 0;
};

}  // namespace cil::tools

#endif  // CIL_TOOLS_ITOKENIZER_H_
