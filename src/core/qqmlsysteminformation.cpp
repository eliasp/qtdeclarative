// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qqmlsysteminformation_p.h"
#include <QtCore/qsysinfo.h>

QT_BEGIN_NAMESPACE

/*!
    \qmltype SystemInformation
    \inherits QtObject
    \inqmlmodule QtCore
    \since 6.4
    \brief Provides information about the system.

    The SystemInformation singleton type provides information about the system,
    using the same API as \l QSysInfo.

    \qml
    property string prettyProductName: SystemInformation.prettyProductName
    \endqml

    \sa QSysInfo
*/

QQmlSystemInformation::QQmlSystemInformation(QObject *parent) : QObject(parent)
{
}

QString QQmlSystemInformation::buildCpuArchitecture() const
{
    return QSysInfo::buildCpuArchitecture();
}

QString QQmlSystemInformation::currentCpuArchitecture() const
{
    return QSysInfo::currentCpuArchitecture();
}

QString QQmlSystemInformation::buildAbi() const
{
    return QSysInfo::buildAbi();
}

QString QQmlSystemInformation::kernelType() const
{
    return QSysInfo::kernelType();
}

QString QQmlSystemInformation::kernelVersion() const
{
    return QSysInfo::kernelVersion();
}

QString QQmlSystemInformation::productType() const
{
    return QSysInfo::productType();
}

QString QQmlSystemInformation::productVersion() const
{
    return QSysInfo::productVersion();
}

QString QQmlSystemInformation::prettyProductName() const
{
    return QSysInfo::prettyProductName();
}

QString QQmlSystemInformation::machineHostName() const
{
    return QSysInfo::machineHostName();
}

QByteArray QQmlSystemInformation::machineUniqueId() const
{
    return QSysInfo::machineUniqueId();
}

QByteArray QQmlSystemInformation::bootUniqueId() const
{
    return QSysInfo::bootUniqueId();
}

QT_END_NAMESPACE

#include "moc_qqmlsysteminformation_p.cpp"
