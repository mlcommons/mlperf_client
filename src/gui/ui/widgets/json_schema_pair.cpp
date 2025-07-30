#include "json_schema_pair.h"

JsonSchemaPair::JsonSchemaPair(const QString& key, const nlohmann::json& schema,
                               QWidget* parent)
    : key_(key), schema_(schema), parent_(parent) {
  label_ = new QLabel(key_, parent_);
  label_->setObjectName("json_schema_label");
  input_widget_ = CreateInputWidget(schema_);
}

QComboBox* JsonSchemaPair::CreateComboBox(const nlohmann::json& schema) {
  auto combo_box = new QComboBox(parent_);
  for (const auto& item : schema["enum"]) {
    combo_box->addItem(QString::fromStdString(item));
  }
  if (schema.contains("default")) {
    combo_box->setCurrentText(QString::fromStdString(schema["default"]));
  }
  return combo_box;
}

QLineEdit* JsonSchemaPair::CreateLineEdit(const nlohmann::json& schema) {
  auto line_edit = new QLineEdit(parent_);
  if (schema.contains("maxLength")) {
    line_edit->setMaxLength(schema["maxLength"]);
  }
  if (schema.contains("default")) {
    line_edit->setText(QString::fromStdString(schema["default"]));
  }
  line_edit->setMinimumWidth(150);
  line_edit->setMaximumHeight(30);
  return line_edit;
}

QSpinBox* JsonSchemaPair::CreateSpinBox(const nlohmann::json& schema) {
  auto spin_box = new QSpinBox(parent_);
  if (schema.contains("minimum")) {
    spin_box->setMinimum(schema["minimum"]);
  }
  if (schema.contains("maximum")) {
    spin_box->setMaximum(schema["maximum"]);
  } else {
    spin_box->setMaximum(999);
  }
  if (schema.contains("default")) {
    spin_box->setValue(schema["default"]);
  }
  return spin_box;
}

QCheckBox* JsonSchemaPair::CreateCheckBox(const nlohmann::json& schema) {
  auto check_box = new QCheckBox(parent_);
  check_box->setProperty("class", "secondary_checkbox");
  if (schema.contains("default")) {
    check_box->setChecked(schema["default"]);
  }
  return check_box;
}

QWidget* JsonSchemaPair::CreateInputWidget(const nlohmann::json& schema) {
  if (schema.contains("enum")) {
    widget_type_ = "combo_box";
    return CreateComboBox(schema);
  }
  if (schema.contains("type")) {
    if (schema["type"] == "string") {
      widget_type_ = "line_edit";
      return CreateLineEdit(schema);
    }
    if (schema["type"] == "integer") {
      widget_type_ = "spin_box";
      return CreateSpinBox(schema);
    }
    if (schema["type"] == "boolean") {
      widget_type_ = "check_box";
      return CreateCheckBox(schema);
    }
  }
  return new QLabel("Unsupported type");
}

nlohmann::json JsonSchemaPair::GetValue() const {
  nlohmann::json json_value;
  if (widget_type_ == "combo_box") {
    json_value =
        static_cast<QComboBox*>(input_widget_)->currentText().toStdString();
  } else if (widget_type_ == "line_edit") {
    json_value = static_cast<QLineEdit*>(input_widget_)->text().toStdString();
  } else if (widget_type_ == "spin_box") {
    json_value = static_cast<QSpinBox*>(input_widget_)->value();
  } else if (widget_type_ == "check_box") {
    json_value = static_cast<QCheckBox*>(input_widget_)->isChecked();
  } else {
    json_value = nullptr;
  }

  return json_value;
}
