// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QQUICKSTACKLAYOUT_H
#define QQUICKSTACKLAYOUT_H

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

#include <QtQuickLayouts/private/qquicklayoutglobal_p.h>
#include <QtQuickLayouts/private/qquicklayout_p.h>

QT_BEGIN_NAMESPACE

class QQuickStackLayoutPrivate;
class QQuickStackLayoutAttached;

class Q_QUICKLAYOUTS_PRIVATE_EXPORT QQuickStackLayout : public QQuickLayout
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    QML_NAMED_ELEMENT(StackLayout)
    QML_ADDED_IN_VERSION(1, 3)
    QML_ATTACHED(QQuickStackLayoutAttached)

public:
    explicit QQuickStackLayout(QQuickItem *parent = nullptr);
    int count() const;
    int currentIndex() const;
    void setCurrentIndex(int index);

    void componentComplete() override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    QSizeF sizeHint(Qt::SizeHint whichSizeHint) const override;
    void setAlignment(QQuickItem *item, Qt::Alignment align)  override;
    void invalidate(QQuickItem *childItem = nullptr)  override;
    void updateLayoutItems() override { }
    void rearrange(const QSizeF &) override;

    // iterator
    Q_INVOKABLE QQuickItem *itemAt(int index) const override;
    int itemCount() const override;
    int indexOf(QQuickItem *item) const;

    static QQuickStackLayoutAttached *qmlAttachedProperties(QObject *object);

signals:
    void currentIndexChanged();
    void countChanged();

private:
    static void collectItemSizeHints(QQuickItem *item, QSizeF *sizeHints);
    bool shouldIgnoreItem(QQuickItem *item) const;
    void childItemsChanged();
    Q_DECLARE_PRIVATE(QQuickStackLayout)

    friend class QQuickStackLayoutAttached;

    QList<QQuickItem*> m_items;

    struct SizeHints {
        inline QSizeF &min() { return array[Qt::MinimumSize]; }
        inline QSizeF &pref() { return array[Qt::PreferredSize]; }
        inline QSizeF &max() { return array[Qt::MaximumSize]; }
        QSizeF array[Qt::NSizeHints];
    };

    mutable QVector<SizeHints> m_cachedItemSizeHints;
    mutable QSizeF m_cachedSizeHints[Qt::NSizeHints];
};

class QQuickStackLayoutPrivate : public QQuickLayoutPrivate
{
    Q_DECLARE_PUBLIC(QQuickStackLayout)
public:
    QQuickStackLayoutPrivate() : count(0), currentIndex(-1), explicitCurrentIndex(false) {}
private:
    int count;
    int currentIndex;
    bool explicitCurrentIndex;
};

class QQuickStackLayoutAttached : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int index READ index NOTIFY indexChanged)
    Q_PROPERTY(bool isCurrentItem READ isCurrentItem NOTIFY isCurrentItemChanged)
    Q_PROPERTY(QQuickStackLayout *layout READ layout NOTIFY layoutChanged)

public:
    QQuickStackLayoutAttached(QObject *object);

    int index() const;
    void setIndex(int index);

    bool isCurrentItem() const;
    void setIsCurrentItem(bool isCurrentItem);

    QQuickStackLayout *layout() const;
    void setLayout(QQuickStackLayout *layout);

signals:
    void indexChanged();
    void isCurrentItemChanged();
    void layoutChanged();

private:
    int m_index = -1;
    bool m_isCurrentItem = false;
    QQuickStackLayout *m_layout = nullptr;
};

QT_END_NAMESPACE

#endif // QQUICKSTACKLAYOUT_H
