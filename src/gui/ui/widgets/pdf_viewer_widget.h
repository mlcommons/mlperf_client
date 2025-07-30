#ifndef PDF_VIEWER_WIDGET_H_
#define PDF_VIEWER_WIDGET_H_

#include <QWidget>

class QPdfDocument;
class QPdfView;

class PdfViewerWidget : public QWidget {
  Q_OBJECT

 public:
  explicit PdfViewerWidget(QWidget* parent = nullptr);

  bool LoadPdf(const QString& file_path);

 private:
  QPdfDocument* pdf_document_;
  QPdfView* pdf_view_;
};

#endif  // PDF_VIEWER_WIDGET_H_