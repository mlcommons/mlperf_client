#include "file_signature_verifier.h"

#include <log4cxx/logger.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

#include <fstream>
#include <nlohmann/json.hpp>

#include "json_schema.h"
#include "utils.h"

using namespace log4cxx;
LoggerPtr loggerFileVerifier(Logger::getLogger("ScenarioDataProvider"));

namespace cil {
FileSignatureVerifier::FileSignatureVerifier(
    const std::string& data_verification_file_path,
    const std::string& data_verification_file_schema_path) {
  if (data_verification_file_path.empty()) {
    LOG4CXX_ERROR(loggerFileVerifier,
                  "Data verificaiton file is not provided!");
    return;
  }

  // Load the JSON data
  nlohmann::json json_data;
  std::ifstream json_file(data_verification_file_path);
  if (!json_file.is_open()) {
    LOG4CXX_ERROR(loggerFileVerifier,
                  "Unable to load data verification file with path: "
                      << data_verification_file_path);
    return;
  }

  try {
    json_file >> json_data;
    // Validate the JSON data against the schema
    if (!cil::ValidateJSONSchema(data_verification_file_schema_path, json_data)
             .empty()) {
      LOG4CXX_ERROR(loggerFileVerifier,
                    "Data verification file schema validaiton failed, path: "
                        << data_verification_file_path);
      return;
    }
  } catch (std::exception& e) {
    LOG4CXX_ERROR(loggerFileVerifier,
                  "An exception occured while reading and validating of data "
                  "verification file, path: "
                      << data_verification_file_path
                      << ", exception: " << e.what());
    return;
  }

  auto& hashes = json_data.at("FileHashes");
  for (const auto& hash : hashes) {
    std::string file_hash;
    hash.get_to(file_hash);
    file_hashes_for_verification_.insert(file_hash);
  }
}

bool FileSignatureVerifier::Verify(const std::string& path,
                                   std::string& calculated_hash) {
  calculated_hash = utils::ComputeFileSHA256(path, loggerFileVerifier);
  return file_hashes_for_verification_.find(calculated_hash) !=
         file_hashes_for_verification_.end();
}

}  // namespace cil