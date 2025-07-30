#include "ep_filters_expandable_widget.h"

#include <QCheckBox>
#include <QLabel>
#include <QLayout>
#include <QPushButton>

EPFiltersHeaderWidget::EPFiltersHeaderWidget(
    const QList<gui::EPFilter>& filters, QWidget* parent) {
  setFixedHeight(46);

  QLabel* header_label = new QLabel("Filters", this);
  header_label->setProperty("class", "medium_strong_label");

  QHBoxLayout* header_filters_layout = new QHBoxLayout;
  header_filters_layout->setContentsMargins(0, 0, 0, 0);
  header_filters_layout->setSpacing(15);

  for (int i = 0; i < filters.size(); ++i) {
    QLabel* name_label = new QLabel(filters.at(i).name, this);
    name_label->setProperty("class", "medium_normal_label");
    header_filters_layout->addWidget(name_label);
    if (i != filters.size() - 1) {
      QWidget* separator_widget = new QWidget();
      separator_widget->setFixedWidth(1);
      separator_widget->setObjectName("separator_vertical_line_widget");
      header_filters_layout->addWidget(separator_widget);
    }
  }
  filter_names_widget_ = new QWidget(this);
  filter_names_widget_->setLayout(header_filters_layout);

  QHBoxLayout* main_layout = new QHBoxLayout;
  main_layout->setContentsMargins(20, 12, 0, 12);
  main_layout->setSpacing(15);
  main_layout->addWidget(header_label);
  main_layout->addSpacing(5);
  main_layout->addWidget(filter_names_widget_);
  main_layout->addStretch();

  setLayout(main_layout);
}

EPFilterWidget::EPFilterWidget(const gui::EPFilter& filter, QWidget* parent)
    : QWidget(parent), filter_(filter) {
  setAttribute(Qt::WA_StyledBackground, true);
  QVBoxLayout* main_layout = new QVBoxLayout;
  main_layout->setContentsMargins(10, 12, 10, 12);
  main_layout->setSpacing(6);

  QLabel* name_label = new QLabel(filter_.name, this);
  name_label->setProperty("class", "medium_regular_label");
  main_layout->addWidget(name_label);

  QWidget* separator_widget = new QWidget(this);
  separator_widget->setFixedHeight(1);
  separator_widget->setObjectName("separator_horizontal_line_widget");
  main_layout->addWidget(separator_widget);

  for (auto& [option_name, is_active] : filter_.options) {
    QCheckBox* box = new QCheckBox(option_name, this);
    box->setProperty("class", "secondary_checkbox");
    box->setChecked(is_active);
    main_layout->addSpacing(2);
    main_layout->addWidget(box);
    connect(box, &QCheckBox::toggled, this, [&](bool checked) {
      is_active = checked;
      emit FilterChanged();
    });
  }
  main_layout->addStretch();
  setLayout(main_layout);
}

gui::EPFilter EPFilterWidget::GetFilter() { return filter_; }

EPFiltersContentWidget::EPFiltersContentWidget(
    const QList<gui::EPFilter>& filters, QWidget* parent) {
  QHBoxLayout* main_layout = new QHBoxLayout;
  main_layout->setSpacing(20);
  main_layout->setContentsMargins(20, 15, 20, 23);
  for (const auto& filter : filters) {
    EPFilterWidget* filter_widget = new EPFilterWidget(filter, this);
    filter_widgets_.append(filter_widget);
    main_layout->addWidget(filter_widget);

    connect(filter_widget, &EPFilterWidget::FilterChanged, this,
            &EPFiltersContentWidget::FilterChanged);
  }

  setLayout(main_layout);
}

QList<gui::EPFilter> EPFiltersContentWidget::GetFilters() const {
  QList<gui::EPFilter> filters;
  for (auto filter_widget : filter_widgets_)
    filters.append(filter_widget->GetFilter());
  return filters;
}

void EPFiltersHeaderWidget::SetFilterNamesHidden(bool hidden) {
  filter_names_widget_->setVisible(!hidden);
}

EPFiltersExpandableWidget::EPFiltersExpandableWidget(
    const QList<gui::EPFilter>& filters, QWidget* parent)
    : ExpandableWidget(parent) {
  EPFiltersHeaderWidget* header_widget =
      new EPFiltersHeaderWidget(filters, this);

  EPFiltersContentWidget* content_widget =
      new EPFiltersContentWidget(filters, this);

  SetupUi(header_widget, content_widget);

  connect(expand_button_, &QAbstractButton::toggled, header_widget,
          &EPFiltersHeaderWidget::SetFilterNamesHidden);
  connect(content_widget, &EPFiltersContentWidget::FilterChanged, this,
          &EPFiltersExpandableWidget::FilterChanged);
}

QList<gui::EPFilter> EPFiltersExpandableWidget::GetFilters() const {
  return dynamic_cast<EPFiltersContentWidget*>(content_widget_)->GetFilters();
}