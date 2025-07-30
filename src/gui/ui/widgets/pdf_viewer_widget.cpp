#include "pdf_viewer_widget.h"

#include <QVBoxLayout>
#include <QtPdf/QPdfDocument>
#include <QtPdfWidgets/QPdfView>

#ifdef Q_OS_IOS
#include <QScroller>
#endif

PdfViewerWidget::PdfViewerWidget(QWidget* parent)
    : QWidget(parent),
      pdf_document_(new QPdfDocument(this)),
      pdf_view_(new QPdfView(this)) {
  auto* layout = new QVBoxLayout(this);
  layout->addWidget(pdf_view_);
  layout->setContentsMargins(0, 0, 0, 0);

  pdf_view_->setDocument(pdf_document_);
  pdf_view_->setPageMode(QPdfView::PageMode::MultiPage);
  pdf_view_->setZoomMode(QPdfView::ZoomMode::FitToWidth);
  pdf_view_->setPageSpacing(1);
  pdf_view_->setDocumentMargins(QMargins(0, 0, 0, 0));

  pdf_view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

#ifdef Q_OS_IOS
  QScroller::grabGesture(pdf_view_, QScroller::TouchGesture);
#endif
}

bool PdfViewerWidget::LoadPdf(const QString& file_path) {
  return pdf_document_->load(file_path) != QPdfDocument::Error::None;
}