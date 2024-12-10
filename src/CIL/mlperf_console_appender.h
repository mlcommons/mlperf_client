#ifndef MLPERF_CONSOLE_APPENDER_H_
#define MLPERF_CONSOLE_APPENDER_H_

#include <log4cxx/ConsoleAppender.h>

#include <iostream>

using namespace log4cxx;

class MLPerfConsoleAppender : public ConsoleAppender {
 public:
  DECLARE_LOG4CXX_OBJECT(MLPerfConsoleAppender)

  static std::recursive_mutex& accessMutex();

  void append(const spi::LoggingEventPtr& event, helpers::Pool& p) override;

 private:
  static std::recursive_mutex mutex_;
};

#endif  // MLPERF_CONSOLE_APPENDER_H_