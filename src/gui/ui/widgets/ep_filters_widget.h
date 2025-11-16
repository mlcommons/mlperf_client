#ifndef EP_FILTERS_WIDGET_H_
#define EP_FILTERS_WIDGET_H_

#include <QMap>
#include <QWidget>

#include "../../core/types.h"

class CheckableComboBox;

/**
 * @class EPFiltersWidget
 * @brief Widget for managing execution provider filter options.
 */
class EPFiltersWidget : public QWidget {
  Q_OBJECT
 public:
  explicit EPFiltersWidget(const QList<gui::EPFilter> &filters,
                           QWidget *parent = nullptr);
  QList<gui::EPFilter> GetFilters() const;

 signals:
  void FilterChanged();

 private:
  QMap<QString, CheckableComboBox *> combo_box_widgets_;
};

#endif  // EP_FILTERS_WIDGET_H_