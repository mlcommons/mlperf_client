#ifndef JSON_SCHEMA_WIDGET_H
#define JSON_SCHEMA_WIDGET_H

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QString>
#include <QWidget>
#include <QcheckBox>
#include <memory>
#include <nlohmann/json.hpp>

class JsonSchemaPair {
 public:
  explicit JsonSchemaPair(const QString& key, const nlohmann::json& schema,
                          QWidget* parent = nullptr);
  nlohmann::json GetValue() const;
  QString GetKey() const { return key_; }
  QLabel* GetLabelWidget() { return label_; }
  QWidget* GetInputWidget() { return input_widget_; }

  int GetHeight() const {
    int label_height = label_->height();
    int input_height = input_widget_->height();
    return std::max(label_height, input_height);
  }

 private:
  QWidget* CreateInputWidget(const nlohmann::json& schema);
  QComboBox* CreateComboBox(const nlohmann::json& schema);
  QLineEdit* CreateLineEdit(const nlohmann::json& schema);
  QSpinBox* CreateSpinBox(const nlohmann::json& schema);
  QCheckBox* CreateCheckBox(const nlohmann::json& schema);

  QString key_;
  QString widget_type_;
  nlohmann::json schema_;
  QLabel* label_;
  QWidget* input_widget_;
  QWidget* parent_;
};

#endif  // JSON_SCHEMA_WIDGET_H
