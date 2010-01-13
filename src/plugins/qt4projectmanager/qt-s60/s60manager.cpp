/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact:  Qt Software Information (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at qt-sales@nokia.com.
**
**************************************************************************/

#include "s60manager.h"

#include "s60devices.h"
#include "s60devicespreferencepane.h"
#include "winscwtoolchain.h"
#include "gccetoolchain.h"
#include "rvcttoolchain.h"
#include "s60emulatorrunconfiguration.h"
#include "s60devicerunconfiguration.h"

#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>

#include <QtGui/QMainWindow>

using namespace Qt4ProjectManager::Internal;

S60Manager *S60Manager::m_instance = 0;

namespace {
static const char *S60_AUTODETECTION_SOURCE = "QTS60";
}

S60Manager *S60Manager::instance() { return m_instance; }

S60Manager::S60Manager(QObject *parent)
        : QObject(parent),
        m_devices(new S60Devices(this)),
        m_devicesPreferencePane(new S60DevicesPreferencePane(m_devices, this)),
        m_s60EmulatorRunConfigurationFactory(new S60EmulatorRunConfigurationFactory(this)),
        m_s60EmulatorRunConfigurationRunner(new S60EmulatorRunConfigurationRunner(this)),
        m_s60DeviceRunConfigurationFactory(new S60DeviceRunConfigurationFactory(this)),
        m_s60DeviceRunConfigurationRunner(new S60DeviceRunConfigurationRunner(this)),
        m_serialDeviceLister(new SerialDeviceLister(this))
{
    m_instance = this;
    m_devices->detectQtForDevices();
    ExtensionSystem::PluginManager::instance()
            ->addObject(m_devicesPreferencePane);
    ExtensionSystem::PluginManager::instance()
            ->addObject(m_s60EmulatorRunConfigurationFactory);
    ExtensionSystem::PluginManager::instance()
            ->addObject(m_s60EmulatorRunConfigurationRunner);
    ExtensionSystem::PluginManager::instance()
            ->addObject(m_s60DeviceRunConfigurationFactory);
    ExtensionSystem::PluginManager::instance()
            ->addObject(m_s60DeviceRunConfigurationRunner);
    updateQtVersions();
    connect(m_devices, SIGNAL(qtVersionsChanged()),
            this, SLOT(updateQtVersions()));
    connect(Core::ICore::instance()->mainWindow(), SIGNAL(deviceChange()),
            m_serialDeviceLister, SLOT(update()));
}

S60Manager::~S60Manager()
{
    ExtensionSystem::PluginManager::instance()
            ->removeObject(m_s60DeviceRunConfigurationRunner);
    ExtensionSystem::PluginManager::instance()
            ->removeObject(m_s60DeviceRunConfigurationFactory);
    ExtensionSystem::PluginManager::instance()
            ->removeObject(m_s60EmulatorRunConfigurationRunner);
    ExtensionSystem::PluginManager::instance()
            ->removeObject(m_s60EmulatorRunConfigurationFactory);
    ExtensionSystem::PluginManager::instance()
            ->removeObject(m_devicesPreferencePane);
}

QString S60Manager::deviceIdFromDetectionSource(const QString &autoDetectionSource) const
{
    if (autoDetectionSource.startsWith(S60_AUTODETECTION_SOURCE))
        return autoDetectionSource.mid(QString(S60_AUTODETECTION_SOURCE).length()+1);
    return "";
}

void S60Manager::updateQtVersions()
{
    // This assumes that the QtVersionManager has already read
    // the Qt versions from the settings
    QtVersionManager *versionManager = QtVersionManager::instance();
    QList<QtVersion *> versions = versionManager->versions();
    QList<QtVersion *> handledVersions;
    QList<QtVersion *> versionsToAdd;
    foreach (const S60Devices::Device &device, m_devices->devices()) {
        if (device.qt.isEmpty()) // no Qt version found for this sdk
            continue;
        QtVersion *deviceVersion = 0;
        // look if we have a respective Qt version already
        foreach (QtVersion *version, versions) {
            if (version->isAutodetected()
                    && deviceIdFromDetectionSource(version->autodetectionSource()) == device.id) {
                deviceVersion = version;
                break;
            }
        }
        if (deviceVersion) {
            deviceVersion->setName(QString("%1 (Qt %2)").arg(device.id, deviceVersion->qtVersionString()));
            deviceVersion->setPath(device.qt);
            handledVersions.append(deviceVersion);
        } else {
            deviceVersion = new QtVersion(QString("%1 (Qt %2)").arg(device.id), device.qt,
                                          true, QString("%1.%2").arg(S60_AUTODETECTION_SOURCE, device.id));
            deviceVersion->setName(deviceVersion->name().arg(deviceVersion->qtVersionString()));
            versionsToAdd.append(deviceVersion);
        }
    }
    // remove old autodetected versions
    foreach (QtVersion *version, versions) {
        if (version->isAutodetected()
                && version->autodetectionSource().startsWith(S60_AUTODETECTION_SOURCE)
                && !handledVersions.contains(version)) {
            versionManager->removeVersion(version);
        }
    }
    // add new versions
    foreach (QtVersion *version, versionsToAdd) {
        versionManager->addVersion(version);
    }
}

ProjectExplorer::ToolChain *S60Manager::createWINSCWToolChain(const Qt4ProjectManager::QtVersion *version) const
{
    return new WINSCWToolChain(deviceForQtVersion(version), version->mwcDirectory());
}

ProjectExplorer::ToolChain *S60Manager::createGCCEToolChain(const Qt4ProjectManager::QtVersion *version) const
{
    return new GCCEToolChain(deviceForQtVersion(version));
}

ProjectExplorer::ToolChain *S60Manager::createRVCTToolChain(const Qt4ProjectManager::QtVersion *version,
                                                ProjectExplorer::ToolChain::ToolChainType type) const
{
    QString makeTargetBase;
    switch (type) {
    case ProjectExplorer::ToolChain::RVCT_ARMV5:
        makeTargetBase = "armv5";
        break;
    case ProjectExplorer::ToolChain::RVCT_ARMV6:
        makeTargetBase = "armv6";
        break;
    default:
        makeTargetBase = "InternalError";
    }
    return new RVCTToolChain(deviceForQtVersion(version), type, makeTargetBase);
}

S60Devices::Device S60Manager::deviceForQtVersion(const Qt4ProjectManager::QtVersion *version) const
{
    S60Devices::Device device;
    QString deviceId;
    if (version->isAutodetected())
        deviceId = deviceIdFromDetectionSource(version->autodetectionSource());
    if (deviceId.isEmpty()) { // it's not an s60 autodetected version
        // have a look if we find the device root anyhow
        if (QFile::exists(QString::fromLatin1("%1/epoc32").arg(version->path()))) {
            device.epocRoot = version->path();
            device.toolsRoot = device.epocRoot;
            device.qt = device.epocRoot;
            device.isDefault = false;
            device.name = QString::fromLatin1("SDK");
            device.id = QString::fromLatin1("SDK");
        }
    } else {
        device = m_devices->deviceForId(deviceId);
    }
    return device;
}
