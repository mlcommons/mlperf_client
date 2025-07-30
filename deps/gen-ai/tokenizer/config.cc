// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <string>
#include <fstream>
#include <streambuf>
#include "../filesystem.h"

#include "config.h"

namespace tfm {

bool AddedToken::FromJson(const simdjson::dom::element& element) {
  if (element.is_string()) {
    content_ = element.get_c_str();
    return true;
  }
  if (TryToGetJson(element, "__type", token_type_) != simdjson::SUCCESS)
    return false;
  if (TryToGetJson(element, "content", content_) != simdjson::SUCCESS)
    return false;
  if (TryToGetJson(element, "lstrip", lstrip_) != simdjson::SUCCESS)
    return false;
  if (TryToGetJson(element, "normalized", normalized_) != simdjson::SUCCESS)
    return false;
  if (TryToGetJson(element, "rstrip", rstrip_) != simdjson::SUCCESS)
    return false;
  if (TryToGetJson(element, "single_word", single_word_) != simdjson::SUCCESS)
    return false;
  return true;
}

TokenConfig::TokenConfig() = default;

TokenConfig::~TokenConfig() = default;

static std::string PatchJsonText(const std::string& json_path) {
  std::string json_text;
  std::ifstream t(json_path);

  t.seekg(0, std::ios::end);
  json_text.reserve(t.tellg());
  t.seekg(0, std::ios::beg);

  json_text.assign((std::istreambuf_iterator<char>(t)),
                   std::istreambuf_iterator<char>());

  for (size_t n = 0; n < json_text.size(); ++n) {
    size_t num_len = 0;
    while (std::isdigit(json_text[n])) {
      num_len++;
      n++;
      if (n >= json_text.size()) {
        break;
      }
    }

    // if some number is too long, simpjson will fail to parse it
    if (num_len > 20) {
      json_text[n - 2] = '.';  // convert it to a floating number
    }
  }

  return json_text;
}

TfmStatus TokenConfig::LoadJson(const std::string& json_path) {
  simdjson::dom::parser parser;
  simdjson::dom::element root;

  if (!fs::exists(fs::path(json_path))) {
    return {kTfmErrorInvalidFile, std::string(json_path) + " not found"};
  }
  std::string json_text = PatchJsonText(json_path);
  auto parse_result = parser.parse(json_text);
  if (parse_result.error()) {
    std::string error_msg = simdjson::error_message(parse_result.error());
    return {kTfmErrorInvalidFile, error_msg};
  }
  root = parse_result.value();

  if (!FromJson(root)) {
    return {kTfmErrorInvalidFile, "failed to parse json config file"};
  }

  return {};
}

bool TokenConfig::FromJson(const simdjson::dom::element& element) {
  // Check if this is a PreTrainedTokenizerFast (Llama3) config
  std::string tokenizer_class;
  if (TryToGetJson(element, "tokenizer_class", tokenizer_class) == simdjson::SUCCESS &&
      tokenizer_class == "PreTrainedTokenizerFast") {
    // PreTrainedTokenizerFast format (Llama3)
    tokenizer_class_ = tokenizer_class;
    
    // Set defaults appropriate for Llama3
    add_bos_token_ = true; 
    add_eos_token_ = false;
    clean_up_tokenization_spaces_ = true;
    
    TryToGetJson(element, "clean_up_tokenization_spaces", clean_up_tokenization_spaces_);
    TryToGetJson(element, "model_max_length", model_max_length_);
    
    
    std::string bos_token_str;
    if (TryToGetJson(element, "bos_token", bos_token_str) == simdjson::SUCCESS) {
      bos_token_.content_ = bos_token_str;
    }
    
    std::string eos_token_str;
    if (TryToGetJson(element, "eos_token", eos_token_str) == simdjson::SUCCESS) {
      eos_token_.content_ = eos_token_str;
    }
    
    TryToGetJson(element, "pad_token", pad_token_);
    
    std::string unk_token_str;
    if (TryToGetJson(element, "unk_token", unk_token_str) == simdjson::SUCCESS) {
      unk_token_.content_ = unk_token_str;
    }
    
    auto added_tokens_decoder_result = element["added_tokens_decoder"];
    if (added_tokens_decoder_result.error() == simdjson::SUCCESS) {
      simdjson::dom::element added_tokens_decoder = added_tokens_decoder_result.value();
      auto tokens_result = added_tokens_decoder.get_object();
      if (tokens_result.error() == simdjson::SUCCESS) {
        for (auto field : tokens_result.value()) {
          simdjson::dom::element token_obj;
          auto error = field.value.get(token_obj);
          if (error == simdjson::SUCCESS) {
            std::string content;
            if (TryToGetJson(token_obj, "content", content) == simdjson::SUCCESS) {
              if (content == bos_token_.content_) {
                // Found BOS token entry - parse individual fields
                TryToGetJson(token_obj, "__type", bos_token_.token_type_);
                TryToGetJson(token_obj, "lstrip", bos_token_.lstrip_);
                TryToGetJson(token_obj, "normalized", bos_token_.normalized_);
                TryToGetJson(token_obj, "rstrip", bos_token_.rstrip_);
                TryToGetJson(token_obj, "single_word", bos_token_.single_word_);
              } else if (content == eos_token_.content_) {
                // Found EOS token entry - parse individual fields
                TryToGetJson(token_obj, "__type", eos_token_.token_type_);
                TryToGetJson(token_obj, "lstrip", eos_token_.lstrip_);
                TryToGetJson(token_obj, "normalized", eos_token_.normalized_);
                TryToGetJson(token_obj, "rstrip", eos_token_.rstrip_);
                TryToGetJson(token_obj, "single_word", eos_token_.single_word_);
              } else if (!unk_token_.content_.empty() && content == unk_token_.content_) {
                // Found UNK token entry - parse individual fields
                TryToGetJson(token_obj, "__type", unk_token_.token_type_);
                TryToGetJson(token_obj, "lstrip", unk_token_.lstrip_);
                TryToGetJson(token_obj, "normalized", unk_token_.normalized_);
                TryToGetJson(token_obj, "rstrip", unk_token_.rstrip_);
                TryToGetJson(token_obj, "single_word", unk_token_.single_word_);
              }
            }
          }
        }
      }
    }
    
    return true;
  }
  
  // Standard tokenizer format (Llama2, etc.)
  if (TryToGetJson(element, "add_bos_token", add_bos_token_) != simdjson::SUCCESS)
    return false;
  if (TryToGetJson(element, "add_eos_token", add_eos_token_) != simdjson::SUCCESS)
    return false;
  if (!bos_token_.FromJson(element["bos_token"]))
    return false;
  if (TryToGetJson(element, "clean_up_tokenization_spaces", clean_up_tokenization_spaces_) != simdjson::SUCCESS)
    return false;
  if (!eos_token_.FromJson(element["eos_token"]))
    return false;
  if (TryToGetJson(element, "model_max_length", model_max_length_) != simdjson::SUCCESS)
    return false;
  if (TryToGetJson(element, "pad_token", pad_token_) != simdjson::SUCCESS)
    return false;
  if (TryToGetJson(element, "tokenizer_class", tokenizer_class_) != simdjson::SUCCESS)
    return false;
  try {
    if (!unk_token_.FromJson(element["unk_token"]))
      return false;
  }
  catch (simdjson::simdjson_error& e) {
    // unk_token is missing
  }
  
  return true;
}

}  // namespace tfm
