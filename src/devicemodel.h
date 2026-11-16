#pragma once

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>

#include "devicerepository.h"

class DeviceTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColNum = 0,
        ColName,
        ColFactory,
        ColVerification,
        ColIndicator,
        ColCount
    };

    enum Role {
        SortRole = Qt::UserRole + 1,
        FilterRole,
        BucketRole,
        StatusTextRole,
        DeviceIdRole
    };

    explicit DeviceTableModel(QObject* parent = nullptr);

    void setDevices(const QList<Device>& devices);
    Device deviceAt(int row) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    QList<Device> m_devices;
};

class DeviceFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit DeviceFilterProxyModel(QObject* parent = nullptr);

    void setSearchText(const QString& text);
    void setStatusFilter(int bucket); // -1 — все приборы

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    QString m_search;
    int m_statusFilter = -1;
};

class DeviceStatusDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit DeviceStatusDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};
