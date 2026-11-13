#pragma once
#include <QString>
#include <QSettings>

class SettingsManager {
public:
    static SettingsManager& instance();

    QString reportsPath() const;
    void setReportsPath(const QString& p);

    QString pcName();
    void setPcName(const QString& n);

    bool showAllReports() const;
    void setShowAllReports(bool v);

    QString adminPassword() const;

private:
    SettingsManager();
    QSettings m_s;
};