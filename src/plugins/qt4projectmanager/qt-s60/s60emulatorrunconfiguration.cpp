/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
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
** contact the sales department at http://qt.nokia.com/contact.
**
**************************************************************************/

#include "s60emulatorrunconfiguration.h"

#include "qt4project.h"
#include "qtversionmanager.h"
#include "profilereader.h"
#include "s60manager.h"
#include "s60devices.h"

#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>
#include <utils/qtcassert.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/project.h>

using namespace ProjectExplorer;
using namespace Qt4ProjectManager::Internal;

// ======== S60EmulatorRunConfiguration
S60EmulatorRunConfiguration::S60EmulatorRunConfiguration(Project *project, const QString &proFilePath)
    : RunConfiguration(project),
    m_proFilePath(proFilePath),
    m_cachedTargetInformationValid(false)
{
    if (!m_proFilePath.isEmpty())
        setName(tr("%1 in Emulator").arg(QFileInfo(m_proFilePath).completeBaseName()));
    else
        setName(tr("QtS60EmulatorRunConfiguration"));

    connect(project, SIGNAL(activeBuildConfigurationChanged()),
            this, SLOT(invalidateCachedTargetInformation()));

    connect(project, SIGNAL(targetInformationChanged()),
            this, SLOT(invalidateCachedTargetInformation()));
}

S60EmulatorRunConfiguration::~S60EmulatorRunConfiguration()
{
}

QString S60EmulatorRunConfiguration::type() const
{
    return "Qt4ProjectManager.EmulatorRunConfiguration";
}

bool S60EmulatorRunConfiguration::isEnabled() const
{
    Qt4Project *pro = qobject_cast<Qt4Project*>(project());
    QTC_ASSERT(pro, return false);
    ToolChain::ToolChainType type = pro->toolChainType(pro->activeBuildConfiguration());
    return type == ToolChain::WINSCW;
}

QWidget *S60EmulatorRunConfiguration::configurationWidget()
{
    return new S60EmulatorRunConfigurationWidget(this);
}

void S60EmulatorRunConfiguration::save(PersistentSettingsWriter &writer) const
{
    const QDir projectDir = QFileInfo(project()->file()->fileName()).absoluteDir();
    writer.saveValue("ProFile", projectDir.relativeFilePath(m_proFilePath));
    RunConfiguration::save(writer);
}

void S60EmulatorRunConfiguration::restore(const PersistentSettingsReader &reader)
{
    RunConfiguration::restore(reader);
    const QDir projectDir = QFileInfo(project()->file()->fileName()).absoluteDir();
    m_proFilePath = projectDir.filePath(reader.restoreValue("ProFile").toString());
}

QString S60EmulatorRunConfiguration::executable() const
{
    const_cast<S60EmulatorRunConfiguration *>(this)->updateTarget();
    return m_executable;
}

void S60EmulatorRunConfiguration::updateTarget()
{
    if (m_cachedTargetInformationValid)
        return;
    Qt4Project *pro = static_cast<Qt4Project *>(project());
    Qt4PriFileNode * priFileNode = static_cast<Qt4Project *>(project())->rootProjectNode()->findProFileFor(m_proFilePath);
    if (!priFileNode) {
        m_executable = QString::null;
        m_cachedTargetInformationValid = true;
        emit targetInformationChanged();
        return;
    }
    QtVersion *qtVersion = pro->qtVersion(pro->activeBuildConfiguration());
    ProFileReader *reader = priFileNode->createProFileReader();
    reader->setCumulative(false);
    reader->setQtVersion(qtVersion);

    // Find out what flags we pass on to qmake, this code is duplicated in the qmake step
    QtVersion::QmakeBuildConfig defaultBuildConfiguration = qtVersion->defaultBuildConfig();
    QtVersion::QmakeBuildConfig projectBuildConfiguration = QtVersion::QmakeBuildConfig(pro->value(pro->activeBuildConfiguration(), "buildConfiguration").toInt());
    QStringList addedUserConfigArguments;
    QStringList removedUserConfigArguments;
    if ((defaultBuildConfiguration & QtVersion::BuildAll) && !(projectBuildConfiguration & QtVersion::BuildAll))
        removedUserConfigArguments << "debug_and_release";
    if (!(defaultBuildConfiguration & QtVersion::BuildAll) && (projectBuildConfiguration & QtVersion::BuildAll))
        addedUserConfigArguments << "debug_and_release";
    if ((defaultBuildConfiguration & QtVersion::DebugBuild) && !(projectBuildConfiguration & QtVersion::DebugBuild))
        addedUserConfigArguments << "release";
    if (!(defaultBuildConfiguration & QtVersion::DebugBuild) && (projectBuildConfiguration & QtVersion::DebugBuild))
        addedUserConfigArguments << "debug";

    reader->setUserConfigCmdArgs(addedUserConfigArguments, removedUserConfigArguments);

    if (!reader->readProFile(m_proFilePath)) {
        delete reader;
        Core::ICore::instance()->messageManager()->printToOutputPane(tr("Could not parse %1. The QtS60 emulator run configuration %2 can not be started.").arg(m_proFilePath).arg(name()));
        return;
    }

    QString baseDir = S60Manager::instance()->deviceForQtVersion(qtVersion).epocRoot;
    QString qmakeBuildConfig = "urel";
    if (projectBuildConfiguration & QtVersion::DebugBuild)
        qmakeBuildConfig = "udeb";
    baseDir += "/epoc32/release/winscw/" + qmakeBuildConfig;

    QString target = reader->value("TARGET");
    if (target.isEmpty())
        target = QFileInfo(m_proFilePath).baseName();

    m_executable = QDir::toNativeSeparators(
            QDir::cleanPath(baseDir + QLatin1Char('/') + target));
    m_executable += QLatin1String(".exe");

    delete reader;
    m_cachedTargetInformationValid = true;
    emit targetInformationChanged();
}

void S60EmulatorRunConfiguration::invalidateCachedTargetInformation()
{
    m_cachedTargetInformationValid = false;
    emit targetInformationChanged();
}

// ======== S60EmulatorRunConfigurationWidget

S60EmulatorRunConfigurationWidget::S60EmulatorRunConfigurationWidget(S60EmulatorRunConfiguration *runConfiguration,
                                                                     QWidget *parent)
    : QWidget(parent),
    m_runConfiguration(runConfiguration)
{
    QFormLayout *toplayout = new QFormLayout();
    toplayout->setMargin(0);
    setLayout(toplayout);

    QLabel *nameLabel = new QLabel(tr("Name:"));
    m_nameLineEdit = new QLineEdit(m_runConfiguration->name());
    nameLabel->setBuddy(m_nameLineEdit);
    toplayout->addRow(nameLabel, m_nameLineEdit);

    m_executableLabel = new QLabel(m_runConfiguration->executable());
    toplayout->addRow(tr("Executable:"), m_executableLabel);

    connect(m_nameLineEdit, SIGNAL(textEdited(QString)),
        this, SLOT(nameEdited(QString)));
    connect(m_runConfiguration, SIGNAL(targetInformationChanged()),
            this, SLOT(updateTargetInformation()));
}

void S60EmulatorRunConfigurationWidget::nameEdited(const QString &text)
{
    m_runConfiguration->setName(text);
}

void S60EmulatorRunConfigurationWidget::updateTargetInformation()
{
    m_executableLabel->setText(m_runConfiguration->executable());
}

// ======== S60EmulatorRunConfigurationFactory

S60EmulatorRunConfigurationFactory::S60EmulatorRunConfigurationFactory(QObject *parent)
    : IRunConfigurationFactory(parent)
{
}

S60EmulatorRunConfigurationFactory::~S60EmulatorRunConfigurationFactory()
{
}

bool S60EmulatorRunConfigurationFactory::canRestore(const QString &type) const
{
    return type == "Qt4ProjectManager.EmulatorRunConfiguration";
}

QStringList S60EmulatorRunConfigurationFactory::availableCreationTypes(Project *pro) const
{
    Qt4Project *qt4project = qobject_cast<Qt4Project *>(pro);
    if (qt4project) {
        QStringList applicationProFiles;
        QList<Qt4ProFileNode *> list = qt4project->applicationProFiles();
        foreach (Qt4ProFileNode * node, list) {
            applicationProFiles.append("QtS60EmulatorRunConfiguration." + node->path());
        }
        return applicationProFiles;
    } else {
        return QStringList();
    }
}

QString S60EmulatorRunConfigurationFactory::displayNameForType(const QString &type) const
{
    QString fileName = type.mid(QString("QtS60EmulatorRunConfiguration.").size());
    return tr("%1 in Emulator").arg(QFileInfo(fileName).completeBaseName());
}

QSharedPointer<RunConfiguration> S60EmulatorRunConfigurationFactory::create(Project *project, const QString &type)
{
    Qt4Project *p = qobject_cast<Qt4Project *>(project);
    Q_ASSERT(p);
    if (type.startsWith("QtS60EmulatorRunConfiguration.")) {
        QString fileName = type.mid(QString("QtS60EmulatorRunConfiguration.").size());
        return QSharedPointer<RunConfiguration>(new S60EmulatorRunConfiguration(p, fileName));
    }
    Q_ASSERT(type == "Qt4ProjectManager.EmulatorRunConfiguration");
    // The right path is set in restoreSettings
    QSharedPointer<RunConfiguration> rc(new S60EmulatorRunConfiguration(p, QString::null));
    return rc;
}

// ======== S60EmulatorRunConfigurationRunner

S60EmulatorRunConfigurationRunner::S60EmulatorRunConfigurationRunner(QObject *parent)
    : IRunConfigurationRunner(parent)
{
}

bool S60EmulatorRunConfigurationRunner::canRun(QSharedPointer<RunConfiguration> runConfiguration, const QString &mode)
{
    return (mode == ProjectExplorer::Constants::RUNMODE)
            && (!runConfiguration.dynamicCast<S60EmulatorRunConfiguration>().isNull());
}

RunControl* S60EmulatorRunConfigurationRunner::run(QSharedPointer<RunConfiguration> runConfiguration, const QString &mode)
{
    QSharedPointer<S60EmulatorRunConfiguration> rc = runConfiguration.dynamicCast<S60EmulatorRunConfiguration>();
    Q_ASSERT(!rc.isNull());
    Q_ASSERT(mode == ProjectExplorer::Constants::RUNMODE);

    S60EmulatorRunControl *runControl = new S60EmulatorRunControl(rc);
    return runControl;
}

QString S60EmulatorRunConfigurationRunner::displayName() const
{
    return tr("Run in Emulator");
}

QWidget *S60EmulatorRunConfigurationRunner::configurationWidget(QSharedPointer<ProjectExplorer::RunConfiguration> /* runConfiguration */)
{
    return 0;
}

// ======== S60EmulatorRunControl

S60EmulatorRunControl::S60EmulatorRunControl(QSharedPointer<RunConfiguration> runConfiguration)
    : RunControl(runConfiguration)
{
    connect(&m_applicationLauncher, SIGNAL(applicationError(QString)),
            this, SLOT(slotError(QString)));
    connect(&m_applicationLauncher, SIGNAL(appendOutput(QString)),
            this, SLOT(slotAddToOutputWindow(QString)));
    connect(&m_applicationLauncher, SIGNAL(processExited(int)),
            this, SLOT(processExited(int)));
    connect(&m_applicationLauncher, SIGNAL(bringToForegroundRequested(qint64)),
            this, SLOT(bringApplicationToForeground(qint64)));
}

void S60EmulatorRunControl::start()
{
    QSharedPointer<S60EmulatorRunConfiguration> rc = runConfiguration().dynamicCast<S60EmulatorRunConfiguration>();
    Q_ASSERT(!rc.isNull());

    // stuff like the EPOCROOT and EPOCDEVICE env variable
    Environment env = Environment::systemEnvironment();
    static_cast<Qt4Project *>(rc->project())->toolChain(rc->project()->activeBuildConfiguration())->addToEnvironment(env);
    m_applicationLauncher.setEnvironment(env.toStringList());

    m_executable = rc->executable();

    m_applicationLauncher.start(ApplicationLauncher::Gui,
                                m_executable, QStringList());
    emit started();

    emit addToOutputWindow(this, tr("Starting %1...").arg(QDir::toNativeSeparators(m_executable)));
}

void S60EmulatorRunControl::stop()
{
    m_applicationLauncher.stop();
}

bool S60EmulatorRunControl::isRunning() const
{
    return m_applicationLauncher.isRunning();
}

void S60EmulatorRunControl::slotError(const QString & err)
{
    emit error(this, err);
    emit finished();
}

void S60EmulatorRunControl::slotAddToOutputWindow(const QString &line)
{
    static QString prefix = tr("[Qt Message]");
    static int prefixLength = prefix.length();
    int index = line.indexOf(prefix);
    if (index != -1) {
        emit addToOutputWindowInline(this, line.mid(index + prefixLength + 1));
    }
}

void S60EmulatorRunControl::processExited(int exitCode)
{
    emit addToOutputWindow(this, tr("%1 exited with code %2").arg(QDir::toNativeSeparators(m_executable)).arg(exitCode));
    emit finished();
}
