#include "mlperf_console_appender.h"

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

using namespace log4cxx;

std::recursive_mutex MLPerfConsoleAppender::mutex_;
std::function<void(const std::string&)> MLPerfConsoleAppender::callback_;

std::recursive_mutex& MLPerfConsoleAppender::accessMutex() { return mutex_; }

void MLPerfConsoleAppender::SetCallback(
    std::function<void(const std::string&)> callback) {
  callback_ = callback;
}

void MLPerfConsoleAppender::append(const spi::LoggingEventPtr& event,
                                   helpers::Pool& p) {
  if (mutex_.try_lock()) {
    ConsoleAppender::append(event, p);
#if TARGET_OS_IOS
    // Temporary solution to capture and display log messages within the iOS
    // app.
    // The log message is printed to std::cout, which is then captured in the
    // Objective-C code to redirect standard output to a text view.
    std::string log_message = event->getRenderedMessage().c_str();
    std::cout << log_message << std::endl;
#endif
    if (callback_) callback_(event->getRenderedMessage());

    mutex_.unlock();
  }
}

IMPLEMENT_LOG4CXX_OBJECT(MLPerfConsoleAppender);
