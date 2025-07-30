#ifndef EULA_WIDGET_H
#define EULA_WIDGET_H

#include "abstract_view.h"
#include "ui_eula_widget.h"

namespace gui {
namespace views {

class EULAWidget : public AbstractView {
  Q_OBJECT
 public:
  EULAWidget(QWidget *parent = nullptr);
  ~EULAWidget() = default;

 protected:
  void SetupUi() override;
  void InstallSignalHandlers() override;

 private:
  Ui::EulaWidget ui_;

 signals:
  void EulaAccepted();
};

}  // namespace views
}  // namespace gui

#endif  // EULA_WIDGET_H