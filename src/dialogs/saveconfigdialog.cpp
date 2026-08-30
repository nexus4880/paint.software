#include "saveconfigdialog.h"
#include "i18n.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QTimer>
#include <QBuffer>
#include <QImageWriter>
#include <QPainter>
#include <QLocale>

bool SaveConfigDialog::supportsQuality(const QString &suffix) {
    const QString s = suffix.toLower();
    return s == "jpg" || s == "jpeg" || s == "webp";
}

SaveConfigDialog::SaveConfigDialog(const QImage &flat, const QString &suffix, QWidget *parent)
    : QDialog(parent) {
    const QString s = suffix.toLower();
    m_format = (s == "jpg" || s == "jpeg") ? "jpeg" : s;

    setWindowTitle(TR("Configuration d'enregistrement"));

    // Match Document::save's colour handling: formats without an alpha channel
    // get an opaque white background so transparent areas aren't encoded black.
    static const QStringList alphaFormats = {"png", "webp", "tiff", "tif", "ico", "gif"};
    if (!alphaFormats.contains(s)) {
        QImage opaque(flat.size(), QImage::Format_RGB32);
        opaque.fill(Qt::white);
        QPainter p(&opaque);
        p.drawImage(0, 0, flat);
        p.end();
        m_encodeSource = opaque;
    } else {
        m_encodeSource = flat;
    }

    auto *root = new QVBoxLayout(this);

    m_preview = new QLabel;
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(340, 240);
    m_preview->setStyleSheet("border: 1px solid palette(mid);");
    root->addWidget(m_preview);

    auto *grid = new QGridLayout;
    grid->addWidget(new QLabel(TR("Qualité :")), 0, 0);
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(0, 100);
    m_slider->setValue(90);
    m_slider->setMinimumWidth(200);
    grid->addWidget(m_slider, 0, 1);
    m_spin = new QSpinBox;
    m_spin->setRange(0, 100);
    m_spin->setValue(90);
    grid->addWidget(m_spin, 0, 2);
    root->addLayout(grid);

    m_sizeLabel = new QLabel;
    m_sizeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    root->addWidget(m_sizeLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_slider, &QSlider::valueChanged, m_spin, &QSpinBox::setValue);
    connect(m_spin, QOverload<int>::of(&QSpinBox::valueChanged), m_slider, &QSlider::setValue);
    connect(m_slider, &QSlider::valueChanged, this, &SaveConfigDialog::scheduleUpdate);

    // Re-encoding a large image on every slider tick would stutter; coalesce.
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(120);
    connect(m_debounce, &QTimer::timeout, this, &SaveConfigDialog::updatePreview);

    updatePreview();
}

int SaveConfigDialog::quality() const {
    return m_slider->value();
}

void SaveConfigDialog::scheduleUpdate() {
    m_debounce->start();
}

QByteArray SaveConfigDialog::encode(int quality) const {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, m_format.toUtf8());
    writer.setQuality(quality);
    writer.write(m_encodeSource);
    return bytes;
}

void SaveConfigDialog::updatePreview() {
    const QByteArray bytes = encode(m_slider->value());

    // Estimated on-disk size.
    const QString size = QLocale().formattedDataSize(bytes.size(), 1, QLocale::DataSizeSIFormat);
    m_sizeLabel->setText(TR("Taille estimée : ") + size);

    // Show the *decoded* compressed result so JPEG/WebP artefacts are visible.
    QImage decoded = QImage::fromData(bytes, m_format.toUtf8());
    if (decoded.isNull()) decoded = m_encodeSource;   // codec absent: show source
    const QSize target = m_preview->size() - QSize(4, 4);
    m_preview->setPixmap(QPixmap::fromImage(
        decoded.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}
