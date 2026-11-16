#pragma once

#include <QAbstractTableModel>
#include <QHash>
#include <QList>
#include <QStyledItemDelegate>

#include "reportmanager.h"

class ReportModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColId = 0,
        ColTitle,
        ColType,
        ColMessages,
        ColStatus,
        ColUpdated,
        ColCount
    };

    enum Role {
        SortRole = Qt::UserRole + 1,
        FilterRole,
        StatusRole,
        ReportIdRole,
        UnreadRole
    };

    explicit ReportModel(QObject* parent = nullptr);

    void setReports(const QList<Report>& reports);
    void setSelfId(const QString& selfId);
    void clear();
    Report at(int row) const;
    bool contains(const QString& reportId) const;
    bool updateFromReport(const Report& report);
    QString selfId() const { return m_selfId; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    int unreadCount(const Report& report) const;
    static QString activityText(const Report& report);

    QList<Report> m_reports;
    QString m_selfId;
};

class ReportStatusDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit ReportStatusDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};
