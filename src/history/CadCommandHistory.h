#pragma once

#include "cad/CadDocument.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace CadHistory {

struct DocumentSnapshot {
    QString label;
    QDateTime createdAt;
    QJsonObject documentJson;
};

class CadCommandHistory
{
public:
    explicit CadCommandHistory(int maximumDepth = 100);

    void clear();
    void setMaximumDepth(int depth);
    int maximumDepth() const { return m_maximumDepth; }

    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;

    void beginTransaction(const CadDocument& document, const QString& label);
    void commitTransaction(const CadDocument& document);
    void cancelTransaction();
    bool transactionActive() const { return m_transactionActive; }

    void pushSnapshot(const CadDocument& document, const QString& label);
    bool undo(CadDocument& document, QString* error = nullptr);
    bool redo(CadDocument& document, QString* error = nullptr);

    QVector<QString> undoLabels() const;
    QVector<QString> redoLabels() const;

private:
    void trimToMaximumDepth();

    QVector<DocumentSnapshot> m_undoStack;
    QVector<DocumentSnapshot> m_redoStack;
    DocumentSnapshot m_transactionBefore;
    QString m_transactionLabel;
    bool m_transactionActive = false;
    int m_maximumDepth = 100;
};

} // namespace CadHistory
