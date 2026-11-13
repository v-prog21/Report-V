#pragma once
#include <QAbstractTableModel>
#include <QList>
#include "reportmanager.h"

class ReportModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Col { ColId = 0, ColTitle, ColAuthor, ColStatus, ColCreated, ColCount };
    explicit ReportModel(QObject* parent = nullptr);

    void setReports(const QList<Report>& reports);
    Report at(int row) const;

    int rowCount(const QModelIndex&) const override;
    int columnCount(const QModelIndex&) const override;
    QVariant data(const QModelIndex&, int role) const override;
    QVariant headerData(int sec, Qt::Orientation o, int role) const override;

private:
    QList<Report> m_reports;
};