#pragma once

#include <QDialog>
#include <QImage>
#include <QString>

class QLabel;
class QSlider;
class QSpinBox;
class QTimer;

// Paint.NET-style "Save Configuration" dialog: shown after the user has picked a
// lossy format (JPEG / WebP) in Save As. It offers a quality slider with a live
// preview of the compressed result and an estimated file size, so the size /
// quality trade-off is visible before committing to disk.
//
// Only meaningful for formats whose encoder honours quality; call
// supportsQuality(suffix) to decide whether to show it at all.
class SaveConfigDialog : public QDialog {
    Q_OBJECT
public:
    // flat: the flattened image about to be written. suffix: lower-case extension
    // without the dot ("jpg", "jpeg", "webp"...).
    SaveConfigDialog(const QImage &flat, const QString &suffix, QWidget *parent = nullptr);

    int quality() const;

    // True for formats that carry a quality setting worth a dialog.
    static bool supportsQuality(const QString &suffix);

private:
    void scheduleUpdate();   // debounce re-encoding while the slider is dragged
    void updatePreview();    // encode at the current quality; refresh size + image
    QByteArray encode(int quality) const;

    QImage m_encodeSource;    // colour-corrected copy fed to the encoder
    QString m_format;         // Qt format name passed to QImageWriter ("jpeg"...)
    QSlider *m_slider = nullptr;
    QSpinBox *m_spin = nullptr;
    QLabel *m_preview = nullptr;
    QLabel *m_sizeLabel = nullptr;
    QTimer *m_debounce = nullptr;
};
