#include "ep_exapandable_widget.h"

#include <QCheckBox>
#include <QLayout>
#include <QListView>
#include <QPushButton>

#include "elided_label.h"
#include "json_schema_pair.h"

#define NO_OF_WIDGETS_IN_ROW 2

ContentWidget::ContentWidget(const nlohmann::json &fields,
                             const nlohmann::json &values,
                             const QStringList &devices, QWidget *parent)
    : QWidget(parent) {
  SetupUi(fields, values, devices);
}

void ContentWidget::SetupUi(const nlohmann::json &fields,
                            const nlohmann::json &values,
                            const QStringList &devices) {
  main_layout_ = new QGridLayout();
  main_layout_->setContentsMargins(20, 10, 20, 10);
  main_layout_->setHorizontalSpacing(20);
  main_layout_->setVerticalSpacing(10);
  setLayout(main_layout_);
  main_layout_->setColumnStretch(1, 1);
  main_layout_->setColumnStretch(3, 1);

  nlohmann::json fields_overriden = fields;
  for (const auto &[name, value] : values.items()) {
    if (fields_overriden.contains(name)) {
      fields_overriden[name]["default"] = value;
    } else {
      nlohmann::json new_schema;
      new_schema["default"] = value;
      std::string value_type_name;
      switch (value.type()) {
        case nlohmann::json::value_t::number_integer:
        case nlohmann::json::value_t::number_unsigned:
          value_type_name = "integer";
          break;
        case nlohmann::json::value_t::number_float:
          value_type_name = "float";
          break;
        default:
          value_type_name = value.type_name();
          break;
      }
      new_schema["type"] = value_type_name;
      fields_overriden[name] = new_schema;
    }
  }

  nlohmann::json device_schema;
  device_schema["type"] = "string";
  if (devices.size() > 1) device_schema["enum"].push_back("All");
  for (const auto &device : devices)
    device_schema["enum"].push_back(device.toStdString());
  fields_overriden["Device"] = device_schema;

  int count = 0;
  for (const auto &[field_name, field_schema] : fields_overriden.items()) {
    QString field_name_str = QString::fromStdString(field_name);
    auto json_schema_pair =
        new JsonSchemaPair(field_name_str, field_schema, this);
    m_json_schema_pairs.append(json_schema_pair);

    // hide device type and vendor for now
    if (field_name_str == "device_type" || field_name_str == "device_id" ||
        field_name_str == "device_vendor" || field_name_str == "device_name") {
      json_schema_pair->GetInputWidget()->hide();
      json_schema_pair->GetLabelWidget()->hide();
      continue;
    }

    int row = count / NO_OF_WIDGETS_IN_ROW;
    int col = count % NO_OF_WIDGETS_IN_ROW;
    // create a warpping widget for the input widget to able to add a spacer
    QWidget *input_widget_wrapper = new QWidget(this);
    QHBoxLayout *input_widget_wrapper_layout =
        new QHBoxLayout(input_widget_wrapper);
    input_widget_wrapper_layout->setContentsMargins(0, 0, 0, 0);
    input_widget_wrapper_layout->addWidget(json_schema_pair->GetInputWidget());
    input_widget_wrapper_layout->addStretch();
    input_widget_wrapper->setLayout(input_widget_wrapper_layout);

    main_layout_->addWidget(json_schema_pair->GetLabelWidget(), row, col * 2);
    main_layout_->addWidget(input_widget_wrapper, row, col * 2 + 1);
    count++;
  }
}

nlohmann::json ContentWidget::GetConfiguration() {
  nlohmann::json output = nlohmann::json::object();
  for (auto widget : m_json_schema_pairs) {
    output[widget->GetKey().toStdString()] = widget->GetValue();
  }
  return output;
}

EPExpandableHeaderWidget::EPExpandableHeaderWidget(
    const QString &ep_name, const QString &short_description,
    const QString &help_text, const QString &category, QWidget *parent)
    : QWidget(parent) {
  SetupUi(ep_name, short_description, help_text, category);
  SetupConnections();
  SetSelected(false);
}

void EPExpandableHeaderWidget::SetDeletable(bool b) {
  m_delete_button->setEnabled(b);
}

bool EPExpandableHeaderWidget::IsSelected() const {
  return m_select_button->isChecked();
}
QString EPExpandableHeaderWidget::GetPromptsType() const {
  return m_prompts_box->currentText().toLower();
}

void EPExpandableHeaderWidget::SetChecked(bool checked) const {
  m_select_button->setChecked(checked);
}

int EPExpandableHeaderWidget::GetEPNameWidthHint() const {
  return m_ep_name->sizeHint().width();
}

int EPExpandableHeaderWidget::GetEPNameWidth() const {
  return m_ep_name->width();
}

void EPExpandableHeaderWidget::SetEPNameWidth(int width) {
  m_ep_name->setFixedWidth(width);
}

void EPExpandableHeaderWidget::SetupUi(const QString &ep_name,
                                       const QString &short_description,
                                       const QString &help_text,
                                       const QString &category) {
  setFixedHeight(60);

  m_select_button = new QCheckBox(this);
  m_select_button->setProperty("class", "primary_checkbox");

  m_ep_name = new QLabel(ep_name, this);
  m_ep_name->setObjectName("ep_name_label");
  m_ep_name->setProperty("class", "ep_name_label");
  m_ep_name->setMinimumWidth(200);

  QLabel *info_icon = new QLabel(this);
  info_icon->setPixmap(
      QIcon(":/icons/resources/icons/ep_info_icon.png").pixmap(16, 16));
  info_icon->setToolTip(help_text);

  ElidedLabel *description_label = new ElidedLabel(short_description, this);
  description_label->setObjectName("ep_short_description_label");

  m_prompts_box = new QComboBox(this);
  m_prompts_box->setView(new QListView());
  m_prompts_box->addItems({"Base", "Extended", "Both"});
  m_prompts_box->setItemData(0, "Run the benchmark on base prompts only.",
                             Qt::ToolTipRole);
  m_prompts_box->setItemData(
      1, "Run the benchmark on extended (4k/8k) prompts only.",
      Qt::ToolTipRole);
  m_prompts_box->setItemData(
      2, "Run the benchmark on both base and extended prompts.",
      Qt::ToolTipRole);

  // m_prompts_box->setProperty("class", "secondary_checkbox");
  QLabel *ext_prompts_label = new QLabel("Prompts", this);
  ext_prompts_label->setObjectName("ep_short_description_label");
  QHBoxLayout *ext_prompts_layout = new QHBoxLayout();
  ext_prompts_layout->setSpacing(5);
  ext_prompts_layout->addWidget(ext_prompts_label);
  ext_prompts_layout->addWidget(m_prompts_box);
  m_prompts_widget = new QWidget(this);
  m_prompts_widget->setLayout(ext_prompts_layout);

  QLabel *category_icon = new QLabel(this);
  category_icon->setPixmap(
      QIcon(QString(":/icons/resources/icons/category_%1.png").arg(category))
          .pixmap(16, 16));

  QString category_text_upper = category;
  if (!category_text_upper.isEmpty())
    category_text_upper[0] = category_text_upper[0].toUpper();
  QLabel *category_label = new QLabel(category_text_upper, this);
  category_label->setProperty("class", "medium_normal_label");

  QHBoxLayout *category_layout = new QHBoxLayout;
  category_layout->setContentsMargins(10, 0, 10, 0);
  category_layout->addWidget(category_icon);
  category_layout->addWidget(category_label);
  QWidget *category_widget = new QWidget(this);
  category_widget->setProperty("class", "category_widget");
  category_widget->setProperty("category", category);
  category_widget->setLayout(category_layout);

  // Add and Delete buttons
  auto createBtnFn = [this](const QString &icon_normal,
                            const QString &icon_disabled) {
    QPushButton *button = new QPushButton(this);
    button->setFixedSize(40, 40);
    button->setIconSize(QSize(24, 24));
    QIcon icon;
    icon.addPixmap(QPixmap(icon_normal), QIcon::Normal);
    icon.addPixmap(QPixmap(icon_disabled), QIcon::Disabled);
    button->setIcon(QIcon(icon));
    button->setProperty("class", "secondary_button_with_icon");
    button->setProperty("has_border", true);
    return button;
  };
  m_add_button = createBtnFn(":/icons/resources/icons/add.png",
                             ":/icons/resources/icons/add_disabled.png");
  m_delete_button = createBtnFn(":/icons/resources/icons/delete.png",
                                ":/icons/resources/icons/delete_disabled.png");

  QHBoxLayout *main_layout = new QHBoxLayout(this);
  main_layout->setContentsMargins(15, 5, 15, 5);
  main_layout->setSpacing(5);
  main_layout->addWidget(m_select_button);
  main_layout->addWidget(m_ep_name);
  main_layout->addSpacing(5);
  main_layout->addWidget(info_icon);
  main_layout->addWidget(description_label, 1);
  main_layout->addWidget(category_widget);
  main_layout->addWidget(m_prompts_widget);
  main_layout->addWidget(m_delete_button);
  main_layout->addSpacing(5);
  main_layout->addWidget(m_add_button);
  setLayout(main_layout);
}

void EPExpandableHeaderWidget::SetupConnections() {
  connect(m_add_button, &QPushButton::clicked, this,
          &EPExpandableHeaderWidget::AddButtonClicked);
  connect(m_delete_button, &QPushButton::clicked, this,
          &EPExpandableHeaderWidget::DeleteButtonClicked);
  connect(m_select_button, &QCheckBox::toggled, this,
          &EPExpandableHeaderWidget::SetSelected);
}

void EPExpandableHeaderWidget::SetSelected(bool selected) {
  m_prompts_widget->setVisible(selected);
  m_add_button->setVisible(selected);
  m_delete_button->setVisible(selected);
  emit SelectionChanged(selected);
}

EPExpandableWidget::EPExpandableWidget(
    const QString &ep_name, const QString &short_description,
    const QString &help_text, const QString &category,
    const QStringList &devices, const nlohmann::json &fields,
    const nlohmann::json &values, QWidget *parent)

    : ExpandableWidget(parent) {
  SetupUi(ep_name, short_description, help_text, category, devices, fields,
          values);
  SetupConnections();

  OnSelectionChanged(m_header_widget->IsSelected());
}

void EPExpandableWidget::SetDeletable(bool b) {
  m_header_widget->SetDeletable(b);
}

bool EPExpandableWidget::IsSelected() const {
  return m_header_widget->IsSelected();
}

QString EPExpandableWidget::GetPromptsType() const {
  return m_header_widget->GetPromptsType();
}

int EPExpandableWidget::GetEPNameWidthHint() const {
  return m_header_widget->GetEPNameWidthHint();
}

int EPExpandableWidget::GetEPNameWidth() const {
  return m_header_widget->GetEPNameWidth();
}

void EPExpandableWidget::SetEPNameWidth(int width) {
  m_header_widget->SetEPNameWidth(width);
}

void EPExpandableWidget::SetupUi(const QString &ep_name,
                                 const QString &short_description,
                                 const QString &help_text,
                                 const QString &category,
                                 const QStringList &devices,
                                 const nlohmann::json &fields,
                                 const nlohmann::json &values) {
  QString description = short_description;
  if (devices.empty()) description += " (No Supported Device)";
  m_header_widget = new EPExpandableHeaderWidget(ep_name, description,
                                                 help_text, category, this);
  m_content_widget = new ContentWidget(fields, values, devices, this);

  ExpandableWidget::SetupUi(m_header_widget, m_content_widget);

  if (devices.empty()) setEnabled(false);
}

void EPExpandableWidget::SetupConnections() {
  connect(m_header_widget, &EPExpandableHeaderWidget::AddButtonClicked, this,
          &EPExpandableWidget::AddButtonClicked);
  connect(m_header_widget, &EPExpandableHeaderWidget::DeleteButtonClicked, this,
          &EPExpandableWidget::DeleteButtonClicked);
  connect(m_header_widget, &EPExpandableHeaderWidget::SelectionChanged, this,
          &EPExpandableWidget::OnSelectionChanged);
}

void EPExpandableWidget::SetSelected(bool selected) const {
  m_header_widget->SetChecked(selected);
}

nlohmann::json EPExpandableWidget::GetConfiguration() {
  return m_content_widget->GetConfiguration();
}

void EPExpandableWidget::OnSelectionChanged(bool is_selected) {
  if (expand_button_->isChecked()) expand_button_->click();
  expand_button_->setVisible(is_selected);
  emit SelectionChanged(is_selected);
}