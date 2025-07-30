#include "progress_tracker_handler.h"

#include <log4cxx/consoleappender.h>
#include <log4cxx/fileappender.h>
#include <log4cxx/helpers/object.h>
#include <log4cxx/logger.h>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#include "utils.h"

#ifdef _WIN32
#include <windows.h>

#undef max
#else

#include <ostream>

#endif

#include "mlperf_console_appender.h"

const LoggerPtr loggerProgressTrackerHandler(
    Logger::getLogger("ProgressTracker"));

namespace cil {

const std::string ProgressTrackerHandler::kRotatingCursor = "|/-\\";
const std::string ProgressTrackerHandler::kDescriptionTemplate = "$D";

bool isConsoleAppenderAttached(log4cxx::LoggerPtr logger) {
  log4cxx::AppenderList appenderList = logger->getAllAppenders();
  for (log4cxx::AppenderPtr appender : appenderList) {
    if (std::dynamic_pointer_cast<log4cxx::ConsoleAppender>(appender)) {
      return true;
    }
  }
  return false;
}

void MoveCursorUp() {
#ifdef _WIN32
  HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  GetConsoleScreenBufferInfo(consoleHandle, &csbi);
  csbi.dwCursorPosition.Y--;  // Move up one line
  SetConsoleCursorPosition(consoleHandle, csbi.dwCursorPosition);
#else
  std::cout << "\033[A";  // ANSI escape code for moving up
#endif
}

ProgressTrackerHandler::ProgressTrackerHandler(std::atomic<bool> &interrupt)
    : interrupt_(interrupt) {}

void ProgressTrackerHandler::StartTracking(cil::ProgressTracker &tracker) {
  tracker.StartTracking();

  console_width_ = cil::utils::GetConsoleWidth();
  // there is a console min width restriction for interactive console mode
  interactive_console_mode_ =
      isConsoleAppenderAttached(loggerProgressTrackerHandler) &&
      console_width_ >= 80;
#if defined(__APPLE__)
  // On MacOS if debugger is attached(e. g. when running from XCode), we don't
  // use interactive console mode
  if (interactive_console_mode_)
    interactive_console_mode_ = !cil::utils::IsDebuggerPresent();
#endif
  if (interactive_console_mode_) MLPerfConsoleAppender::accessMutex().lock();
}

void ProgressTrackerHandler::StopTracking(cil::ProgressTracker &tracker) {
  tracker.StopTracking();
  if (interactive_console_mode_) MLPerfConsoleAppender::accessMutex().unlock();
}

void ProgressTrackerHandler::Update(cil::ProgressTracker &tracker) {
  DisplayCurrentTaskAndOverallProgress(tracker);
}

bool ProgressTrackerHandler::RequestInterrupt(cil::ProgressTracker &tracker) {
  if (force_accept_interrupt_request_) return true;
  std::cout << "\n\nWould you like to continue [y/n] default \"n\" ";
  auto response = getchar();
  if (response != '\n')
    // Consume any extra characters in the input buffer, including the newline
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

  if (response == 'y' || response == 'Y') {
    MoveCursorUp();
    std::cout << "\r                                               ";
    MoveCursorUp();
    MoveCursorUp();
    return false;
  }
  LOG4CXX_INFO(loggerProgressTrackerHandler, "\nInterrupted by user!\n");
  return true;
}

std::atomic<bool> &ProgressTrackerHandler::GetInterrupt() { return interrupt_; }

void ProgressTrackerHandler::SetForceAcceptInterruptRequest(bool accept) {
  force_accept_interrupt_request_ = accept;
}

void ProgressTrackerHandler::DisplayCurrentTaskAndOverallProgress(
    cil::ProgressTracker &tracker) {
  const auto &task_description = tracker.GetTaskDescription();
  const auto &tasks = tracker.GetTasks();
  const auto expected_task_count_ = tracker.GetExpectedTaskCount();
  auto &current_task_index_ = GetCurrentTaskIndex(tracker);
  const auto update_interval_ = tracker.GetUpdateInterval();

  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  const auto &current_task = tasks[current_task_index_];

  using enum cil::ProgressableTask::Status;

  cil::ProgressableTask::Status task_status = current_task->GetStatus();
  bool task_finished = task_status > kRunning;
  int progress = current_task->GetProgress();
  auto start_time = current_task->GetStartTime();
  auto elapsed_time = cil::utils::FormatDuration(
      task_status == kReady ? std::chrono::steady_clock::duration::zero()
                            : now - start_time,
      update_interval_ < std::chrono::seconds(1));
  std::string description = current_task->GetDescription();

  const size_t progress_ticks_count = 12;

  std::string status_string =
      cil::ProgressableTask::StatusToString(task_status);

  std::stringstream current_task_stream;
  if (interactive_console_mode_) {
    if (task_status == kFailed) {
      std::string error_message = current_task->getErrorMessage();
      if (error_message.empty()) {
        error_message = "Something went wrong";
      }
      current_task_stream << status_string << " " << kDescriptionTemplate
                          << " - ET: " << elapsed_time << " - "
                          << error_message;
      std::string output =
          FormatToConsoleWidth(current_task_stream.str(), description);
      std::cout << "\r" << output;
      LogCallback(output, true);
    } else if (task_status == KSkipped) {
      // this will most likely be because  the task already executed before
      // the progress tracker started
      // the task now contain method called getSkippingReason() that will return
      // the reason why the task was skipped
      current_task_stream << status_string << " " << kDescriptionTemplate
                          << " - ET: " << elapsed_time << " - "
                          << current_task->getSkippingReason();
      std::string output =
          FormatToConsoleWidth(current_task_stream.str(), description);
      std::cout << "\r" << output;
      LogCallback(output, true);
    } else {
      // Display current task progress
      current_task_stream << status_string << " " << kDescriptionTemplate
                          << " - ET: " << elapsed_time << " - ";

      if (progress == -1) {
        if (task_finished)
          current_task_stream << "Done!";
        else
          current_task_stream
              << "Progress: " << kRotatingCursor[rotating_cursor_position_]
              << "                       ";
      } else {
        size_t filled_length = progress_ticks_count * progress / 100;
        size_t unfilled_length = progress_ticks_count > filled_length
                                     ? progress_ticks_count - filled_length
                                     : 0;
        current_task_stream << "Progress: [" << std::string(filled_length, '=')
                            << std::string(unfilled_length, ' ') << "] "
                            << progress << "%";
      }
      std::string output =
          FormatToConsoleWidth(current_task_stream.str(), description);
      std::cout << "\r" << output;
      LogCallback(output, true);
    }
  }
  bool show_overral = false;
  if (task_finished) {
    if (!interactive_console_mode_) {
      std::stringstream ss;
      ss << description << " (Task " << current_task_index_ + 1 << "/"
         << expected_task_count_ << ") finished in " << elapsed_time
         << " seconds";
      std::string output = ss.str();
      LOG4CXX_INFO(loggerProgressTrackerHandler, output);
      LogCallback(output);
      show_overral = true;
    }
    ++current_task_index_;
  }
  // Display overall progress

  size_t completed_tasks = current_task_index_;
  size_t overall_progress = (completed_tasks * 100) / expected_task_count_;
  bool completed = completed_tasks == expected_task_count_;

  std::stringstream ss;
  if (task_description == "download") {
    // loop over the tasks to check how many task have been failed
    int no_of_completed_downloads = 0;
    int no_of_skipped_downloads = 0;
    int no_of_failed_downloads = 0;
    for (auto task : tasks) {
      if (task->GetStatus() == kCompleted) {
        no_of_completed_downloads++;
      } else if (task->GetStatus() == KSkipped) {
        no_of_skipped_downloads++;
      } else if (task->GetStatus() == kFailed) {
        no_of_failed_downloads++;
      }
    }
    // if completed_tasks == no_of_skipped_downloads this means all the file
    // were already downloaded and we haven't downloaded any file if
    // completed_tasks == no_of_completed_downloads this means all the files
    // were downloaded successfully let's format the Overall Progress message
    // accordingly
    if (no_of_skipped_downloads == expected_task_count_) {
      ss << "Overall Progress: ["
         << "All files were already downloaded"
         << "]";
    } else if (no_of_completed_downloads == expected_task_count_) {
      ss << "Overall Progress: ["
         << "All files were downloaded successfully"
         << "]";
    } else if (completed && no_of_skipped_downloads == 0 &&
               no_of_completed_downloads == 0) {
      // we failed to download any file
      ss << "Overall Progress: ["
         << "Failed to download the files "
         << "]";
    } else {
      // this should have actull indication of the progrss of the download based
      // on the number of files that were downloaded
      ss << "Overall Progress: [" << no_of_completed_downloads
         << " files downloaded, " << no_of_skipped_downloads << " skipped, "
         << no_of_failed_downloads << " failed"
         << "]";
    }
  } else {
    ss << "Overall Progress: ["
       << std::string(
              progress_ticks_count * completed_tasks / expected_task_count_,
              '=')
       << std::string(progress_ticks_count *
                          (expected_task_count_ - completed_tasks) /
                          expected_task_count_,
                      '-')
       << "] " << overall_progress << "% (" << completed_tasks << "/"
       << expected_task_count_ << " " << kDescriptionTemplate
       << "(s) completed)";
  }

  std::string stream_string = ss.str();
  // Move cursor back up to the beginning of the first line
  if (interactive_console_mode_) {
    std::string output = FormatToConsoleWidth(stream_string, task_description);
    std::cout << "\n" << output;
    LogCallback(output, false, false, true);
    if (!completed && !task_finished) {
      MoveCursorUp();
      LogCallback("", false, true);
    }
    if (completed) {
      std::cout << "\n";
      LogCallback("\n");
    }
  } else {
    stream_string =
        "\n" + ReplaceDescriptionSubString(stream_string, task_description) +
        "\n";
  }

  if (show_overral) {
    LOG4CXX_INFO(loggerProgressTrackerHandler, stream_string);
    LogCallback(stream_string);
  }

  rotating_cursor_position_ = (rotating_cursor_position_ + 1) % 4;
}

std::string ProgressTrackerHandler::FormatToConsoleWidth(
    const std::string &stream_string, const std::string &description) const {
  int console_width = console_width_ - 1;
  size_t description_text_max_size =
      console_width - stream_string.size() +
      kDescriptionTemplate
          .size();  // stream_string should contain description_template_

  std::string description_trimmed = description;
  if (description_text_max_size > 3) {
    if (description_text_max_size - 3 < description_trimmed.size())
      description_trimmed =
          description_trimmed.substr(0, description_text_max_size - 3) + "...";
  } else
    description_trimmed = "";

  std::string replaced_string =
      ReplaceDescriptionSubString(stream_string, description_trimmed);

  // append whitespaces to fill the console line
  if (console_width > replaced_string.size())
    replaced_string.append(console_width - replaced_string.size(), ' ');
  return replaced_string;
}

std::string ProgressTrackerHandler::ReplaceDescriptionSubString(
    const std::string &stream_string, const std::string &description) const {
  size_t pos = stream_string.find(kDescriptionTemplate);
  if (pos != std::string::npos) {
    std::string replaced_string = stream_string;
    replaced_string.replace(pos, kDescriptionTemplate.size(), description);
    return replaced_string;
  }

  return stream_string;
}

void ProgressTrackerHandler::LogCallback(const std::string &message,
                                         bool move_start, bool move_up,
                                         bool move_down) {
  if (logger_callback_)
    logger_callback_(message, move_start, move_up, move_down);
}

}  // namespace cil
