#include "ep_filters_widget.h"

#include <QLabel>
#include <QLayout>

#include "checkable_combo_box.h"

EPFiltersWidget::EPFiltersWidget(const QList<gui::EPFilter>& filters,
                                 QWidget* parent) {
  QHBoxLayout* header_filters_layout = new QHBoxLayout;
  header_filters_layout->setContentsMargins(0, 0, 0, 0);
  header_filters_layout->setSpacing(20);

  for (int i = 0; i < filters.size(); ++i) {
    QVBoxLayout* widget_layout = new QVBoxLayout;
    widget_layout->setContentsMargins(0, 0, 0, 0);
    widget_layout->setSpacing(6);
    QLabel* name_label = new QLabel(filters.at(i).name, this);
    name_label->setProperty("class", "medium_normal_label");
    widget_layout->addWidget(name_label);
    CheckableComboBox* checkable_box =
        new CheckableComboBox(filters.at(i).options, this);
    combo_box_widgets_.insert(filters.at(i).name, checkable_box);
    widget_layout->addWidget(checkable_box);
    widget_layout->addStretch();
    header_filters_layout->addLayout(widget_layout);
    connect(checkable_box, &CheckableComboBox::FilterChanged, this,
            &EPFiltersWidget::FilterChanged);
  }

  setLayout(header_filters_layout);
}

QList<gui::EPFilter> EPFiltersWidget::GetFilters() const {
  QList<gui::EPFilter> filters;
  for (const auto& [name, combo_box] : combo_box_widgets_.asKeyValueRange())
    filters.append({name, combo_box->GetOptions()});
  return filters;
}