#ifndef SMC_RESULT_H_
#define SMC_RESULT_H_

#include <IOKit/IOKitLib.h>

#include <unordered_map>
#include <vector>
#include <string>

enum SMCResult : UInt8 { kSuccess = 0, kKeyNotFound = 132 };

enum SMCSelector : UInt8 {
  kSMCHandleYPCEvent = 2,
  kSMCReadKey = 5,
  kSMCGetKeyFromIndex = 8,
  kSMCGetKeyInfo = 9
};

struct DataType {
  DataType() {};
  DataType(const std::string& type_str, UInt32 size);
  DataType(FourCharCode type_code, UInt32 size);

  FourCharCode type = 0;
  UInt32 size = 0;
};

struct SMCVersion {
  UInt8 major = 0;
  UInt8 minor = 0;
  UInt8 build = 0;
  UInt8 reserved = 0;
  UInt16 release = 0;
};

struct SMCLimitData {
  UInt16 version = 0;
  UInt16 length = 0;
  UInt32 cpu_plimit = 0;
  UInt32 gpu_plimit = 0;
  UInt32 mem_plimit = 0;
};

struct SMCKeyInfoData {
  UInt32 data_size = 0;
  UInt32 data_type = 0;
  char data_attributes = 0;
};

struct SMCParamStruct {
  UInt32 key = 0;
  SMCVersion vers = SMCVersion();
  SMCLimitData plimit_data = SMCLimitData();
  SMCKeyInfoData key_info = SMCKeyInfoData();
  UInt8 result = 0;
  UInt8 status = 0;
  UInt8 data8 = 0;
  UInt32 data32 = 0;
  UInt8 bytes[32] = {0};
};

class SMCReader {
 public:
  SMCReader();
  ~SMCReader();

  void Open();
  void Close();

  std::vector<std::pair<std::string, float>> GetThermalSensorValues();

 private:
  DataType GetKeyInfo(const std::string& key);
  SMCParamStruct CallSMC(const SMCParamStruct& input,
                         SMCSelector selector = kSMCHandleYPCEvent);

  io_connect_t connection_handle_;
  std::unordered_map<std::string, DataType> key_info_cache_;
};

#endif  // SMC_RESULT_H_
