#include "tokenizer.h"

#include "common/scope_exit.h"
#include "ortx_tokenizer.h"
#include "ortx_utils.h"

#ifdef _WIN32
#ifdef TOKENIZER_BUILDING
#define TOKENIZER_API __declspec(dllexport)
#else
#define TOKENIZER_API __declspec(dllimport)
#endif
#else
#define TOKENIZER_API __attribute__((visibility("default")))
#endif

namespace cil::tools {

struct Tokenizer::Impl {
  ::OrtxTokenizer* tokenizer = nullptr;

  ~Impl() {
    if (tokenizer) {
      OrtxDispose(reinterpret_cast<OrtxObject**>(&tokenizer));
    }
  }
};

namespace {

std::string MakeError(extError_t err) {
  if (err == kOrtxOK) return {};
  if (const char* m = OrtxGetLastErrorMessage(); m && *m) return m;
  return "ortx error " + std::to_string(static_cast<int>(err));
}

}  // namespace

Tokenizer::Tokenizer(std::unique_ptr<Impl> impl,
                     std::unique_ptr<ChatTemplate> chat)
    : impl_(std::move(impl)), chat_(std::move(chat)) {}

Tokenizer::~Tokenizer() = default;

std::unique_ptr<Tokenizer> Tokenizer::Create(const std::string& tokenizer_dir,
                                             std::string& error) {
  auto impl = std::make_unique<Impl>();
  const auto err = OrtxCreateTokenizer(&impl->tokenizer, tokenizer_dir.c_str());
  error = MakeError(err);
  if (err != kOrtxOK) return nullptr;

  std::string chat_error;
  auto chat = std::make_unique<ChatTemplate>(tokenizer_dir, chat_error);

  return std::unique_ptr<Tokenizer>(
      new Tokenizer(std::move(impl), std::move(chat)));
}

cil::Expected<std::vector<std::vector<uint32_t>>> Tokenizer::Encode(
    const std::vector<std::string_view>& inputs,
    bool add_special_tokens) const {
  using Result = std::vector<std::vector<uint32_t>>;

  if (!impl_->tokenizer) {
    return cil::Expected<Result>::error("tokenizer not initialized");
  }

  cil::utils::ScopeExit on_exit;
  if (!add_special_tokens) {
    const char* keys[] = {"add_special_tokens"};
    const char* vals[] = {"false"};
    OrtxUpdateTokenizerOptions(impl_->tokenizer, keys, vals, 1);
    on_exit.SetCallback([tok = impl_->tokenizer] {
      const char* keys[] = {"add_special_tokens"};
      const char* vals[] = {"true"};
      OrtxUpdateTokenizerOptions(tok, keys, vals, 1);
    });
  }

  std::vector<std::string> owned;
  owned.reserve(inputs.size());
  std::vector<const char*> c_inputs;
  c_inputs.reserve(inputs.size());
  for (const auto& sv : inputs) {
    owned.emplace_back(sv);
    c_inputs.push_back(owned.back().c_str());
  }

  OrtxTokenId2DArray* output = nullptr;
  const auto err =
      OrtxTokenize(impl_->tokenizer, c_inputs.data(), c_inputs.size(), &output);
  if (err != kOrtxOK) {
    if (output) OrtxDispose(reinterpret_cast<OrtxObject**>(&output));
    return cil::Expected<Result>::error(MakeError(err));
  }

  size_t batch = 0;
  if (auto e = OrtxTokenId2DArrayGetBatch(output, &batch); e != kOrtxOK) {
    OrtxDispose(reinterpret_cast<OrtxObject**>(&output));
    return cil::Expected<Result>::error(MakeError(e));
  }

  Result token_ids(batch);
  for (size_t i = 0; i < batch; ++i) {
    const extTokenId_t* item = nullptr;
    size_t len = 0;
    if (auto e = OrtxTokenId2DArrayGetItem(output, i, &item, &len);
        e != kOrtxOK) {
      OrtxDispose(reinterpret_cast<OrtxObject**>(&output));
      return cil::Expected<Result>::error(MakeError(e));
    }
    token_ids[i].assign(item, item + len);
  }

  OrtxDispose(reinterpret_cast<OrtxObject**>(&output));
  return token_ids;
}

cil::Expected<std::vector<std::string>> Tokenizer::Decode(
    const std::vector<std::span<const uint32_t>>& token_ids) const {
  using Result = std::vector<std::string>;

  if (!impl_->tokenizer) {
    return cil::Expected<Result>::error("tokenizer not initialized");
  }

  Result texts(token_ids.size());

  for (size_t i = 0; i < token_ids.size(); ++i) {
    OrtxStringArray* str_array = nullptr;
    const auto err = OrtxDetokenize1D(impl_->tokenizer, token_ids[i].data(),
                                      token_ids[i].size(), &str_array);
    if (err != kOrtxOK) {
      if (str_array) OrtxDispose(reinterpret_cast<OrtxObject**>(&str_array));
      return cil::Expected<Result>::error(MakeError(err));
    }

    const char* item = nullptr;
    if (auto e = OrtxStringArrayGetItem(str_array, 0, &item); e != kOrtxOK) {
      OrtxDispose(reinterpret_cast<OrtxObject**>(&str_array));
      return cil::Expected<Result>::error(MakeError(e));
    }
    texts[i] = item ? item : "";

    OrtxDispose(reinterpret_cast<OrtxObject**>(&str_array));
  }

  return texts;
}

bool Tokenizer::HasChatTemplate() const {
  return chat_ && chat_->HasTemplate();
}

std::string Tokenizer::ApplyChatTemplate(
    const std::string& messages_json, bool add_generation_prompt,
    std::optional<bool> enable_thinking) const {
  if (!chat_) return {};
  return chat_->Apply(messages_json, add_generation_prompt, enable_thinking);
}

bool Tokenizer::DetectChatTemplateInText(const std::string& text) const {
  if (chat_) return chat_->DetectInText(text);
  return false;
}

}  // namespace cil::tools

extern "C" {

TOKENIZER_API cil::tools::ITokenizer* create_tokenizer(
    const char* tokenizer_dir, int* out_status) {
  std::string error;

  auto tokenizer = cil::tools::Tokenizer::Create(tokenizer_dir, error);

  if (!tokenizer) {
    if (out_status) *out_status = -1;
    return nullptr;
  }
  if (out_status) *out_status = 0;

  return tokenizer.release();
}

TOKENIZER_API void destroy_tokenizer(cil::tools::ITokenizer* ptr) {
  delete ptr;
}

}  // extern "C"
