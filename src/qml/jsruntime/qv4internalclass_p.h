/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtQml module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/
#ifndef QV4INTERNALCLASS_H
#define QV4INTERNALCLASS_H

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

#include "qv4global_p.h"

#include <QHash>
#include <private/qv4identifier_p.h>
#include <private/qv4heap_p.h>

QT_BEGIN_NAMESPACE

namespace QV4 {

struct VTable;
struct MarkStack;

struct PropertyHashData;
struct PropertyHash
{
    struct Entry {
        Identifier identifier;
        uint index;
    };

    PropertyHashData *d;

    inline PropertyHash();
    inline PropertyHash(const PropertyHash &other);
    inline ~PropertyHash();
    PropertyHash &operator=(const PropertyHash &other);

    void addEntry(const Entry &entry, int classSize);
    uint lookup(Identifier identifier) const;
    int removeIdentifier(Identifier identifier, int classSize);
    void detach(bool grow, int classSize);
};

struct PropertyHashData
{
    PropertyHashData(int numBits);
    ~PropertyHashData() {
        free(entries);
    }

    int refCount;
    int alloc;
    int size;
    int numBits;
    PropertyHash::Entry *entries;
};

inline PropertyHash::PropertyHash()
{
    d = new PropertyHashData(3);
}

inline PropertyHash::PropertyHash(const PropertyHash &other)
{
    d = other.d;
    ++d->refCount;
}

inline PropertyHash::~PropertyHash()
{
    if (!--d->refCount)
        delete d;
}

inline PropertyHash &PropertyHash::operator=(const PropertyHash &other)
{
    ++other.d->refCount;
    if (!--d->refCount)
        delete d;
    d = other.d;
    return *this;
}



inline uint PropertyHash::lookup(Identifier identifier) const
{
    Q_ASSERT(d->entries);

    uint idx = identifier.id % d->alloc;
    while (1) {
        if (d->entries[idx].identifier == identifier)
            return d->entries[idx].index;
        if (!d->entries[idx].identifier.isValid())
            return UINT_MAX;
        ++idx;
        idx %= d->alloc;
    }
}

template <typename T>
struct SharedInternalClassData {
    struct Private {
        Private(int alloc)
            : refcount(1),
              alloc(alloc),
              size(0)
        { data = new T  [alloc]; }
        ~Private() { delete [] data; }

        int refcount;
        uint alloc;
        uint size;
        T *data;
    };
    Private *d;

    inline SharedInternalClassData() {
        d = new Private(8);
    }

    inline SharedInternalClassData(const SharedInternalClassData &other)
        : d(other.d)
    {
        ++d->refcount;
    }
    inline ~SharedInternalClassData() {
        if (!--d->refcount)
            delete d;
    }
    SharedInternalClassData &operator=(const SharedInternalClassData &other) {
        ++other.d->refcount;
        if (!--d->refcount)
            delete d;
        d = other.d;
        return *this;
    }

    void add(uint pos, T value) {
        if (pos < d->size) {
            Q_ASSERT(d->refcount > 1);
            // need to detach
            Private *dd = new Private(pos + 8);
            memcpy(dd->data, d->data, pos*sizeof(T));
            dd->size = pos + 1;
            dd->data[pos] = value;
            if (!--d->refcount)
                delete d;
            d = dd;
            return;
        }
        Q_ASSERT(pos == d->size);
        if (pos == d->alloc) {
            T *n = new T[d->alloc * 2];
            memcpy(n, d->data, d->alloc*sizeof(T));
            delete [] d->data;
            d->data = n;
            d->alloc *= 2;
        }
        d->data[pos] = value;
        ++d->size;
    }

    void set(uint pos, T value) {
        Q_ASSERT(pos < d->size);
        if (d->refcount > 1) {
            // need to detach
            Private *dd = new Private(d->alloc);
            memcpy(dd->data, d->data, d->size*sizeof(T));
            dd->size = d->size;
            if (!--d->refcount)
                delete d;
            d = dd;
        }
        d->data[pos] = value;
    }

    T *constData() const {
        return d->data;
    }
    T at(uint i) const {
        Q_ASSERT(i < d->size);
        return d->data[i];
    }
    T operator[] (uint i) {
        Q_ASSERT(i < d->size);
        return d->data[i];
    }
};

struct InternalClassTransition
{
    union {
        Identifier id;
        const VTable *vtable;
        Heap::Object *prototype;
    };
    Heap::InternalClass *lookup;
    int flags;
    enum {
        // range 0-0xff is reserved for attribute changes
        NotExtensible = 0x100,
        VTableChange = 0x200,
        PrototypeChange = 0x201,
        ProtoClass = 0x202,
        Sealed = 0x203,
        Frozen = 0x204,
        RemoveMember = -1
    };

    bool operator==(const InternalClassTransition &other) const
    { return id == other.id && flags == other.flags; }

    bool operator<(const InternalClassTransition &other) const
    { return id < other.id || (id == other.id && flags < other.flags); }
};

namespace Heap {

struct InternalClass : Base {
    ExecutionEngine *engine;
    const VTable *vtable;
    quintptr protoId; // unique across the engine, gets changed whenever the proto chain changes
    Heap::Object *prototype;
    InternalClass *parent;

    PropertyHash propertyTable; // id to valueIndex
    SharedInternalClassData<Identifier> nameMap;
    SharedInternalClassData<PropertyAttributes> propertyData;

    typedef InternalClassTransition Transition;
    std::vector<Transition> transitions;
    InternalClassTransition &lookupOrInsertTransition(const InternalClassTransition &t);

    uint size;
    bool extensible;
    bool isSealed;
    bool isFrozen;
    bool isUsedAsProto;

    void init(ExecutionEngine *engine);
    void init(InternalClass *other);
    void destroy();

    Q_REQUIRED_RESULT InternalClass *nonExtensible();

    static void addMember(QV4::Object *object, Identifier id, PropertyAttributes data, uint *index);
    Q_REQUIRED_RESULT InternalClass *addMember(Identifier identifier, PropertyAttributes data, uint *index = nullptr);
    Q_REQUIRED_RESULT InternalClass *changeMember(Identifier identifier, PropertyAttributes data, uint *index = nullptr);
    static void changeMember(QV4::Object *object, Identifier id, PropertyAttributes data, uint *index = nullptr);
    static void removeMember(QV4::Object *object, Identifier identifier);
    uint find(const Identifier id)
    {
        Q_ASSERT(id.isValid());

        uint index = propertyTable.lookup(id);
        if (index < size)
            return index;

        return UINT_MAX;
    }

    Q_REQUIRED_RESULT InternalClass *sealed();
    Q_REQUIRED_RESULT InternalClass *frozen();
    Q_REQUIRED_RESULT InternalClass *propertiesFrozen() const;

    Q_REQUIRED_RESULT InternalClass *asProtoClass();

    Q_REQUIRED_RESULT InternalClass *changeVTable(const VTable *vt) {
        if (vtable == vt)
            return this;
        return changeVTableImpl(vt);
    }
    Q_REQUIRED_RESULT InternalClass *changePrototype(Heap::Object *proto) {
        if (prototype == proto)
            return this;
        return changePrototypeImpl(proto);
    }

    void updateProtoUsage(Heap::Object *o);

    static void markObjects(Heap::Base *ic, MarkStack *stack);

private:
    Q_QML_EXPORT InternalClass *changeVTableImpl(const VTable *vt);
    Q_QML_EXPORT InternalClass *changePrototypeImpl(Heap::Object *proto);
    InternalClass *addMemberImpl(Identifier identifier, PropertyAttributes data, uint *index);

    void removeChildEntry(InternalClass *child);
    friend struct ExecutionEngine;
};

inline
void Base::markObjects(Base *b, MarkStack *stack)
{
    b->internalClass->mark(stack);
}

}

}

QT_END_NAMESPACE

#endif
