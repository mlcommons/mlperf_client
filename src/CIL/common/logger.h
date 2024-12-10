#ifndef LOGGER_H_
#define LOGGER_H_

#include <string>

namespace cil {

enum class LogLevel {
  kInfo,
  kWarning,
  kError,
  kFatal,
};
using Logger = std::function<void(LogLevel, std::string)>;

}  // namespace cil

#endif  // !LOGGER_H_
