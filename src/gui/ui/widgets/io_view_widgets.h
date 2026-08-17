#ifndef IO_VIEW_WIDGETS_H_
#define IO_VIEW_WIDGETS_H_

#include <QDialog>
#include <QGraphicsView>
#include <QLabel>

class QEvent;
class QScreen;
class QVBoxLayout;

namespace custom_widgets {

// Panel with a header (leading icon, title, expand button) over a body. The
// expand button — or a double-click on the body — opens a viewer dialog.
// Subclasses supply the body via SetBody() and implement OpenViewer().
class BlockWidget : public QFrame {
  Q_OBJECT
 protected:
  BlockWidget(const QString& title, const QString& icon_path,
              QWidget* parent = nullptr);
  void SetBody(QWidget* body, QWidget* double_click_target = nullptr);
  virtual void OpenViewer() = 0;
  bool eventFilter(QObject* watched, QEvent* event) override;

  QString title_;

 private:
  QVBoxLayout* layout_ = nullptr;
  QWidget* double_click_target_ = nullptr;
};

// Block whose body is a full text
class TextBlockWidget : public BlockWidget {
  Q_OBJECT
 public:
  TextBlockWidget(const QString& title, const QString& body,
                  const QString& icon_path, QWidget* parent = nullptr);

 protected:
  void OpenViewer() override;

 private:
  QString body_;
};

// QLabel that scales a pixmap to fit its area, keeping aspect ratio.
class FittedImageLabel : public QLabel {
  Q_OBJECT
 public:
  explicit FittedImageLabel(const QPixmap& image, QWidget* parent = nullptr);

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  void UpdateScaled();

  QPixmap original_;
};

// Block whose body is an image
class ImageBlockWidget : public BlockWidget {
  Q_OBJECT
 public:
  ImageBlockWidget(const QString& title, const QString& image_path,
                   const QString& icon_path, QWidget* parent = nullptr);

 protected:
  void OpenViewer() override;

 private:
  QPixmap image_;
};

// Frameless, rounded viewer dialog with a dark header bar (title + close
// button) over a body fill. Subclasses provide the content via SetContent().
class ViewerDialog : public QDialog {
  Q_OBJECT
 protected:
  explicit ViewerDialog(const QString& title, QWidget* parent = nullptr);
  void SetContent(QWidget* content);
  // Size to `fraction` of the parent screen's available area, centered on it.
  void ResizeToScreenFraction(double fraction);
  void paintEvent(QPaintEvent* event) override;

 private:
  QScreen* TargetScreen() const;

  QWidget* header_ = nullptr;
  QVBoxLayout* content_layout_ = nullptr;
};

// Viewer dialog for the full body text of a TextBlockWidget.
class TextViewerDialog : public ViewerDialog {
  Q_OBJECT
 public:
  TextViewerDialog(const QString& title, const QString& body,
                   QWidget* parent = nullptr);
};

// QGraphicsView showing a pixmap at full resolution, with wheel-zoom (anchored
// under the cursor) and drag-to-pan.
class ZoomableImageView : public QGraphicsView {
  Q_OBJECT
 public:
  explicit ZoomableImageView(const QPixmap& image, QWidget* parent = nullptr);

 protected:
  void wheelEvent(QWheelEvent* event) override;

 private:
  // Smallest zoom allowed: the image fitted to the view (never upscaling a
  // smaller-than-view image past its natural size).
  double MinScale() const;
};

// Viewer dialog for a generated image, with zoom and pan.
class ImageViewerDialog : public ViewerDialog {
  Q_OBJECT
 public:
  ImageViewerDialog(const QString& title, const QPixmap& image,
                    QWidget* parent = nullptr);
};

}  // namespace custom_widgets

#endif  // IO_VIEW_WIDGETS_H_
