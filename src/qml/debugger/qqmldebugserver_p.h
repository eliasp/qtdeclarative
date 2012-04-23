/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/
**
** This file is part of the QtQml module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QQMLDEBUGSERVER_H
#define QQMLDEBUGSERVER_H

#include <QtQml/qtqmlglobal.h>
#include <private/qqmldebugserverconnection_p.h>
#include <private/qqmldebugservice_p.h>

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

QT_BEGIN_HEADER

QT_BEGIN_NAMESPACE


class QQmlDebugServerPrivate;
class Q_QML_EXPORT QQmlDebugServer : public QObject
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(QQmlDebugServer)
    Q_DISABLE_COPY(QQmlDebugServer)
public:
    ~QQmlDebugServer();

    static QQmlDebugServer *instance();

    void setConnection(QQmlDebugServerConnection *connection);

    bool hasDebuggingClient() const;
    bool blockingMode() const;

    QList<QQmlDebugService*> services() const;
    QStringList serviceNames() const;


    bool addService(QQmlDebugService *service);
    bool removeService(QQmlDebugService *service);

    void receiveMessage(const QByteArray &message);

    void sendMessages(QQmlDebugService *service, const QList<QByteArray> &messages);

private:
    friend class QQmlDebugService;
    friend class QQmlDebugServicePrivate;
    friend class QQmlDebugServerThread;
    QQmlDebugServer();
    Q_PRIVATE_SLOT(d_func(), void _q_changeServiceState(const QString &serviceName,
                                                        QQmlDebugService::State state))
    Q_PRIVATE_SLOT(d_func(), void _q_sendMessages(QList<QByteArray>))

public:
    static int s_dataStreamVersion;
};

QT_END_NAMESPACE

QT_END_HEADER

#endif // QQMLDEBUGSERVICE_H
