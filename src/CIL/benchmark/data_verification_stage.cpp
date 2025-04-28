#include "data_verification_stage.h"

#include "file_signature_verifier.h"
#include "utils.h"

using namespace log4cxx;
namespace fs = std::filesystem;

namespace cil {
bool DataVerificationStage::Run(const ScenarioConfig& scenario_config,
                                ScenarioData& scenario_data,
                                const ReportProgressCb&) {
  std::string data_verification_file_path =
      scenario_config.GetDataVerificationFile();
  if (data_verification_file_path.empty() &&
      !unpacker_.UnpackAsset(Unpacker::Asset::kDataVerificationFile,
                             data_verification_file_path)) {
    LOG4CXX_ERROR(logger_,
                  "Failed to unpack data verification file, aborting...");
    return false;
  }

  auto verifier = std::make_unique<FileSignatureVerifier>(
      data_verification_file_path, data_verification_file_schema_path_);

  std::string calculated_hash;
  std::map<std::string, std::string> not_verified_files;

  for (const auto& model_file_path : scenario_data.model_file_paths) {
    if (!verifier->Verify(model_file_path, calculated_hash))
      not_verified_files[model_file_path] = calculated_hash;
  }
  for (const auto& input_file_path : scenario_data.input_file_paths) {
    if (!verifier->Verify(input_file_path, calculated_hash))
      not_verified_files[input_file_path] = calculated_hash;
  }

  if (not_verified_files.empty()) return true;

  LOG4CXX_WARN(logger_, "Following files used for the model "
                            << scenario_config.GetName()
                            << " cannot be verified, using at your risk:");
  for (const auto& [file_path, hash] : not_verified_files)
    LOG4CXX_WARN(logger_, "[" << utils::GetFileNameFromPath(file_path) << ": "
                              << hash << "]");

  return false;
}

}  // namespace cil
