#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "tokenizer/tfmtok.h"

int main(int argc, char* argv[]) {
  const char* usage = "Usage: test_tfm_tokenizer";
  if (argc != 1) {
    std::cerr << usage << std::endl;
    return 1;
  }

  // Paths
  const std::string tokenizer_folder = "./llama2";
  const std::string input_file = "./llama2/tokenizer_tests.txt";
  const std::string sentencepiece_encode_file =
      "./llama2/tokenizer_encode_results_sentencepiece.txt";

  // Check file exists
  if (!std::filesystem::exists(tokenizer_folder)) {
    std::cerr << "Tokenizer folder does not exist: " << tokenizer_folder
              << std::endl;
    return 1;
  }
  if (!std::filesystem::exists(input_file)) {
    std::cerr << "Input file does not exist: " << input_file << std::endl;
    return 1;
  }
  if (!std::filesystem::exists(sentencepiece_encode_file)) {
    std::cerr << "Sentencepiece encode file does not exist: "
              << sentencepiece_encode_file << std::endl;
    return 1;
  }

  TfmStatus status;
  std::unique_ptr<tfm::Tokenizer> tokenizer =
      tfm::CreateTokenizer(tokenizer_folder, "", &status);
  std::cout << "Tokenizer created with status: " << status.code() << " - "
            << status.message() << std::endl;

  // Read all lines from input file, tokenize them, and compare with
  // sentencepiece results
  std::ifstream input_stream(input_file);
  std::ifstream sentencepiece_stream(sentencepiece_encode_file);

  std::string input_line;
  std::string sentencepiece_line;

  while (std::getline(input_stream, input_line) &&
         std::getline(sentencepiece_stream, sentencepiece_line)) {
    std::vector<std::string> tokens;
    std::vector<std::string_view> input_texts = {input_line};
    std::vector<std::vector<tfmTokenId_t>> token_ids;
    tokenizer->Tokenize(input_texts, token_ids);
    std::string tokenized_line = "";
    // Remove the first token, which is the BOS token
    for (size_t i = 1; i < token_ids[0].size(); i++) {
      tokenized_line += std::to_string(token_ids[0][i]) + " ";
    }
    // Strip trailing space
    tokenized_line = tokenized_line.substr(0, tokenized_line.size() - 1);
    if (tokenized_line != sentencepiece_line) {
      std::cerr << "Tokenization mismatch: " << "Line: " << input_line << " -> "
                << tokenized_line << " vs " << sentencepiece_line << std::endl;
      return 1;
    } else {
      std::cout << "Tokenization match: " << "Line: " << input_line << " -> "
                << tokenized_line << std::endl;
    }
  }

  return 0;
}