#include "history/CadCommandHistory.h"

#include <QtGlobal>

namespace CadHistory {

CadCommandHistory::CadCommandHistory(int maximumDepth)
    : m_maximumDepth(qMax(1, maximumDepth))
{
}

void CadCommandHistory::clear()
{
    m_undoStack.clear();
    m_redoStack.clear();
    m_transactionActive = false;
}

void CadCommandHistory::setMaximumDepth(int depth)
{
    m_maximumDepth = qMax(1, depth);
    trimToMaximumDepth();
}

bool CadCommandHistory::canUndo() const { return !m_undoStack.isEmpty(); }
bool CadCommandHistory::canRedo() const { return !m_redoStack.isEmpty(); }
QString CadCommandHistory::undoText() const { return canUndo() ? m_undoStack.last().label : QString(); }
QString CadCommandHistory::redoText() const { return canRedo() ? m_redoStack.last().label : QString(); }

void CadCommandHistory::beginTransaction(const CadDocument& document, const QString& label)
{
    if (m_transactionActive) {
        commitTransaction(document);
    }
    m_transactionBefore.label = label.trimmed().isEmpty() ? QStringLiteral("Edit") : label.trimmed();
    m_transactionBefore.createdAt = QDateTime::currentDateTimeUtc();
    m_transactionBefore.documentJson = document.toJson();
    m_transactionLabel = m_transactionBefore.label;
    m_transactionActive = true;
}

void CadCommandHistory::commitTransaction(const CadDocument& document)
{
    if (!m_transactionActive) return;
    if (m_transactionBefore.documentJson != document.toJson()) {
        m_undoStack.push_back(m_transactionBefore);
        m_redoStack.clear();
        trimToMaximumDepth();
    }
    m_transactionActive = false;
}

void CadCommandHistory::cancelTransaction()
{
    m_transactionActive = false;
}

void CadCommandHistory::pushSnapshot(const CadDocument& document, const QString& label)
{
    DocumentSnapshot snapshot;
    snapshot.label = label.trimmed().isEmpty() ? QStringLiteral("Edit") : label.trimmed();
    snapshot.createdAt = QDateTime::currentDateTimeUtc();
    snapshot.documentJson = document.toJson();
    m_undoStack.push_back(snapshot);
    m_redoStack.clear();
    trimToMaximumDepth();
}

bool CadCommandHistory::undo(CadDocument& document, QString* error)
{
    if (!canUndo()) {
        if (error) *error = QStringLiteral("Nothing to undo");
        return false;
    }

    DocumentSnapshot current;
    current.label = m_undoStack.last().label;
    current.createdAt = QDateTime::currentDateTimeUtc();
    current.documentJson = document.toJson();
    m_redoStack.push_back(current);

    const DocumentSnapshot previous = m_undoStack.takeLast();
    if (!document.fromJson(previous.documentJson, error)) {
        return false;
    }
    document.setModified(true);
    return true;
}

bool CadCommandHistory::redo(CadDocument& document, QString* error)
{
    if (!canRedo()) {
        if (error) *error = QStringLiteral("Nothing to redo");
        return false;
    }

    DocumentSnapshot current;
    current.label = m_redoStack.last().label;
    current.createdAt = QDateTime::currentDateTimeUtc();
    current.documentJson = document.toJson();
    m_undoStack.push_back(current);

    const DocumentSnapshot next = m_redoStack.takeLast();
    if (!document.fromJson(next.documentJson, error)) {
        return false;
    }
    document.setModified(true);
    trimToMaximumDepth();
    return true;
}

QVector<QString> CadCommandHistory::undoLabels() const
{
    QVector<QString> labels;
    labels.reserve(m_undoStack.size());
    for (const DocumentSnapshot& snapshot : m_undoStack) labels.push_back(snapshot.label);
    return labels;
}

QVector<QString> CadCommandHistory::redoLabels() const
{
    QVector<QString> labels;
    labels.reserve(m_redoStack.size());
    for (const DocumentSnapshot& snapshot : m_redoStack) labels.push_back(snapshot.label);
    return labels;
}

void CadCommandHistory::trimToMaximumDepth()
{
    while (m_undoStack.size() > m_maximumDepth) {
        m_undoStack.removeFirst();
    }
    while (m_redoStack.size() > m_maximumDepth) {
        m_redoStack.removeFirst();
    }
}

} // namespace CadHistory
