#pragma once

#include <QDialog>
#include <QSize>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QRadioButton;

class ExportDialog : public QDialog {
    Q_OBJECT

public:
    ExportDialog(QSize projectSize, double projectFps, const QString& suggestedPath,
                 bool canCopy, const QString& whyNotCopy, QWidget* parent = nullptr);

    QString path() const;
    QSize size() const;
    double fps() const;
    int crf() const;
    bool useGraphicsCard() const;
    bool instant() const; // copy without re-encoding

private:
    void browse();

    QSize m_projectSize;
    double m_projectFps;
    QLineEdit* m_path;
    QComboBox* m_resolution;
    QComboBox* m_frameRate;
    QComboBox* m_quality;
    QCheckBox* m_graphicsCard;
    QRadioButton* m_instant;
    QRadioButton* m_normal;
};
