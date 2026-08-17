#include "chat_template.h"

#include <filesystem>
#include <fstream>
#include <minja/chat-template.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>

namespace fs = std::filesystem;

namespace cil::tools {

ChatTemplate::ChatTemplate(const std::string& tokenizer_dir,
                           std::string& error) {
  fs::path config_path = fs::path(tokenizer_dir) / "tokenizer_config.json";
  if (!fs::exists(config_path)) return;

  std::ifstream ifs(config_path);
  if (!ifs.is_open()) {
    error = "failed to open " + config_path.string();
    return;
  }

  nlohmann::json config;
  try {
    ifs >> config;
  } catch (const std::exception& e) {
    error = std::string("failed to parse tokenizer_config.json: ") + e.what();
    return;
  }

  if (!config.contains("chat_template") ||
      !config["chat_template"].is_string()) {
    fs::path jinja_path = fs::path(tokenizer_dir) / "chat_template.jinja";
    std::ifstream jinja_ifs(jinja_path);
    if (jinja_ifs.is_open()) {
      std::stringstream buf;
      buf << jinja_ifs.rdbuf();
      config["chat_template"] = buf.str();
    } else {
      fs::path json_path = fs::path(tokenizer_dir) / "chat_template.json";
      std::ifstream json_ifs(json_path);
      if (json_ifs.is_open()) {
        try {
          nlohmann::json chat_cfg =
              nlohmann::json::parse(json_ifs, nullptr, false, true);
          if (chat_cfg.is_object() && chat_cfg.contains("chat_template") &&
              chat_cfg["chat_template"].is_string()) {
            config["chat_template"] = chat_cfg["chat_template"];
          }
        } catch (...) {
        }
      }
    }
  }

  if (!config.contains("chat_template") ||
      !config["chat_template"].is_string()) {
    return;
  }

  std::string template_str = config["chat_template"].get<std::string>();

  auto extract_token = [&](const char* key) -> std::string {
    if (!config.contains(key)) return {};
    auto& v = config[key];
    if (v.is_string()) return v.get<std::string>();
    if (v.is_object() && v.contains("content") && v["content"].is_string())
      return v["content"].get<std::string>();
    return {};
  };

  bos_token_ = extract_token("bos_token");
  eos_token_ = extract_token("eos_token");

  if (config.contains("added_tokens_decoder") &&
      config["added_tokens_decoder"].is_object()) {
    for (auto& [id, token] : config["added_tokens_decoder"].items()) {
      if (token.is_object() && token.value("special", false) &&
          token.contains("content") && token["content"].is_string()) {
        special_tokens_.push_back(token["content"].get<std::string>());
      }
    }
  }
  if (special_tokens_.empty()) {
    if (!bos_token_.empty()) special_tokens_.push_back(bos_token_);
    if (!eos_token_.empty()) special_tokens_.push_back(eos_token_);
  }

  try {
    impl_ = std::make_unique<minja::chat_template>(template_str, bos_token_,
                                                   eos_token_);
  } catch (const std::exception& e) {
    error = std::string("failed to compile chat template: ") + e.what();
    return;
  }
}

ChatTemplate::~ChatTemplate() = default;

bool ChatTemplate::HasTemplate() const { return impl_ != nullptr; }

std::string ChatTemplate::Apply(const std::string& messages_json,
                                bool add_generation_prompt,
                                std::optional<bool> enable_thinking) const {
  if (!impl_) return {};

  try {
    nlohmann::ordered_json extra_context;
    if (enable_thinking.has_value()) {
      extra_context = {{"enable_thinking", *enable_thinking}};
    }
    return impl_->apply({
        .messages = nlohmann::ordered_json::parse(messages_json),
        .add_generation_prompt = add_generation_prompt,
        .extra_context = extra_context,
    });
  } catch (...) {
    return {};
  }
}

bool ChatTemplate::DetectInText(const std::string& text) const {
  if (text.empty()) return false;

  for (const auto& token : special_tokens_) {
    if (text.find(token) != std::string::npos) return true;
  }

  return false;
}

}  // namespace cil::tools
