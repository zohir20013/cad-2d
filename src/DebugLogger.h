#pragma once

#include <QString>

class QApplication;

class DebugLogger
{
public:
    static void initialize(QApplication& app);
    static void shutdown();

    static QString logFilePath();
    static QString logDirectoryPath();
    static void logInfo(const QString& message);
    static void logDebug(const QString& message);
    static void logWarning(const QString& message);
    static void logError(const QString& message);
    static void logContext(const QString& context, const QString& detail = QString());

private:
    DebugLogger() = delete;
};
