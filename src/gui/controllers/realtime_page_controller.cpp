#include "controllers/realtime_page_controller.h"

#include <QThread>
#include <QTimer>

#include "../CIL/mlperf_console_appender.h"
#include "../CIL/system_info_provider.h"
#include "../gui_progress_tracker_handler.h"
#include "core/gui_utils.h"
#include "ui/realtime_monitoring_page.h"
#include "ui/widgets/execution_progress_widget.h"
#include "ui/widgets/logs_widget.h"
#include "ui/widgets/system_monitoring_widget.h"

namespace gui {
namespace controllers {
RealtimePageController::RealtimePageController(std::atomic<bool>& interrupt,
                                               QObject* parent)
    : AbstractController(parent),
      monitoring_thread_(nullptr),
      monitoring_update_in_progress_(false),
      monitoring_timer_(nullptr),
      progress_handler_(new GuiProgressTrackerHandler(interrupt)),
      current_stage_(0) {
  connect(progress_handler_, &GuiProgressTrackerHandler::CurrentTaskInfoChanged,
          this, &RealtimePageController::OnCurrentTaskInfoChanged);
  connect(
      progress_handler_, &GuiProgressTrackerHandler::LogMessage, this,
      [this](const QString& message, bool move_start, bool move_up,
             bool move_down) {
        GetView()->GetLogsWidget()->InsertLogMessage(message, move_start,
                                                     move_up, move_down);
      },
      Qt::QueuedConnection);

  MLPerfConsoleAppender::SetCallback([this](const std::string& message) {
    QMetaObject::invokeMethod(
        this,
        [this, message]() {
          GetView()->GetLogsWidget()->AppendLogMessage(
              QString(message.c_str()));
        },
        Qt::QueuedConnection);
  });

  InitializeMonitoringThread();
}

RealtimePageController::~RealtimePageController() { StopMonitoring(); }

void RealtimePageController::SetView(views::RealTimeMonitoringPage* view) {
  AbstractController::SetView(view);
  connect(view->GetExecutionProgressWidget(),
          &ExecutionProgressWidget::HideCountersToggled, this,
          &RealtimePageController::OnHideCountersToggled);
  connect(view->GetExecutionProgressWidget(),
          &ExecutionProgressWidget::CancelClicked, this,
          &RealtimePageController::OnCancelClicked);
  connect(view->GetSystemMonitoringWidget(),
          &SystemMonitoringWidget::VisibilityChanged, this,
          &RealtimePageController::OnSystemMonitoringWidgetVisibilityChanged);
}

void RealtimePageController::InitExecutionWithEPs(
    const QList<EPInformationCard>& eps) {
  auto execution_progress_widget = GetExecutionProgressWidget();
  execution_progress_widget->ClearEPs();
  execution_progress_widget->SetCancelingState(false);
  for (const auto& ep : eps)
    execution_progress_widget->AddEP(
        ep.name_, ep.description_,
        QString::fromStdString(ep.config_["device_type"]),
        ep.long_name_ + "(" + ep.model_name_ + ")");
  GetView()->GetLogsWidget()->ClearLogs();

  progress_handler_->SetInterrupt(false);
}

void RealtimePageController::SetEpSelected(int index) {
  GetExecutionProgressWidget()->SetEpSelected(index);
}

void RealtimePageController::MoveToNextEP(bool current_success) {
  GetExecutionProgressWidget()->MoveToNextEP(current_success);
}

void RealtimePageController::SetStagesProgressRatios(
    const QVector<double>& ratios) {
  stages_progress_ratios_ = ratios;
}

void RealtimePageController::SetCurrentStage(int current_stage) {
  current_stage_ = current_stage;
}

void RealtimePageController::StartMonitoring() {
  StopMonitoring();

  InitializeMonitoringWidgets();
  monitoring_update_in_progress_.store(false);
  monitoring_thread_->start();
}

void RealtimePageController::StopMonitoring() {
  monitoring_thread_->quit();
  monitoring_thread_->wait();
}

void RealtimePageController::InitializeMonitoringWidgets() {
  auto monitoring_widget = GetView()->GetSystemMonitoringWidget();
  if (monitoring_widget->Initialized()) return;

  auto system_info_provider = cil::SystemInfoProvider::Instance();

  QList<SystemMonitoringWidget::MonitoringWidgetConfig> configs;
  const auto& cpu_info = system_info_provider->GetCpuInfo();
  const auto& performance_info = system_info_provider->GetPerformanceInfo();
  QString cpu_name = QString::fromStdString(cpu_info.model_name);
  configs.append({SystemMonitoringWidget::MonitoringWidgetType::kCircularGauge,
                  "CPU Utilization", gui::utils::GetDeviceIcon("CPU"), cpu_name,
                  "%", 100.0});
  const auto& memory_info = system_info_provider->GetMemoryInfo();
  configs.append({SystemMonitoringWidget::MonitoringWidgetType::kCapacityBar,
                  "System Memory", "", cpu_name, "GB",
                  gui::utils::BytesToGb(memory_info.physical_total)});
  if (performance_info.cpu_temperature_is_available)
    configs.append({SystemMonitoringWidget::MonitoringWidgetType::kThermometer,
                    "CPU Temperature",
                    ":/icons/resources/icons/mdi_temperature.png", cpu_name, "",
                    0.0});

  const auto& gpu_info = system_info_provider->GetGpuInfo();
  for (const auto& gpu : gpu_info) {
    QString gpu_name = QString::fromStdString(gpu.name);
    monitoring_widget_indices_[gpu_name] = configs.size();
    configs.append(
        {SystemMonitoringWidget::MonitoringWidgetType::kCircularGauge,
         "GPU Utilization", gui::utils::GetDeviceIcon("GPU"), gpu_name, "%",
         100.0});
    double dedicated_memory_gb =
        gui::utils::BytesToGb(gpu.dedicated_memory_size);
    double shared_memory_gb = gui::utils::BytesToGb(gpu.shared_memory_size);
    bool is_dedicated = dedicated_memory_gb > 0.5;
    is_gpu_dedicated[gpu_name] = is_dedicated;
    configs.append(
        {SystemMonitoringWidget::MonitoringWidgetType::kCapacityBar,
         QString("GPU Memory (%1)").arg(is_dedicated ? "Dedicated" : "Shared"),
         "", gpu_name, "GB",
         is_dedicated ? dedicated_memory_gb : shared_memory_gb});

    auto gpu_performance_info = performance_info.gpu_info.find(gpu.name);
    if (gpu_performance_info != performance_info.gpu_info.end() &&
        gpu_performance_info->second.temperature_is_available)
      configs.append(
          {SystemMonitoringWidget::MonitoringWidgetType::kThermometer,
           "GPU Temperature", ":/icons/resources/icons/mdi_temperature.png",
           gpu_name, "", 0.0});
  }

  const auto& npu_info = system_info_provider->GetNpuInfo();
  if (!npu_info.name.empty()) {
    QString npu_name = QString::fromStdString(npu_info.name);
    monitoring_widget_indices_[npu_name] = configs.size();
    configs.append(
        {SystemMonitoringWidget::MonitoringWidgetType::kCircularGauge,
         "NPU Utilization", gui::utils::GetDeviceIcon("NPU"), npu_name, "%",
         100.0});
  }

  if (performance_info.power_scheme_is_available) {
    monitoring_widget_indices_["Power Mode"] = configs.size();
    configs.append({SystemMonitoringWidget::MonitoringWidgetType::kPowerMode,
                    "Power Mode",
                    ":/icons/resources/icons/ic_baseline-power.png",
                    "The current power mode of the system.", "", 0.0});
  }

  monitoring_widget->InitMonitoringWidgets(configs);
}

void RealtimePageController::OnMonitoringTimerTimeout() {
  bool expected = false;
  if (!monitoring_update_in_progress_.compare_exchange_strong(expected, true))
    return;

  auto performance_data =
      cil::SystemInfoProvider::Instance()->GetPerformanceInfo();

  QMetaObject::invokeMethod(
      this,
      [=]() {
        auto monitoring_widget = GetView()->GetSystemMonitoringWidget();

        monitoring_widget->SetWidgetValue(0, performance_data.cpu_usage);
        monitoring_widget->SetWidgetValue(
            1, gui::utils::BytesToGb(performance_data.memory_usage));
        if (performance_data.cpu_temperature_is_available)
          monitoring_widget->SetWidgetValue(2,
                                            performance_data.cpu_temperature);

        if (performance_data.power_scheme_is_available)
          monitoring_widget->SetWidgetValue(
              monitoring_widget_indices_["Power Mode"],
              QString::fromStdString(performance_data.power_scheme));

        for (const auto& gpu : performance_data.gpu_info) {
          auto gpu_name = QString::fromStdString(gpu.first);
          auto gpu_index_it = monitoring_widget_indices_.find(gpu_name);
          if (gpu_index_it == monitoring_widget_indices_.end()) continue;
          int index = *gpu_index_it;
          size_t gpu_memory_usage = is_gpu_dedicated[gpu_name]
                                        ? gpu.second.dedicated_memory_usage
                                        : gpu.second.shared_memory_usage;
          monitoring_widget->SetWidgetValue(index, gpu.second.usage);
          monitoring_widget->SetWidgetValue(
              index + 1, gui::utils::BytesToGb(gpu_memory_usage));
          if (gpu.second.temperature_is_available)
            monitoring_widget->SetWidgetValue(index + 2,
                                              gpu.second.temperature);
        }

        if (!performance_data.npu_info.name.empty()) {
          auto npu_name =
              QString::fromStdString(performance_data.npu_info.name);
          int index = monitoring_widget_indices_[npu_name];
          monitoring_widget->SetWidgetValue(index,
                                            performance_data.npu_info.usage);
        }
        monitoring_update_in_progress_.store(false);
      },
      Qt::QueuedConnection);
}

GuiProgressTrackerHandler* RealtimePageController::GetProgressHandler() const {
  return progress_handler_;
}

views::RealTimeMonitoringPage* RealtimePageController::GetView() const {
  auto realtime_page = dynamic_cast<views::RealTimeMonitoringPage*>(view_);
  return realtime_page;
}

ExecutionProgressWidget* RealtimePageController::GetExecutionProgressWidget()
    const {
  return GetView()->GetExecutionProgressWidget();
}

void RealtimePageController::InitializeMonitoringThread() {
  monitoring_thread_ = new QThread(this);
  monitoring_timer_ = new QTimer();
  monitoring_timer_->moveToThread(monitoring_thread_);

  monitoring_timer_->setInterval(1000);
  connect(monitoring_timer_, &QTimer::timeout, this,
          &RealtimePageController::OnMonitoringTimerTimeout,
          Qt::DirectConnection);
  connect(monitoring_thread_, &QThread::started, monitoring_timer_,
          qOverload<>(&QTimer::start));
  connect(monitoring_thread_, &QThread::finished, monitoring_timer_,
          qOverload<>(&QTimer::stop));
  connect(monitoring_thread_, &QThread::destroyed, monitoring_timer_,
          &QTimer::deleteLater);
}

void RealtimePageController::OnHideCountersToggled(bool checked) {
  GetView()->SetHideCounters(checked);
}

void RealtimePageController::OnSystemMonitoringWidgetVisibilityChanged(
    bool visible) {
  if (visible)
    StartMonitoring();
  else
    StopMonitoring();
}

void RealtimePageController::OnCancelClicked() {
  GetExecutionProgressWidget()->SetCancelingState(true);
  emit ExecutionCancelRequested();
}

void RealtimePageController::OnCurrentTaskInfoChanged(
    const QString& name, int progress, int total_tasks, int current_task,
    int total_steps, int current_step) {
  auto progress_widget = GetExecutionProgressWidget();

  double global_progress = 0;
  if (!stages_progress_ratios_.empty()) {
    int current_ep = progress_widget->GetCurrentSelectedEP();
    int total_eps = progress_widget->GetTotalEPs();
    int total_stages_ = stages_progress_ratios_.size();
    global_progress = current_ep * 100.0 / total_eps;
    for (int i = 0; i < current_stage_; ++i) {
      global_progress += stages_progress_ratios_[i] * 100.0 / total_eps;
    }
    global_progress +=
        stages_progress_ratios_[current_stage_] * progress / total_eps;
  }
  progress_widget->SetProgress(static_cast<int>(global_progress));

  QString task_name;
  if (!name.isEmpty()) task_name = name[0].toUpper() + name.mid(1);
  if (task_name == "Benchmark" && total_tasks > 1)
    task_name += QString(" %1/%2").arg(current_task).arg(total_tasks);

  progress_widget->SetTaskName(task_name);
  progress_widget->SetSelectedEpProgress(total_steps, current_step);
}

void RealtimePageController::OnLogMessageAppended(const QString& message) {
  GetView()->GetLogsWidget()->AppendLogMessage(message);
}

}  // namespace controllers
}  // namespace gui
