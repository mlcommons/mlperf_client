#include "performance_query.h"

#include <log4cxx/logger.h>

#include "performance_counter_data.h"
#include "performance_counter_group.h"

#pragma comment(lib, "pdh.lib")

using namespace log4cxx;
extern LoggerPtr loggerSystemInfoProviderWindows;

namespace {
std::string GetLastErrorAsString() {
  DWORD errorMessageID = ::GetLastError();
  if (errorMessageID == 0) return std::string();

  LPSTR messageBuffer = nullptr;
  size_t size = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      (LPSTR)&messageBuffer, 0, NULL);

  std::string message(messageBuffer, size);

  LocalFree(messageBuffer);
  return message;
}
}  // namespace

namespace cil {

PerformanceQuery::PerformanceQuery() : query_handle_(INVALID_HANDLE_VALUE) {}

PerformanceQuery::~PerformanceQuery() {
  counter_groups_.clear();
  Close();
}

bool PerformanceQuery::Open() {
  PDH_STATUS status = PdhOpenQuery(NULL, 0, &query_handle_);
  if (status != ERROR_SUCCESS) {
    LOG4CXX_ERROR(
        loggerSystemInfoProviderWindows,
        "PdhOpenQuery failed: " << status << ", " << GetLastErrorAsString());
    return false;
  }

  return true;
}

bool PerformanceQuery::Close() {
  if (query_handle_ == INVALID_HANDLE_VALUE) return true;

  PDH_STATUS status = PdhCloseQuery(query_handle_);
  if (status != ERROR_SUCCESS) {
    LOG4CXX_ERROR(
        loggerSystemInfoProviderWindows,
        "PdhCloseQuery failed: " << status << ", " << GetLastErrorAsString());
    return false;
  }

  return true;
}

bool PerformanceQuery::Query() {
  PDH_STATUS status = PdhCollectQueryData(query_handle_);
  if (status != ERROR_SUCCESS) {
    LOG4CXX_ERROR(loggerSystemInfoProviderWindows,
                  "PdhCollectQueryData failed: " << status << ", "
                                                 << GetLastErrorAsString());
    return false;
  }

  return true;
}

bool PerformanceQuery::AddCounterGroup(
    std::shared_ptr<PerformanceCounterGroup> counter_group) {
  bool result = true;
  for (auto& counter : counter_group->counters_) {
    PDH_STATUS status =
        PdhAddCounterA(query_handle_, counter.counter_path.c_str(), 0,
                       &counter.counter_handle);
    if (status != ERROR_SUCCESS) {
      LOG4CXX_ERROR(loggerSystemInfoProviderWindows,
                    "PdhAddCounter (" << counter.counter_path
                                      << ") failed: " << status << ", "
                                      << GetLastErrorAsString());
      result = false;
    }
  }

  counter_groups_.push_back(counter_group);
  return result;
}

bool PerformanceQuery::FormatCounters() {
  bool result = true;
  for (auto& counter_group : counter_groups_) result &= counter_group->Format();

  return result;
}

}  // namespace cil