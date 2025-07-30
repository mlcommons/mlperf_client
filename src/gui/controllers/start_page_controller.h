#ifndef __START_PAGE_CONTROLLER_H__
#define __START_PAGE_CONTROLLER_H__

#include "controllers/abstract_controller.h"

namespace gui {
namespace views {
class StartPage;
}

struct EPInformationCard;

namespace controllers {
class StartPageController : public AbstractController {
  Q_OBJECT
 public:
  explicit StartPageController(QObject* parent = nullptr)
      : AbstractController(parent) {}
  virtual ~StartPageController() = default;
  void SetView(views::StartPage* view);
  void LoadSystemInformation();
  void LoadEPsInformation(const nlohmann::json& schema,
                          const QList<EPInformationCard>& configs);
  // we return vector here instead of map, since the order matters
  QList<QPair<int, EPInformationCard>> GetCurrentEPsConfigurations() const;

  void ResetSelectedEPsConfigurations() const;

 signals:
  void StartBenchmark(bool download_deps_only);

 private slots:
  void OnAddEP(int base_ep_index);
  void OnDeleteEP(int ep_index);
  void OnEPsFilterChanged();

 private:
  nlohmann::json config_schema_;
  QList<EPInformationCard> eps_configs_;
};
}  // namespace controllers
}  // namespace gui

#endif  // __START_PAGE_CONTROLLER_H__