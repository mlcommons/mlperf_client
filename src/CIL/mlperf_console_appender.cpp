#include "mlperf_console_appender.h"

using namespace log4cxx;

std::recursive_mutex MLPerfConsoleAppender::mutex_;

std::recursive_mutex& MLPerfConsoleAppender::accessMutex() { return mutex_; }

void MLPerfConsoleAppender::append(const spi::LoggingEventPtr& event,
                                   helpers::Pool& p) {
  if (mutex_.try_lock()) {
    ConsoleAppender::append(event, p);
    mutex_.unlock();
  }
}

IMPLEMENT_LOG4CXX_OBJECT(MLPerfConsoleAppender);
