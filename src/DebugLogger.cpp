#include "DebugLogger.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QThread>
#include <QVersionNumber>

#include <csignal>
#include <cstdlib>
#include <exception>

#if defined(Q_OS_UNIX)
#include <execinfo.h>
#include <unistd.h>
#endif

namespace {
QFile* g_logFile = nullptr;
QMutex g_logMutex;
QtMessageHandler g_previousMessageHandler = nullptr;
QString g_logPath;

QString nowText()
{
    return QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
}

QString levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return "DEBUG";
    case QtInfoMsg: return "INFO";
    case QtWarningMsg: return "WARNING";
    case QtCriticalMsg: return "CRITICAL";
    case QtFatalMsg: return "FATAL";
    }
    return "UNKNOWN";
}

void writeRawLine(const QString& line)
{
    QMutexLocker locker(&g_logMutex);
    if (!g_logFile) {
        return;
    }

    QTextStream out(g_logFile);
    out << line << '\n';
    out.flush();
    g_logFile->flush();
}

void writeMessage(const QString& level, const QString& message)
{
    const QString tid = QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16);
    writeRawLine(QString("[%1] [%2] [thread:%3] %4").arg(nowText(), level, tid, message));
}

QString buildBacktrace()
{
#if defined(Q_OS_UNIX)
    void* frames[64];
    const int count = backtrace(frames, 64);
    char** symbols = backtrace_symbols(frames, count);

    QString result;
    QTextStream stream(&result);
    stream << "Backtrace (" << count << " frames):\n";
    if (symbols) {
        for (int i = 0; i < count; ++i) {
            stream << "  #" << i << " " << symbols[i] << "\n";
        }
        std::free(symbols);
    }
    return result;
#else
    return "Backtrace non disponible sur cette plateforme.";
#endif
}

void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    QString where;
    if (context.file) {
        where = QString(" (%1:%2, %3)").arg(context.file).arg(context.line).arg(context.function ? context.function : "");
    }

    writeMessage(levelName(type), msg + where);

    if (g_previousMessageHandler) {
        g_previousMessageHandler(type, context, msg);
    }

    if (type == QtFatalMsg) {
        writeMessage("FATAL", buildBacktrace());
        std::abort();
    }
}

void terminateHandler()
{
    writeMessage("FATAL", "std::terminate appele. Unhandled exception possible.");
    if (const std::exception_ptr eptr = std::current_exception()) {
        try {
            std::rethrow_exception(eptr);
        } catch (const std::exception& e) {
            writeMessage("FATAL", QString("Exception: %1").arg(e.what()));
        } catch (...) {
            writeMessage("FATAL", "Unknown exception.");
        }
    }
    writeMessage("FATAL", buildBacktrace());
    DebugLogger::shutdown();
    std::abort();
}

void signalHandler(int sig)
{
    writeMessage("FATAL", QString("Signal recu: %1").arg(sig));
    writeMessage("FATAL", buildBacktrace());
    DebugLogger::shutdown();

#if defined(Q_OS_UNIX)
    signal(sig, SIG_DFL);
    raise(sig);
#else
    std::abort();
#endif
}

void installCrashHandlers()
{
    std::set_terminate(terminateHandler);
    signal(SIGSEGV, signalHandler);
    signal(SIGABRT, signalHandler);
    signal(SIGFPE, signalHandler);
    signal(SIGILL, signalHandler);
#if defined(SIGBUS)
    signal(SIGBUS, signalHandler);
#endif
}
}

void DebugLogger::initialize(QApplication& app)
{
    app.setApplicationName("DWGViewerAdvanced");
    app.setOrganizationName("DWGViewerAdvanced");

    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (baseDir.isEmpty()) {
        baseDir = QDir::homePath() + "/.local/share/DWGViewerAdvanced";
    }

    const QString logDir = baseDir + "/logs";
    QDir().mkpath(logDir);

    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    g_logPath = logDir + QString("/dwgviewer_%1.log").arg(stamp);

    g_logFile = new QFile(g_logPath);
    if (g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        writeRawLine("============================================================");
        writeMessage("INFO", "Demarrage DWGViewer Advanced");
        writeMessage("INFO", QString("Log: %1").arg(g_logPath));
        writeMessage("INFO", QString("OS: %1").arg(QSysInfo::prettyProductName()));
        writeMessage("INFO", QString("CPU: %1").arg(QSysInfo::currentCpuArchitecture()));
        writeMessage("INFO", QString("Qt: %1").arg(qVersion()));
        writeMessage("INFO", QString("Application path: %1").arg(app.applicationFilePath()));
        writeMessage("INFO", QString("Working directory: %1").arg(QDir::currentPath()));
    } else {
        delete g_logFile;
        g_logFile = nullptr;
    }

    g_previousMessageHandler = qInstallMessageHandler(qtMessageHandler);
    installCrashHandlers();
}

void DebugLogger::shutdown()
{
    QMutexLocker locker(&g_logMutex);
    if (g_logFile) {
        QTextStream out(g_logFile);
        out << "[" << nowText() << "] [INFO] Fermeture du logger" << '\n';
        out.flush();
        g_logFile->flush();
        g_logFile->close();
        delete g_logFile;
        g_logFile = nullptr;
    }
}

QString DebugLogger::logFilePath()
{
    return g_logPath;
}

QString DebugLogger::logDirectoryPath()
{
    return QFileInfo(g_logPath).absolutePath();
}

void DebugLogger::logInfo(const QString& message)
{
    writeMessage("INFO", message);
}

void DebugLogger::logWarning(const QString& message)
{
    writeMessage("WARNING", message);
}

void DebugLogger::logDebug(const QString& message)
{
    writeMessage("DEBUG", message);
}

void DebugLogger::logError(const QString& message)
{
    writeMessage("ERROR", message);
}

void DebugLogger::logContext(const QString& context, const QString& detail)
{
    if (detail.isEmpty()) {
        writeMessage("TRACE", context);
    } else {
        writeMessage("TRACE", context + " | " + detail);
    }
}
