#include "tokenizer.h"

#include <log4cxx/logger.h>

#include <dylib.hpp>
#include <optional>

#include "unpacker.h"
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

static const log4cxx::LoggerPtr logger_tools(
    log4cxx::Logger::getLogger("tools::Tokenizer"));

namespace cil::tools {

using CreateSig = ITokenizer*(const char*, int*);
using DestroyFn = void (*)(ITokenizer*);

struct Tokenizer::Impl {
  std::unique_ptr<dylib> lib;
  std::unique_ptr<ITokenizer, DestroyFn> tok{nullptr, nullptr};
};

Tokenizer::Tokenizer(const std::string& tokenizer_dir, std::string& error)
    : impl_(std::make_unique<Impl>()) {
  std::string lib_path;

  Unpacker unpacker;
  if (!unpacker.UnpackAsset(Unpacker::Asset::kTokenizer, lib_path)) {
    error = "failed to unpack tokenizer shared library";
    LOG4CXX_ERROR(logger_tools, error);
    return;
  }

  LOG4CXX_INFO(logger_tools, "Loading tokenizer from: " << lib_path);

  try {
    impl_->lib =
        std::make_unique<dylib>(lib_path, dylib::no_filename_decorations);
  } catch (const std::exception& e) {
    error = std::string("failed to load tokenizer: ") + e.what();
    LOG4CXX_ERROR(logger_tools, error);
    return;
  }

  CreateSig* create_fn = nullptr;
  DestroyFn destroy_fn = nullptr;
  try {
    create_fn = impl_->lib->get_function<CreateSig>("create_tokenizer");
    destroy_fn =
        impl_->lib->get_function<void(ITokenizer*)>("destroy_tokenizer");
  } catch (const std::exception& e) {
    error = std::string("failed to resolve tokenizer symbols: ") + e.what();
    LOG4CXX_ERROR(logger_tools, error);
    return;
  }

  int status_code = 0;
  ITokenizer* raw = create_fn(tokenizer_dir.c_str(), &status_code);

  if (!raw) {
    error = "create_tokenizer returned null";
    LOG4CXX_ERROR(logger_tools, error);
  } else {
    impl_->tok = {raw, destroy_fn};
    LOG4CXX_INFO(logger_tools, "Tokenizer loaded successfully");
  }
}

Tokenizer::~Tokenizer() = default;

cil::Expected<std::vector<std::vector<uint32_t>>> Tokenizer::Encode(
    const std::vector<std::string_view>& inputs,
    bool add_special_tokens) const {
  if (!impl_->tok) {
    return cil::Expected<std::vector<std::vector<uint32_t>>>::error(
        "tokenizer not loaded");
  }
  return impl_->tok->Encode(inputs, add_special_tokens);
}

cil::Expected<std::vector<std::string>> Tokenizer::Decode(
    const std::vector<std::span<const uint32_t>>& token_ids) const {
  if (!impl_->tok) {
    return cil::Expected<std::vector<std::string>>::error(
        "tokenizer not loaded");
  }
  return impl_->tok->Decode(token_ids);
}

bool Tokenizer::HasChatTemplate() const {
  if (impl_->tok) return impl_->tok->HasChatTemplate();
  return false;
}

std::string Tokenizer::ApplyChatTemplate(
    const std::string& messages_json, bool add_generation_prompt,
    std::optional<bool> enable_thinking) const {
  if (!impl_->tok) return {};
  return impl_->tok->ApplyChatTemplate(messages_json, add_generation_prompt,
                                       enable_thinking);
}

bool Tokenizer::DetectChatTemplateInText(const std::string& text) const {
  if (impl_->tok) return impl_->tok->DetectChatTemplateInText(text);
  return false;
}

}  // namespace cil::tools
