#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QFontDatabase>
#include <QString>
#include <nlohmann/json.hpp>

#include "command_option.h"
#include "command_parser.h"
#include "controllers/app_controller.h"
#include "main_window.h"
#include "single_instance_guard.h"
#include "utils.h"
#include "widgets/popup_widget.h"

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IOS
#include <unistd.h>

#include "ios/ios_utils.h"

#endif
#endif

void load_stylesheet(QApplication& app, const QString& file_name) {
  QFile file(file_name);
  if (file.open(QFile::ReadOnly)) {
    QString stylesheet = QLatin1String(file.readAll());
    app.setStyleSheet(stylesheet);
    file.close();
  } else {
    qDebug() << "Failed to load stylesheet: " << file_name;
  }
}

void load_fonts() {
  const QStringList font_files = {
      ":/fonts/resources/fonts/Roboto/Roboto-Black.ttf",
      ":/fonts/resources/fonts/Roboto/Roboto-Bold.ttf",
      ":/fonts/resources/fonts/Roboto/Roboto-Light.ttf",
      ":/fonts/resources/fonts/Roboto/Roboto-Medium.ttf",
      ":/fonts/resources/fonts/Roboto/Roboto-Regular.ttf",
      ":/fonts/resources/fonts/Roboto/Roboto-Thin.ttf"};
  for (const auto& font_file : font_files) {
    auto id = QFontDatabase::addApplicationFont(font_file);
    if (id == -1) {
      qDebug() << "Failed to load font: " << font_file;
    }
  }
}

std::string app_description =
    "MLPerf is a benchmark application for Windows and MacOS.The "
    "goal of the benchmark is to rank various client form factors "
    "on a set of scenarios that require ML inference to function.";

void generate_command_options(CommandParser& command_parser) {
  // Common options
  // @todo: Add the common option to the command parser
  CommandOption help_option =
      CommandOption("help", 'h', "Show this help message and exit.", false);
  command_parser.AddFlag(help_option);

  CommandOption version_option = CommandOption(
      "version", 'v', "Show the application version and exit.", false);
  command_parser.AddFlag(version_option);
}

int main(int argc, char* argv[]) {
#if defined(_WIN32) || defined(_WIN64)
  CommandParser command_parser("mlperf-windows-gui.exe", app_description);
#elif defined(__APPLE__)
#if TARGET_OS_IOS
  // The default working directory is not writable on iOS, so we change the
  // working directory to the application's Documents directory. This ensures
  // that files can be saved and are also accessible through the file explorer
  // for easier access and review.
  chdir(cil::ios_utils::GetDocumentsDirectoryPath().c_str());
#endif
  CommandParser command_parser("mlperf-macos-gui", app_description);
#endif
  //@todo: Serve these options from the command line parser
  generate_command_options(command_parser);
  bool is_parsed_successfully = command_parser.Process(argc, argv);
  if (!is_parsed_successfully) {
    qWarning() << "Incorrect options provided, aborting...";
    qWarning() << "Use -h or --help for more information.";
    return 1;
  }
  // Check if non option arguments are provided
  if (command_parser.GetUnknownOptionNames().size() > 0) {
    qWarning() << "Incorrect options provided, aborting...";
    qWarning() << "Use -h or --help for more information.";
    return 1;
  }
  // Check for the version option
  if (command_parser.OptionPassed("version")) {
    command_parser.ShowVersion();
    return 0;
  }
  // Check for the help option
  if (command_parser.OptionPassed("help")) {
    command_parser.ShowHelp();
    return 0;
  }

  QApplication app(argc, argv);

  load_fonts();
  load_stylesheet(app, ":/resources/styles.qss");

#if !TARGET_OS_IOS
  gui::SingleInstanceGuard instance_guard;
  if (!instance_guard.TryLock()) {
    PopupWidget popup;
    popup.SetMessage("App is already running.");
    popup.show();
    int res = app.exec();
    instance_guard.BringInstanceWindowToFront();
    return res;
  }
#endif

#if !TARGET_OS_IOS
  // Make sure we are using the correct working directory from executable
  // For iOS this is already set in the main_entry function
  cil::utils::SetCurrentDirectory(cil::utils::GetCurrentDirectory());
#endif

  gui::controllers::AppController app_controller;

  gui::MainWindow main_window(&app_controller);

  app_controller.Init();

  main_window.show();

  return app.exec();
}
