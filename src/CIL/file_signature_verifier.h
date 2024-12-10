#ifndef FILE_SIGNATURE_VERIFIER_H_
#define FILE_SIGNATURE_VERIFIER_H_

#include <map>
#include <string>
#include <set>

namespace cil {

class FileSignatureVerifier {
 public:
  FileSignatureVerifier(const std::string& data_verification_file_path,
                        const std::string& data_verification_file_schema_path);
  bool Verify(const std::string& path, std::string& calculated_hash);

 private:
  std::set<std::string> file_hashes_for_verification_;
};
}  // namespace cil

#endif  // !FILE_SIGNATURE_VERIFIER_H_