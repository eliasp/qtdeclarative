// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only


#ifndef QQMLCONFIGURABLEDEBUGSEVICE_P_H
#define QQMLCONFIGURABLEDEBUGSEVICE_P_H

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

#include "qqmldebugservice_p.h"
#include "qqmldebugconnector_p.h"

#include <QtCore/qmutex.h>

QT_BEGIN_NAMESPACE

template <class Base>
class QQmlConfigurableDebugService : public Base
{
protected:
    QQmlConfigurableDebugService(float version, QObject *parent = nullptr) :
        Base(version, parent)
    {
        init();
    }

    void stopWaiting()
    {
        QMutexLocker lock(&m_configMutex);
        m_waitingForConfiguration = false;
        for (QJSEngine *engine : qAsConst(m_waitingEngines))
            emit Base::attachedToEngine(engine);
        m_waitingEngines.clear();
    }

    void init()
    {
        QMutexLocker lock(&m_configMutex);
        // If we're not enabled or not blocking, don't wait for configuration
        m_waitingForConfiguration = (Base::state() == QQmlDebugService::Enabled &&
                                     QQmlDebugConnector::instance()->blockingMode());
    }

    void stateChanged(QQmlDebugService::State newState) override
    {
        if (newState != QQmlDebugService::Enabled)
            stopWaiting();
        else
            init();
    }

    void engineAboutToBeAdded(QJSEngine *engine) override
    {
        QMutexLocker lock(&m_configMutex);
        if (m_waitingForConfiguration)
            m_waitingEngines.append(engine);
        else
            emit Base::attachedToEngine(engine);
    }

    QRecursiveMutex m_configMutex;
    QList<QJSEngine *> m_waitingEngines;
    bool m_waitingForConfiguration;
};

QT_END_NAMESPACE

#endif // QQMLCONFIGURABLEDEBUGSEVICE_P_H
