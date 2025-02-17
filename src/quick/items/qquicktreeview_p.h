// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QQUICKTREEVIEW_P_H
#define QQUICKTREEVIEW_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <QtCore/qabstractitemmodel.h>
#include "qquicktableview_p.h"

QT_BEGIN_NAMESPACE

class QQuickTreeViewPrivate;

class Q_QUICK_PRIVATE_EXPORT QQuickTreeView : public QQuickTableView
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TreeView)
    QML_ADDED_IN_VERSION(6, 3)

public:
    QQuickTreeView(QQuickItem *parent = nullptr);
    ~QQuickTreeView() override;

    Q_INVOKABLE int depth(int row) const;

    Q_INVOKABLE bool isExpanded(int row) const;
    Q_INVOKABLE void expand(int row);
    Q_INVOKABLE void collapse(int row);
    Q_INVOKABLE void toggleExpanded(int row);

    Q_REVISION(6, 4) Q_INVOKABLE void expandRecursively(int row = -1, int depth = -1);
    Q_REVISION(6, 4) Q_INVOKABLE void collapseRecursively(int row = -1);
    Q_REVISION(6, 4) Q_INVOKABLE void expandToIndex(const QModelIndex &index);

    Q_INVOKABLE QModelIndex modelIndex(const QPoint &cell) const override;
    Q_INVOKABLE QModelIndex modelIndex(int column, int row) const override;
    Q_INVOKABLE QPoint cellAtIndex(const QModelIndex &index) const override;

signals:
    void expanded(int row, int depth);
    void collapsed(int row, bool recursively);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    Q_DISABLE_COPY(QQuickTreeView)
    Q_DECLARE_PRIVATE(QQuickTreeView)
};

QT_END_NAMESPACE

QML_DECLARE_TYPE(QQuickTreeView)

#endif // QQUICKTREEVIEW_P_H
