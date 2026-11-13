#include "reportmodel.h"
#include <QFileInfo>

ReportModel::ReportModel(QObject* parent) : QAbstractTableModel(parent) {}

void ReportModel::setReports(const QList<Report>& reports) {
    beginResetModel();
    m_reports = reports;
    endResetModel();
}

Report ReportModel::at(int row) const {
    if (row < 0 || row >= m_reports.size()) return {};
    return m_reports[row];
}

int ReportModel::rowCount(const QModelIndex&) const { return m_reports.size(); }
int ReportModel::columnCount(const QModelIndex&) const { return ColCount; }

QVariant ReportModel::data(const QModelIndex& idx, int role) const {
    if (!idx.isValid()) return {};
    const Report& r = m_reports[idx.row()];
    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
        case ColId:      return r.id;
        case ColTitle:   return r.title;
        case ColAuthor:  return r.author;
        case ColStatus:  return r.status == "resolved" ? "Решено" : "Открыт";
        case ColCreated: return r.created;
        }
    }
    if (role == Qt::UserRole) {
        // Поиск по всем полям
        return r.title + " " + r.id + " " + r.description + " " + r.author;
    }
    return {};
}

QVariant ReportModel::headerData(int sec, Qt::Orientation o, int role) const {
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (sec) {
    case ColId:      return "ID";
    case ColTitle:   return "Название";
    case ColAuthor:  return "Автор";
    case ColStatus:  return "Статус";
    case ColCreated: return "Создан";
    }
    return {};
}