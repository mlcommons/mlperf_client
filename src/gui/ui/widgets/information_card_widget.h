#ifndef INFORMATION_CARD_WIDGET_H_
#define INFORMATION_CARD_WIDGET_H_

#include <QWidget>

/**
 * @class InformationCardWidget
 * @brief Card widget for displaying structured information.
 */
class InformationCardWidget : public QWidget {
  Q_OBJECT

 public:
  InformationCardWidget(const QString &image_path, const QString &header_text,
                        const QList<QPair<QString, QString>> &header_key_values,
                        QWidget *parent = nullptr);
  ~InformationCardWidget() = default;

 private:
  /**
   * @brief Create a grid layout for displaying key-value pairs.
   */
  QLayout *CreateKeyValueLayout(
      const QList<QPair<QString, QString>> &key_values);
};

#endif  // INFORMATION_CARD_WIDGET_H_