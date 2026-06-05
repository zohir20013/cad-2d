#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

namespace CadCommands {

enum class CommandCategory {
    Draw,
    Modify,
    Annotate,
    View,
    Layer,
    Selection,
    Constraint,
    File,
    Utility,
    Unknown
};

struct CommandDefinition {
    QString name;
    QStringList aliases;
    QString description;
    QString syntax;
    CommandCategory category = CommandCategory::Unknown;
    bool transparent = false;
    bool repeatable = true;

    QJsonObject toJson() const;
    static CommandDefinition fromJson(const QJsonObject& obj);
};

struct ParsedCommand {
    QString rawText;
    QString commandName;
    QString resolvedName;
    QStringList arguments;
    bool valid = false;
    QString error;
};

struct CommandHistoryEntry {
    QString rawText;
    QString resolvedName;
    QDateTime timestamp;
    bool accepted = false;

    QJsonObject toJson() const;
    static CommandHistoryEntry fromJson(const QJsonObject& obj);
};

class CadCommandLineEngine
{
public:
    CadCommandLineEngine();

    void registerDefaultCommands();
    bool registerCommand(const CommandDefinition& command, QString* error = nullptr);
    bool unregisterCommand(const QString& nameOrAlias);
    bool hasCommand(const QString& nameOrAlias) const;
    CommandDefinition command(const QString& nameOrAlias, bool* found = nullptr) const;

    ParsedCommand parse(const QString& text) const;
    QVector<ParsedCommand> parseScript(const QString& scriptText) const;
    QStringList autocomplete(const QString& prefix, int maxResults = 20) const;
    QString helpText(const QString& nameOrAlias = QString()) const;
    QStringList commandsInCategory(CommandCategory category) const;

    void addHistory(const ParsedCommand& parsed, bool accepted);
    QVector<CommandHistoryEntry> history(int maxEntries = 100) const;
    QString lastRepeatableCommand() const;
    void clearHistory();

    void setSystemVariable(const QString& name, const QVariant& value);
    QVariant systemVariable(const QString& name, const QVariant& fallback = QVariant()) const;
    QStringList systemVariableNames() const;

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& obj, QString* error = nullptr);

    static QString categoryName(CommandCategory category);
    static CommandCategory categoryFromName(const QString& name);
    static QStringList tokenize(const QString& input);
    static QStringList splitScriptLines(const QString& scriptText);

private:
    QString canonicalKey(const QString& nameOrAlias) const;
    void indexCommand(const CommandDefinition& command);
    void removeIndexForCommand(const QString& canonicalName);

    QMap<QString, CommandDefinition> m_commands;
    QMap<QString, QString> m_aliasIndex;
    QVector<CommandHistoryEntry> m_history;
    QMap<QString, QVariant> m_systemVariables;
    int m_maxHistory = 500;
};

} // namespace CadCommands
