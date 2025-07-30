#include "smc_reader.h"

#include <log4cxx/logger.h>

using namespace log4cxx;
LoggerPtr loggerSMCReader(Logger::getLogger("SystemInfoProvider"));

namespace {

FourCharCode StringToFourCharCode(const std::string& str) {
  const char* bytes = str.c_str();
  return (static_cast<UInt32>(bytes[0]) << 24) |
         (static_cast<UInt32>(bytes[1]) << 16) |
         (static_cast<UInt32>(bytes[2]) << 8) | static_cast<UInt32>(bytes[3]);
}

std::string FourCharCodeToString(FourCharCode code) {
  return std::string{static_cast<char>(code >> 24),
                     static_cast<char>(code >> 16),
                     static_cast<char>(code >> 8), static_cast<char>(code)};
}

UInt32 BytesToUInt32(const char* str, int size) {
  UInt32 total = 0;
  for (int i = 0; i < size; i++)
    total += ((unsigned char)(str[i]) << (size - 1 - i) * 8);

  return total;
}

}  // namespace

DataType::DataType(const std::string& str, UInt32 sz)
    : type(StringToFourCharCode(str)), size(sz) {}

DataType::DataType(FourCharCode code, UInt32 sz) : type(code), size(sz) {}

SMCReader::SMCReader() : connection_handle_(0) {}

SMCReader::~SMCReader() { Close(); }

void SMCReader::Open() {
  io_service_t service = IOServiceGetMatchingService(
      MACH_PORT_NULL, IOServiceMatching("AppleSMC"));
  if (!service) {
    LOG4CXX_ERROR(loggerSMCReader, "Unable to find the SMC service.");
  }

  if (IOServiceOpen(service, mach_task_self_, 0, &connection_handle_) !=
      kIOReturnSuccess) {
    IOObjectRelease(service);
    LOG4CXX_ERROR(loggerSMCReader, "Unable to open a connection to the SMC.");
  }

  IOObjectRelease(service);
}

void SMCReader::Close() {
  if (connection_handle_ != 0) {
    IOServiceClose(connection_handle_);
    connection_handle_ = 0;
  }
}

DataType SMCReader::GetKeyInfo(const std::string& key) {
  auto it = key_info_cache_.find(key);
  if (it != key_info_cache_.end()) {
    return it->second;
  }

  SMCParamStruct input = {};
  input.key = StringToFourCharCode(key);
  input.data8 = kSMCGetKeyInfo;

  SMCParamStruct result = CallSMC(input);
  FourCharCode type_code =
      *reinterpret_cast<FourCharCode*>(&result.key_info.data_type);

  DataType type(type_code, result.key_info.data_size);
  key_info_cache_[key] = type;
  return type;
}

SMCParamStruct SMCReader::CallSMC(const SMCParamStruct& input,
                                  SMCSelector selector) {
  SMCParamStruct output = {};
  size_t input_size = sizeof(SMCParamStruct);
  size_t output_size = sizeof(SMCParamStruct);

  kern_return_t status = IOConnectCallStructMethod(
      connection_handle_, selector, &input, input_size, &output, &output_size);

  if (status == kIOReturnSuccess && output.result == kSuccess) {
    return output;
  } else if (status == kIOReturnSuccess && output.result == kKeyNotFound) {
    throw std::runtime_error("SMC key not found.");
  } else if (status == kIOReturnNotPrivileged) {
    throw std::runtime_error("Insufficient privileges to access SMC key.");
  }

  throw std::runtime_error("Unknown error during SMC communication.");
}

std::vector<std::pair<std::string, float>> SMCReader::GetThermalSensorValues() {
  std::vector<std::pair<std::string, float>> results;

  const std::string key_count_str = "#KEY";
  DataType key_count_type = GetKeyInfo(key_count_str);
  SMCParamStruct key_count_read = {};
  key_count_read.key = StringToFourCharCode(key_count_str);
  key_count_read.data8 = kSMCReadKey;
  key_count_read.key_info.data_size = key_count_type.size;

  SMCParamStruct count_result = CallSMC(key_count_read);
  UInt32 total_keys = BytesToUInt32((char*)count_result.bytes, 4);

  for (UInt32 i = 0; i < total_keys; ++i) {
    SMCParamStruct index_query = {};
    index_query.data8 = kSMCGetKeyFromIndex;
    index_query.data32 = i;

    SMCParamStruct key_result;
    try {
      key_result = CallSMC(index_query);
    } catch (const std::exception& e) {
      LOG4CXX_ERROR(loggerSMCReader, e.what());
      continue;
    }

    std::string key = FourCharCodeToString(key_result.key);
    if (!(key.rfind("Tg", 0) == 0 || key.rfind("Tp", 0) == 0)) {
      continue;
    }

    try {
      DataType type = GetKeyInfo(key);
      SMCParamStruct value_query = {};
      value_query.key = key_result.key;
      value_query.key_info.data_size = type.size;
      value_query.data8 = kSMCReadKey;

      SMCParamStruct value_result = CallSMC(value_query);

      float temp = 0.0f;
      if (type.type == StringToFourCharCode("sp78") && type.size == 2) {
        temp = ((value_result.bytes[0] << 8) | value_result.bytes[1]) / 256.0f;
      } else if (type.type == StringToFourCharCode("flt ") && type.size == 4) {
        memcpy(&temp, value_result.bytes, sizeof(float));
      } else {
        continue;
      }

      results.emplace_back(key, temp);
    } catch (const std::exception& e) {
      LOG4CXX_ERROR(loggerSMCReader, e.what());
    }
  }

  return results;
}
