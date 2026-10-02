#include "ExportDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>

#include <cmath>

ExportDialog::ExportDialog(QSize projectSize, double projectFps, const QString& suggestedPath, QWidget* parent)
    : QDialog(parent)
    , m_projectSize(projectSize)
    , m_projectFps(projectFps)
{
    setWindowTitle("Export");
    setMinimumWidth(460);

    m_path = new QLineEdit(suggestedPath);
    auto* browseButton = new QPushButton("Browse…");
    connect(browseButton, &QPushButton::clicked, this, &ExportDialog::browse);
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(browseButton);

    // Offer the project size plus the usual suspects that are smaller than it
    m_resolution = new QComboBox;
    m_resolution->addItem(QString("Same as project (%1×%2)").arg(projectSize.width()).arg(projectSize.height()),
                          projectSize.height());
    for (int h : { 1080, 720, 480 })
        if (h < projectSize.height())
            m_resolution->addItem(QString("%1p").arg(h), h);

    m_frameRate = new QComboBox;
    m_frameRate->addItem(QString("Same as project (%1 fps)").arg(projectFps, 0, 'g', 4), projectFps);
    for (double f : { 60.0, 30.0, 24.0 })
        if (std::abs(f - projectFps) > 0.01)
            m_frameRate->addItem(QString("%1 fps").arg(f), f);

    m_quality = new QComboBox;
    m_quality->addItem("High", 18);
    m_quality->addItem("Normal", 21);
    m_quality->addItem("Small file", 26);
    m_quality->setCurrentIndex(1);

    m_graphicsCard = new QCheckBox("Use the graphics card (much faster)");
    m_graphicsCard->setChecked(true);
    m_graphicsCard->setToolTip("Falls back to the CPU automatically if your graphics card can't do it");

    auto* form = new QFormLayout;
    form->addRow("Save to", pathRow);
    form->addRow("Resolution", m_resolution);
    form->addRow("Frame rate", m_frameRate);
    form->addRow("Quality", m_quality);
    form->addRow("", m_graphicsCard);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Export");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void ExportDialog::browse()
{
    QString chosen = QFileDialog::getSaveFileName(this, "Export to", m_path->text(), "MP4 video (*.mp4)");
    if (!chosen.isEmpty())
        m_path->setText(chosen);
}

QString ExportDialog::path() const
{
    QString p = m_path->text().trimmed();
    if (!p.isEmpty() && !p.endsWith(".mp4", Qt::CaseInsensitive))
        p += ".mp4";
    return p;
}

QSize ExportDialog::size() const
{
    int h = m_resolution->currentData().toInt();
    double aspect = double(m_projectSize.width()) / m_projectSize.height();
    int w = int(std::lround(h * aspect));
    return QSize(w & ~1, h & ~1); // video encoders want even numbers
}

double ExportDialog::fps() const
{
    return m_frameRate->currentData().toDouble();
}

int ExportDialog::crf() const
{
    return m_quality->currentData().toInt();
}

bool ExportDialog::useGraphicsCard() const
{
    return m_graphicsCard->isChecked();
}
