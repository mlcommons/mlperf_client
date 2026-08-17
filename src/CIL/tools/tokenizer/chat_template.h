#ifndef CIL_TOOLS_TOKENIZER_CHAT_TEMPLATE_H_
#define CIL_TOOLS_TOKENIZER_CHAT_TEMPLATE_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace minja {
class chat_template;
}

namespace cil::tools {

class ChatTemplate {
 public:
  ChatTemplate(const std::string& tokenizer_dir, std::string& error);
  ~ChatTemplate();

  bool HasTemplate() const;

  std::string Apply(const std::string& messages_json,
                    bool add_generation_prompt,
                    std::optional<bool> enable_thinking = std::nullopt) const;

  bool DetectInText(const std::string& text) const;

 private:
  std::unique_ptr<minja::chat_template> impl_;
  std::string bos_token_;
  std::string eos_token_;
  std::vector<std::string> special_tokens_;
};

}  // namespace cil::tools

#endif  // CIL_TOOLS_TOKENIZER_CHAT_TEMPLATE_H_
