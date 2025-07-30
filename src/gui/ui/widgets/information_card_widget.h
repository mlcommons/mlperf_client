#ifndef INFORMATION_CARD_WIDGET_H_
#define INFORMATION_CARD_WIDGET_H_

#include <QWidget>

class InformationCardWidget : public QWidget {
  Q_OBJECT

 public:
  InformationCardWidget(const QString &image_path, const QString &header_text,
                        const QList<QPair<QString, QString>> &header_key_values,
                        QWidget *parent = nullptr);
  ~InformationCardWidget() = default;

 private:
  QLayout *CreateKeyValueLayout(
      const QList<QPair<QString, QString>> &key_values);
};

#endif  // INFORMATION_CARD_WIDGET_H_