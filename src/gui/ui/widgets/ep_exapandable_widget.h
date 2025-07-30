#ifndef EP_EXPANDABLE_WIDGET_H_
#define EP_EXPANDABLE_WIDGET_H_

#include <nlohmann/json.hpp>

#include "expandable_widget.h"

class QGridLayout;
class QHBoxLayout;
class QLabel;
class QCheckBox;
class JsonSchemaPair;

class ContentWidget : public QWidget {
  Q_OBJECT
 public:
  explicit ContentWidget(const nlohmann::json &fields,
                         const nlohmann::json &values,
                         const QStringList &devices, QWidget *parent = nullptr);
  nlohmann::json GetConfiguration();

 private:
  QGridLayout *main_layout_;
  QList<JsonSchemaPair *> m_json_schema_pairs;
  void SetupUi(const nlohmann::json &fields, const nlohmann::json &values,
               const QStringList &devices);
};

class EPExpandableHeaderWidget : public QWidget {
  Q_OBJECT

 public:
  explicit EPExpandableHeaderWidget(const QString &ep_name,
                                    const QString &short_description,
                                    const QString &help_text,
                                    QWidget *parent = nullptr);
  void SetDeletable(bool b);
  bool IsSelected() const;

  void SetChecked(bool checked) const;
  int GetEPNameWidthHint() const;
  int GetEPNameWidth() const;
  void SetEPNameWidth(int width);
 signals:
  void AddButtonClicked();
  void DeleteButtonClicked();
  void SelectionChanged(bool is_selected);

 private:
  QHBoxLayout *m_layout;
  QCheckBox *m_select_button;
  QLabel *m_ep_name;
  QLabel *m_info_icon;
  QLabel *m_short_description;
  QPushButton *m_add_button;
  QPushButton *m_delete_button;
  void SetupUi(const QString &ep_name, const QString &short_description,
               const QString &help_text);
  void SetupConnections();

 private slots:
  void SetSelected(bool selected);
};

class EPExpandableWidget : public ExpandableWidget {
  Q_OBJECT
 public:
  explicit EPExpandableWidget(const QString &ep_name,
                              const QString &short_description,
                              const QString &help_text,
                              const QStringList &devices,
                              const nlohmann::json &fields,
                              const nlohmann::json &values, QWidget *parent);
  void SetDeletable(bool b);
  bool IsSelected() const;
  void SetSelected(bool selected) const;

  int GetEPNameWidthHint() const;
  int GetEPNameWidth() const;
  void SetEPNameWidth(int width);

  nlohmann::json GetConfiguration();
 signals:
  void AddButtonClicked();
  void DeleteButtonClicked();
  void SelectionChanged(bool is_selected);

 private:
  EPExpandableHeaderWidget *m_header_widget;
  ContentWidget *m_content_widget;

  void SetupUi(const QString &ep_name, const QString &short_description,
               const QString &help_text, const QStringList &devices,
               const nlohmann::json &fields, const nlohmann::json &values);
  void SetupConnections();

 private slots:
  void OnSelectionChanged(bool is_selected);
};

#endif  // EP_EXPANDABLE_WIDGET_H_