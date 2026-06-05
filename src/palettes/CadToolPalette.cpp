#include "palettes/CadToolPalette.h"

#include <QJsonArray>
#include <algorithm>

namespace CadPalettes {
namespace {

static QString normalized(const QString& value)
{
    return value.trimmed().toUpper();
}

static ToolDescriptor makeTool(const QString& id, const QString& title, const QString& command,
                               const QString& category, const QStringList& aliases = {},
                               const QString& shortcut = QString())
{
    ToolDescriptor t;
    t.id = id;
    t.title = title;
    t.command = command;
    t.category = category;
    t.aliases = aliases;
    t.keySequence = shortcut;
    t.iconName = id;
    t.tooltip = title + QStringLiteral(" command");
    return t;
}

static QList<ToolDescriptor> sortedTools(QList<ToolDescriptor> tools)
{
    std::sort(tools.begin(), tools.end(), [](const ToolDescriptor& a, const ToolDescriptor& b) {
        if (a.category != b.category) return a.category < b.category;
        return a.title < b.title;
    });
    return tools;
}

} // namespace

QJsonObject ToolDescriptor::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = id;
    obj[QStringLiteral("title")] = title;
    obj[QStringLiteral("command")] = command;
    obj[QStringLiteral("category")] = category;
    obj[QStringLiteral("iconName")] = iconName;
    obj[QStringLiteral("tooltip")] = tooltip;
    obj[QStringLiteral("keySequence")] = keySequence;
    obj[QStringLiteral("visible")] = visible;
    obj[QStringLiteral("favorite")] = favorite;
    QJsonArray aliasArray;
    for (const QString& alias : aliases) aliasArray.append(alias);
    obj[QStringLiteral("aliases")] = aliasArray;
    return obj;
}

ToolDescriptor ToolDescriptor::fromJson(const QJsonObject& obj)
{
    ToolDescriptor t;
    t.id = obj.value(QStringLiteral("id")).toString();
    t.title = obj.value(QStringLiteral("title")).toString();
    t.command = obj.value(QStringLiteral("command")).toString();
    t.category = obj.value(QStringLiteral("category")).toString();
    t.iconName = obj.value(QStringLiteral("iconName")).toString();
    t.tooltip = obj.value(QStringLiteral("tooltip")).toString();
    t.keySequence = obj.value(QStringLiteral("keySequence")).toString();
    t.visible = obj.value(QStringLiteral("visible")).toBool(true);
    t.favorite = obj.value(QStringLiteral("favorite")).toBool(false);
    for (const QJsonValue& value : obj.value(QStringLiteral("aliases")).toArray()) t.aliases << value.toString();
    return t;
}

CadToolPalette::CadToolPalette()
{
    resetToDefaultTools();
}

void CadToolPalette::resetToDefaultTools()
{
    m_tools.clear();
    const QList<ToolDescriptor> defaults = {
        makeTool(QStringLiteral("line"), QStringLiteral("Line"), QStringLiteral("LINE"), QStringLiteral("Draw"), {QStringLiteral("L")}),
        makeTool(QStringLiteral("polyline"), QStringLiteral("Polyline"), QStringLiteral("PLINE"), QStringLiteral("Draw"), {QStringLiteral("PL")}),
        makeTool(QStringLiteral("circle"), QStringLiteral("Circle"), QStringLiteral("CIRCLE"), QStringLiteral("Draw"), {QStringLiteral("C")}),
        makeTool(QStringLiteral("arc"), QStringLiteral("Arc"), QStringLiteral("ARC"), QStringLiteral("Draw"), {QStringLiteral("A")}),
        makeTool(QStringLiteral("rectangle"), QStringLiteral("Rectangle"), QStringLiteral("RECTANGLE"), QStringLiteral("Draw"), {QStringLiteral("REC")}),
        makeTool(QStringLiteral("hatch"), QStringLiteral("Hatch"), QStringLiteral("HATCH"), QStringLiteral("Annotate"), {QStringLiteral("H")}),
        makeTool(QStringLiteral("move"), QStringLiteral("Move"), QStringLiteral("MOVE"), QStringLiteral("Modify"), {QStringLiteral("M")}),
        makeTool(QStringLiteral("copy"), QStringLiteral("Copy"), QStringLiteral("COPY"), QStringLiteral("Modify"), {QStringLiteral("CO"), QStringLiteral("CP")}),
        makeTool(QStringLiteral("rotate"), QStringLiteral("Rotate"), QStringLiteral("ROTATE"), QStringLiteral("Modify"), {QStringLiteral("RO")}),
        makeTool(QStringLiteral("scale"), QStringLiteral("Scale"), QStringLiteral("SCALE"), QStringLiteral("Modify"), {QStringLiteral("SC")}),
        makeTool(QStringLiteral("mirror"), QStringLiteral("Mirror"), QStringLiteral("MIRROR"), QStringLiteral("Modify"), {QStringLiteral("MI")}),
        makeTool(QStringLiteral("offset"), QStringLiteral("Offset"), QStringLiteral("OFFSET"), QStringLiteral("Modify"), {QStringLiteral("O")}),
        makeTool(QStringLiteral("trim"), QStringLiteral("Trim"), QStringLiteral("TRIM"), QStringLiteral("Modify"), {QStringLiteral("TR")}),
        makeTool(QStringLiteral("extend"), QStringLiteral("Extend"), QStringLiteral("EXTEND"), QStringLiteral("Modify"), {QStringLiteral("EX")}),
        makeTool(QStringLiteral("fillet"), QStringLiteral("Fillet"), QStringLiteral("FILLET"), QStringLiteral("Modify"), {QStringLiteral("F")}),
        makeTool(QStringLiteral("chamfer"), QStringLiteral("Chamfer"), QStringLiteral("CHAMFER"), QStringLiteral("Modify"), {QStringLiteral("CHA")}),
        makeTool(QStringLiteral("stretch"), QStringLiteral("Stretch"), QStringLiteral("STRETCH"), QStringLiteral("Modify"), {QStringLiteral("S")}),
        makeTool(QStringLiteral("lengthen"), QStringLiteral("Lengthen"), QStringLiteral("LENGTHEN"), QStringLiteral("Modify"), {QStringLiteral("LEN")}),
        makeTool(QStringLiteral("break"), QStringLiteral("Break"), QStringLiteral("BREAK"), QStringLiteral("Modify"), {QStringLiteral("BR")}),
        makeTool(QStringLiteral("join"), QStringLiteral("Join"), QStringLiteral("JOIN"), QStringLiteral("Modify"), {QStringLiteral("J")}),
        makeTool(QStringLiteral("divide"), QStringLiteral("Divide"), QStringLiteral("DIVIDE"), QStringLiteral("Modify")),
        makeTool(QStringLiteral("measure"), QStringLiteral("Measure"), QStringLiteral("MEASURE"), QStringLiteral("Inquiry"), {QStringLiteral("MEA")}),
        makeTool(QStringLiteral("dist"), QStringLiteral("Distance"), QStringLiteral("DIST"), QStringLiteral("Inquiry"), {QStringLiteral("DI")}),
        makeTool(QStringLiteral("area"), QStringLiteral("Area"), QStringLiteral("AREA"), QStringLiteral("Inquiry"), {QStringLiteral("AA")}),
        makeTool(QStringLiteral("dimlinear"), QStringLiteral("Linear Dimension"), QStringLiteral("DIMLINEAR"), QStringLiteral("Annotate"), {QStringLiteral("DLI")}),
        makeTool(QStringLiteral("dimaligned"), QStringLiteral("Aligned Dimension"), QStringLiteral("DIMALIGNED"), QStringLiteral("Annotate"), {QStringLiteral("DAL")}),
        makeTool(QStringLiteral("mleader"), QStringLiteral("Multileader"), QStringLiteral("MLEADER"), QStringLiteral("Annotate"), {QStringLiteral("MLD")}),
        makeTool(QStringLiteral("layer"), QStringLiteral("Layer Properties"), QStringLiteral("LAYER"), QStringLiteral("Manage"), {QStringLiteral("LA")}),
        makeTool(QStringLiteral("audit"), QStringLiteral("Audit Drawing"), QStringLiteral("AUDIT"), QStringLiteral("Manage")),
        makeTool(QStringLiteral("purge"), QStringLiteral("Purge"), QStringLiteral("PURGE"), QStringLiteral("Manage"), {QStringLiteral("PU")})
    };
    for (const ToolDescriptor& tool : defaults) addTool(tool);
}

bool CadToolPalette::addTool(const ToolDescriptor& tool, QString* error)
{
    if (tool.id.trimmed().isEmpty() || tool.command.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("Tool id and command are required.");
        return false;
    }
    m_tools[tool.id] = tool;
    return true;
}

bool CadToolPalette::removeTool(const QString& id)
{
    return m_tools.remove(id) > 0;
}

bool CadToolPalette::setFavorite(const QString& id, bool favorite)
{
    if (!m_tools.contains(id)) return false;
    m_tools[id].favorite = favorite;
    return true;
}

bool CadToolPalette::setVisible(const QString& id, bool visible)
{
    if (!m_tools.contains(id)) return false;
    m_tools[id].visible = visible;
    return true;
}

ToolDescriptor CadToolPalette::tool(const QString& id) const
{
    return m_tools.value(id);
}

ToolDescriptor CadToolPalette::toolForCommandOrAlias(const QString& text) const
{
    const QString key = normalized(text);
    for (const ToolDescriptor& tool : m_tools) {
        if (normalized(tool.command) == key || normalized(tool.id) == key) return tool;
        for (const QString& alias : tool.aliases) {
            if (normalized(alias) == key) return tool;
        }
    }
    return ToolDescriptor{};
}

QStringList CadToolPalette::categories() const
{
    QStringList out;
    for (const ToolDescriptor& tool : m_tools) {
        if (!out.contains(tool.category)) out << tool.category;
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}

QList<ToolDescriptor> CadToolPalette::toolsInCategory(const QString& category, bool includeHidden) const
{
    QList<ToolDescriptor> out;
    for (const ToolDescriptor& tool : m_tools) {
        if (tool.category.compare(category, Qt::CaseInsensitive) == 0 && (includeHidden || tool.visible)) out << tool;
    }
    return sortedTools(out);
}

QList<ToolDescriptor> CadToolPalette::favoriteTools() const
{
    QList<ToolDescriptor> out;
    for (const ToolDescriptor& tool : m_tools) {
        if (tool.favorite && tool.visible) out << tool;
    }
    return sortedTools(out);
}

QList<ToolDescriptor> CadToolPalette::search(const QString& text, bool includeHidden) const
{
    QList<ToolDescriptor> out;
    const QString q = text.trimmed();
    for (const ToolDescriptor& tool : m_tools) {
        if (!includeHidden && !tool.visible) continue;
        bool match = tool.title.contains(q, Qt::CaseInsensitive) || tool.command.contains(q, Qt::CaseInsensitive) ||
                     tool.category.contains(q, Qt::CaseInsensitive) || tool.tooltip.contains(q, Qt::CaseInsensitive);
        for (const QString& alias : tool.aliases) match = match || alias.contains(q, Qt::CaseInsensitive);
        if (match) out << tool;
    }
    return sortedTools(out);
}

QJsonObject CadToolPalette::toJson() const
{
    QJsonObject obj;
    QJsonArray tools;
    for (const ToolDescriptor& tool : m_tools) tools.append(tool.toJson());
    obj[QStringLiteral("tools")] = tools;
    return obj;
}

bool CadToolPalette::fromJson(const QJsonObject& obj, QString* error)
{
    Q_UNUSED(error)
    m_tools.clear();
    for (const QJsonValue& value : obj.value(QStringLiteral("tools")).toArray()) addTool(ToolDescriptor::fromJson(value.toObject()));
    if (m_tools.isEmpty()) resetToDefaultTools();
    return true;
}

} // namespace CadPalettes
