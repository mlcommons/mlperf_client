﻿// Licensed under the MIT License.
#include <gsl/narrow>
#include <fstream>

#include "../filesystem.h"
#include "token_bpe.h"

namespace tfm {

const char kModel_GPT2[] = "GPT2";
const char kModel_Roberta[] = "Roberta";
const char kModel_CLIP[] = "CLIP";
const char kModel_CodeGen[] = "CodeGen";
const char kModel_Llama[] = "Llama";
const char KModel_Gemma[] = "Gemma";
const char kModel_Llama3[] = "Llama3";  // Used for models with tiktoken encoding

bool BPETokenizer::IsSupportedModel(const std::string_view& model_name) {
  return model_name == kModel_GPT2 || model_name == kModel_Roberta || model_name == kModel_CLIP ||
         model_name == kModel_CodeGen || model_name == kModel_Llama || model_name == KModel_Gemma ||
         model_name == kModel_Llama3;
}

void BPETokenizer::CreateByteEncoder() {
  char32_t index = 256;
  for (char32_t i = 0; i < 256; ++i) {
    /*
    bs = (
      list(range(ord("!"), ord("~") + 1)) + list(range(ord("¡"), ord("¬") + 1)) + list(range(ord("®"), ord("ÿ") + 1))
    )
    */
    if ((/* i >= 0 && */ i < 33) || (i >= 127 && i < 161) || (i == 173)) {
      byte_decoder_[index] = gsl::narrow<unsigned char>(i);
      byte_encoder_[i] = bbpe_encoder_.GetTokenId(EncodeUTF8Char(index++));
    } else {
      byte_encoder_[i] = bbpe_encoder_.GetTokenId(EncodeUTF8Char(i));
      byte_decoder_[i] = gsl::narrow<unsigned char>(i);
    }
  }
}

BPETokenizer::BPETokenizer() {}

BPETokenizer::~BPETokenizer() = default;

std::string_view BPETokenizer::ModelName() const { return model_name_; }

BpeModelType BPETokenizer::ModelType() const {
  auto name = ModelName();
  auto type = BpeModelType::kBmtGPT2;
  
  if (name == kModel_Llama || name == KModel_Gemma) {
    type = BpeModelType::kBmtSPM;
  } else if (name == kModel_CLIP) {
    type = BpeModelType::kBmtCLIP;
  } else if (name == kModel_Llama3) {
    type = BpeModelType::kBmtTKTM;
  }

  return type;
}

void BPETokenizer::LoadPredefinedTokens(const TokenConfig& config) {
  unk_token_ = config.unk_token_.content_;
  bos_token_ = config.bos_token_.content_;
  eos_token_ = config.eos_token_.content_;
  pad_token_ = config.pad_token_;

  unk_token_id_ = bbpe_encoder_.GetTokenId(unk_token_);
  bos_token_id_ = bbpe_encoder_.GetTokenId(bos_token_);
  eos_token_id_ = bbpe_encoder_.GetTokenId(eos_token_);
  pad_token_id_ = bbpe_encoder_.GetTokenId(pad_token_);

  added_tokens_.emplace(std::pair(unk_token_id_, unk_token_));
  added_tokens_.emplace(std::pair(bos_token_id_, bos_token_));
  added_tokens_.emplace(std::pair(eos_token_id_, eos_token_));
  added_tokens_.emplace(std::pair(pad_token_id_, pad_token_));

  all_special_ids_.emplace(unk_token_id_);
  all_special_ids_.emplace(bos_token_id_);
  all_special_ids_.emplace(eos_token_id_);
  all_special_ids_.emplace(pad_token_id_);

  auto model_name = ModelName();
  auto model_type = ModelType();

  if (model_name == kModel_Llama) {
    add_dummpy_prefix_ = true;
  }

  if (model_type != BpeModelType::kBmtGPT2) {
    add_bos_token_ = true;
    add_eos_token_ = true;
  }

  if (model_type == BpeModelType::kBmtSPM) {
    add_eos_token_ = false;
  }
  
  if (model_type == BpeModelType::kBmtTKTM) {
    add_bos_token_ = true;
    add_eos_token_ = false;
  }
}

TfmStatus BPETokenizer::DecodeExtraArgs(const simdjson::dom::element& root) {
  auto decoder_result = root.at_key("decoder");
  if (decoder_result.error() != simdjson::SUCCESS && decoder_result.error() != simdjson::NO_SUCH_FIELD) {
    return {kTfmErrorInvalidFile, "Cannot parse the decoder section in the the tokenizer.json"};
  }
  
  if (decoder_result.error() == simdjson::SUCCESS) {
    simdjson::dom::element decoder_obj = decoder_result.value();
    TryToGetJson(decoder_obj, "add_prefix_space", decode_extra_args_.add_prefix_space);
  }
  
  return TfmStatus::OK();
}

TfmStatus BPETokenizer::OnLoad() {
  auto& config = *GetConfig();
  
  if (config.tokenizer_class_ == "PreTrainedTokenizerFast") {
    model_name_ = kModel_Llama3;
  } else {
    model_name_ = std::string_view(config.tokenizer_class_.c_str(), config.tokenizer_class_.find("Tokenizer"));
  }
  
  std::string tokenizer_file = GetDataDir() + "/tokenizer.json";
  if (!fs::exists(tokenizer_file)) {
    return {kTfmErrorInvalidFile, "Failed to find tokenizer.json"};
  }
  
  simdjson::dom::parser parser;
  simdjson::dom::element root;
  auto error = parser.load(tokenizer_file);
  if (error.error() != simdjson::SUCCESS) {
    return {kTfmErrorInvalidFile, "Failed to load tokenizer.json"};
  }
  root = error.value();
  
  auto status = bbpe_encoder_.Load(root, config);
  if (!status.ok()) {
    return status;
  }
  
  CreateByteEncoder();
  
  error = root["added_tokens"];
  if (error.error() == simdjson::SUCCESS) {
    simdjson::dom::element added_tokens_obj = error.value();
    status = extended_token_.LoadAddedTokens(added_tokens_obj, added_tokens_);
    bbpe_encoder_.UpdateAddedTokensMap(added_tokens_);
  } else {
    std::string_view added_tokens[] = {config.unk_token_.content_, config.eos_token_.content_,
                                       config.bos_token_.content_, config.pad_token_};
    size_t num_added_tokens = sizeof(added_tokens) / sizeof(added_tokens[0]);

    if (config.pad_token_.empty()) {
      num_added_tokens--;
    }
    if (config.bos_token_.content_.empty()) {
      num_added_tokens--;
    }
    std::vector<uint32_t> ids(num_added_tokens);
    for (size_t i = 0; i < num_added_tokens; ++i) {
      ids[i] = bbpe_encoder_.GetTokenId(std::string(added_tokens[i]));
    }

    status = extended_token_.LoadAddedTokens(added_tokens, ids.data(), num_added_tokens);
  }
  
  if (!status.ok()) {
    return status;
  }
  
  LoadPredefinedTokens(config);
  arr_vocab_ = bbpe_encoder_.BuildDecoder();
  status = DecodeExtraArgs(root);
  
  return status;
}

static bool IsUnicodeSpace(char32_t ch) {
  const std::set<char32_t> unicode_spaces = {
      0x0009,  // CHARACTER TABULATION
      0x000A,  // LINE FEED (LF)
      0x000B,  // LINE TABULATION
      0x000C,  // FORM FEED (FF)
      0x000D,  // CARRIAGE RETURN (CR)
      0x001C,  // FILE SEPARATOR
      0x001D,  // GROUP SEPARATOR
      0x001E,  // RECORD SEPARATOR
      0x001F,  // UNIT SEPARATOR
      0x0020,  // SPACE
      0x0085,  // NEXT
      0x00A0,  // NO-BREAK SPACE
      0x1680,  // OGHAM SPACE MARK
      0x2000,  // EN QUAD
      0x2001,  // EM QUAD
      0x2002,  // EN SPACE
      0x2003,  // EM SPACE
      0x2004,  // THREE-PER-EM SPACE
      0x2005,  // FOUR-PER-EM SPACE
      0x2006,  // SIX-PER-EM SPACE
      0x2007,  // FIGURE SPACE
      0x2008,  // PUNCTUATION SPACE
      0x2009,  // THIN SPACE
      0x200A,  // HAIR SPACE
      0x2028,  // LINE SEPARATOR
      0x2029,  // PARAGRAPH SEPARATOR
      0x202F,  // NARROW NO-BREAK SPACE
      0x205F,  // MEDIUM MATHEMATICAL SPACE
      0x3000,  // IDEOGRAPHIC SPACE
  };
  return unicode_spaces.count(ch) > 0;
}

// only support latin now
static char32_t ToLower(char32_t c) {
  if ((c >= 'A') && (c <= 'Z')) {
    return c + 'a' - 'A';
  } else if ((c >= U'À' && (c <= U'ß'))) {
    return c + U'à' - U'À';
  }
  return c;
}

static bool AllSpaceUstring(const std::u32string& str) {
  return std::all_of(str.begin(), str.end(), [](char32_t ch) { return IsUnicodeSpace(ch); });
}

static std::u32string RemoveConsecutiveSpaces(const std::u32string& input) {
  std::u32string result;
  result.reserve(input.size());
  bool lastWasSpace = false;

  for (auto ch : input) {
    if (IsUnicodeSpace(ch)) {
      if (!lastWasSpace) {
        result.push_back(ch);
      }
      lastWasSpace = true;
    } else {
      result.push_back(ch);
      lastWasSpace = false;
    }
  }

  return result;
}

std::vector<tfmTokenId_t> BPETokenizer::Encode(std::string_view sv_input, int64_t max_length,
                                               bool compute_offset_mapping,
                                               std::list<OffsetMappingType>& offset_map) const {
  std::vector<tfmTokenId_t> res;
  std::list<std::pair<uint32_t, uint32_t>> byte_list;

  std::u32string input = FromUTF8(sv_input);

  auto model_type = ModelType();
  auto model_name = ModelName();

  bool clean_up_spaces = false;
  if (model_name == kModel_CLIP) {
    clean_up_spaces = true;
    // Merges consecutive '\s+' for CLIP
    /*
      text = re.sub(r"\s+", " ", text)
      text = text.strip()
    */
    std::u32string str = RemoveConsecutiveSpaces(input);
    if (!str.empty()) {
      if (IsUnicodeSpace(str.front())) {
        str.erase(str.begin());
      }
      if (IsUnicodeSpace(str.back())) {
        str.pop_back();
      }
      // remove newlines as CLIP ignores them (treats them as whitespace which is then cleaned)
      str.erase(std::remove(str.begin(), str.end(), U'\n'), str.end());
      str.erase(std::remove(str.begin(), str.end(), U'\r'), str.end());
    }
    input = str;
  }

  if (AllSpaceUstring(input) && model_name == kModel_CLIP) {
    res.push_back(bos_token_id_);
    res.push_back(eos_token_id_);
    return res;
  }

  if (add_bos_token_) {
    // Add BOS token to result
    res.push_back(bos_token_id_);
  }
  if (model_name == kModel_CLIP) {
    // Convert to lowercase
    std::transform(input.begin(), input.end(), input.begin(),
                   [](char32_t c) { return static_cast<char32_t>(ToLower(c)); });
  }

  // Parse input
  auto special_token_split_res = extended_token_.Split(input);
  bpe::TokenWithRegularExp regcmp;

  for (auto& seg_id : special_token_split_res) {
    if (static_cast<int64_t>(res.size()) >= max_length) break;

    if (seg_id.second != bpe::kInvalidTokenId) {
      res.push_back(seg_id.second);
      continue;
    }

    // Note: keep ptr to make sure the string_view is valid in the following process
    std::u32string str(seg_id.first);
    regcmp.Set(str);

    size_t offset = 0;
    OffsetMappingType offset_mapping;

    if (compute_offset_mapping) {
      if (model_name != kModel_GPT2) {
        // Add offset mapping for BOS token
        offset_mapping.emplace_back(0, 0);
      }
    }

    while (static_cast<int64_t>(res.size()) < max_length) {
      auto [b, tok] = regcmp.GetNextToken();

      if (!b) break;

      std::string utf8_token = ToUTF8(std::u32string(tok));

      int32_t space_dif = 0;
      if (compute_offset_mapping) {
        // Handle special case for offset mapping
        if (utf8_token.at(0) == ' ') {
          offset++;
          space_dif = -1;  // account for spaces used in offset map algorithm in bpe(byte_list_)
        }
      }

      // Get byte encodings prior to performing BPE
      byte_list.clear();

      if (clean_up_spaces) {
        // Whitespace clean
        utf8_token.erase(std::remove(utf8_token.begin(), utf8_token.end(), ' '), utf8_token.end());

        for (size_t i = 0; i < utf8_token.length(); i++) {
          if (i == utf8_token.length() - 1) {
            std::string boundary(1, utf8_token[i]);
            byte_list.emplace_back(bbpe_encoder_.GetTokenId(boundary + "</w>"), 1);
          } else {
            byte_list.emplace_back(byte_encoder_[static_cast<unsigned char>(utf8_token[i])], 1);
          }
        }
      } else {
        for (char& cp : utf8_token) {
          byte_list.emplace_back(byte_encoder_[static_cast<unsigned char>(cp)], 1);
        }
      }

      // Perform BPE
      bbpe_encoder_.PerformBPE(byte_list);

      // Add output to result
      for (const auto& p : byte_list) {
        if (static_cast<int64_t>(res.size()) >= max_length) {
          break;
        }

        res.push_back(p.first);

        if (compute_offset_mapping) {
          if (clean_up_spaces) {
            offset_mapping.emplace_back(std::make_pair(offset, gsl::narrow<size_t>(offset + p.second)));
            offset += p.second;
          } else {
            offset_mapping.emplace_back(std::make_pair(offset, gsl::narrow<size_t>(offset + p.second + space_dif)));
            offset += ((size_t)p.second + space_dif);
          }
        }
      }
    }

    if (compute_offset_mapping) {
      if (model_name != kModel_GPT2) {
        // Add offset mapping for EOS token
        offset_mapping.emplace_back(std::make_pair(0, 0));
      }
      // Add offset mappings for input in this instance to list of offset mappings for all inputs
      offset_map.emplace_back(offset_mapping);
    }
  }

  if (add_eos_token_) {
    // Add EOS token to result
    res.push_back(eos_token_id_);
  }

  return res;
}

static std::string ReplaceAll(std::string_view s, const std::string& search, const std::string& replace) {
  std::string result;
  for (size_t pos = 0;; pos += search.length()) {
    auto new_pos = s.find(search, pos);
    if (new_pos == std::string::npos) {
      result += s.substr(pos, s.size() - pos);
      break;
    }
    result += s.substr(pos, new_pos - pos);
    result += replace;
    pos = new_pos;
  }

  return result;
}

static const char spm_underscore[] = "\xe2\x96\x81";

std::vector<tfmTokenId_t> BPETokenizer::SpmEncode(std::string_view sv_input, int64_t max_length,
                                                  bool /*compute_offset_mapping */,
                                                  std::list<OffsetMappingType>& /* offset_map */) const {
  std::vector<tfmTokenId_t> res;
  std::list<std::pair<uint32_t, uint32_t>> byte_list;

  if (add_bos_token_) {
    res.push_back(bos_token_id_);
  }

  // Replace all spaces with '▁' for SP model
  std::string prefix = add_dummpy_prefix_ ? spm_underscore : "";
  auto escaped_input = prefix + ReplaceAll(sv_input, " ", spm_underscore);
  auto ucs32_input = FromUTF8(escaped_input);
  auto special_token_split_res = extended_token_.Split(ucs32_input);

  for (auto& seg_id : special_token_split_res) {
    if (static_cast<int64_t>(res.size()) >= max_length) {
      break;
    }

    byte_list.clear();
    int id = seg_id.second;
    if (id != bpe::kInvalidTokenId) {
      size_t utf8_len = 0;
      for (auto chr : seg_id.first) {
        utf8_len += UTF8Len(chr);
      }
      byte_list.emplace_back(static_cast<tfmTokenId_t>(id), gsl::narrow<uint32_t>(utf8_len));
    } else {
      for (size_t offset = 0; offset < seg_id.first.size(); ++offset) {
        auto utf8s = EncodeUTF8Char(seg_id.first[offset]);
        auto byte_id = bbpe_encoder_.GetTokenId(utf8s);
        if (static_cast<int>(byte_id) == bpe::kInvalidTokenId) {
          byte_id = unk_token_id_;  // for safety
        }

        byte_list.emplace_back(static_cast<tfmTokenId_t>(byte_id), gsl::narrow<uint32_t>(utf8s.length()));
      }
    }
    
    bbpe_encoder_.PerformBPE(byte_list);

    for (const auto& p : byte_list) {
      if (static_cast<int64_t>(res.size()) >= max_length) {
        break;
      }

      res.push_back(p.first);
    }
  }

  if (add_eos_token_) {
    res.push_back(eos_token_id_);
  }

  return res;
}

TfmStatus BPETokenizer::Encode(std::string_view sv_input, std::vector<tfmTokenId_t>& ids) const {
  std::list<OffsetMappingType> offset_map;
  auto max_length = padding_length_ < 0 ? std::numeric_limits<uint32_t>::max() : padding_length_;
  
  if (ModelType() == BpeModelType::kBmtTKTM) {
    ids = TKTMEncode(sv_input, max_length, false, offset_map);
  } else if (ModelType() == BpeModelType::kBmtSPM) {
    ids = SpmEncode(sv_input, max_length, false, offset_map);
  } else {
    ids = Encode(sv_input, max_length, false, offset_map);
  }
  
  return TfmStatus::OK();
}

TfmStatus BPETokenizer::Decode(const span<tfmTokenId_t const>& ids, std::string& text) const {
  // Handle standard decoder types
  bool f_special_last = false;
  std::vector<tfmTokenId_t> ids_copy(ids.begin(), ids.end());

  auto model_type = ModelType();

  if (model_type == BpeModelType::kBmtTKTM) {
    // Remove special tokens
    ids_copy.erase(
      std::remove_if(
        ids_copy.begin(), ids_copy.end(),
        [this](tfmTokenId_t id) { return id >= bbpe_encoder_.GetMaxTokenId(); }
      ),
      ids_copy.end()
    );
  }

  auto count = static_cast<size_t>(ids_copy.size());
  auto p_ids = ids_copy.data();

  auto& args = decode_extra_args_;
  for (size_t tok_idx = 0; tok_idx < count; ++tok_idx) {
    const auto id = *(p_ids + tok_idx);
    std::string decoded_token;
    auto status = model_type == BpeModelType::kBmtSPM
                     ? SpmId2Token(id, decoded_token, f_special_last)
                     : Id2Token(id, decoded_token, args.skip_special_tokens_, f_special_last);

    if (!status.ok()) {
      return status;
    }

    bool f_special = all_special_ids_.count(id) ? true : false;

    if (args.whitespace_token_ && f_special && (tok_idx > 0 && !f_special_last)) {
      text.push_back(' ');
    }

    text.append(decoded_token);

    if (args.whitespace_token_ && f_special && tok_idx != count - 1) {
      text.push_back(' ');
    }
  }

  if (model_type == BpeModelType::kBmtCLIP && !text.empty() && text.back() == ' ') {
    text.pop_back();
  }

  return TfmStatus::OK();
}

TfmStatus BPETokenizer::Id2Token(tfmTokenId_t id, std::string& token, bool skip_special_tokens,
                                 bool& f_special_last) const {
  bool f_special = all_special_ids_.count(id) ? true : false;
  if (skip_special_tokens && f_special) {
    f_special_last = f_special;
    return TfmStatus::OK();
  }

  if (added_tokens_.count(id)) {
    const std::string ws = added_tokens_.at(id);
    token = (std::string)ws;
  } else if (static_cast<size_t>(id) < arr_vocab_.size()) {
    const auto str = FromUTF8(arr_vocab_[id]);
    for (auto wchr : str) {
      if (byte_decoder_.count(wchr) == 0 && wchr <= 0xFF) {
        token.push_back(gsl::narrow<unsigned char>(wchr));
      } else {
        unsigned char uchr = byte_decoder_.at(wchr);
        token.push_back(uchr);
      }
    }
  } else {
    if (skip_special_tokens) {
      f_special_last = f_special;
      return TfmStatus::OK();
    } else {
      token = unk_token_;
    }
  }

  // remove the end_word_suffix like </w> or </s> etc.
  auto& end_word_suffix = bbpe_encoder_.GetEndWordSuffix();
  if (end_word_suffix.size() > 0) {
    if (token.size() >= end_word_suffix.size() &&
        token.substr(token.size() - end_word_suffix.size()) == end_word_suffix) {
      token = token.substr(0, token.size() - end_word_suffix.size());
      token += ' ';
    }
  }

  f_special_last = f_special;
  return TfmStatus::OK();
}

TfmStatus BPETokenizer::SpmId2Token(tfmTokenId_t id, std::string& token, bool& f_special_last) const {
  std::string_view piece = id < arr_vocab_.size() ? arr_vocab_[id] : "";
  bool f_special = false;
  if (piece.empty() || all_special_ids_.count(id)) {
    token = "";
    f_special = true;
  } else if (IsSpmByteWord(piece)) {
    char buf[3] = {piece[3], piece[4], 0};  // something like <0x20>
    token = {static_cast<char>(strtol(buf, NULL, 16))};
  } else {
    token = ReplaceAll(piece, spm_underscore, " ");
  }

  if (!token.empty() && token[0] == ' ' && f_special_last && add_dummpy_prefix_) {
    token = token.substr(1);
  }

  f_special_last = f_special;
  return {};
}

TfmStatus BPETokenizer::Id2Token(tfmTokenId_t id, std::string& token, DecoderState** state) const {
  // Standard implementation for all tokenizer types
  auto bpe_state = static_cast<BPEDeocerState*>(*state);
  std::unique_ptr<BPEDeocerState> bpe_state_ptr;
  bool is_first = false;
  if (bpe_state == nullptr) {
    bpe_state_ptr = std::make_unique<BPEDeocerState>();
    bpe_state = bpe_state_ptr.get();
    is_first = true;
  }

  bool f_special_last = bpe_state->f_special_last;
  auto status = ModelType() == BpeModelType::kBmtSPM
                    ? SpmId2Token(id, token, bpe_state->f_special_last)
                    : Id2Token(id, token, decode_extra_args_.skip_special_tokens_, bpe_state->f_special_last);

  if (status.ok()) {
    if (bpe_state_ptr) {
      *state = bpe_state_ptr.release();
    }
  }

  return status;
}

// Implementation for Llama3 with tiktoken encoding (TKTM)
std::vector<tfmTokenId_t> BPETokenizer::TKTMEncode(
    std::string_view sv_input, int64_t max_length, bool compute_offset_mapping,
    std::list<OffsetMappingType>& offset_map) const {
  
  std::vector<tfmTokenId_t> res;
  if (sv_input.empty()) {
    return res;
  }

  std::u32string input = FromUTF8(sv_input);

  if (decode_extra_args_.add_prefix_space && (input.empty() || input.front() != U' ')) {
    input.insert(input.begin(), U' ');
  }

  if (add_bos_token_) {
    res.push_back(bos_token_id_);
  }

  OffsetMappingType offset_mapping;
  size_t offset = 0;
  if (compute_offset_mapping && add_bos_token_) {
    offset_mapping.emplace_back(0, 0);
  }

  auto special_token_split_res = extended_token_.Split(input);
  bpe::TokenWithRegularExp regcmp;

  for (auto& seg_id : special_token_split_res) {
    if (static_cast<int64_t>(res.size()) >= max_length) {
      break;
    }

    // If this segment is a special token, just emit its ID
    if (seg_id.second != bpe::kInvalidTokenId) {
      res.push_back(seg_id.second);
      if (compute_offset_mapping) {
        offset_mapping.emplace_back(0, 0);
      }
      continue;
    }

    // Otherwise, run the tiktoken regex split on this segment
    std::u32string seg_str(seg_id.first);
    regcmp.Set(seg_str);

    while (static_cast<int64_t>(res.size()) < max_length) {
      auto [ok, tok] = regcmp.GetNextToken();
      if (!ok) break;

      std::string utf8_token = ToUTF8(std::u32string(tok));

      std::list<std::pair<uint32_t, uint32_t>> byte_list;
      for (unsigned char c : utf8_token) {
        byte_list.emplace_back(byte_encoder_[c], 1);
      }

      bbpe_encoder_.PerformBPE(byte_list);

      for (const auto& p : byte_list) {
        if (static_cast<int64_t>(res.size()) >= max_length) {
          break;
        }
        res.push_back(p.first);
        if (compute_offset_mapping) {
          offset_mapping.emplace_back(offset, offset + p.second);
          offset += p.second;
        }
      }
    }
  }

  if (add_eos_token_ && static_cast<int64_t>(res.size()) < max_length) {
    res.push_back(eos_token_id_);
    if (compute_offset_mapping) {
      offset_mapping.emplace_back(0, 0);
    }
  }

  if (compute_offset_mapping) {
    offset_map.emplace_back(offset_mapping);
  }

  return res;
}

}  // namespace tfm
