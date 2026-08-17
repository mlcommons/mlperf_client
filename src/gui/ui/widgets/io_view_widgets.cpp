#include "io_view_widgets.h"

#include <QApplication>
#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QWheelEvent>

static constexpr int VIEWER_CORNER_RADIUS = 10;
static constexpr double VIEWER_ZOOM_STEP = 1.15;
static constexpr double VIEWER_MAX_ZOOM = 16.0;
static constexpr double VIEWER_SCREEN_FRACTION = 0.7;

namespace custom_widgets {

// ---------------------------------------------------------------------------
// BlockWidget
// ---------------------------------------------------------------------------

BlockWidget::BlockWidget(const QString& title, const QString& icon_path,
                         QWidget* parent)
    : QFrame(parent), title_(title) {
  setObjectName("io_text_block");

  layout_ = new QVBoxLayout(this);
  layout_->setContentsMargins(0, 0, 0, 0);
  layout_->setSpacing(0);

  QFrame* header = new QFrame(this);
  header->setObjectName("io_block_header");
  QHBoxLayout* header_row = new QHBoxLayout(header);
  header_row->setContentsMargins(10, 6, 6, 6);
  header_row->setSpacing(6);

  QIcon lead_icon(icon_path);
  QLabel* icon_label = new QLabel(header);
  icon_label->setPixmap(lead_icon.pixmap(QSize(20, 20)));
  header_row->addWidget(icon_label);

  QLabel* title_label = new QLabel(title_, header);
  title_label->setProperty("class", "medium_regular_label");
  header_row->addWidget(title_label);
  header_row->addStretch(1);

  QPushButton* expand = new QPushButton(header);
  expand->setProperty("class", "secondary_button_with_icon");
  expand->setIcon(QIcon(":/icons/resources/icons/expand_fullscreen.png"));
  expand->setFixedSize(24, 24);
  expand->setIconSize(QSize(20, 20));
  expand->setToolTip("Expand");
  QObject::connect(expand, &QPushButton::clicked, this,
                   &BlockWidget::OpenViewer);
  header_row->addWidget(expand);

  layout_->addWidget(header);
}

void BlockWidget::SetBody(QWidget* body, QWidget* double_click_target) {
  layout_->addWidget(body);
  if (double_click_target) {
    double_click_target_ = double_click_target;
    double_click_target_->installEventFilter(this);
  }
}

bool BlockWidget::eventFilter(QObject* watched, QEvent* event) {
  if (watched == double_click_target_ &&
      event->type() == QEvent::MouseButtonDblClick) {
    OpenViewer();
    return true;
  }
  return QFrame::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// TextBlockWidget
// ---------------------------------------------------------------------------

TextBlockWidget::TextBlockWidget(const QString& title, const QString& body,
                                 const QString& icon_path, QWidget* parent)
    : BlockWidget(title, icon_path, parent), body_(body) {
  QPlainTextEdit* text = new QPlainTextEdit(this);
  text->setObjectName("io_text_body");
  text->setReadOnly(true);
  text->setFrameShape(QFrame::NoFrame);
  text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
  text->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  text->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  text->setMaximumHeight(220);
  text->setTextInteractionFlags(Qt::NoTextInteraction);
  text->document()->setDocumentMargin(10);
  text->setPlainText(body_);

  text->viewport()->setCursor(Qt::PointingHandCursor);
  text->setToolTip("Double-click to view full text");

  QWidget* body_row = new QWidget(this);
  QHBoxLayout* body_row_layout = new QHBoxLayout(body_row);
  body_row_layout->setContentsMargins(0, 6, 3, 6);
  body_row_layout->addWidget(text);

  SetBody(body_row, text->viewport());
}

void TextBlockWidget::OpenViewer() {
  (new TextViewerDialog(title_, body_, this))->show();
}

// ---------------------------------------------------------------------------
// FittedImageLabel
// ---------------------------------------------------------------------------

FittedImageLabel::FittedImageLabel(const QPixmap& image, QWidget* parent)
    : QLabel(parent), original_(image) {
  setMinimumSize(120, 120);
  setMaximumHeight(360);
  setAlignment(Qt::AlignCenter);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  UpdateScaled();
}

void FittedImageLabel::resizeEvent(QResizeEvent* event) {
  UpdateScaled();
  QLabel::resizeEvent(event);
}

void FittedImageLabel::UpdateScaled() {
  if (original_.isNull()) return;
  QSize available = size();
  if (available.width() <= 0) available.setWidth(width());
  if (available.height() <= 0) available.setHeight(maximumHeight());
  setPixmap(original_.scaled(available, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation));
}

// ---------------------------------------------------------------------------
// ImageBlockWidget
// ---------------------------------------------------------------------------

ImageBlockWidget::ImageBlockWidget(const QString& title,
                                   const QString& image_path,
                                   const QString& icon_path, QWidget* parent)
    : BlockWidget(title, icon_path, parent), image_(image_path) {
  if (image_.isNull()) {
    const QString message =
        image_path.isEmpty() ? QString("[image missing]")
                             : QString("[image not found]\n%1").arg(image_path);
    QLabel* missing = new QLabel(message);
    missing->setProperty("class", "small_normal_label");
    missing->setAlignment(Qt::AlignCenter);
    missing->setWordWrap(true);
    SetBody(missing);
    return;
  }

  FittedImageLabel* image_label = new FittedImageLabel(image_);
  image_label->setCursor(Qt::PointingHandCursor);
  image_label->setToolTip("Double-click to zoom");

  QWidget* body_row = new QWidget(this);
  QHBoxLayout* body_row_layout = new QHBoxLayout(body_row);
  body_row_layout->setContentsMargins(0, 6, 6, 6);
  body_row_layout->addWidget(image_label);

  SetBody(body_row, image_label);
}

void ImageBlockWidget::OpenViewer() {
  if (!image_.isNull()) (new ImageViewerDialog(title_, image_, this))->show();
}

// ---------------------------------------------------------------------------
// ViewerDialog
// ---------------------------------------------------------------------------

ViewerDialog::ViewerDialog(const QString& title, QWidget* parent)
    : QDialog(parent) {
  setAttribute(Qt::WA_DeleteOnClose);
  setAttribute(Qt::WA_TranslucentBackground);
  setWindowFlag(Qt::FramelessWindowHint, true);
  setWindowTitle(title);

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  header_ = new QWidget(this);
  QHBoxLayout* header_row = new QHBoxLayout(header_);
  header_row->setContentsMargins(16, 10, 12, 10);

  QLabel* title_label = new QLabel(title, header_);
  title_label->setProperty("class", "medium_strong_label");
  header_row->addWidget(title_label);
  header_row->addStretch(1);

  QPushButton* close_btn = new QPushButton(header_);
  close_btn->setIcon(QIcon(":/icons/resources/icons/close_icon.png"));
  close_btn->setIconSize(QSize(18, 18));
  close_btn->setFixedSize(24, 24);
  QObject::connect(close_btn, &QPushButton::clicked, this, &QDialog::reject);
  header_row->addWidget(close_btn);

  layout->addWidget(header_);

  QWidget* content_area = new QWidget(this);
  content_layout_ = new QVBoxLayout(content_area);
  content_layout_->setContentsMargins(24, 24, 24, 24);
  layout->addWidget(content_area);
}

void ViewerDialog::SetContent(QWidget* content) {
  content_layout_->addWidget(content);
}

QScreen* ViewerDialog::TargetScreen() const {
  const QWidget* ref = parentWidget() ? parentWidget() : this;
  QScreen* screen = ref->screen();
  return screen ? screen : QApplication::primaryScreen();
}

void ViewerDialog::ResizeToScreenFraction(double fraction) {
  const QScreen* screen = TargetScreen();
  if (!screen) return;
  const QRect available = screen->availableGeometry();
  resize(available.size() * fraction);
  move(available.center() - rect().center());
}

void ViewerDialog::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
  QPainterPath path;
  path.addRoundedRect(bounds, VIEWER_CORNER_RADIUS, VIEWER_CORNER_RADIUS);

  painter.setClipPath(path);
  painter.fillRect(rect(), QColor("#125483"));
  painter.fillRect(QRect(0, 0, width(), header_->height()), QColor("#0C324E"));
  painter.setClipping(false);

  painter.setPen(QPen(QColor("#5583A7"), 1));
  painter.setBrush(Qt::NoBrush);
  painter.drawRoundedRect(bounds, VIEWER_CORNER_RADIUS, VIEWER_CORNER_RADIUS);
}

// ---------------------------------------------------------------------------
// TextViewerDialog
// ---------------------------------------------------------------------------

TextViewerDialog::TextViewerDialog(const QString& title, const QString& body,
                                   QWidget* parent)
    : ViewerDialog(title, parent) {
  QPlainTextEdit* text = new QPlainTextEdit();
  text->setObjectName("io_text_body");
  text->setReadOnly(true);
  text->setFrameShape(QFrame::NoFrame);
  text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
  text->document()->setDocumentMargin(0);
  text->setPlainText(body);
  SetContent(text);

  ResizeToScreenFraction(VIEWER_SCREEN_FRACTION);
}

// ---------------------------------------------------------------------------
// ZoomableImageView
// ---------------------------------------------------------------------------

ZoomableImageView::ZoomableImageView(const QPixmap& image, QWidget* parent)
    : QGraphicsView(parent) {
  QGraphicsScene* scene = new QGraphicsScene(this);
  scene->addPixmap(image);
  setScene(scene);

  setDragMode(QGraphicsView::ScrollHandDrag);
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
  setBackgroundBrush(QColor("#125483"));

  // Fit to the view once it has been laid out (shrink to fit; never upscale).
  QMetaObject::invokeMethod(
      this,
      [this] {
        const double fit = MinScale();
        if (fit < 1.0) scale(fit, fit);
      },
      Qt::QueuedConnection);
}

void ZoomableImageView::wheelEvent(QWheelEvent* event) {
  const double factor =
      event->angleDelta().y() > 0 ? VIEWER_ZOOM_STEP : 1.0 / VIEWER_ZOOM_STEP;
  const double current = transform().m11();
  const double next = qBound(MinScale(), current * factor, VIEWER_MAX_ZOOM);
  if (!qFuzzyCompare(next, current)) scale(next / current, next / current);
}

double ZoomableImageView::MinScale() const {
  const QRectF image = sceneRect();
  const QSize view = viewport()->size();
  if (image.isEmpty() || view.isEmpty()) return 1.0;
  const double fit =
      qMin(view.width() / image.width(), view.height() / image.height());
  return qMin(1.0, fit);
}

// ---------------------------------------------------------------------------
// ImageViewerDialog
// ---------------------------------------------------------------------------

ImageViewerDialog::ImageViewerDialog(const QString& title, const QPixmap& image,
                                     QWidget* parent)
    : ViewerDialog(title, parent) {
  SetContent(new ZoomableImageView(image));

  ResizeToScreenFraction(VIEWER_SCREEN_FRACTION);
}

}  // namespace custom_widgets
