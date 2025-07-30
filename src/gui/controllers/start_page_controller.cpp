#include "start_page_controller.h"

#include "../CIL/system_info_provider.h"
#include "core/gui_utils.h"
#include "core/types.h"
#include "ui/start_page.h"

namespace gui {
namespace controllers {
void StartPageController::SetView(views::StartPage* view) {
  AbstractController::SetView(view);

  connect(view, &views::StartPage::AddEPRequested, this,
          &StartPageController::OnAddEP);
  connect(view, &views::StartPage::DeleteEPRequested, this,
          &StartPageController::OnDeleteEP);
  connect(view, &gui::views::StartPage::StartBenchmarkClicked,
          [this]() { emit StartBenchmark(false); });
  connect(view, &gui::views::StartPage::DownloadOnlyClicked,
          [this]() { emit StartBenchmark(true); });
  connect(view, &gui::views::StartPage::EPsFilterChanged, this,
          &StartPageController::OnEPsFilterChanged);
}

void StartPageController::LoadSystemInformation() {
  auto start_page = dynamic_cast<gui::views::StartPage*>(view_);
  auto system_info_provider = cil::SystemInfoProvider::Instance();
  const auto& cpu_info = system_info_provider->GetCpuInfo();

  QString icon =
      gui::utils::GetCPUIcon(QString::fromStdString(cpu_info.vendor_id));
  QString main_title = QString::fromStdString(cpu_info.model_name);
  QList<QPair<QString, QString>> details;
  if (!cpu_info.architecture.empty())
    details << qMakePair("Architecture",
                         QString::fromStdString(cpu_info.architecture));
  if (cpu_info.frequency != 0)
    details << qMakePair(
        "Frequency",
        QString::number(qRound(cpu_info.frequency / (1024.0 * 1024.0))) +
            " MHz");
  if (cpu_info.physical_cpus != 0)
    details << qMakePair("CPU Cores", QString::number(cpu_info.physical_cpus));
  if (cpu_info.logical_cpus != 0)
    details << qMakePair("Logical Cores",
                         QString::number(cpu_info.logical_cpus));

  start_page->AddSystemInformationCard(icon, main_title, details);

  for (const auto& gpu_info : system_info_provider->GetGpuInfo()) {
    icon = gui::utils::GetGPUIcon(QString::fromStdString(gpu_info.vendor));
    main_title = QString::fromStdString(gpu_info.name);
    details.clear();
    if (!gpu_info.vendor.empty())
      details << qMakePair("Manufacturer",
                           QString::fromStdString(gpu_info.vendor));
    if (!gpu_info.driver_version.empty())
      details << qMakePair("Driver Version",
                           QString::fromStdString(gpu_info.driver_version));
    if (gpu_info.dedicated_memory_size != 0)
      details << qMakePair("Memory ", gui::utils::BytesToGbString(
                                          gpu_info.dedicated_memory_size));
    if (gpu_info.shared_memory_size != 0)
      details << qMakePair("Shared Memory ", gui::utils::BytesToGbString(
                                                 gpu_info.shared_memory_size));
    start_page->AddSystemInformationCard(icon, main_title, details);
  }

  const auto& npu_info = system_info_provider->GetNpuInfo();

  if (!npu_info.name.empty()) {
    icon = gui::utils::GetNPUIcon(QString::fromStdString(npu_info.vendor));
    main_title = QString::fromStdString(npu_info.name);
    details.clear();
    if (!npu_info.vendor.empty())
      details << qMakePair("Manufacturer",
                           QString::fromStdString(npu_info.vendor));

    if (npu_info.dedicated_memory_size != 0)
      details.append({"Memory ", gui::utils::BytesToGbString(
                                     npu_info.dedicated_memory_size)});
    if (npu_info.shared_memory_size != 0)
      details.append({"Shared Memory ", gui::utils::BytesToGbString(
                                            npu_info.shared_memory_size)});
    start_page->AddSystemInformationCard(icon, main_title, details);
  }

  const auto& os_info = system_info_provider->GetOsInfo();
  const auto& memory_info = system_info_provider->GetMemoryInfo();
  icon = gui::utils::GetOSIcon(QString::fromStdString(os_info.full_name));
  main_title = QString::fromStdString(os_info.full_name);
  details.clear();
  if (!os_info.version.empty())
    details << qMakePair("Version", QString::fromStdString(os_info.version));

  if (memory_info.physical_total != 0)
    details << qMakePair("Total Memory", gui::utils::BytesToGbString(
                                             memory_info.physical_total));
  if (memory_info.virtual_total != 0)
    details << qMakePair(
        "Total Virtual Memory",
        gui::utils::BytesToGbString(memory_info.virtual_total));

  start_page->AddSystemInformationCard(icon, main_title, details);
}

void StartPageController::LoadEPsInformation(
    const nlohmann::json& schema, const QList<EPInformationCard>& configs) {
  config_schema_ = schema;
  eps_configs_ = configs;
  auto start_page = dynamic_cast<gui::views::StartPage*>(view_);

  EPFilter vendor_filter{"Vendor", {}};
  EPFilter ep_filter{"EP", {}};
  EPFilter device_type_filter{"Device Type", {}};
  EPFilter model_type_filter{"Model Type", {}};
  EPFilter other_prompts_filter{"Other", {}};

  auto addOptionFn = [](std::map<QString, bool>& options,
                        const QString& option_name, bool is_experimental) {
    if (!options.contains(option_name))
      options[option_name] = !is_experimental;
    else
      options[option_name] |= !is_experimental;
  };

  for (auto& config : eps_configs_) {
    QStringList long_name_splitted = config.long_name_.split(' ');
    addOptionFn(vendor_filter.options, long_name_splitted.front(),
                config.is_experimental);
    addOptionFn(ep_filter.options, config.mapped_name_, config.is_experimental);
    addOptionFn(device_type_filter.options, long_name_splitted.back(),
                config.is_experimental);
    addOptionFn(model_type_filter.options, config.model_name_,
                config.is_experimental);
  }
  // Regular prompts are ON by default
  // If the config is not inside LongPrompts, than it has regular prompts
  other_prompts_filter.options["Base prompts"] = true;
  other_prompts_filter.options["Long prompts"] = false;

  start_page->LoadFiltersCard({vendor_filter, ep_filter, device_type_filter,
                               model_type_filter, other_prompts_filter});
  start_page->LoadEPInformationCards(schema, configs);

  OnEPsFilterChanged();
  if (!start_page->HasVisibleEPCard()) start_page->ExpandFiltersCard();
}

QList<QPair<int, EPInformationCard>>
StartPageController::GetCurrentEPsConfigurations() const {
  QList<QPair<int, EPInformationCard>> eps;
  auto start_page = dynamic_cast<gui::views::StartPage*>(view_);
  for (int i = 0; i < eps_configs_.size(); ++i)
    if (start_page->IsEPCardSelected(i)) {
      int ep_base_id = start_page->GetEPCardBaseId(i);
      EPInformationCard card = eps_configs_[i];
      card.config_ = start_page->GetEPConfiguration(i);
      eps.push_back(qMakePair(ep_base_id, card));
    }
  return eps;
}

void StartPageController::ResetSelectedEPsConfigurations() const {
  auto start_page = dynamic_cast<gui::views::StartPage*>(view_);
  for (int i = 0; i < eps_configs_.size(); ++i)
    start_page->SetEPCardSelected(i, false);
}

void StartPageController::OnAddEP(int base_ep_index) {
  if (base_ep_index < 0 || base_ep_index >= eps_configs_.size()) return;

  auto start_page = dynamic_cast<gui::views::StartPage*>(view_);

  auto new_config = eps_configs_[base_ep_index];
  int new_config_index = base_ep_index + 1;
  int new_config_base_id = start_page->GetEPCardBaseId(base_ep_index);
  eps_configs_.insert(eps_configs_.begin() + new_config_index, new_config);
  start_page->AddEpInformationCard(config_schema_, new_config,
                                   new_config_base_id, new_config_index, true);
}

void StartPageController::OnDeleteEP(int ep_index) {
  auto start_page = dynamic_cast<gui::views::StartPage*>(view_);
  eps_configs_.erase(eps_configs_.begin() + ep_index);
  start_page->DeleteEpInformationCard(ep_index);
}

void StartPageController::OnEPsFilterChanged() {
  auto start_page = dynamic_cast<gui::views::StartPage*>(view_);
  auto filters = start_page->GetEPsFilter();

  for (int i = 0; i < eps_configs_.size(); ++i) {
    bool show_ep = true;
    for (const auto& filter : filters) {
      for (const auto& [name, is_active] : filter.options) {
        if (is_active) continue;
        if ((filter.name == "Vendor" && eps_configs_[i].long_name_.startsWith(
                                            name, Qt::CaseInsensitive)) ||
            (filter.name == "EP" && eps_configs_[i].mapped_name_.compare(
                                        name, Qt::CaseInsensitive) == 0) ||
            (filter.name == "Device Type" &&
             eps_configs_[i].device_type_.compare(name, Qt::CaseInsensitive) ==
                 0) ||
            (filter.name == "Model Type" &&
             eps_configs_[i].model_name_.compare(name, Qt::CaseInsensitive) ==
                 0) ||
            (name == "Base prompts" && !eps_configs_[i].support_long_prompts) ||
            (name == "Long prompts" && eps_configs_[i].support_long_prompts)) {
          show_ep = false;
          break;
        }
        if (!show_ep) break;
      }
      if (!show_ep) break;
    }

    start_page->SetEPCardVisible(i, show_ep);
  }
}

}  // namespace controllers
}  // namespace gui
