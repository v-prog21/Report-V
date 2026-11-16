#pragma once

#include <QAbstractTableModel>
#include <QHash>
#include <QList>
#include <QSet>
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
        UnreadRole,
        LastMessageRole,
        KindRole,
        OnlineRole,
        ActivityTextRole
    };

    explicit ReportModel(QObject* parent = nullptr);

    void setReports(const QList<Report>& reports);
    void setSelfId(const QString& selfId);
    void setOnlineIds(const QSet<QString>& ids);
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
    QString previewText(const Report& report) const;
    static QString activityText(const Report& report);

    QList<Report> m_reports;
    QSet<QString> m_onlineIds;
    QString m_selfId;
};

/// Делегат двухстрочного списка диалогов в стиле Telegram Desktop:
/// имя + время, ниже последнее сообщение + бейдж непрочитанных.
class ChatListDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit ChatListDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};
