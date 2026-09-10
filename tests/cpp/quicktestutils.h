#pragma once
#include <QQuickItem>
#include <QQuickWindow>

inline QQuickItem* findQuickItem(QQuickItem* root, const QString& name)
{
    if (!root)
        return nullptr;
    if (root->objectName() == name)
        return root;
    for (auto* child : root->childItems())
        if (auto* found = findQuickItem(child, name))
            return found;
    return nullptr;
}
inline QQuickItem* findQuickItem(QQuickWindow* window, const QString& name)
{
    if (!window)
        return nullptr;
    if (auto* item = window->findChild<QQuickItem*>(name))
        return item;
    return findQuickItem(window->contentItem(), name);
}
