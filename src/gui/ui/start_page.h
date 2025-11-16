#ifndef START_PAGE_H
#define START_PAGE_H

#include <nlohmann/json.hpp>

#include "abstract_view.h"
#include "ui_start_page.h"

class InformationCardWidget;
class EPExpandableWidget;
class EPFiltersWidget;

namespace gui {

struct EPInformationCard;
struct EPFilter;

namespace views {

/**
 * @class StartPage
 * @brief Main page for configuring and launching benchmark executions.
 */
class StartPage : public AbstractView {
  Q_OBJECT

 public:
  explicit StartPage(QString schema_file_path, QWidget* parent = nullptr);
  ~StartPage() = default;

  /**
   * @brief Add system information card to the page.
   * @param image_path Path to the system icon image.
   * @param header_text Header text for the information card.
   * @param header_key_values List of key-value pairs to display.
   */
  void AddSystemInformationCard(
      const QString& image_path, const QString& header_text,
      const QList<QPair<QString, QString>>& header_key_values);
  void ExpandSystemInformationWidget();

  /**
   * @brief Load filters card with the provided filters list.
   */
  void LoadFiltersCard(const QList<EPFilter>& filters);

  /**
   * @brief Load EP information cards.
   * @param schema JSON schema for configuration validation.
   * @param configs List of EP configuration cards to load.
   */
  void LoadEPInformationCards(const nlohmann::json& schema,
                              const QList<EPInformationCard>& configs);

  /**
   * @brief Clear all EP information cards.
   */
  void ClearEPInformationCards();

  /**
   * @brief Add a single EP information card.
   * @param schema JSON schema for configuration validation.
   * @param ep_config EP configuration to add.
   * @param base_id Base identifier for the EP.
   * @param index Index position to insert at, defaults to -1 (append).
   * @param deletable Whether the card can be deleted, defaults to false.
   */
  void AddEpInformationCard(const nlohmann::json& schema,
                            const EPInformationCard& ep_config, int base_id,
                            int index = -1, bool deletable = false);

  /**
   * @brief Delete EP information card at specified index.
   */
  void DeleteEpInformationCard(int index);

  /**
   * @brief Check if EP card at index is visible.
   */
  bool IsEPCardVisible(int index) const;

  /**
   * @brief Set visibility of EP card at specified index.
   */
  void SetEPCardVisible(int index, bool visible);

  /**
   * @brief Check if EP card at index is selected.
   * @return True if the EP card is selected.
   */
  bool IsEPCardSelected(int index) const;

  /**
   * @brief Returns the prompts type of EP card at index.
   * @return "base", "extended", or "both".
   */
  QString GetEPCardPromptsType(int index) const;

  /**
   * @brief Set selection state of EP card at specified index.
   */
  void SetEPCardSelected(int index, bool selected);

  /**
   * @brief Get base ID of EP card at specified index.
   * @return Base identifier of the EP card.
   */
  int GetEPCardBaseId(int index) const;

  /**
   * @brief Get configuration of EP at specified index.
   * @return JSON configuration of the execution provider.
   */
  nlohmann::json GetEPConfiguration(int index) const;

  /**
   * @brief Get current EP filter settings.
   * @return List of EP filters.
   */
  QList<EPFilter> GetEPsFilter() const;

  /**
   * @brief Check if show only runnable filter is enabled.
   * @return True if showing only runnable EPs.
   */
  bool IsShowOnlyRunnableOn() const;

 public slots:
  void UpdateUI();

 signals:
  void AddEPRequested(int base_ep_index);
  void DeleteEPRequested(int ep_index);
  void StartBenchmarkClicked();
  void DownloadOnlyClicked();
  void EPsFilterChanged();
  void SelectAllToggled(bool checked);

 protected:
  void SetupUi() override;
  void InstallSignalHandlers() override;

 private slots:
  /**
   * @brief Handle clicks on the add execution provider button.
   */
  void OnEPAddButtonClicked();

  /**
   * @brief Handle clicks on the delete execution provider button.
   */
  void OnEPDeleteButtonClicked();

 private:
  Ui::StartPage ui_;
  EPFiltersWidget* eps_filters_widget_;
  QList<EPExpandableWidget*> eps_widgets_;
  QMap<EPExpandableWidget*, int> eps_widgets_base_ids;
};

}  // namespace views
}  // namespace gui

#endif  // START_PAGE_H