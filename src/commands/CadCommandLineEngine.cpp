#include "CadCommandLineEngine.h"

#include <QJsonDocument>
#include <QMetaType>
#include <QRegularExpression>
#include <algorithm>

namespace CadCommands {

namespace {
QString norm(QString s)
{
    return s.trimmed().toUpper();
}

QString variantToString(const QVariant& value)
{
    if (!value.isValid()) {
        return QString();
    }
    if (value.typeId() == QMetaType::Bool) {
        return value.toBool() ? QStringLiteral("1") : QStringLiteral("0");
    }
    return value.toString();
}

QVariant jsonValueToVariant(const QJsonValue& value)
{
    if (value.isBool()) return value.toBool();
    if (value.isDouble()) return value.toDouble();
    if (value.isString()) return value.toString();
    if (value.isArray()) return value.toArray();
    if (value.isObject()) return value.toObject();
    return QVariant();
}
}

QJsonObject CommandDefinition::toJson() const
{
    QJsonObject obj;
    obj["name"] = name;
    obj["aliases"] = QJsonArray::fromStringList(aliases);
    obj["description"] = description;
    obj["syntax"] = syntax;
    obj["category"] = CadCommandLineEngine::categoryName(category);
    obj["transparent"] = transparent;
    obj["repeatable"] = repeatable;
    return obj;
}

CommandDefinition CommandDefinition::fromJson(const QJsonObject& obj)
{
    CommandDefinition def;
    def.name = obj.value("name").toString();
    const QJsonArray aliases = obj.value("aliases").toArray();
    for (const QJsonValue& v : aliases) {
        def.aliases << v.toString();
    }
    def.description = obj.value("description").toString();
    def.syntax = obj.value("syntax").toString();
    def.category = CadCommandLineEngine::categoryFromName(obj.value("category").toString());
    def.transparent = obj.value("transparent").toBool(false);
    def.repeatable = obj.value("repeatable").toBool(true);
    return def;
}

QJsonObject CommandHistoryEntry::toJson() const
{
    QJsonObject obj;
    obj["rawText"] = rawText;
    obj["resolvedName"] = resolvedName;
    obj["timestamp"] = timestamp.toString(Qt::ISODateWithMs);
    obj["accepted"] = accepted;
    return obj;
}

CommandHistoryEntry CommandHistoryEntry::fromJson(const QJsonObject& obj)
{
    CommandHistoryEntry entry;
    entry.rawText = obj.value("rawText").toString();
    entry.resolvedName = obj.value("resolvedName").toString();
    entry.timestamp = QDateTime::fromString(obj.value("timestamp").toString(), Qt::ISODateWithMs);
    entry.accepted = obj.value("accepted").toBool(false);
    return entry;
}

CadCommandLineEngine::CadCommandLineEngine()
{
    registerDefaultCommands();
    setSystemVariable("ORTHOMODE", false);
    setSystemVariable("OSMODE", 0);
    setSystemVariable("PICKBOX", 6);
    setSystemVariable("SNAPMODE", false);
    setSystemVariable("GRIDMODE", true);
    setSystemVariable("LUNITS", 2);
    setSystemVariable("LUPREC", 3);
    setSystemVariable("AUPREC", 2);
}

void CadCommandLineEngine::registerDefaultCommands()
{
    m_commands.clear();
    m_aliasIndex.clear();

    const QVector<CommandDefinition> defaults = {
        {"LINE", {"L"}, "Create straight line segments.", "LINE point point", CommandCategory::Draw},
        {"XLINE", {"XL"}, "Create infinite construction lines.", "XLINE point point", CommandCategory::Draw},
        {"RAY", {}, "Create semi-infinite construction rays.", "RAY point point", CommandCategory::Draw},
        {"CIRCLE", {"C"}, "Create a circle from center/radius, 2P or 3P.", "CIRCLE center radius | 2P | 3P", CommandCategory::Draw},
        {"ARC", {"A"}, "Create an arc.", "ARC 3P | center start end", CommandCategory::Draw},
        {"RECTANGLE", {"REC"}, "Create a rectangle.", "RECTANGLE corner corner", CommandCategory::Draw},
        {"POLYLINE", {"PL"}, "Create a polyline.", "POLYLINE points...", CommandCategory::Draw},
        {"SPLINE", {"SPL"}, "Create a spline from fit/control points.", "SPLINE points...", CommandCategory::Draw},
        {"HATCH", {"H"}, "Create hatch pattern geometry.", "HATCH boundary pattern", CommandCategory::Draw},
        {"TEXT", {"T"}, "Create single-line text.", "TEXT point height rotation text", CommandCategory::Annotate},
        {"MTEXT", {"MT"}, "Create multi-line annotation text.", "MTEXT box text", CommandCategory::Annotate},
        {"DIMLINEAR", {"DLI"}, "Create a linear dimension.", "DIMLINEAR p1 p2 offset", CommandCategory::Annotate},
        {"DIMALIGNED", {"DAL"}, "Create an aligned dimension.", "DIMALIGNED p1 p2 offset", CommandCategory::Annotate},
        {"DIMANGULAR", {"DAN"}, "Create an angular dimension.", "DIMANGULAR line1 line2", CommandCategory::Annotate},
        {"DIMRADIUS", {"DRA"}, "Create a radius dimension.", "DIMRADIUS circle", CommandCategory::Annotate},
        {"DIMDIAMETER", {"DDI"}, "Create a diameter dimension.", "DIMDIAMETER circle", CommandCategory::Annotate},
        {"MOVE", {"M"}, "Move selected entities.", "MOVE selection base target", CommandCategory::Modify},
        {"COPY", {"CO", "CP"}, "Copy selected entities.", "COPY selection base target", CommandCategory::Modify},
        {"ROTATE", {"RO"}, "Rotate selected entities.", "ROTATE selection base angle", CommandCategory::Modify},
        {"SCALE", {"SC"}, "Scale selected entities.", "SCALE selection base factor", CommandCategory::Modify},
        {"MIRROR", {"MI"}, "Mirror selected entities.", "MIRROR selection axis", CommandCategory::Modify},
        {"OFFSET", {"O"}, "Offset curves.", "OFFSET distance entity side", CommandCategory::Modify},
        {"TRIM", {"TR"}, "Trim curves to cutting edges.", "TRIM cutters objects", CommandCategory::Modify},
        {"EXTEND", {"EX"}, "Extend curves to boundaries.", "EXTEND boundaries objects", CommandCategory::Modify},
        {"FILLET", {"F"}, "Create rounded corner between curves.", "FILLET radius line line", CommandCategory::Modify},
        {"CHAMFER", {"CHA"}, "Create bevel between curves.", "CHAMFER d1 d2 line line", CommandCategory::Modify},
        {"BREAK", {"BR"}, "Break an entity between points.", "BREAK entity p1 p2", CommandCategory::Modify},
        {"JOIN", {"J"}, "Join compatible curves.", "JOIN selection", CommandCategory::Modify},
        {"ARRAYRECT", {"AR"}, "Create a rectangular array.", "ARRAYRECT rows cols spacing", CommandCategory::Modify},
        {"ARRAYPOLAR", {"AP"}, "Create a polar array.", "ARRAYPOLAR center count angle", CommandCategory::Modify},
        {"LAYER", {"LA"}, "Open layer tools.", "LAYER", CommandCategory::Layer},
        {"LAYISO", {}, "Isolate selected entity layers.", "LAYISO selection", CommandCategory::Layer},
        {"LAYUNISO", {}, "Restore isolated layers.", "LAYUNISO", CommandCategory::Layer},
        {"SELECT", {"SEL"}, "Create a selection set.", "SELECT filter", CommandCategory::Selection},
        {"QSELECT", {"QS"}, "Filter entities by properties.", "QSELECT property operator value", CommandCategory::Selection},
        {"ZOOM", {"Z"}, "Change drawing view.", "ZOOM Extents|Window|Scale", CommandCategory::View, true},
        {"PAN", {"P"}, "Pan view.", "PAN delta", CommandCategory::View, true},
        {"VIEW", {"V"}, "Save or restore named views.", "VIEW name", CommandCategory::View, true},
        {"DIST", {"DI"}, "Measure distance.", "DIST p1 p2", CommandCategory::Utility, true, false},
        {"AREA", {"AA"}, "Measure area.", "AREA boundary", CommandCategory::Utility, true, false},
        {"ID", {}, "Report point coordinates.", "ID point", CommandCategory::Utility, true, false},
        {"LIST", {"LI"}, "List entity properties.", "LIST selection", CommandCategory::Utility, true, false},
        {"AUDIT", {}, "Validate and repair drawing data.", "AUDIT", CommandCategory::Utility, false, false},
        {"PURGE", {"PU"}, "Remove unused data.", "PURGE", CommandCategory::Utility, false, false},
        {"CONSTRAINT", {"CON"}, "Apply geometric constraints.", "CONSTRAINT type objects", CommandCategory::Constraint},
        {"GEOMCONSTRAINT", {"GC"}, "Apply a geometric constraint.", "GEOMCONSTRAINT type objects", CommandCategory::Constraint},
        {"PARAMETERS", {"PARAM"}, "Show constraint parameters.", "PARAMETERS", CommandCategory::Constraint},
        {"OPEN", {}, "Open drawing file.", "OPEN path", CommandCategory::File, false, false},
        {"SAVE", {"QSAVE"}, "Save drawing.", "SAVE", CommandCategory::File, false, false},
        {"SAVEAS", {}, "Save drawing as.", "SAVEAS path", CommandCategory::File, false, false},
        {"EXPORT", {}, "Export drawing.", "EXPORT path format", CommandCategory::File, false, false}
    };

    for (const CommandDefinition& def : defaults) {
        registerCommand(def);
    }
}

bool CadCommandLineEngine::registerCommand(const CommandDefinition& command, QString* error)
{
    const QString key = norm(command.name);
    if (key.isEmpty()) {
        if (error) *error = QStringLiteral("Command name is empty.");
        return false;
    }
    if (m_commands.contains(key)) {
        if (error) *error = QStringLiteral("Command already registered: %1").arg(command.name);
        return false;
    }
    for (const QString& alias : command.aliases) {
        const QString aliasKey = norm(alias);
        if (aliasKey.isEmpty()) continue;
        if (m_commands.contains(aliasKey) || m_aliasIndex.contains(aliasKey)) {
            if (error) *error = QStringLiteral("Command alias already registered: %1").arg(alias);
            return false;
        }
    }
    CommandDefinition normalized = command;
    normalized.name = key;
    m_commands.insert(key, normalized);
    indexCommand(normalized);
    return true;
}

bool CadCommandLineEngine::unregisterCommand(const QString& nameOrAlias)
{
    const QString key = canonicalKey(nameOrAlias);
    if (!m_commands.contains(key)) return false;
    removeIndexForCommand(key);
    m_commands.remove(key);
    return true;
}

bool CadCommandLineEngine::hasCommand(const QString& nameOrAlias) const
{
    return m_commands.contains(canonicalKey(nameOrAlias));
}

CommandDefinition CadCommandLineEngine::command(const QString& nameOrAlias, bool* found) const
{
    const QString key = canonicalKey(nameOrAlias);
    const bool ok = m_commands.contains(key);
    if (found) *found = ok;
    return ok ? m_commands.value(key) : CommandDefinition{};
}

ParsedCommand CadCommandLineEngine::parse(const QString& text) const
{
    ParsedCommand parsed;
    parsed.rawText = text;
    const QStringList tokens = tokenize(text);
    if (tokens.isEmpty()) {
        parsed.error = QStringLiteral("Empty command.");
        return parsed;
    }

    parsed.commandName = tokens.first();
    parsed.arguments = tokens.mid(1);
    const QString key = canonicalKey(parsed.commandName);
    if (!m_commands.contains(key)) {
        parsed.error = QStringLiteral("Unknown command: %1").arg(parsed.commandName);
        return parsed;
    }
    parsed.resolvedName = key;
    parsed.valid = true;
    return parsed;
}

QVector<ParsedCommand> CadCommandLineEngine::parseScript(const QString& scriptText) const
{
    QVector<ParsedCommand> result;
    const QStringList lines = splitScriptLines(scriptText);
    for (const QString& line : lines) {
        const ParsedCommand parsed = parse(line);
        if (!parsed.rawText.trimmed().isEmpty()) {
            result.push_back(parsed);
        }
    }
    return result;
}

QStringList CadCommandLineEngine::autocomplete(const QString& prefix, int maxResults) const
{
    const QString p = norm(prefix);
    QStringList candidates;
    for (auto it = m_commands.constBegin(); it != m_commands.constEnd(); ++it) {
        if (it.key().startsWith(p)) candidates << it.key();
        for (const QString& alias : it.value().aliases) {
            const QString a = norm(alias);
            if (a.startsWith(p)) candidates << a;
        }
    }
    candidates.removeDuplicates();
    candidates.sort(Qt::CaseInsensitive);
    if (maxResults > 0 && candidates.size() > maxResults) {
        candidates = candidates.mid(0, maxResults);
    }
    return candidates;
}

QString CadCommandLineEngine::helpText(const QString& nameOrAlias) const
{
    if (!nameOrAlias.trimmed().isEmpty()) {
        bool found = false;
        const CommandDefinition def = command(nameOrAlias, &found);
        if (!found) return QStringLiteral("No help for unknown command: %1").arg(nameOrAlias);
        QString text = def.name;
        if (!def.aliases.isEmpty()) text += QStringLiteral(" (%1)").arg(def.aliases.join(", "));
        text += QStringLiteral("\nCategory: %1\nSyntax: %2\n%3")
                    .arg(categoryName(def.category), def.syntax, def.description);
        return text;
    }

    QStringList lines;
    for (auto it = m_commands.constBegin(); it != m_commands.constEnd(); ++it) {
        lines << QStringLiteral("%1 - %2").arg(it.key(), it.value().description);
    }
    return lines.join('\n');
}

QStringList CadCommandLineEngine::commandsInCategory(CommandCategory category) const
{
    QStringList names;
    for (auto it = m_commands.constBegin(); it != m_commands.constEnd(); ++it) {
        if (it.value().category == category) names << it.key();
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}

void CadCommandLineEngine::addHistory(const ParsedCommand& parsed, bool accepted)
{
    if (parsed.rawText.trimmed().isEmpty()) return;
    CommandHistoryEntry entry;
    entry.rawText = parsed.rawText;
    entry.resolvedName = parsed.resolvedName;
    entry.accepted = accepted;
    entry.timestamp = QDateTime::currentDateTimeUtc();
    m_history.push_back(entry);
    while (m_history.size() > m_maxHistory) {
        m_history.removeFirst();
    }
}

QVector<CommandHistoryEntry> CadCommandLineEngine::history(int maxEntries) const
{
    if (maxEntries <= 0 || maxEntries >= m_history.size()) return m_history;
    return m_history.mid(m_history.size() - maxEntries);
}

QString CadCommandLineEngine::lastRepeatableCommand() const
{
    for (int i = m_history.size() - 1; i >= 0; --i) {
        const CommandHistoryEntry& entry = m_history[i];
        if (!entry.accepted) continue;
        bool found = false;
        const CommandDefinition def = command(entry.resolvedName, &found);
        if (found && def.repeatable) return entry.rawText;
    }
    return QString();
}

void CadCommandLineEngine::clearHistory()
{
    m_history.clear();
}

void CadCommandLineEngine::setSystemVariable(const QString& name, const QVariant& value)
{
    const QString key = norm(name);
    if (!key.isEmpty()) m_systemVariables[key] = value;
}

QVariant CadCommandLineEngine::systemVariable(const QString& name, const QVariant& fallback) const
{
    return m_systemVariables.value(norm(name), fallback);
}

QStringList CadCommandLineEngine::systemVariableNames() const
{
    QStringList names = m_systemVariables.keys();
    names.sort(Qt::CaseInsensitive);
    return names;
}

QJsonObject CadCommandLineEngine::toJson() const
{
    QJsonObject obj;
    QJsonArray commands;
    for (const CommandDefinition& def : m_commands) commands.append(def.toJson());
    obj["commands"] = commands;

    QJsonArray historyArray;
    for (const CommandHistoryEntry& entry : m_history) historyArray.append(entry.toJson());
    obj["history"] = historyArray;

    QJsonObject vars;
    for (auto it = m_systemVariables.constBegin(); it != m_systemVariables.constEnd(); ++it) {
        vars[it.key()] = QJsonValue::fromVariant(it.value());
    }
    obj["systemVariables"] = vars;
    obj["maxHistory"] = m_maxHistory;
    return obj;
}

bool CadCommandLineEngine::fromJson(const QJsonObject& obj, QString* error)
{
    QMap<QString, CommandDefinition> newCommands;
    QMap<QString, QString> newAliasIndex;

    const QJsonArray commands = obj.value("commands").toArray();
    for (const QJsonValue& v : commands) {
        const CommandDefinition def = CommandDefinition::fromJson(v.toObject());
        const QString key = norm(def.name);
        if (key.isEmpty()) continue;
        if (newCommands.contains(key)) {
            if (error) *error = QStringLiteral("Duplicate command in command profile: %1").arg(key);
            return false;
        }
        newCommands.insert(key, def);
        for (const QString& alias : def.aliases) {
            const QString aliasKey = norm(alias);
            if (!aliasKey.isEmpty()) newAliasIndex.insert(aliasKey, key);
        }
    }

    if (!newCommands.isEmpty()) {
        m_commands = newCommands;
        m_aliasIndex = newAliasIndex;
    }

    m_history.clear();
    const QJsonArray historyArray = obj.value("history").toArray();
    for (const QJsonValue& v : historyArray) {
        m_history.push_back(CommandHistoryEntry::fromJson(v.toObject()));
    }

    m_systemVariables.clear();
    const QJsonObject vars = obj.value("systemVariables").toObject();
    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it) {
        m_systemVariables.insert(it.key(), jsonValueToVariant(it.value()));
    }
    m_maxHistory = obj.value("maxHistory").toInt(500);
    return true;
}

QString CadCommandLineEngine::categoryName(CommandCategory category)
{
    switch (category) {
    case CommandCategory::Draw: return QStringLiteral("Draw");
    case CommandCategory::Modify: return QStringLiteral("Modify");
    case CommandCategory::Annotate: return QStringLiteral("Annotate");
    case CommandCategory::View: return QStringLiteral("View");
    case CommandCategory::Layer: return QStringLiteral("Layer");
    case CommandCategory::Selection: return QStringLiteral("Selection");
    case CommandCategory::Constraint: return QStringLiteral("Constraint");
    case CommandCategory::File: return QStringLiteral("File");
    case CommandCategory::Utility: return QStringLiteral("Utility");
    default: return QStringLiteral("Unknown");
    }
}

CommandCategory CadCommandLineEngine::categoryFromName(const QString& name)
{
    const QString key = norm(name);
    if (key == "DRAW") return CommandCategory::Draw;
    if (key == "MODIFY") return CommandCategory::Modify;
    if (key == "ANNOTATE") return CommandCategory::Annotate;
    if (key == "VIEW") return CommandCategory::View;
    if (key == "LAYER") return CommandCategory::Layer;
    if (key == "SELECTION") return CommandCategory::Selection;
    if (key == "CONSTRAINT") return CommandCategory::Constraint;
    if (key == "FILE") return CommandCategory::File;
    if (key == "UTILITY") return CommandCategory::Utility;
    return CommandCategory::Unknown;
}

QStringList CadCommandLineEngine::tokenize(const QString& input)
{
    QStringList tokens;
    QString current;
    bool inQuotes = false;
    QChar quoteChar;

    for (int i = 0; i < input.size(); ++i) {
        const QChar ch = input[i];
        if ((ch == '"' || ch == '\'') && (!inQuotes || ch == quoteChar)) {
            if (inQuotes) {
                inQuotes = false;
                quoteChar = QChar();
            } else {
                inQuotes = true;
                quoteChar = ch;
            }
            continue;
        }
        if (ch.isSpace() && !inQuotes) {
            if (!current.isEmpty()) {
                tokens << current;
                current.clear();
            }
        } else {
            current.append(ch);
        }
    }
    if (!current.isEmpty()) tokens << current;
    return tokens;
}

QStringList CadCommandLineEngine::splitScriptLines(const QString& scriptText)
{
    QStringList lines;
    QString current;
    bool inQuotes = false;
    QChar quoteChar;

    for (int i = 0; i < scriptText.size(); ++i) {
        const QChar ch = scriptText[i];
        if ((ch == '"' || ch == '\'') && (!inQuotes || ch == quoteChar)) {
            inQuotes = !inQuotes;
            quoteChar = inQuotes ? ch : QChar();
            current.append(ch);
            continue;
        }
        const bool separator = !inQuotes && (ch == '\n' || ch == '\r' || ch == ';');
        if (separator) {
            QString line = current.trimmed();
            if (!line.isEmpty() && !line.startsWith('#') && !line.startsWith("//")) {
                lines << line;
            }
            current.clear();
        } else {
            current.append(ch);
        }
    }
    QString line = current.trimmed();
    if (!line.isEmpty() && !line.startsWith('#') && !line.startsWith("//")) {
        lines << line;
    }
    return lines;
}

QString CadCommandLineEngine::canonicalKey(const QString& nameOrAlias) const
{
    const QString key = norm(nameOrAlias);
    return m_aliasIndex.value(key, key);
}

void CadCommandLineEngine::indexCommand(const CommandDefinition& command)
{
    const QString key = norm(command.name);
    for (const QString& alias : command.aliases) {
        const QString aliasKey = norm(alias);
        if (!aliasKey.isEmpty()) m_aliasIndex.insert(aliasKey, key);
    }
}

void CadCommandLineEngine::removeIndexForCommand(const QString& canonicalName)
{
    QStringList aliasesToRemove;
    for (auto it = m_aliasIndex.constBegin(); it != m_aliasIndex.constEnd(); ++it) {
        if (it.value() == canonicalName) aliasesToRemove << it.key();
    }
    for (const QString& alias : aliasesToRemove) m_aliasIndex.remove(alias);
}

} // namespace CadCommands
