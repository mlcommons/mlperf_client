#ifndef START_PAGE_H
#define START_PAGE_H

#include <nlohmann/json.hpp>

#include "abstract_view.h"
#include "ui_start_page.h"

class InformationCardWidget;
class EPExpandableWidget;
class EPFiltersExpandableWidget;

namespace gui {

struct EPInformationCard;
struct EPFilter;

namespace views {

class StartPage : public AbstractView {
  Q_OBJECT
 public:
  explicit StartPage(QString schema_file_path, QWidget* parent = nullptr);
  ~StartPage() = default;
  // system information cards
  void ClearSystemInfoCards();
  void AddSystemInformationCard(
      const QString& image_path, const QString& header_text,
      const QList<QPair<QString, QString>>& header_key_values);
  void LoadFiltersCard(const QList<EPFilter>& filters);
  void ExpandFiltersCard();
  // Execution Provider widgets
  void LoadEPInformationCards(const nlohmann::json& schema,
                              const QList<EPInformationCard>& configs);
  void ClearEPInformationCards();
  void AddEpInformationCard(const nlohmann::json& schema,
                            const EPInformationCard& ep_config, int base_id,
                            int index = -1, bool deletable = false);
  void DeleteEpInformationCard(int index);
  void SetEPCardVisible(int index, bool visible);
  bool HasVisibleEPCard() const;
  bool IsEPCardSelected(int index) const;
  void SetEPCardSelected(int index, bool selected);
  int GetEPCardBaseId(int index) const;
  // Get the information from the EP widget
  nlohmann::json GetEPConfiguration(int index) const;

  QList<EPFilter> GetEPsFilter() const;

 signals:
  void AddEPRequested(int base_ep_index);
  void DeleteEPRequested(int ep_index);
  void StartBenchmarkClicked();
  void DownloadOnlyClicked();
  void EPsFilterChanged();

 protected:
  void SetupUi() override;
  void InstallSignalHandlers() override;

 private slots:
  void OnEPAddButtonClicked();
  void OnEPDeleteButtonClicked();

  void UpdateBenchmarkButtons();

 private:
  Ui::StartPage ui_;
  QList<InformationCardWidget*> system_information_card_widgets_;
  EPFiltersExpandableWidget* eps_filters_widget_;
  QList<EPExpandableWidget*> eps_widgets_;
  QMap<EPExpandableWidget*, int> eps_widgets_base_ids;
};
}  // namespace views
}  // namespace gui

#endif  // START_PAGE_H
