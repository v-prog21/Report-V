#pragma once

#include <QDialog>

#include "devicerepository.h"

class QTableWidget;

class DeviceDetailDialog : public QDialog {
    Q_OBJECT
public:
    DeviceDetailDialog(const Device& device, const QList<DeviceTest>& tests,
                       QWidget* parent = nullptr);

signals:
    void fgisRequested(const QString& numDoc);

private:
    QString selectedNumDoc() const;

    QList<DeviceTest> m_tests;
    QTableWidget* m_testsTable = nullptr;
};
