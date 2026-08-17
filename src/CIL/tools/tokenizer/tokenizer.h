#ifndef CIL_TOOLS_TOKENIZER_H_
#define CIL_TOOLS_TOKENIZER_H_

#include <memory>

#include "chat_template.h"
#include "tools/itokenizer.h"

namespace cil::tools {

class Tokenizer : public ITokenizer {
 public:
  static std::unique_ptr<Tokenizer> Create(const std::string& tokenizer_dir,
                                           std::string& error);

  ~Tokenizer() override;

  cil::Expected<std::vector<std::vector<uint32_t>>> Encode(
      const std::vector<std::string_view>& inputs,
      bool add_special_tokens = true) const override;

  cil::Expected<std::vector<std::string>> Decode(
      const std::vector<std::span<const uint32_t>>& token_ids) const override;

  bool HasChatTemplate() const override;

  std::string ApplyChatTemplate(
      const std::string& messages_json, bool add_generation_prompt,
      std::optional<bool> enable_thinking = std::nullopt) const override;

  bool DetectChatTemplateInText(const std::string& text) const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::unique_ptr<ChatTemplate> chat_;

  Tokenizer(std::unique_ptr<Impl> impl, std::unique_ptr<ChatTemplate> chat);
};

}  // namespace cil::tools

#endif  // CIL_TOOLS_TOKENIZER_H_
