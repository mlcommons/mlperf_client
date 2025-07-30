#ifndef MLPERF_CONSOLE_APPENDER_H_
#define MLPERF_CONSOLE_APPENDER_H_

#include <log4cxx/ConsoleAppender.h>

#include <iostream>

using namespace log4cxx;

class MLPerfConsoleAppender : public ConsoleAppender {
 public:
  DECLARE_LOG4CXX_OBJECT(MLPerfConsoleAppender)

  static std::recursive_mutex& accessMutex();
  static void SetCallback(std::function<void(const std::string&)> callback);

  void append(const spi::LoggingEventPtr& event, helpers::Pool& p) override;

 private:
  static std::recursive_mutex mutex_;
  static std::function<void(const std::string&)> callback_;
};

#endif  // MLPERF_CONSOLE_APPENDER_H_