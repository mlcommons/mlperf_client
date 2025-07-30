#ifndef EP_FILTERS_EXPANDABLE_WIDGET_H_
#define EP_FILTERS_EXPANDABLE_WIDGET_H_

#include "../../core/types.h"
#include "expandable_widget.h"

class EPFiltersHeaderWidget : public QWidget {
  Q_OBJECT
 public:
  explicit EPFiltersHeaderWidget(const QList<gui::EPFilter> &filters,
                                 QWidget *parent = nullptr);
 public slots:
  void SetFilterNamesHidden(bool hidden);

 private:
  QWidget *filter_names_widget_;
};

class EPFilterWidget : public QWidget {
  Q_OBJECT
 public:
  explicit EPFilterWidget(const gui::EPFilter &filter,
                          QWidget *parent = nullptr);
  gui::EPFilter GetFilter();
 signals:
  void FilterChanged();

 private:
  gui::EPFilter filter_;
};

class EPFiltersContentWidget : public QWidget {
  Q_OBJECT
 public:
  explicit EPFiltersContentWidget(const QList<gui::EPFilter> &filters,
                                  QWidget *parent = nullptr);
  QList<gui::EPFilter> GetFilters() const;

 signals:
  void FilterChanged();

 private:
  QList<EPFilterWidget *> filter_widgets_;
};

class EPFiltersExpandableWidget : public ExpandableWidget {
  Q_OBJECT
 public:
  explicit EPFiltersExpandableWidget(const QList<gui::EPFilter> &filters,
                                     QWidget *parent = nullptr);
  QList<gui::EPFilter> GetFilters() const;

 signals:
  void FilterChanged();
};

#endif  // EP_FILTERS_EXPANDABLE_WIDGET_H_