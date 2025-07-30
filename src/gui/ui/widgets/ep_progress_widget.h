#ifndef EP_PROGRESS_WIDGET_H
#define EP_PROGRESS_WIDGET_H

#include <QWidget>

class QFrame;
class QLabel;
class ElidedLabel;

class EPProgressWidget : public QWidget {
  Q_OBJECT
 public:
  EPProgressWidget(QString name, QString description, QString icon_path,
                   QString long_name, QWidget *parent = nullptr);
  void SetProgress(int total_steps, int current_step);
  QString GetIconPath() const;
  void Start();
  void SetCompleted(bool success);
  QString GetName() const;
  void SetTransparent(bool transparent);

 private:
  void SetupUI();
  QFrame *content_frame_;
  QLabel *icon_label_;
  ElidedLabel *ep_name_label_;
  QLabel *progress_label_;
  QString ep_name_;
  QString ep_long_name_;
  QString ep_description_;
  QString icon_path_;
};

#endif  // EP_PROGRESS_WIDGET_H
