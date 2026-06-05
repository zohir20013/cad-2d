#pragma once

#include <QJsonObject>
#include <QKeySequence>
#include <QMap>
#include <QString>
#include <QStringList>

namespace CadPalettes {

struct ToolDescriptor {
    QString id;
    QString title;
    QString command;
    QString category;
    QString iconName;
    QString tooltip;
    QStringList aliases;
    QString keySequence;
    bool visible = true;
    bool favorite = false;

    QJsonObject toJson() const;
    static ToolDescriptor fromJson(const QJsonObject& obj);
};

class CadToolPalette
{
public:
    CadToolPalette();

    void resetToDefaultTools();
    bool addTool(const ToolDescriptor& tool, QString* error = nullptr);
    bool removeTool(const QString& id);
    bool setFavorite(const QString& id, bool favorite);
    bool setVisible(const QString& id, bool visible);

    ToolDescriptor tool(const QString& id) const;
    ToolDescriptor toolForCommandOrAlias(const QString& text) const;
    QStringList categories() const;
    QList<ToolDescriptor> toolsInCategory(const QString& category, bool includeHidden = false) const;
    QList<ToolDescriptor> favoriteTools() const;
    QList<ToolDescriptor> search(const QString& text, bool includeHidden = false) const;

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& obj, QString* error = nullptr);

private:
    QMap<QString, ToolDescriptor> m_tools;
};

} // namespace CadPalettes
