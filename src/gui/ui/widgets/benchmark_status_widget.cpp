#include "benchmark_status_widget.h"

#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>

#include "core/gui_utils.h"
#include "core/types.h"
#include "elided_label.h"

#ifdef Q_OS_IOS
#include <QScroller>
#endif

static constexpr int STATUS_WIDGET_MIN_WIDTH = 600;
static constexpr int STATUS_WIDGET_MIN_HEIGHT = 300;
static constexpr int STATUS_WIDGET_RADIUS = 10;

BenchmarkStatusCardWidget::BenchmarkStatusCardWidget(
    const gui::EPBenchmarkStatus& status, QWidget* parent)
    : QWidget(parent),
      show_more_(false),
      error_label_(nullptr),
      show_more_button_(nullptr) {
  setAttribute(Qt::WA_StyledBackground, true);

  ElidedLabel* header_label = new ElidedLabel(status.ep_name_, this);
  header_label->SetElideMode(Qt::ElideMiddle);
  header_label->setProperty("class", "medium_regular_label");

  QHBoxLayout* top_layout = new QHBoxLayout;
  top_layout->setSpacing(50);
  top_layout->addWidget(header_label, 1);
  top_layout->addWidget(CreateStatusLabel(status.success_));

  QVBoxLayout* main_layout = new QVBoxLayout;
  main_layout->addLayout(top_layout);
  setLayout(main_layout);

  if (status.error_message_.isEmpty()) return;

  error_label_ = new ElidedLabel(
      "Error: " + gui::utils::NormalizeNewlines(status.error_message_), this);
  error_label_->setProperty("class", "medium_regular_label");

  show_more_button_ = new QPushButton(this);
  show_more_button_->setObjectName("show_more_button");

  QHBoxLayout* error_layout = new QHBoxLayout;
  error_layout->addWidget(error_label_, 1);
  error_layout->addWidget(show_more_button_, 0,
                          Qt::AlignBottom | Qt::AlignRight);

  main_layout->addLayout(error_layout, 1);

  connect(show_more_button_, &QPushButton::clicked, this,
          &BenchmarkStatusCardWidget::OnShowMoreClicked);

  show_more_ = true;
  OnShowMoreClicked();
}

void BenchmarkStatusCardWidget::OnShowMoreClicked() {
  show_more_ = !show_more_;
  error_label_->SetPreferredLineCount(show_more_ ? 10 : 1);
  error_label_->updateGeometry();
  show_more_button_->setText(show_more_ ? "Show less" : "Show more");
}

QWidget* BenchmarkStatusCardWidget::CreateStatusLabel(bool success) {
  QLabel* status_label = new QLabel(success ? "Succeeded" : "Failed", this);
  status_label->setProperty("class", "medium_normal_label");

  QHBoxLayout* status_layout = new QHBoxLayout;
  status_layout->setContentsMargins(10, 0, 10, 0);
  status_layout->addWidget(status_label);

  QWidget* status_widget = new QWidget(this);
  status_widget->setProperty("class", "status_label");

  if (success) status_widget->setProperty("success", "true");
  status_widget->setLayout(status_layout);

  return status_widget;
}

BenchmarkStatusWidget::BenchmarkStatusWidget(const QString& action_type,
                                             const gui::BenchmarkStatus& status,
                                             QWidget* parent)
    : QDialog(parent),
      open_logs_requested_(false),
      open_report_requested_(false) {
  setMinimumSize(STATUS_WIDGET_MIN_WIDTH, STATUS_WIDGET_MIN_HEIGHT);
  setWindowFlag(Qt::FramelessWindowHint, true);
  setModal(true);
  setAttribute(Qt::WA_TranslucentBackground);

  close_button_ = new QPushButton(this);
  close_button_->setIcon(QIcon(":/icons/resources/icons/close_icon.png"));
  close_button_->setIconSize(QSize(18, 18));
  close_button_->setFixedSize(24, 24);
  connect(close_button_, &QPushButton::clicked, this, &QDialog::reject);

  bool has_succeeded_ep = false;
  for (const auto& st : status.eps_benchmark_status_)
    has_succeeded_ep |= st.success_;

  // The plain "cancelled" message is only appropriate when nothing completed.
  bool show_cancelled_message = status.cancelled_ && !has_succeeded_ep;

  QLabel* header_label = new QLabel(this);
  header_label->setProperty("class", "header_H3");
  header_label->setText(action_type +
                        (status.cancelled_
                             ? " Cancelled"
                             : (status.success_ ? " Succeeded" : " Failed")));

  QWidget* content_widget = nullptr;
  if (show_cancelled_message) {
    const QString message =
        action_type == "Download"
            ? "The download was cancelled before completion.\nNot all files "
              "were downloaded."
            : "The benchmark run was cancelled before completion.\nNo results "
              "were generated.";
    QLabel* message_label = new QLabel(message, this);
    message_label->setProperty("class", "medium_regular_label");
    message_label->setAlignment(Qt::AlignCenter);
    QVBoxLayout* message_layout = new QVBoxLayout;
    message_layout->addStretch();
    message_layout->addWidget(message_label);
    message_layout->addStretch();
    content_widget = new QWidget(this);
    content_widget->setLayout(message_layout);
  } else {
    QWidget* cards_widget = new QWidget(this);
    QVBoxLayout* cards_layout = new QVBoxLayout;
    cards_layout->setContentsMargins(20, 0, 20, 0);
    cards_layout->setSpacing(20);
    for (auto& st : status.eps_benchmark_status_) {
      auto* widget = new BenchmarkStatusCardWidget(st, cards_widget);
      cards_layout->addWidget(widget);
    }
    cards_layout->addStretch();

    cards_widget->setLayout(cards_layout);

    QScrollArea* cards_scroll_area = new QScrollArea();
    cards_scroll_area->setWidgetResizable(true);
    cards_scroll_area->setWidget(cards_widget);
#ifdef Q_OS_IOS
    QScroller::grabGesture(cards_scroll_area);
#endif
    content_widget = cards_scroll_area;
  }

  auto* main_layout = new QVBoxLayout;
  main_layout->setContentsMargins(9, 9, 9, 9);
  main_layout->setSpacing(0);
  main_layout->addWidget(close_button_, 0, Qt::AlignRight);
  main_layout->addWidget(header_label, 0, Qt::AlignCenter);
  main_layout->addSpacing(10);
  main_layout->addWidget(content_widget);

#ifdef Q_OS_IOS
  const QString logs_button_text = "Share Logs";
#else
  const QString logs_button_text = "Open Logs";
#endif
  QPushButton* open_logs_button = new QPushButton(logs_button_text, this);
  open_logs_button->setFixedSize(150, 40);
  open_logs_button->setProperty("class", "secondary_button");
  connect(open_logs_button, &QPushButton::clicked, this, [this]() {
    open_logs_requested_ = true;
    accept();
  });

  const bool show_open_report = has_succeeded_ep && action_type != "Download";

  QHBoxLayout* bottom_layout = new QHBoxLayout;
  bottom_layout->addStretch();
  bottom_layout->addWidget(open_logs_button);
  bottom_layout->addStretch();

  if (show_open_report) {
    QPushButton* open_report_button = new QPushButton("Open Report", this);
    open_report_button->setFixedSize(150, 40);
    open_report_button->setProperty("class", "secondary_button");
    connect(open_report_button, &QPushButton::clicked, this, [this]() {
      open_report_requested_ = true;
      accept();
    });
    bottom_layout->addWidget(open_report_button);
    bottom_layout->addStretch();
  }

  main_layout->addSpacing(10);
  main_layout->addLayout(bottom_layout);
  main_layout->addSpacing(10);
  setLayout(main_layout);
}

void BenchmarkStatusWidget::paintEvent(QPaintEvent* event) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setBrush(QColor("#05487E"));
  QPen pen(QColor("#5583A7"), 1);
  painter.setPen(pen);
  painter.drawRoundedRect(rect(), STATUS_WIDGET_RADIUS, STATUS_WIDGET_RADIUS);

  QDialog::paintEvent(event);
}
