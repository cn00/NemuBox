/* $Id: NemuGlobal.cpp $ */
/** @file
 * Nemu Qt GUI - NemuGlobal class implementation.
 */

/*
 * Copyright (C) 2006-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifdef NEMU_WITH_PRECOMPILED_HEADERS
# include <precomp.h>
#else  /* !NEMU_WITH_PRECOMPILED_HEADERS */

/* Qt includes: */
# include <QFileDialog>
# include <QToolTip>
# include <QTranslator>
# include <QDesktopWidget>
# include <QDesktopServices>
# include <QMutex>
# include <QToolButton>
# include <QProcess>
# include <QThread>
# include <QPainter>
# include <QTimer>
# include <QDir>
# include <QLocale>
# include <QSpinBox>

# ifdef Q_WS_WIN
#  include <QEventLoop>
# endif /* Q_WS_WIN */

# ifdef Q_WS_X11
#  include <QTextBrowser>
#  include <QScrollBar>
#  include <QX11Info>
# endif /* Q_WS_X11 */

# ifdef NEMU_GUI_WITH_PIDFILE
#  include <QTextStream>
# endif /* NEMU_GUI_WITH_PIDFILE */

/* GUI includes: */
# include "NemuGlobal.h"
# include "UISelectorWindow.h"
# include "UIMessageCenter.h"
# include "UIPopupCenter.h"
# include "QIMessageBox.h"
# include "QIDialogButtonBox.h"
# include "UIIconPool.h"
# include "UIThreadPool.h"
# include "UIShortcutPool.h"
# include "UIExtraDataManager.h"
# include "QIFileDialog.h"
# ifdef NEMU_GUI_WITH_NETWORK_MANAGER
#  include "UINetworkManager.h"
#  include "UIUpdateManager.h"
# endif /* NEMU_GUI_WITH_NETWORK_MANAGER */
# include "UIMachine.h"
# include "UISession.h"
# include "UIConverter.h"
# include "UIMediumEnumerator.h"
# include "UIMedium.h"
# include "UIModalWindowManager.h"
# include "UIIconPool.h"
# include "UIVirtualBoxEventHandler.h"

# ifdef Q_WS_X11
#  include "UIHostComboEditor.h"
#  include "UIDesktopWidgetWatchdog.h"
#  ifndef NEMU_OSE
#   include "NemuLicenseViewer.h"
#  endif /* NEMU_OSE */
# endif /* Q_WS_X11 */

# ifdef Q_WS_MAC
#  include "NemuUtils-darwin.h"
#  include "UIMachineWindowFullscreen.h"
#  include "UIMachineWindowSeamless.h"
# endif /* Q_WS_MAC */

# ifdef NEMU_WITH_VIDEOHWACCEL
#  include "NemuFBOverlay.h"
# endif /* NEMU_WITH_VIDEOHWACCEL */

/* COM includes: */
# include "CMachine.h"
# include "CSystemProperties.h"
# include "CUSBDevice.h"
# include "CUSBDeviceFilters.h"
# include "CUSBDeviceFilter.h"
# include "CBIOSSettings.h"
# include "CVRDEServer.h"
# include "CStorageController.h"
# include "CMediumAttachment.h"
# include "CAudioAdapter.h"
# include "CNetworkAdapter.h"
# include "CSerialPort.h"
# include "CParallelPort.h"
# include "CUSBController.h"
# include "CHostUSBDevice.h"
# include "CHostVideoInputDevice.h"
# include "CMediumFormat.h"
# include "CSharedFolder.h"
# include "CConsole.h"
# include "CSnapshot.h"

/* Other Nemu includes: */
# include <iprt/asm.h>
# include <iprt/param.h>
# include <iprt/path.h>
# include <iprt/env.h>
# include <iprt/ldr.h>
# include <iprt/system.h>
# include <iprt/stream.h>
# ifdef Q_WS_X11
#  include <iprt/mem.h>
# endif /* Q_WS_X11 */

# include <Nemu/sup.h>
# include <Nemu/com/Guid.h>

# ifdef Q_WS_WIN
#  include "shlobj.h"
# endif /* Q_WS_WIN */

#endif /* !NEMU_WITH_PRECOMPILED_HEADERS */

/* VirtualBox interface declarations: */
#ifndef NEMU_WITH_XPCOM
# include "VirtualBox.h"
#else /* !NEMU_WITH_XPCOM */
# include "VirtualBox_XPCOM.h"
#endif /* NEMU_WITH_XPCOM */

#include <QLibraryInfo>
#include <QProgressDialog>
#include <QSettings>
#include <QStyleOptionSpinBox>

#include <Nemu/NemuOGL.h>
#include <Nemu/vd.h>

#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/file.h>

#ifdef Q_WS_MAC
# include <sys/utsname.h>
#endif /* Q_WS_MAC */

/* External includes: */
# include <math.h>

#ifdef Q_WS_X11
# undef BOOL /* typedef CARD8 BOOL in Xmd.h conflicts with #define BOOL PRBool
              * in COMDefs.h. A better fix would be to isolate X11-specific
              * stuff by placing XX* helpers below to a separate source file. */
# include <X11/X.h>
# include <X11/Xmd.h>
# include <X11/Xlib.h>
# include <X11/Xatom.h>
# include <X11/extensions/Xinerama.h>

# define BOOL PRBool
#endif /* Q_WS_X11 */


//#define NEMU_WITH_FULL_DETAILS_REPORT /* hidden for now */

/* Namespaces: */
using namespace UIExtraDataDefs;


// NemuGlobal
////////////////////////////////////////////////////////////////////////////////

/* static */
bool NemuGlobal::m_sfCleanupInProgress = false;
NemuGlobal* NemuGlobal::m_spInstance = 0;
NemuGlobal* NemuGlobal::instance() { return m_spInstance; }

/* static */
void NemuGlobal::create()
{
    /* Make sure instance is NOT created yet: */
    if (m_spInstance)
    {
        AssertMsgFailed(("NemuGlobal instance is already created!"));
        return;
    }

    /* Create instance: */
    new NemuGlobal;
    /* Prepare instance: */
    m_spInstance->prepare();
}

/* static */
void NemuGlobal::destroy()
{
    /* Make sure instance is NOT destroyed yet: */
    if (!m_spInstance)
    {
        AssertMsgFailed(("NemuGlobal instance is already destroyed!"));
        return;
    }

    /* Cleanup instance:
     * 1. By default, automatically on QApplication::aboutToQuit() signal.
     * 2. But if QApplication was not started at all and we perform
     *    early shutdown, we should do cleanup ourselves. */
    if (m_spInstance->isValid())
        m_spInstance->cleanup();
    /* Destroy instance: */
    delete m_spInstance;
}

NemuGlobal::NemuGlobal()
    : mValid (false)
#ifdef Q_WS_MAC
    , m_osRelease(MacOSXRelease_Old)
#endif /* Q_WS_MAC */
    , m_fWrappersValid(false)
    , m_fNemuSVCAvailable(true)
    , m_fSeparateProcess(false)
    , m_pMediumEnumerator(0)
#ifdef Q_WS_X11
    , m_enmWindowManagerType(X11WMType_Unknown)
    , m_pDesktopWidgetWatchdog(0)
#endif /* Q_WS_X11 */
#if defined(DEBUG_bird)
    , mAgressiveCaching(false)
#else
    , mAgressiveCaching(true)
#endif
    , mRestoreCurrentSnapshot(false)
    , mDisablePatm(false)
    , mDisableCsam(false)
    , mRecompileSupervisor(false)
    , mRecompileUser(false)
    , mExecuteAllInIem(false)
    , mWarpPct(100)
    , mVerString("1.0")
    , m3DAvailable(-1)
    , mSettingsPwSet(false)
    , m_pIconPool(0)
    , m_pThreadPool(0)
{
    /* Assign instance: */
    m_spInstance = this;
}

NemuGlobal::~NemuGlobal()
{
    /* Unassign instance: */
    m_spInstance = 0;
}

/* static */
QString NemuGlobal::qtRTVersionString()
{
    return QString::fromLatin1 (qVersion());
}

/* static */
uint NemuGlobal::qtRTVersion()
{
    QString rt_ver_str = NemuGlobal::qtRTVersionString();
    return (rt_ver_str.section ('.', 0, 0).toInt() << 16) +
           (rt_ver_str.section ('.', 1, 1).toInt() << 8) +
           rt_ver_str.section ('.', 2, 2).toInt();
}

/* static */
QString NemuGlobal::qtCTVersionString()
{
    return QString::fromLatin1 (QT_VERSION_STR);
}

/* static */
uint NemuGlobal::qtCTVersion()
{
    QString ct_ver_str = NemuGlobal::qtCTVersionString();
    return (ct_ver_str.section ('.', 0, 0).toInt() << 16) +
           (ct_ver_str.section ('.', 1, 1).toInt() << 8) +
           ct_ver_str.section ('.', 2, 2).toInt();
}

QString NemuGlobal::nemuVersionString() const
{
    return m_nemu.GetVersion();
}

QString NemuGlobal::nemuVersionStringNormalized() const
{
    return m_nemu.GetVersionNormalized();
}

bool NemuGlobal::isBeta() const
{
    return m_nemu.GetVersion().contains("BETA", Qt::CaseInsensitive);
}

#ifdef Q_WS_MAC
/* static */
MacOSXRelease NemuGlobal::determineOsRelease()
{
    /* Prepare 'utsname' struct: */
    utsname info;
    if (uname(&info) != -1)
    {
        /* Compose map of known releases: */
        QMap<int, MacOSXRelease> release;
        release[10] = MacOSXRelease_SnowLeopard;
        release[11] = MacOSXRelease_Lion;
        release[12] = MacOSXRelease_MountainLion;
        release[13] = MacOSXRelease_Mavericks;
        release[14] = MacOSXRelease_Yosemite;
        release[15] = MacOSXRelease_ElCapitan;

        /* Cut the major release index of the string we have, s.a. 'man uname': */
        const int iRelease = QString(info.release).section('.', 0, 0).toInt();

        /* Return release if determined, return 'New' if version more recent than latest, return 'Old' otherwise: */
        return release.value(iRelease, iRelease > release.keys().last() ? MacOSXRelease_New : MacOSXRelease_Old);
    }
    /* Return 'Old' by default: */
    return MacOSXRelease_Old;
}
#endif /* Q_WS_MAC */

int NemuGlobal::screenCount() const
{
    /* Redirect call to QDesktopWidget: */
    return QApplication::desktop()->screenCount();
}

int NemuGlobal::screenNumber(const QWidget *pWidget) const
{
    /* Redirect call to QDesktopWidget: */
    return QApplication::desktop()->screenNumber(pWidget);
}

int NemuGlobal::screenNumber(const QPoint &point) const
{
    /* Redirect call to QDesktopWidget: */
    return QApplication::desktop()->screenNumber(point);
}

const QRect NemuGlobal::screenGeometry(int iHostScreenIndex /* = -1 */) const
{
#ifdef Q_WS_X11
    /* Make sure desktop-widget watchdog already created: */
    AssertPtrReturn(m_pDesktopWidgetWatchdog, QApplication::desktop()->screenGeometry(iHostScreenIndex));
    /* Redirect call to UIDesktopWidgetWatchdog: */
    return m_pDesktopWidgetWatchdog->screenGeometry(iHostScreenIndex);
#endif /* Q_WS_X11 */

    /* Redirect call to QDesktopWidget: */
    return QApplication::desktop()->screenGeometry(iHostScreenIndex);
}

const QRect NemuGlobal::availableGeometry(int iHostScreenIndex /* = -1 */) const
{
#ifdef Q_WS_X11
    /* Make sure desktop-widget watchdog already created: */
    AssertPtrReturn(m_pDesktopWidgetWatchdog, QApplication::desktop()->availableGeometry(iHostScreenIndex));
    /* Redirect call to UIDesktopWidgetWatchdog: */
    return m_pDesktopWidgetWatchdog->availableGeometry(iHostScreenIndex);
#endif /* Q_WS_X11 */

    /* Redirect call to QDesktopWidget: */
    return QApplication::desktop()->availableGeometry(iHostScreenIndex);
}

const QRect NemuGlobal::screenGeometry(const QWidget *pWidget) const
{
    /* Redirect call to existing wrapper: */
    return screenGeometry(screenNumber(pWidget));
}

const QRect NemuGlobal::availableGeometry(const QWidget *pWidget) const
{
    /* Redirect call to existing wrapper: */
    return availableGeometry(screenNumber(pWidget));
}

const QRect NemuGlobal::screenGeometry(const QPoint &point) const
{
    /* Redirect call to existing wrapper: */
    return screenGeometry(screenNumber(point));
}

const QRect NemuGlobal::availableGeometry(const QPoint &point) const
{
    /* Redirect call to existing wrapper: */
    return availableGeometry(screenNumber(point));
}

/**
 *  Sets the new global settings and saves them to the VirtualBox server.
 */
bool NemuGlobal::setSettings (NemuGlobalSettings &gs)
{
    gs.save(m_nemu);

    if (!m_nemu.isOk())
    {
        msgCenter().cannotSaveGlobalConfig(m_nemu);
        return false;
    }

    /* We don't assign gs to our gset member here, because NemuCallback
     * will update gset as necessary when new settings are successfully
     * sent to the VirtualBox server by gs.save(). */

    return true;
}

QWidget* NemuGlobal::activeMachineWindow() const
{
    if (isVMConsoleProcess() && gpMachine && gpMachine->activeWindow())
        return gpMachine->activeWindow();
    return 0;
}

/**
 * Inner worker that for lazily querying for 3D support.
 *
 * Rational is that when starting a text mode guest (like DOS) that does not
 * have 3D enabled, don't wast the developer's or user's time on launching the
 * test application when starting the VM or editing it's settings.
 *
 * @returns true / false.
 * @note If we ever end up checking this concurrently on multiple threads, use a
 *       RTONCE construct to serialize the efforts.
 */
bool NemuGlobal::is3DAvailableWorker() const
{
#ifdef NEMU_WITH_CROGL
    bool fSupported = NemuOglIs3DAccelerationSupported();
#else
    bool fSupported = false;
#endif
    unconst(this)->m3DAvailable = fSupported;
    return fSupported;
}


#ifdef NEMU_GUI_WITH_PIDFILE
void NemuGlobal::createPidfile()
{
    if (!m_strPidfile.isEmpty())
    {
        qint64 pid = qApp->applicationPid();
        QFile file(m_strPidfile);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
             QTextStream out(&file);
             out << pid << endl;
        }
        else
            LogRel(("Failed to create pid file %s\n", m_strPidfile.toUtf8().constData()));
    }
}

void NemuGlobal::deletePidfile()
{
    if (   !m_strPidfile.isEmpty()
        && QFile::exists(m_strPidfile))
        QFile::remove(m_strPidfile);
}
#endif

bool NemuGlobal::brandingIsActive (bool aForce /* = false*/)
{
    if (aForce)
        return true;

    if (mBrandingConfig.isEmpty())
    {
        mBrandingConfig = QDir(QApplication::applicationDirPath()).absolutePath();
        mBrandingConfig += "/custom/custom.ini";
    }
    return QFile::exists (mBrandingConfig);
}

/**
  * Gets a value from the custom .ini file
  */
QString NemuGlobal::brandingGetKey (QString aKey)
{
    QSettings s(mBrandingConfig, QSettings::IniFormat);
    return s.value(QString("%1").arg(aKey)).toString();
}

#ifdef Q_WS_X11
QList<QRect> XGetDesktopList()
{
    /* Prepare empty resulting list: */
    QList<QRect> result;

    /* Get current display: */
    Display* pDisplay = QX11Info::display();

    /* If it's a Xinerama desktop: */
    if (XineramaIsActive(pDisplay))
    {
        /* Reading Xinerama data: */
        int iScreens = 0;
        XineramaScreenInfo *pScreensData = XineramaQueryScreens(pDisplay, &iScreens);

        /* Fill resulting list: */
        for (int i = 0; i < iScreens; ++ i)
            result << QRect(pScreensData[i].x_org, pScreensData[i].y_org,
                            pScreensData[i].width, pScreensData[i].height);

        /* Free screens data: */
        XFree(pScreensData);
    }

    /* Return resulting list: */
    return result;
}

QList<Window> XGetWindowIDList()
{
    /* Get current display: */
    Display *pDisplay = QX11Info::display();

    /* Get virtual desktop window: */
    Window window = QX11Info::appRootWindow();

    /* Get 'client list' atom: */
    Atom propNameAtom = XInternAtom(pDisplay, "_NET_CLIENT_LIST", True /* only if exists */);

    /* Prepare empty resulting list: */
    QList<Window> result;

    /* If atom does not exists return empty list: */
    if (propNameAtom == None)
        return result;

    /* Get atom value: */
    Atom realAtomType = None;
    int iRealFormat = 0;
    unsigned long uItemsCount = 0;
    unsigned long uBytesAfter = 0;
    unsigned char *pData = 0;
    int rc = XGetWindowProperty(pDisplay, window, propNameAtom,
                                0, 0x7fffffff /*LONG_MAX*/, False /* delete */,
                                XA_WINDOW, &realAtomType, &iRealFormat,
                                &uItemsCount, &uBytesAfter, &pData);

    /* If get property is failed return empty list: */
    if (rc != Success)
        return result;

    /* Fill resulting list with win ids: */
    Window *pWindowData = reinterpret_cast<Window*>(pData);
    for (ulong i = 0; i < uItemsCount; ++ i)
        result << pWindowData[i];

    /* Releasing resources: */
    XFree(pData);

    /* Return resulting list: */
    return result;
}

QList<ulong> XGetStrut(Window window)
{
    /* Get current display: */
    Display *pDisplay = QX11Info::display();

    /* Get 'strut' atom: */
    Atom propNameAtom = XInternAtom(pDisplay, "_NET_WM_STRUT_PARTIAL", True /* only if exists */);

    /* Prepare empty resulting list: */
    QList<ulong> result;

    /* If atom does not exists return empty list: */
    if (propNameAtom == None)
        return result;

    /* Get atom value: */
    Atom realAtomType = None;
    int iRealFormat = 0;
    ulong uItemsCount = 0;
    ulong uBytesAfter = 0;
    unsigned char *pData = 0;
    int rc = XGetWindowProperty(pDisplay, window, propNameAtom,
                                0, LONG_MAX, False /* delete */,
                                XA_CARDINAL, &realAtomType, &iRealFormat,
                                &uItemsCount, &uBytesAfter, &pData);

    /* If get property is failed return empty list: */
    if (rc != Success)
        return result;

    /* Fill resulting list with strut shifts: */
    ulong *pStrutsData = reinterpret_cast<ulong*>(pData);
    for (ulong i = 0; i < uItemsCount; ++ i)
        result << pStrutsData[i];

    /* Releasing resources: */
    XFree(pData);

    /* Return resulting list: */
    return result;
}
#endif /* ifdef Q_WS_X11 */

/**
 *  Returns the list of few guest OS types, queried from
 *  IVirtualBox corresponding to every family id.
 */
QList<CGuestOSType> NemuGlobal::vmGuestOSFamilyList() const
{
    QList<CGuestOSType> result;
    for (int i = 0; i < m_guestOSFamilyIDs.size(); ++i)
        result << m_guestOSTypes[i][0];
    return result;
}

/**
 *  Returns the list of all guest OS types, queried from
 *  IVirtualBox corresponding to passed family id.
 */
QList<CGuestOSType> NemuGlobal::vmGuestOSTypeList(const QString &aFamilyId) const
{
    AssertMsg(m_guestOSFamilyIDs.contains(aFamilyId), ("Family ID incorrect: '%s'.", aFamilyId.toLatin1().constData()));
    return m_guestOSFamilyIDs.contains(aFamilyId) ?
           m_guestOSTypes[m_guestOSFamilyIDs.indexOf(aFamilyId)] : QList<CGuestOSType>();
}

QPixmap NemuGlobal::vmGuestOSTypeIcon(const QString &strOSTypeID, QSize *pLogicalSize /* = 0 */) const
{
    /* Prepare fallback pixmap: */
    static QPixmap nullPixmap;

    /* Make sure general icon-pool initialized: */
    AssertReturn(m_pIconPool, nullPixmap);

    /* Redirect to general icon-pool: */
    return m_pIconPool->guestOSTypeIcon(strOSTypeID, pLogicalSize);
}

QPixmap NemuGlobal::vmGuestOSTypePixmap(const QString &strOSTypeID, const QSize &physicalSize) const
{
    /* Prepare fallback pixmap: */
    static QPixmap nullPixmap;

    /* Make sure general icon-pool initialized: */
    AssertReturn(m_pIconPool, nullPixmap);

    /* Redirect to general icon-pool: */
    return m_pIconPool->guestOSTypePixmap(strOSTypeID, physicalSize);
}

QPixmap NemuGlobal::vmGuestOSTypePixmapHiDPI(const QString &strOSTypeID, const QSize &physicalSize) const
{
    /* Prepare fallback pixmap: */
    static QPixmap nullPixmap;

    /* Make sure general icon-pool initialized: */
    AssertReturn(m_pIconPool, nullPixmap);

    /* Redirect to general icon-pool: */
    return m_pIconPool->guestOSTypePixmapHiDPI(strOSTypeID, physicalSize);
}

/**
 *  Returns the guest OS type object corresponding to the given type id of list
 *  containing OS types related to OS family determined by family id attribute.
 *  If the index is invalid a null object is returned.
 */
CGuestOSType NemuGlobal::vmGuestOSType(const QString &aTypeId,
             const QString &aFamilyId /* = QString::null */) const
{
    QList <CGuestOSType> list;
    if (m_guestOSFamilyIDs.contains (aFamilyId))
    {
        list = m_guestOSTypes [m_guestOSFamilyIDs.indexOf (aFamilyId)];
    }
    else
    {
        for (int i = 0; i < m_guestOSFamilyIDs.size(); ++ i)
            list += m_guestOSTypes [i];
    }
    for (int j = 0; j < list.size(); ++ j)
        if (!list [j].GetId().compare (aTypeId))
            return list [j];
    AssertMsgFailed (("Type ID incorrect: '%s'.", aTypeId.toLatin1().constData()));
    return CGuestOSType();
}

/**
 *  Returns the description corresponding to the given guest OS type id.
 */
QString NemuGlobal::vmGuestOSTypeDescription (const QString &aTypeId) const
{
    for (int i = 0; i < m_guestOSFamilyIDs.size(); ++ i)
    {
        QList <CGuestOSType> list (m_guestOSTypes [i]);
        for ( int j = 0; j < list.size(); ++ j)
            if (!list [j].GetId().compare (aTypeId))
                return list [j].GetDescription();
    }
    return QString::null;
}

struct PortConfig
{
    const char *name;
    const ulong IRQ;
    const ulong IOBase;
};

static const PortConfig kComKnownPorts[] =
{
    { "COM1", 4, 0x3F8 },
    { "COM2", 3, 0x2F8 },
    { "COM3", 4, 0x3E8 },
    { "COM4", 3, 0x2E8 },
    /* must not contain an element with IRQ=0 and IOBase=0 used to cause
     * toCOMPortName() to return the "User-defined" string for these values. */
};

static const PortConfig kLptKnownPorts[] =
{
    { "LPT1", 7, 0x378 },
    { "LPT2", 5, 0x278 },
    { "LPT1", 2, 0x3BC },
    /* must not contain an element with IRQ=0 and IOBase=0 used to cause
     * toLPTPortName() to return the "User-defined" string for these values. */
};

/**
 * Similar to toString (KMediumType), but returns 'Differencing' for
 * normal hard disks that have a parent.
 */
QString NemuGlobal::mediumTypeString(const CMedium &medium) const
{
    if (!medium.GetParent().isNull())
    {
        Assert(medium.GetType() == KMediumType_Normal);
        return mDiskTypes_Differencing;
    }
    return gpConverter->toString(medium.GetType());
}

/**
 *  Returns the list of the standard COM port names (i.e. "COMx").
 */
QStringList NemuGlobal::COMPortNames() const
{
    QStringList list;
    for (size_t i = 0; i < RT_ELEMENTS (kComKnownPorts); ++ i)
        list << kComKnownPorts [i].name;

    return list;
}

/**
 *  Returns the list of the standard LPT port names (i.e. "LPTx").
 */
QStringList NemuGlobal::LPTPortNames() const
{
    QStringList list;
    for (size_t i = 0; i < RT_ELEMENTS (kLptKnownPorts); ++ i)
        list << kLptKnownPorts [i].name;

    return list;
}

/**
 *  Returns the name of the standard COM port corresponding to the given
 *  parameters, or "User-defined" (which is also returned when both
 *  @a aIRQ and @a aIOBase are 0).
 */
QString NemuGlobal::toCOMPortName (ulong aIRQ, ulong aIOBase) const
{
    for (size_t i = 0; i < RT_ELEMENTS (kComKnownPorts); ++ i)
        if (kComKnownPorts [i].IRQ == aIRQ &&
            kComKnownPorts [i].IOBase == aIOBase)
            return kComKnownPorts [i].name;

    return mUserDefinedPortName;
}

/**
 *  Returns the name of the standard LPT port corresponding to the given
 *  parameters, or "User-defined" (which is also returned when both
 *  @a aIRQ and @a aIOBase are 0).
 */
QString NemuGlobal::toLPTPortName (ulong aIRQ, ulong aIOBase) const
{
    for (size_t i = 0; i < RT_ELEMENTS (kLptKnownPorts); ++ i)
        if (kLptKnownPorts [i].IRQ == aIRQ &&
            kLptKnownPorts [i].IOBase == aIOBase)
            return kLptKnownPorts [i].name;

    return mUserDefinedPortName;
}

/**
 *  Returns port parameters corresponding to the given standard COM name.
 *  Returns @c true on success, or @c false if the given port name is not one
 *  of the standard names (i.e. "COMx").
 */
bool NemuGlobal::toCOMPortNumbers (const QString &aName, ulong &aIRQ,
                                   ulong &aIOBase) const
{
    for (size_t i = 0; i < RT_ELEMENTS (kComKnownPorts); ++ i)
        if (strcmp (kComKnownPorts [i].name, aName.toUtf8().data()) == 0)
        {
            aIRQ = kComKnownPorts [i].IRQ;
            aIOBase = kComKnownPorts [i].IOBase;
            return true;
        }

    return false;
}

/**
 *  Returns port parameters corresponding to the given standard LPT name.
 *  Returns @c true on success, or @c false if the given port name is not one
 *  of the standard names (i.e. "LPTx").
 */
bool NemuGlobal::toLPTPortNumbers (const QString &aName, ulong &aIRQ,
                                   ulong &aIOBase) const
{
    for (size_t i = 0; i < RT_ELEMENTS (kLptKnownPorts); ++ i)
        if (strcmp (kLptKnownPorts [i].name, aName.toUtf8().data()) == 0)
        {
            aIRQ = kLptKnownPorts [i].IRQ;
            aIOBase = kLptKnownPorts [i].IOBase;
            return true;
        }

    return false;
}

QString NemuGlobal::details(const CMedium &cmedium, bool fPredictDiff, bool fUseHtml /* = true*/)
{
    /* Search for corresponding UI medium: */
    const QString strMediumID = cmedium.isNull() ? UIMedium::nullID() : cmedium.GetId();
    UIMedium uimedium = medium(strMediumID);
    if (!cmedium.isNull() && uimedium.isNull())
    {
        /* UI medium may be new and not among our mediums, request enumeration: */
        startMediumEnumeration();

        /* Search for corresponding UI medium again: */

        uimedium = medium(strMediumID);
        if (uimedium.isNull())
        {
            /* Medium might be deleted already, return null string: */
            return QString();
        }
    }

    /* Return UI medium details: */
    return fUseHtml ? uimedium.detailsHTML(true /* aNoDiffs */, fPredictDiff) :
                      uimedium.details(true /* aNoDiffs */, fPredictDiff);
}

/**
 *  Returns the details of the given USB device as a single-line string.
 */
QString NemuGlobal::details (const CUSBDevice &aDevice) const
{
    QString sDetails;
    if (aDevice.isNull())
        sDetails = tr("Unknown device", "USB device details");
    else
    {
        QVector<QString> devInfoVector = aDevice.GetDeviceInfo();
        QString m;
        QString p;

        if (devInfoVector.size() >= 1)
            m = devInfoVector[0].trimmed();
        if (devInfoVector.size() >= 2)
            p = devInfoVector[1].trimmed();

        if (m.isEmpty() && p.isEmpty())
        {
            sDetails =
                tr ("Unknown device %1:%2", "USB device details")
                .arg (QString().sprintf ("%04hX", aDevice.GetVendorId()))
                .arg (QString().sprintf ("%04hX", aDevice.GetProductId()));
        }
        else
        {
            if (p.toUpper().startsWith (m.toUpper()))
                sDetails = p;
            else
                sDetails = m + " " + p;
        }
        ushort r = aDevice.GetRevision();
        if (r != 0)
            sDetails += QString().sprintf (" [%04hX]", r);
    }

    return sDetails.trimmed();
}

/**
 *  Returns the multi-line description of the given USB device.
 */
QString NemuGlobal::toolTip (const CUSBDevice &aDevice) const
{
    QString tip =
        tr ("<nobr>Vendor ID: %1</nobr><br>"
            "<nobr>Product ID: %2</nobr><br>"
            "<nobr>Revision: %3</nobr>", "USB device tooltip")
        .arg (QString().sprintf ("%04hX", aDevice.GetVendorId()))
        .arg (QString().sprintf ("%04hX", aDevice.GetProductId()))
        .arg (QString().sprintf ("%04hX", aDevice.GetRevision()));

    QString ser = aDevice.GetSerialNumber();
    if (!ser.isEmpty())
        tip += QString (tr ("<br><nobr>Serial No. %1</nobr>", "USB device tooltip"))
                        .arg (ser);

    /* add the state field if it's a host USB device */
    CHostUSBDevice hostDev (aDevice);
    if (!hostDev.isNull())
    {
        tip += QString (tr ("<br><nobr>State: %1</nobr>", "USB device tooltip"))
                        .arg (gpConverter->toString (hostDev.GetState()));
    }

    return tip;
}

/**
 *  Returns the multi-line description of the given USB filter
 */
QString NemuGlobal::toolTip (const CUSBDeviceFilter &aFilter) const
{
    QString tip;

    QString vendorId = aFilter.GetVendorId();
    if (!vendorId.isEmpty())
        tip += tr ("<nobr>Vendor ID: %1</nobr>", "USB filter tooltip")
                   .arg (vendorId);

    QString productId = aFilter.GetProductId();
    if (!productId.isEmpty())
        tip += tip.isEmpty() ? "":"<br/>" + tr ("<nobr>Product ID: %2</nobr>", "USB filter tooltip")
                                                .arg (productId);

    QString revision = aFilter.GetRevision();
    if (!revision.isEmpty())
        tip += tip.isEmpty() ? "":"<br/>" + tr ("<nobr>Revision: %3</nobr>", "USB filter tooltip")
                                                .arg (revision);

    QString product = aFilter.GetProduct();
    if (!product.isEmpty())
        tip += tip.isEmpty() ? "":"<br/>" + tr ("<nobr>Product: %4</nobr>", "USB filter tooltip")
                                                .arg (product);

    QString manufacturer = aFilter.GetManufacturer();
    if (!manufacturer.isEmpty())
        tip += tip.isEmpty() ? "":"<br/>" + tr ("<nobr>Manufacturer: %5</nobr>", "USB filter tooltip")
                                                .arg (manufacturer);

    QString serial = aFilter.GetSerialNumber();
    if (!serial.isEmpty())
        tip += tip.isEmpty() ? "":"<br/>" + tr ("<nobr>Serial No.: %1</nobr>", "USB filter tooltip")
                                                .arg (serial);

    QString port = aFilter.GetPort();
    if (!port.isEmpty())
        tip += tip.isEmpty() ? "":"<br/>" + tr ("<nobr>Port: %1</nobr>", "USB filter tooltip")
                                                .arg (port);

    /* add the state field if it's a host USB device */
    CHostUSBDevice hostDev (aFilter);
    if (!hostDev.isNull())
    {
        tip += tip.isEmpty() ? "":"<br/>" + tr ("<nobr>State: %1</nobr>", "USB filter tooltip")
                                                .arg (gpConverter->toString (hostDev.GetState()));
    }

    return tip;
}

/** Returns the multi-line description of the given CHostVideoInputDevice filter. */
QString NemuGlobal::toolTip(const CHostVideoInputDevice &webcam) const
{
    QStringList records;

    QString strName = webcam.GetName();
    if (!strName.isEmpty())
        records << strName;

    QString strPath = webcam.GetPath();
    if (!strPath.isEmpty())
        records << strPath;

    return records.join("<br>");
}

/**
 * Returns a details report on a given VM represented as a HTML table.
 *
 * @param aMachine      Machine to create a report for.
 * @param aWithLinks    @c true if section titles should be hypertext links.
 */
QString NemuGlobal::detailsReport (const CMachine &aMachine, bool aWithLinks)
{
    /* Details templates */
    static const char *sTableTpl =
        "<table border=0 cellspacing=1 cellpadding=0>%1</table>";
    static const char *sSectionHrefTpl =
        "<tr><td width=22 rowspan=%1 align=left><img width=16 height=16 src='%2'></td>"
            "<td colspan=3><b><a href='%3'><nobr>%4</nobr></a></b></td></tr>"
            "%5"
        "<tr><td colspan=3><font size=1>&nbsp;</font></td></tr>";
    static const char *sSectionBoldTpl =
        "<tr><td width=22 rowspan=%1 align=left><img width=16 height=16 src='%2'></td>"
            "<td colspan=3><!-- %3 --><b><nobr>%4</nobr></b></td></tr>"
            "%5"
        "<tr><td colspan=3><font size=1>&nbsp;</font></td></tr>";
    static const char *sSectionItemTpl1 =
        "<tr><td width=40%><nobr><i>%1</i></nobr></td><td/><td/></tr>";
    static const char *sSectionItemTpl2 =
        "<tr><td width=40%><nobr>%1:</nobr></td><td/><td>%2</td></tr>";
    static const char *sSectionItemTpl3 =
        "<tr><td width=40%><nobr>%1</nobr></td><td/><td/></tr>";

    const QString &sectionTpl = aWithLinks ? sSectionHrefTpl : sSectionBoldTpl;

    /* Compose details report */
    QString report;

    /* General */
    {
        QString item = QString (sSectionItemTpl2).arg (tr ("Name", "details report"),
                                                       aMachine.GetName())
                     + QString (sSectionItemTpl2).arg (tr ("OS Type", "details report"),
                                                       vmGuestOSTypeDescription (aMachine.GetOSTypeId()));

        report += sectionTpl
                  .arg (2 + 2) /* rows */
                  .arg (":/machine_16px.png", /* icon */
                        "#general", /* link */
                        tr ("General", "details report"), /* title */
                        item); /* items */
    }

    /* System */
    {
#ifdef NEMU_WITH_FULL_DETAILS_REPORT
        /* BIOS Settings holder */
        CBIOSSettings biosSettings = aMachine.GetBIOSSettings();
#endif /* NEMU_WITH_FULL_DETAILS_REPORT */

        /* System details row count: */
        int iRowCount = 2; /* Memory & CPU details rows initially. */

        /* Boot order */
        QString bootOrder;
        for (ulong i = 1; i <= m_nemu.GetSystemProperties().GetMaxBootPosition(); ++ i)
        {
            KDeviceType device = aMachine.GetBootOrder (i);
            if (device == KDeviceType_Null)
                continue;
            if (!bootOrder.isEmpty())
                bootOrder += ", ";
            bootOrder += gpConverter->toString (device);
        }
        if (bootOrder.isEmpty())
            bootOrder = gpConverter->toString (KDeviceType_Null);

        iRowCount += 1; /* Boot-order row. */

#ifdef NEMU_WITH_FULL_DETAILS_REPORT
        /* ACPI */
        QString acpi = biosSettings.GetACPIEnabled()
            ? tr ("Enabled", "details report (ACPI)")
            : tr ("Disabled", "details report (ACPI)");

        /* I/O APIC */
        QString ioapic = biosSettings.GetIOAPICEnabled()
            ? tr ("Enabled", "details report (I/O APIC)")
            : tr ("Disabled", "details report (I/O APIC)");

        /* PAE/NX */
        QString pae = aMachine.GetCpuProperty(KCpuPropertyType_PAE)
            ? tr ("Enabled", "details report (PAE/NX)")
            : tr ("Disabled", "details report (PAE/NX)");

        iRowCount += 3; /* Full report rows. */
#endif /* NEMU_WITH_FULL_DETAILS_REPORT */

        /* VT-x/AMD-V */
        QString virt = aMachine.GetHWVirtExProperty(KHWVirtExPropertyType_Enabled)
            ? tr ("Enabled", "details report (VT-x/AMD-V)")
            : tr ("Disabled", "details report (VT-x/AMD-V)");

        /* Nested Paging */
        QString nested = aMachine.GetHWVirtExProperty(KHWVirtExPropertyType_NestedPaging)
            ? tr ("Enabled", "details report (Nested Paging)")
            : tr ("Disabled", "details report (Nested Paging)");

        /* VT-x/AMD-V availability: */
        bool fVTxAMDVSupported = host().GetProcessorFeature(KProcessorFeature_HWVirtEx);

        if (fVTxAMDVSupported)
            iRowCount += 2; /* VT-x/AMD-V items. */

        /* Paravirtualization Interface: */
        const QString strParavirtProvider = gpConverter->toString(aMachine.GetParavirtProvider());

        iRowCount += 1; /* Paravirtualization Interface. */

        QString item = QString (sSectionItemTpl2).arg (tr ("Base Memory", "details report"),
                                                       tr ("<nobr>%1 MB</nobr>", "details report"))
                       .arg (aMachine.GetMemorySize())
                     + QString (sSectionItemTpl2).arg (tr ("Processor(s)", "details report"),
                                                       tr ("<nobr>%1</nobr>", "details report"))
                       .arg (aMachine.GetCPUCount())
                     + QString (sSectionItemTpl2).arg (tr ("Execution Cap", "details report"),
                                                       tr ("<nobr>%1%</nobr>", "details report"))
                       .arg (aMachine.GetCPUExecutionCap())
                     + QString (sSectionItemTpl2).arg (tr ("Boot Order", "details report"), bootOrder)
#ifdef NEMU_WITH_FULL_DETAILS_REPORT
                     + QString (sSectionItemTpl2).arg (tr ("ACPI", "details report"), acpi)
                     + QString (sSectionItemTpl2).arg (tr ("I/O APIC", "details report"), ioapic)
                     + QString (sSectionItemTpl2).arg (tr ("PAE/NX", "details report"), pae)
#endif /* NEMU_WITH_FULL_DETAILS_REPORT */
                     ;

        if (fVTxAMDVSupported)
                item += QString (sSectionItemTpl2).arg (tr ("VT-x/AMD-V", "details report"), virt)
                     +  QString (sSectionItemTpl2).arg (tr ("Nested Paging", "details report"), nested);

        item += QString(sSectionItemTpl2).arg(tr("Paravirtualization Interface", "details report"), strParavirtProvider);

        report += sectionTpl
                  .arg (2 + iRowCount) /* rows */
                  .arg (":/chipset_16px.png", /* icon */
                        "#system", /* link */
                        tr ("System", "details report"), /* title */
                        item); /* items */
    }

    /* Display */
    {
        /* Rows including section header and footer */
        int rows = 2;

        /* Video tab */
        QString item = QString(sSectionItemTpl2)
                       .arg(tr ("Video Memory", "details report"),
                             tr ("<nobr>%1 MB</nobr>", "details report"))
                       .arg(aMachine.GetVRAMSize());
        ++rows;

        int cGuestScreens = aMachine.GetMonitorCount();
        if (cGuestScreens > 1)
        {
            item += QString(sSectionItemTpl2)
                    .arg(tr("Screens", "details report"))
                    .arg(cGuestScreens);
            ++rows;
        }

        QString acc3d = aMachine.GetAccelerate3DEnabled() && is3DAvailable()
            ? tr ("Enabled", "details report (3D Acceleration)")
            : tr ("Disabled", "details report (3D Acceleration)");

        item += QString(sSectionItemTpl2)
                .arg(tr("3D Acceleration", "details report"), acc3d);
        ++rows;

#ifdef NEMU_WITH_VIDEOHWACCEL
        QString acc2dVideo = aMachine.GetAccelerate2DVideoEnabled()
            ? tr ("Enabled", "details report (2D Video Acceleration)")
            : tr ("Disabled", "details report (2D Video Acceleration)");

        item += QString (sSectionItemTpl2)
                .arg (tr ("2D Video Acceleration", "details report"), acc2dVideo);
        ++ rows;
#endif

        /* VRDP tab */
        CVRDEServer srv = aMachine.GetVRDEServer();
        if (!srv.isNull())
        {
            if (srv.GetEnabled())
                item += QString (sSectionItemTpl2)
                        .arg (tr ("Remote Desktop Server Port", "details report (VRDE Server)"))
                        .arg (srv.GetVRDEProperty("TCP/Ports"));
            else
                item += QString (sSectionItemTpl2)
                        .arg (tr ("Remote Desktop Server", "details report (VRDE Server)"))
                        .arg (tr ("Disabled", "details report (VRDE Server)"));
            ++ rows;
        }

        report += sectionTpl
            .arg (rows) /* rows */
            .arg (":/vrdp_16px.png", /* icon */
                  "#display", /* link */
                  tr ("Display", "details report"), /* title */
                  item); /* items */
    }

    /* Storage */
    {
        /* Rows including section header and footer */
        int rows = 2;

        QString item;

        /* Iterate over the all machine controllers: */
        CStorageControllerVector controllers = aMachine.GetStorageControllers();
        for (int i = 0; i < controllers.size(); ++i)
        {
            /* Get current controller: */
            const CStorageController &controller = controllers[i];
            /* Add controller information: */
            QString strControllerName = QApplication::translate("UIMachineSettingsStorage", "Controller: %1");
            item += QString(sSectionItemTpl3).arg(strControllerName.arg(controller.GetName()));
            ++ rows;

            /* Populate sorted map with attachments information: */
            QMap<StorageSlot,QString> attachmentsMap;
            CMediumAttachmentVector attachments = aMachine.GetMediumAttachmentsOfController(controller.GetName());
            for (int j = 0; j < attachments.size(); ++j)
            {
                /* Get current attachment: */
                const CMediumAttachment &attachment = attachments[j];
                /* Prepare current storage slot: */
                StorageSlot attachmentSlot(controller.GetBus(), attachment.GetPort(), attachment.GetDevice());
                /* Append 'device slot name' with 'device type name' for optical devices only: */
                QString strDeviceType = attachment.GetType() == KDeviceType_DVD ? tr("(Optical Drive)") : QString();
                if (!strDeviceType.isNull())
                    strDeviceType.prepend(' ');
                /* Prepare current medium object: */
                const CMedium &medium = attachment.GetMedium();
                /* Prepare information about current medium & attachment: */
                QString strAttachmentInfo = !attachment.isOk() ? QString() :
                                            QString(sSectionItemTpl2)
                                            .arg(QString("&nbsp;&nbsp;") +
                                                 gpConverter->toString(StorageSlot(controller.GetBus(),
                                                                                   attachment.GetPort(),
                                                                                   attachment.GetDevice())) + strDeviceType)
                                            .arg(details(medium, false));
                /* Insert that attachment into map: */
                if (!strAttachmentInfo.isNull())
                    attachmentsMap.insert(attachmentSlot, strAttachmentInfo);
            }

            /* Iterate over the sorted map with attachments information: */
            QMapIterator<StorageSlot,QString> it(attachmentsMap);
            while (it.hasNext())
            {
                /* Add controller information: */
                it.next();
                item += it.value();
                ++rows;
            }
        }

        if (item.isNull())
        {
            item = QString (sSectionItemTpl1)
                   .arg (tr ("Not Attached", "details report (Storage)"));
            ++ rows;
        }

        report += sectionTpl
            .arg (rows) /* rows */
            .arg (":/hd_16px.png", /* icon */
                  "#storage", /* link */
                  tr ("Storage", "details report"), /* title */
                  item); /* items */
    }

    /* Audio */
    {
        QString item;

        CAudioAdapter audio = aMachine.GetAudioAdapter();
        int rows = audio.GetEnabled() ? 3 : 2;
        if (audio.GetEnabled())
            item = QString (sSectionItemTpl2)
                   .arg (tr ("Host Driver", "details report (audio)"),
                         gpConverter->toString (audio.GetAudioDriver())) +
                   QString (sSectionItemTpl2)
                   .arg (tr ("Controller", "details report (audio)"),
                         gpConverter->toString (audio.GetAudioController()));
        else
            item = QString (sSectionItemTpl1)
                   .arg (tr ("Disabled", "details report (audio)"));

        report += sectionTpl
            .arg (rows + 1) /* rows */
            .arg (":/sound_16px.png", /* icon */
                  "#audio", /* link */
                  tr ("Audio", "details report"), /* title */
                  item); /* items */
    }

    /* Network */
    {
        QString item;

        ulong count = m_nemu.GetSystemProperties().GetMaxNetworkAdapters(KChipsetType_PIIX3);
        int rows = 2; /* including section header and footer */
        for (ulong slot = 0; slot < count; slot ++)
        {
            CNetworkAdapter adapter = aMachine.GetNetworkAdapter (slot);
            if (adapter.GetEnabled())
            {
                KNetworkAttachmentType type = adapter.GetAttachmentType();
                QString attType = gpConverter->toString (adapter.GetAdapterType())
                                  .replace (QRegExp ("\\s\\(.+\\)"), " (%1)");
                /* don't use the adapter type string for types that have
                 * an additional symbolic network/interface name field, use
                 * this name instead */
                if (type == KNetworkAttachmentType_Bridged)
                    attType = attType.arg (tr ("Bridged adapter, %1",
                        "details report (network)").arg (adapter.GetBridgedInterface()));
                else if (type == KNetworkAttachmentType_Internal)
                    attType = attType.arg (tr ("Internal network, '%1'",
                        "details report (network)").arg (adapter.GetInternalNetwork()));
                else if (type == KNetworkAttachmentType_HostOnly)
                    attType = attType.arg (tr ("Host-only adapter, '%1'",
                        "details report (network)").arg (adapter.GetHostOnlyInterface()));
                else if (type == KNetworkAttachmentType_Generic)
                    attType = attType.arg (tr ("Generic, '%1'",
                        "details report (network)").arg (adapter.GetGenericDriver()));
                else if (type == KNetworkAttachmentType_NATNetwork)
                    attType = attType.arg (tr ("NAT network, '%1'",
                        "details report (network)").arg (adapter.GetNATNetwork()));
                else
                    attType = attType.arg (gpConverter->toString (type));

                item += QString (sSectionItemTpl2)
                        .arg (tr ("Adapter %1", "details report (network)")
                              .arg (adapter.GetSlot() + 1))
                        .arg (attType);
                ++ rows;
            }
        }
        if (item.isNull())
        {
            item = QString (sSectionItemTpl1)
                   .arg (tr ("Disabled", "details report (network)"));
            ++ rows;
        }

        report += sectionTpl
            .arg (rows) /* rows */
            .arg (":/nw_16px.png", /* icon */
                  "#network", /* link */
                  tr ("Network", "details report"), /* title */
                  item); /* items */
    }

    /* Serial Ports */
    {
        QString item;

        ulong count = m_nemu.GetSystemProperties().GetSerialPortCount();
        int rows = 2; /* including section header and footer */
        for (ulong slot = 0; slot < count; slot ++)
        {
            CSerialPort port = aMachine.GetSerialPort (slot);
            if (port.GetEnabled())
            {
                KPortMode mode = port.GetHostMode();
                QString data =
                    toCOMPortName (port.GetIRQ(), port.GetIOBase()) + ", ";
                if (mode == KPortMode_HostPipe ||
                    mode == KPortMode_HostDevice ||
                    mode == KPortMode_TCP ||
                    mode == KPortMode_RawFile)
                    data += QString ("%1 (<nobr>%2</nobr>)")
                            .arg (gpConverter->toString (mode))
                            .arg (QDir::toNativeSeparators (port.GetPath()));
                else
                    data += gpConverter->toString (mode);

                item += QString (sSectionItemTpl2)
                        .arg (tr ("Port %1", "details report (serial ports)")
                              .arg (port.GetSlot() + 1))
                        .arg (data);
                ++ rows;
            }
        }
        if (item.isNull())
        {
            item = QString (sSectionItemTpl1)
                   .arg (tr ("Disabled", "details report (serial ports)"));
            ++ rows;
        }

        report += sectionTpl
            .arg (rows) /* rows */
            .arg (":/serial_port_16px.png", /* icon */
                  "#serialPorts", /* link */
                  tr ("Serial Ports", "details report"), /* title */
                  item); /* items */
    }

    /* Parallel Ports */
    {
        QString item;

        ulong count = m_nemu.GetSystemProperties().GetParallelPortCount();
        int rows = 2; /* including section header and footer */
        for (ulong slot = 0; slot < count; slot ++)
        {
            CParallelPort port = aMachine.GetParallelPort (slot);
            if (port.GetEnabled())
            {
                QString data =
                    toLPTPortName (port.GetIRQ(), port.GetIOBase()) +
                    QString (" (<nobr>%1</nobr>)")
                    .arg (QDir::toNativeSeparators (port.GetPath()));

                item += QString (sSectionItemTpl2)
                        .arg (tr ("Port %1", "details report (parallel ports)")
                              .arg (port.GetSlot() + 1))
                        .arg (data);
                ++ rows;
            }
        }
        if (item.isNull())
        {
            item = QString (sSectionItemTpl1)
                   .arg (tr ("Disabled", "details report (parallel ports)"));
            ++ rows;
        }

        /* Temporary disabled */
        QString dummy = sectionTpl /* report += sectionTpl */
            .arg (rows) /* rows */
            .arg (":/parallel_port_16px.png", /* icon */
                  "#parallelPorts", /* link */
                  tr ("Parallel Ports", "details report"), /* title */
                  item); /* items */
    }

    /* USB */
    {
        QString item;

        CUSBDeviceFilters flts = aMachine.GetUSBDeviceFilters();
        if (   !flts.isNull()
            && aMachine.GetUSBProxyAvailable())
        {
            /* The USB controller may be unavailable (i.e. in VirtualBox OSE): */
            if (aMachine.GetUSBControllers().isEmpty())
                item = QString(sSectionItemTpl1).arg(tr("Disabled", "details report (USB)"));
            else
            {
                CUSBDeviceFilterVector coll = flts.GetDeviceFilters();
                uint active = 0;
                for (int i = 0; i < coll.size(); ++i)
                    if (coll[i].GetActive())
                        active ++;

                item = QString (sSectionItemTpl2)
                       .arg (tr ("Device Filters", "details report (USB)"),
                             tr ("%1 (%2 active)", "details report (USB)")
                                 .arg (coll.size()).arg (active));
            }

            report += sectionTpl
                .arg (2 + 1) /* rows */
                .arg (":/usb_16px.png", /* icon */
                      "#usb", /* link */
                      tr ("USB", "details report"), /* title */
                      item); /* items */
        }
    }

    /* Shared Folders */
    {
        QString item;

        ulong count = aMachine.GetSharedFolders().size();
        if (count > 0)
        {
            item = QString (sSectionItemTpl2)
                   .arg (tr ("Shared Folders", "details report (shared folders)"))
                   .arg (count);
        }
        else
            item = QString (sSectionItemTpl1)
                   .arg (tr ("None", "details report (shared folders)"));

        report += sectionTpl
            .arg (2 + 1) /* rows */
            .arg (":/sf_16px.png", /* icon */
                  "#sfolders", /* link */
                  tr ("Shared Folders", "details report"), /* title */
                  item); /* items */
    }

    return QString (sTableTpl). arg (report);
}

CSession NemuGlobal::openSession(const QString &strId, KLockType lockType /* = KLockType_Shared */)
{
    /* Prepare session: */
    CSession session;

    /* Simulate try-catch block: */
    bool fSuccess = false;
    do
    {
        /* Create empty session instance: */
        session.createInstance(CLSID_Session);
        if (session.isNull())
        {
            msgCenter().cannotOpenSession(session);
            break;
        }

        /* Search for the corresponding machine: */
        CMachine machine = m_nemu.FindMachine(strId);
        if (machine.isNull())
        {
            msgCenter().cannotFindMachineById(m_nemu, strId);
            break;
        }

        if (lockType == KLockType_VM)
            session.SetName("GUI/Qt");

        /* Lock found machine to session: */
        machine.LockMachine(session, lockType);
        if (!machine.isOk())
        {
            msgCenter().cannotOpenSession(machine);
            break;
        }

        /* Pass the language ID as the property to the guest: */
        if (session.GetType() == KSessionType_Shared)
        {
            CMachine startedMachine = session.GetMachine();
            /* Make sure that the language is in two letter code.
             * Note: if languageId() returns an empty string lang.name() will
             * return "C" which is an valid language code. */
            QLocale lang(NemuGlobal::languageId());
            startedMachine.SetGuestPropertyValue("/VirtualBox/HostInfo/GUI/LanguageID", lang.name());
        }

        /* Success finally: */
        fSuccess = true;
    }
    while (0);
    /* Cleanup try-catch block: */
    if (!fSuccess)
        session.detach();

    /* Return session: */
    return session;
}

void NemuGlobal::createMedium(const UIMedium &medium)
{
    if (m_mediumEnumeratorDtorRwLock.tryLockForRead())
    {
        /* Create medium in medium-enumerator: */
        if (m_pMediumEnumerator)
            m_pMediumEnumerator->createMedium(medium);
        m_mediumEnumeratorDtorRwLock.unlock();
    }
}

void NemuGlobal::deleteMedium(const QString &strMediumID)
{
    if (m_mediumEnumeratorDtorRwLock.tryLockForRead())
    {
        /* Delete medium from medium-enumerator: */
        if (m_pMediumEnumerator)
            m_pMediumEnumerator->deleteMedium(strMediumID);
        m_mediumEnumeratorDtorRwLock.unlock();
    }
}

/* Open some external medium using file open dialog
 * and temporary cache (enumerate) it in GUI inner mediums cache: */
QString NemuGlobal::openMediumWithFileOpenDialog(UIMediumType mediumType, QWidget *pParent,
                                                 const QString &strDefaultFolder /* = QString() */,
                                                 bool fUseLastFolder /* = false */)
{
    /* Initialize variables: */
    QList < QPair <QString, QString> > filters;
    QStringList backends;
    QStringList prefixes;
    QString strFilter;
    QString strTitle;
    QString allType;
    QString strLastFolder;
    switch (mediumType)
    {
        case UIMediumType_HardDisk:
        {
            filters = nemuGlobal().HDDBackends();
            strTitle = tr("Please choose a virtual hard disk file");
            allType = tr("All virtual hard disk files (%1)");
            strLastFolder = gEDataManager->recentFolderForHardDrives();
            if (strLastFolder.isEmpty())
                strLastFolder = gEDataManager->recentFolderForOpticalDisks();
            if (strLastFolder.isEmpty())
                strLastFolder = gEDataManager->recentFolderForFloppyDisks();
            break;
        }
        case UIMediumType_DVD:
        {
            filters = nemuGlobal().DVDBackends();
            strTitle = tr("Please choose a virtual optical disk file");
            allType = tr("All virtual optical disk files (%1)");
            strLastFolder = gEDataManager->recentFolderForOpticalDisks();
            if (strLastFolder.isEmpty())
                strLastFolder = gEDataManager->recentFolderForFloppyDisks();
            if (strLastFolder.isEmpty())
                strLastFolder = gEDataManager->recentFolderForHardDrives();
            break;
        }
        case UIMediumType_Floppy:
        {
            filters = nemuGlobal().FloppyBackends();
            strTitle = tr("Please choose a virtual floppy disk file");
            allType = tr("All virtual floppy disk files (%1)");
            strLastFolder = gEDataManager->recentFolderForFloppyDisks();
            if (strLastFolder.isEmpty())
                strLastFolder = gEDataManager->recentFolderForOpticalDisks();
            if (strLastFolder.isEmpty())
                strLastFolder = gEDataManager->recentFolderForHardDrives();
            break;
        }
        default:
            break;
    }
    QString strHomeFolder = fUseLastFolder && !strLastFolder.isEmpty() ? strLastFolder :
                            strDefaultFolder.isEmpty() ? nemuGlobal().homeFolder() : strDefaultFolder;

    /* Prepare filters and backends: */
    for (int i = 0; i < filters.count(); ++i)
    {
        /* Get iterated filter: */
        QPair <QString, QString> item = filters.at(i);
        /* Create one backend filter string: */
        backends << QString("%1 (%2)").arg(item.first).arg(item.second);
        /* Save the suffix's for the "All" entry: */
        prefixes << item.second;
    }
    if (!prefixes.isEmpty())
        backends.insert(0, allType.arg(prefixes.join(" ").trimmed()));
    backends << tr("All files (*)");
    strFilter = backends.join(";;").trimmed();

    /* Create open file dialog: */
    QStringList files = QIFileDialog::getOpenFileNames(strHomeFolder, strFilter, pParent, strTitle, 0, true, true);

    /* If dialog has some result: */
    if (!files.empty() && !files[0].isEmpty())
        return openMedium(mediumType, files[0], pParent);

    return QString();
}

QString NemuGlobal::openMedium(UIMediumType mediumType, QString strMediumLocation, QWidget *pParent /* = 0*/)
{
    /* Convert to native separators: */
    strMediumLocation = QDir::toNativeSeparators(strMediumLocation);

    /* Initialize variables: */
    CVirtualBox nemu = virtualBox();

    /* Remember the path of the last chosen medium: */
    switch (mediumType)
    {
        case UIMediumType_HardDisk: gEDataManager->setRecentFolderForHardDrives(QFileInfo(strMediumLocation).absolutePath()); break;
        case UIMediumType_DVD:      gEDataManager->setRecentFolderForOpticalDisks(QFileInfo(strMediumLocation).absolutePath()); break;
        case UIMediumType_Floppy:   gEDataManager->setRecentFolderForFloppyDisks(QFileInfo(strMediumLocation).absolutePath()); break;
        default: break;
    }

    /* Update recently used list: */
    QStringList recentMediumList;
    switch (mediumType)
    {
        case UIMediumType_HardDisk: recentMediumList = gEDataManager->recentListOfHardDrives(); break;
        case UIMediumType_DVD:      recentMediumList = gEDataManager->recentListOfOpticalDisks(); break;
        case UIMediumType_Floppy:   recentMediumList = gEDataManager->recentListOfFloppyDisks(); break;
        default: break;
    }
    if (recentMediumList.contains(strMediumLocation))
        recentMediumList.removeAll(strMediumLocation);
    recentMediumList.prepend(strMediumLocation);
    while(recentMediumList.size() > 5) recentMediumList.removeLast();
    switch (mediumType)
    {
        case UIMediumType_HardDisk: gEDataManager->setRecentListOfHardDrives(recentMediumList); break;
        case UIMediumType_DVD:      gEDataManager->setRecentListOfOpticalDisks(recentMediumList); break;
        case UIMediumType_Floppy:   gEDataManager->setRecentListOfFloppyDisks(recentMediumList); break;
        default: break;
    }

    /* Open corresponding medium: */
    CMedium cmedium = nemu.OpenMedium(strMediumLocation, mediumTypeToGlobal(mediumType), KAccessMode_ReadWrite, false);

    if (nemu.isOk())
    {
        /* Prepare nemu medium wrapper: */
        UIMedium uimedium = medium(cmedium.GetId());

        /* First of all we should test if that medium already opened: */
        if (uimedium.isNull())
        {
            /* And create new otherwise: */
            uimedium = UIMedium(cmedium, mediumType, KMediumState_Created);
            nemuGlobal().createMedium(uimedium);
        }

        /* Return uimedium id: */
        return uimedium.id();
    }
    else
        msgCenter().cannotOpenMedium(nemu, mediumType, strMediumLocation, pParent);

    return QString();
}

void NemuGlobal::startMediumEnumeration(bool fForceStart /* = true*/)
{
    /* Make sure NemuGlobal is already valid: */
    AssertReturnVoid(mValid);

    /* Make sure medium-enumerator is already created: */
    if (!m_pMediumEnumerator)
        return;

    /* Make sure enumeration is not already started: */
    if (isMediumEnumerationInProgress())
        return;

    /* Ignore the request during NemuGlobal cleanup: */
    if (m_sfCleanupInProgress)
        return;

    /* If asked to restore snapshot, don't do this till *after* we're done
     * restoring or the code with have a heart attack. */
    if (shouldRestoreCurrentSnapshot())
        return;

    /* Developer doesn't want any unnecessary media caching! */
    if (!fForceStart && !agressiveCaching())
        return;

    if (m_mediumEnumeratorDtorRwLock.tryLockForRead())
    {
        /* Redirect request to medium-enumerator: */
        if (m_pMediumEnumerator)
            m_pMediumEnumerator->enumerateMediums();
        m_mediumEnumeratorDtorRwLock.unlock();
    }
}

bool NemuGlobal::isMediumEnumerationInProgress() const
{
    /* Redirect request to medium-enumerator: */
    return m_pMediumEnumerator
        && m_pMediumEnumerator->isMediumEnumerationInProgress();
}

UIMedium NemuGlobal::medium(const QString &strMediumID) const
{
    if (m_mediumEnumeratorDtorRwLock.tryLockForRead())
    {
        /* Redirect call to medium-enumerator: */
        UIMedium result;
        if (m_pMediumEnumerator)
            result = m_pMediumEnumerator->medium(strMediumID);
        m_mediumEnumeratorDtorRwLock.unlock();
        return result;
    }
    return UIMedium();
}

QList<QString> NemuGlobal::mediumIDs() const
{
    if (m_mediumEnumeratorDtorRwLock.tryLockForRead())
    {
        /* Redirect call to medium-enumerator: */
        QList<QString> result;
        if (m_pMediumEnumerator)
            result = m_pMediumEnumerator->mediumIDs();
        m_mediumEnumeratorDtorRwLock.unlock();
        return result;
    }
    return QList<QString>();
}

void NemuGlobal::prepareStorageMenu(QMenu &menu,
                                    QObject *pListener, const char *pszSlotName,
                                    const CMachine &machine, const QString &strControllerName, const StorageSlot &storageSlot)
{
    /* Current attachment attributes: */
    const CMediumAttachment currentAttachment = machine.GetMediumAttachment(strControllerName, storageSlot.port, storageSlot.device);
    const CMedium currentMedium = currentAttachment.GetMedium();
    const QString strCurrentID = currentMedium.isNull() ? QString() : currentMedium.GetId();
    const QString strCurrentLocation = currentMedium.isNull() ? QString() : currentMedium.GetLocation();

    /* Other medium-attachments of same machine: */
    const CMediumAttachmentVector attachments = machine.GetMediumAttachments();

    /* Determine device & medium types: */
    const UIMediumType mediumType = mediumTypeToLocal(currentAttachment.GetType());
    AssertMsgReturnVoid(mediumType != UIMediumType_Invalid, ("Incorrect storage medium type!\n"));


    /* Prepare open-existing-medium action: */
    QAction *pActionOpenExistingMedium = menu.addAction(UIIconPool::iconSet(":/select_file_16px.png"), QString(), pListener, pszSlotName);
    pActionOpenExistingMedium->setData(QVariant::fromValue(UIMediumTarget(strControllerName, currentAttachment.GetPort(), currentAttachment.GetDevice(),
                                                                          mediumType)));
    pActionOpenExistingMedium->setText(QApplication::translate("UIMachineSettingsStorage", "Choose disk image...", "This is used for hard disks, optical media and floppies"));


    /* Insert separator: */
    menu.addSeparator();


    /* Get existing-host-drive vector: */
    CMediumVector mediums;
    switch (mediumType)
    {
        case UIMediumType_DVD:    mediums = nemuGlobal().host().GetDVDDrives(); break;
        case UIMediumType_Floppy: mediums = nemuGlobal().host().GetFloppyDrives(); break;
        default: break;
    }
    /* Prepare choose-existing-host-drive actions: */
    foreach (const CMedium &medium, mediums)
    {
        /* Make sure host-drive usage is unique: */
        bool fIsHostDriveUsed = false;
        foreach (const CMediumAttachment &otherAttachment, attachments)
        {
            if (otherAttachment != currentAttachment)
            {
                const CMedium &otherMedium = otherAttachment.GetMedium();
                if (!otherMedium.isNull() && otherMedium.GetId() == medium.GetId())
                {
                    fIsHostDriveUsed = true;
                    break;
                }
            }
        }
        /* If host-drives usage is unique: */
        if (!fIsHostDriveUsed)
        {
            QAction *pActionChooseHostDrive = menu.addAction(UIMedium(medium, mediumType).name(), pListener, pszSlotName);
            pActionChooseHostDrive->setCheckable(true);
            pActionChooseHostDrive->setChecked(!currentMedium.isNull() && medium.GetId() == strCurrentID);
            pActionChooseHostDrive->setData(QVariant::fromValue(UIMediumTarget(strControllerName, currentAttachment.GetPort(), currentAttachment.GetDevice(),
                                                                               mediumType, UIMediumTarget::UIMediumTargetType_WithID, medium.GetId())));
        }
    }


    /* Get recent-medium list: */
    QStringList recentMediumList;
    QStringList recentMediumListUsed;
    switch (mediumType)
    {
        case UIMediumType_HardDisk: recentMediumList = gEDataManager->recentListOfHardDrives(); break;
        case UIMediumType_DVD:      recentMediumList = gEDataManager->recentListOfOpticalDisks(); break;
        case UIMediumType_Floppy:   recentMediumList = gEDataManager->recentListOfFloppyDisks(); break;
        default: break;
    }
    /* Prepare choose-recent-medium actions: */
    foreach (const QString &strRecentMediumLocationBase, recentMediumList)
    {
        /* Confirm medium uniqueness: */
        if (recentMediumListUsed.contains(strRecentMediumLocationBase))
            continue;
        /* Mark medium as known: */
        recentMediumListUsed << strRecentMediumLocationBase;
        /* Convert separators to native: */
        const QString strRecentMediumLocation = QDir::toNativeSeparators(strRecentMediumLocationBase);
        /* Confirm medium presence: */
        if (!QFile::exists(strRecentMediumLocation))
            continue;
        /* Make sure recent-medium usage is unique: */
        bool fIsRecentMediumUsed = false;
        foreach (const CMediumAttachment &otherAttachment, attachments)
        {
            if (otherAttachment != currentAttachment)
            {
                const CMedium &otherMedium = otherAttachment.GetMedium();
                if (!otherMedium.isNull() && otherMedium.GetLocation() == strRecentMediumLocation)
                {
                    fIsRecentMediumUsed = true;
                    break;
                }
            }
        }
        /* If recent-medium usage is unique: */
        if (!fIsRecentMediumUsed)
        {
            QAction *pActionChooseRecentMedium = menu.addAction(QFileInfo(strRecentMediumLocation).fileName(), pListener, pszSlotName);
            pActionChooseRecentMedium->setCheckable(true);
            pActionChooseRecentMedium->setChecked(!currentMedium.isNull() && strRecentMediumLocation == strCurrentLocation);
            pActionChooseRecentMedium->setData(QVariant::fromValue(UIMediumTarget(strControllerName, currentAttachment.GetPort(), currentAttachment.GetDevice(),
                                                                                  mediumType, UIMediumTarget::UIMediumTargetType_WithLocation, strRecentMediumLocation)));
            pActionChooseRecentMedium->setToolTip(strRecentMediumLocation);
        }
    }


    /* Last action for optical/floppy attachments only: */
    if (mediumType == UIMediumType_DVD || mediumType == UIMediumType_Floppy)
    {
        /* Insert separator: */
        menu.addSeparator();

        /* Prepare unmount-current-medium action: */
        QAction *pActionUnmountMedium = menu.addAction(QString(), pListener, pszSlotName);
        pActionUnmountMedium->setEnabled(!currentMedium.isNull());
        pActionUnmountMedium->setData(QVariant::fromValue(UIMediumTarget(strControllerName, currentAttachment.GetPort(), currentAttachment.GetDevice())));
        pActionUnmountMedium->setText(QApplication::translate("UIMachineSettingsStorage", "Remove disk from virtual drive"));
        if (mediumType == UIMediumType_DVD)
            pActionUnmountMedium->setIcon(UIIconPool::iconSet(":/cd_unmount_16px.png", ":/cd_unmount_disabled_16px.png"));
        else if (mediumType == UIMediumType_Floppy)
            pActionUnmountMedium->setIcon(UIIconPool::iconSet(":/fd_unmount_16px.png", ":/fd_unmount_disabled_16px.png"));
    }
}

void NemuGlobal::updateMachineStorage(const CMachine &constMachine, const UIMediumTarget &target)
{
    /* Mount (by default): */
    bool fMount = true;
    /* Null medium (by default): */
    CMedium cmedium;
    /* With null ID (by default): */
    QString strActualID;

    /* Current mount-target attributes: */
    const CStorageController currentController = constMachine.GetStorageControllerByName(target.name);
    const KStorageBus currentStorageBus = currentController.GetBus();
    const CMediumAttachment currentAttachment = constMachine.GetMediumAttachment(target.name, target.port, target.device);
    const CMedium currentMedium = currentAttachment.GetMedium();
    const QString strCurrentID = currentMedium.isNull() ? QString() : currentMedium.GetId();
    const QString strCurrentLocation = currentMedium.isNull() ? QString() : currentMedium.GetLocation();

    /* Which additional info do we have? */
    switch (target.type)
    {
        /* Do we have an exact ID? */
        case UIMediumTarget::UIMediumTargetType_WithID:
        {
            /* New mount-target attributes: */
            QString strNewID;

            /* Invoke file-open dialog to choose medium ID: */
            if (target.mediumType != UIMediumType_Invalid && target.data.isNull())
            {
                /* Keyboard can be captured by machine-view.
                 * So we should clear machine-view focus to let file-open dialog get it.
                 * That way the keyboard will be released too.. */
                QWidget *pLastFocusedWidget = 0;
                if (QApplication::focusWidget())
                {
                    pLastFocusedWidget = QApplication::focusWidget();
                    pLastFocusedWidget->clearFocus();
                }
                /* Call for file-open dialog: */
                const QString strMachineFolder(QFileInfo(constMachine.GetSettingsFilePath()).absolutePath());
                const QString strMediumID = nemuGlobal().openMediumWithFileOpenDialog(target.mediumType, windowManager().mainWindowShown(),
                                                                                      strMachineFolder);
                /* Return focus back: */
                if (pLastFocusedWidget)
                    pLastFocusedWidget->setFocus();
                /* Accept new medium ID: */
                if (!strMediumID.isNull())
                    strNewID = strMediumID;
                /* Else just exit: */
                else return;
            }
            /* Use medium ID which was passed: */
            else if (!target.data.isNull() && target.data != strCurrentID)
                strNewID = target.data;

            /* Should we mount or unmount? */
            fMount = !strNewID.isEmpty();

            /* Prepare target medium: */
            const UIMedium uimedium = nemuGlobal().medium(strNewID);
            cmedium = uimedium.medium();
            strActualID = fMount ? strNewID : strCurrentID;
            break;
        }
        /* Do we have a resent location? */
        case UIMediumTarget::UIMediumTargetType_WithLocation:
        {
            /* Open medium by location and get new medium ID if any: */
            const QString strNewID = nemuGlobal().openMedium(target.mediumType, target.data);
            /* Else just exit: */
            if (strNewID.isEmpty())
                return;

            /* Should we mount or unmount? */
            fMount = strNewID != strCurrentID;

            /* Prepare target medium: */
            const UIMedium uimedium = fMount ? nemuGlobal().medium(strNewID) : UIMedium();
            cmedium = fMount ? uimedium.medium() : CMedium();
            strActualID = fMount ? strNewID : strCurrentID;
            break;
        }
    }

    /* Do not unmount hard-drives: */
    if (target.mediumType == UIMediumType_HardDisk && !fMount)
        return;

    /* Get editable machine: */
    CSession session;
    CMachine machine = constMachine;
    KSessionState sessionState = machine.GetSessionState();
    /* Session state unlocked? */
    if (sessionState == KSessionState_Unlocked)
    {
        /* Open own 'write' session: */
        session = openSession(machine.GetId());
        AssertReturnVoid(!session.isNull());
        machine = session.GetMachine();
    }
    /* Is it Selector UI call? */
    else if (!isVMConsoleProcess())
    {
        /* Open existing 'shared' session: */
        session = openExistingSession(machine.GetId());
        AssertReturnVoid(!session.isNull());
        machine = session.GetMachine();
    }
    /* Else this is Runtime UI call
     * which has session locked for itself. */

    /* Remount medium to the predefined port/device: */
    bool fWasMounted = false;
    /* Hard drive case: */
    if (target.mediumType == UIMediumType_HardDisk)
    {
        /* Detaching: */
        machine.DetachDevice(target.name, target.port, target.device);
        fWasMounted = machine.isOk();
        if (!fWasMounted)
            msgCenter().cannotDetachDevice(machine, UIMediumType_HardDisk, strCurrentLocation,
                                           StorageSlot(currentStorageBus, target.port, target.device));
        else
        {
            /* Attaching: */
            machine.AttachDevice(target.name, target.port, target.device, KDeviceType_HardDisk, cmedium);
            fWasMounted = machine.isOk();
            if (!fWasMounted)
                msgCenter().cannotAttachDevice(machine, UIMediumType_HardDisk, strCurrentLocation,
                                               StorageSlot(currentStorageBus, target.port, target.device));
        }
    }
    /* Optical/floppy drive case: */
    else
    {
        /* Remounting: */
        machine.MountMedium(target.name, target.port, target.device, cmedium, false /* force? */);
        fWasMounted = machine.isOk();
        if (!fWasMounted)
        {
            /* Ask for force remounting: */
            if (msgCenter().cannotRemountMedium(machine, nemuGlobal().medium(strActualID),
                                                fMount, true /* retry? */))
            {
                /* Force remounting: */
                machine.MountMedium(target.name, target.port, target.device, cmedium, true /* force? */);
                fWasMounted = machine.isOk();
                if (!fWasMounted)
                    msgCenter().cannotRemountMedium(machine, nemuGlobal().medium(strActualID),
                                                    fMount, false /* retry? */);
            }
        }
        /* Mounting successful: */
        else
        {
            /* Disable First RUN Wizard: */
            if (gEDataManager->machineFirstTimeStarted(machine.GetId()))
                gEDataManager->setMachineFirstTimeStarted(false, machine.GetId());
        }
    }

    /* Save settings: */
    if (fWasMounted)
    {
        machine.SaveSettings();
        if (!machine.isOk())
            msgCenter().cannotSaveMachineSettings(machine, activeMachineWindow());
    }

    /* Close session to editable machine if necessary: */
    if (!session.isNull())
        session.UnlockMachine();
}

/**
 *  Native language name of the currently installed translation.
 *  Returns "English" if no translation is installed
 *  or if the translation file is invalid.
 */
QString NemuGlobal::languageName() const
{

    return qApp->translate ("@@@", "English",
                            "Native language name");
}

/**
 *  Native language country name of the currently installed translation.
 *  Returns "--" if no translation is installed or if the translation file is
 *  invalid, or if the language is independent on the country.
 */
QString NemuGlobal::languageCountry() const
{
    return qApp->translate ("@@@", "--",
                            "Native language country name "
                            "(empty if this language is for all countries)");
}

/**
 *  Language name of the currently installed translation, in English.
 *  Returns "English" if no translation is installed
 *  or if the translation file is invalid.
 */
QString NemuGlobal::languageNameEnglish() const
{

    return qApp->translate ("@@@", "English",
                            "Language name, in English");
}

/**
 *  Language country name of the currently installed translation, in English.
 *  Returns "--" if no translation is installed or if the translation file is
 *  invalid, or if the language is independent on the country.
 */
QString NemuGlobal::languageCountryEnglish() const
{
    return qApp->translate ("@@@", "--",
                            "Language country name, in English "
                            "(empty if native country name is empty)");
}

/**
 *  Comma-separated list of authors of the currently installed translation.
 *  Returns "Oracle Corporation" if no translation is installed or if the
 *  translation file is invalid, or if the translation is supplied by Oracle
 *  Corporation
 */
QString NemuGlobal::languageTranslators() const
{
    return qApp->translate ("@@@", "Oracle Corporation",
                            "Comma-separated list of translators");
}

/**
 *  Changes the language of all global string constants according to the
 *  currently installed translations tables.
 */
void NemuGlobal::retranslateUi()
{
    mDiskTypes_Differencing = tr ("Differencing", "DiskType");

    mUserDefinedPortName = tr ("User-defined", "serial port");

    mWarningIcon = UIIconPool::defaultIcon(UIIconPool::UIDefaultIconType_MessageBoxWarning).pixmap (16, 16);
    Assert (!mWarningIcon.isNull());

    mErrorIcon = UIIconPool::defaultIcon(UIIconPool::UIDefaultIconType_MessageBoxCritical).pixmap (16, 16);
    Assert (!mErrorIcon.isNull());

    /* Re-enumerate uimedium since they contain some translations too: */
    if (mValid)
        startMediumEnumeration();

#ifdef Q_WS_X11
    /* As X11 do not have functionality for providing human readable key names,
     * we keep a table of them, which must be updated when the language is changed. */
    UINativeHotKey::retranslateKeyNames();
#endif /* Q_WS_X11 */
}

// public static stuff
////////////////////////////////////////////////////////////////////////////////

/* static */
bool NemuGlobal::isDOSType (const QString &aOSTypeId)
{
    if (aOSTypeId.left (3) == "dos" ||
        aOSTypeId.left (3) == "win" ||
        aOSTypeId.left (3) == "os2")
        return true;

    return false;
}

const char *gNemuLangSubDir = "/nls";
const char *gNemuLangFileBase = "VirtualBox_";
const char *gNemuLangFileExt = ".qm";
const char *gNemuLangIDRegExp = "(([a-z]{2})(?:_([A-Z]{2}))?)|(C)";
const char *gNemuBuiltInLangName   = "C";

class NemuTranslator : public QTranslator
{
public:

    NemuTranslator (QObject *aParent = 0)
        : QTranslator (aParent) {}

    bool loadFile (const QString &aFileName)
    {
        QFile file (aFileName);
        if (!file.open (QIODevice::ReadOnly))
            return false;
        mData = file.readAll();
        return load ((uchar*) mData.data(), mData.size());
    }

private:

    QByteArray mData;
};

static NemuTranslator *sTranslator = 0;
static QString sLoadedLangId = gNemuBuiltInLangName;

/**
 *  Returns the loaded (active) language ID.
 *  Note that it may not match with NemuGlobalSettings::languageId() if the
 *  specified language cannot be loaded.
 *  If the built-in language is active, this method returns "C".
 *
 *  @note "C" is treated as the built-in language for simplicity -- the C
 *  locale is used in unix environments as a fallback when the requested
 *  locale is invalid. This way we don't need to process both the "built_in"
 *  language and the "C" language (which is a valid environment setting)
 *  separately.
 */
/* static */
QString NemuGlobal::languageId()
{
    return sLoadedLangId;
}

/**
 *  Loads the language by language ID.
 *
 *  @param aLangId Language ID in in form of xx_YY. QString::null means the
 *                 system default language.
 */
/* static */
void NemuGlobal::loadLanguage (const QString &aLangId)
{
    QString langId = aLangId.isEmpty() ?
        NemuGlobal::systemLanguageId() : aLangId;
    QString languageFileName;
    QString selectedLangId = gNemuBuiltInLangName;

    /* If C is selected we change it temporary to en. This makes sure any extra
     * "en" translation file will be loaded. This is necessary for loading the
     * plural forms of some of our translations. */
    bool fResetToC = false;
    if (langId == "C")
    {
        langId = "en";
        fResetToC = true;
    }

    char szNlsPath[RTPATH_MAX];
    int rc;

    rc = RTPathAppPrivateNoArch(szNlsPath, sizeof(szNlsPath));
    AssertRC (rc);

    QString nlsPath = QString(szNlsPath) + gNemuLangSubDir;
    QDir nlsDir (nlsPath);

    Assert (!langId.isEmpty());
    if (!langId.isEmpty() && langId != gNemuBuiltInLangName)
    {
        QRegExp regExp (gNemuLangIDRegExp);
        int pos = regExp.indexIn (langId);
        /* the language ID should match the regexp completely */
        AssertReturnVoid (pos == 0);

        QString lang = regExp.cap (2);

        if (nlsDir.exists (gNemuLangFileBase + langId + gNemuLangFileExt))
        {
            languageFileName = nlsDir.absoluteFilePath (gNemuLangFileBase + langId +
                                                        gNemuLangFileExt);
            selectedLangId = langId;
        }
        else if (nlsDir.exists (gNemuLangFileBase + lang + gNemuLangFileExt))
        {
            languageFileName = nlsDir.absoluteFilePath (gNemuLangFileBase + lang +
                                                        gNemuLangFileExt);
            selectedLangId = lang;
        }
        else
        {
            /* Never complain when the default language is requested.  In any
             * case, if no explicit language file exists, we will simply
             * fall-back to English (built-in). */
            if (!aLangId.isNull() && langId != "en")
                msgCenter().cannotFindLanguage (langId, nlsPath);
            /* selectedLangId remains built-in here */
            AssertReturnVoid (selectedLangId == gNemuBuiltInLangName);
        }
    }

    /* delete the old translator if there is one */
    if (sTranslator)
    {
        /* QTranslator destructor will call qApp->removeTranslator() for
         * us. It will also delete all its child translations we attach to it
         * below, so we don't have to care about them specially. */
        delete sTranslator;
    }

    /* load new language files */
    sTranslator = new NemuTranslator (qApp);
    Assert (sTranslator);
    bool loadOk = true;
    if (sTranslator)
    {
        if (selectedLangId != gNemuBuiltInLangName)
        {
            Assert (!languageFileName.isNull());
            loadOk = sTranslator->loadFile (languageFileName);
        }
        /* we install the translator in any case: on failure, this will
         * activate an empty translator that will give us English
         * (built-in) */
        qApp->installTranslator (sTranslator);
    }
    else
        loadOk = false;

    if (loadOk)
        sLoadedLangId = selectedLangId;
    else
    {
        msgCenter().cannotLoadLanguage (languageFileName);
        sLoadedLangId = gNemuBuiltInLangName;
    }

    /* Try to load the corresponding Qt translation */
    if (sLoadedLangId != gNemuBuiltInLangName)
    {
#ifdef Q_OS_UNIX
        /* We use system installations of Qt on Linux systems, so first, try
         * to load the Qt translation from the system location. */
        languageFileName = QLibraryInfo::location(QLibraryInfo::TranslationsPath) + "/qt_" +
                           sLoadedLangId + gNemuLangFileExt;
        QTranslator *qtSysTr = new QTranslator (sTranslator);
        Assert (qtSysTr);
        if (qtSysTr && qtSysTr->load (languageFileName))
            qApp->installTranslator (qtSysTr);
        /* Note that the Qt translation supplied by Oracle is always loaded
         * afterwards to make sure it will take precedence over the system
         * translation (it may contain more decent variants of translation
         * that better correspond to VirtualBox UI). We need to load both
         * because a newer version of Qt may be installed on the user computer
         * and the Oracle version may not fully support it. We don't do it on
         * Win32 because we supply a Qt library there and therefore the
         * Oracle translation is always the best one. */
#endif
        languageFileName =  nlsDir.absoluteFilePath (QString ("qt_") +
                                                     sLoadedLangId +
                                                     gNemuLangFileExt);
        QTranslator *qtTr = new QTranslator (sTranslator);
        Assert (qtTr);
        if (qtTr && (loadOk = qtTr->load (languageFileName)))
            qApp->installTranslator (qtTr);
        /* The below message doesn't fit 100% (because it's an additional
         * language and the main one won't be reset to built-in on failure)
         * but the load failure is so rare here that it's not worth a separate
         * message (but still, having something is better than having none) */
        if (!loadOk && !aLangId.isNull())
            msgCenter().cannotLoadLanguage (languageFileName);
    }
    if (fResetToC)
        sLoadedLangId = "C";
#ifdef Q_WS_MAC
    /* Qt doesn't translate the items in the Application menu initially.
     * Manually trigger an update. */
    ::darwinRetranslateAppMenu();
#endif /* Q_WS_MAC */
}

QString NemuGlobal::helpFile() const
{
#if defined (Q_WS_WIN32)
    const QString name = "VirtualBox";
    const QString suffix = "chm";
#elif defined (Q_WS_MAC)
    const QString name = "UserManual";
    const QString suffix = "pdf";
#elif defined (Q_WS_X11)
# if defined NEMU_OSE
    const QString name = "UserManual";
    const QString suffix = "pdf";
# else
    const QString name = "VirtualBox";
    const QString suffix = "chm";
# endif
#endif
    /* Where are the docs located? */
    char szDocsPath[RTPATH_MAX];
    int rc = RTPathAppDocs (szDocsPath, sizeof (szDocsPath));
    AssertRC (rc);
    /* Make sure that the language is in two letter code.
     * Note: if languageId() returns an empty string lang.name() will
     * return "C" which is an valid language code. */
    QLocale lang (NemuGlobal::languageId());

    /* Construct the path and the filename */
    QString manual = QString ("%1/%2_%3.%4").arg (szDocsPath)
                                            .arg (name)
                                            .arg (lang.name())
                                            .arg (suffix);
    /* Check if a help file with that name exists */
    QFileInfo fi (manual);
    if (fi.exists())
        return manual;

    /* Fall back to the standard */
    manual = QString ("%1/%2.%4").arg (szDocsPath)
                                 .arg (name)
                                 .arg (suffix);
    return manual;
}

/**
 *  Replacement for QToolButton::setTextLabel() that handles the shortcut
 *  letter (if it is present in the argument string) as if it were a setText()
 *  call: the shortcut letter is used to automatically assign an "Alt+<letter>"
 *  accelerator key sequence to the given tool button.
 *
 *  @note This method preserves the icon set if it was assigned before. Only
 *  the text label and the accelerator are changed.
 *
 *  @param aToolButton  Tool button to set the text label on.
 *  @param aTextLabel   Text label to set.
 */
/* static */
void NemuGlobal::setTextLabel (QToolButton *aToolButton,
                               const QString &aTextLabel)
{
    AssertReturnVoid (aToolButton != NULL);

    /* remember the icon set as setText() will kill it */
    QIcon iset = aToolButton->icon();
    /* re-use the setText() method to detect and set the accelerator */
    aToolButton->setText (aTextLabel);
    QKeySequence accel = aToolButton->shortcut();
    aToolButton->setText (aTextLabel);
    aToolButton->setIcon (iset);
    /* set the accel last as setIconSet() would kill it */
    aToolButton->setShortcut (accel);
}

/**
 *  Performs direct and flipped search of position for \a aRectangle to make sure
 *  it is fully contained inside \a aBoundRegion region by moving & resizing
 *  \a aRectangle if necessary. Selects the minimum shifted result between direct
 *  and flipped variants.
 */
/* static */
QRect NemuGlobal::normalizeGeometry (const QRect &aRectangle, const QRegion &aBoundRegion,
                                     bool aCanResize /* = true */)
{
    /* Direct search for normalized rectangle */
    QRect var1 (getNormalized (aRectangle, aBoundRegion, aCanResize));

    /* Flipped search for normalized rectangle */
    QRect var2 (flip (getNormalized (flip (aRectangle).boundingRect(),
                                     flip (aBoundRegion), aCanResize)).boundingRect());

    /* Calculate shift from starting position for both variants */
    double length1 = sqrt (pow ((double) (var1.x() - aRectangle.x()), (double) 2) +
                           pow ((double) (var1.y() - aRectangle.y()), (double) 2));
    double length2 = sqrt (pow ((double) (var2.x() - aRectangle.x()), (double) 2) +
                           pow ((double) (var2.y() - aRectangle.y()), (double) 2));

    /* Return minimum shifted variant */
    return length1 > length2 ? var2 : var1;
}

/**
 *  Ensures that the given rectangle \a aRectangle is fully contained within the
 *  region \a aBoundRegion by moving \a aRectangle if necessary. If \a aRectangle is
 *  larger than \a aBoundRegion, top left corner of \a aRectangle is aligned with the
 *  top left corner of maximum available rectangle and, if \a aCanResize is true,
 *  \a aRectangle is shrinked to become fully visible.
 */
/* static */
QRect NemuGlobal::getNormalized (const QRect &aRectangle, const QRegion &aBoundRegion,
                                 bool /* aCanResize = true */)
{
    /* Storing available horizontal sub-rectangles & vertical shifts */
    int windowVertical = aRectangle.center().y();
    QVector <QRect> rectanglesVector (aBoundRegion.rects());
    QList <QRect> rectanglesList;
    QList <int> shiftsList;
    foreach (QRect currentItem, rectanglesVector)
    {
        int currentDelta = qAbs (windowVertical - currentItem.center().y());
        int shift2Top = currentItem.top() - aRectangle.top();
        int shift2Bot = currentItem.bottom() - aRectangle.bottom();

        int itemPosition = 0;
        foreach (QRect item, rectanglesList)
        {
            int delta = qAbs (windowVertical - item.center().y());
            if (delta > currentDelta) break; else ++ itemPosition;
        }
        rectanglesList.insert (itemPosition, currentItem);

        int shift2TopPos = 0;
        foreach (int shift, shiftsList)
            if (qAbs (shift) > qAbs (shift2Top)) break; else ++ shift2TopPos;
        shiftsList.insert (shift2TopPos, shift2Top);

        int shift2BotPos = 0;
        foreach (int shift, shiftsList)
            if (qAbs (shift) > qAbs (shift2Bot)) break; else ++ shift2BotPos;
        shiftsList.insert (shift2BotPos, shift2Bot);
    }

    /* Trying to find the appropriate place for window */
    QRect result;
    for (int i = -1; i < shiftsList.size(); ++ i)
    {
        /* Move to appropriate vertical */
        QRect rectangle (aRectangle);
        if (i >= 0) rectangle.translate (0, shiftsList [i]);

        /* Search horizontal shift */
        int maxShift = 0;
        foreach (QRect item, rectanglesList)
        {
            QRect trectangle (rectangle.translated (item.left() - rectangle.left(), 0));
            if (!item.intersects (trectangle))
                continue;

            if (rectangle.left() < item.left())
            {
                int shift = item.left() - rectangle.left();
                maxShift = qAbs (shift) > qAbs (maxShift) ? shift : maxShift;
            }
            else if (rectangle.right() > item.right())
            {
                int shift = item.right() - rectangle.right();
                maxShift = qAbs (shift) > qAbs (maxShift) ? shift : maxShift;
            }
        }

        /* Shift across the horizontal direction */
        rectangle.translate (maxShift, 0);

        /* Check the translated rectangle to feat the rules */
        if (aBoundRegion.united (rectangle) == aBoundRegion)
            result = rectangle;

        if (!result.isNull()) break;
    }

    if (result.isNull())
    {
        /* Resize window to feat desirable size
         * using max of available rectangles */
        QRect maxRectangle;
        quint64 maxSquare = 0;
        foreach (QRect item, rectanglesList)
        {
            quint64 square = item.width() * item.height();
            if (square > maxSquare)
            {
                maxSquare = square;
                maxRectangle = item;
            }
        }

        result = aRectangle;
        result.moveTo (maxRectangle.x(), maxRectangle.y());
        if (maxRectangle.right() < result.right())
            result.setRight (maxRectangle.right());
        if (maxRectangle.bottom() < result.bottom())
            result.setBottom (maxRectangle.bottom());
    }

    return result;
}

/**
 *  Returns the flipped (transposed) region.
 */
/* static */
QRegion NemuGlobal::flip (const QRegion &aRegion)
{
    QRegion result;
    QVector <QRect> rectangles (aRegion.rects());
    foreach (QRect rectangle, rectangles)
        result += QRect (rectangle.y(), rectangle.x(),
                         rectangle.height(), rectangle.width());
    return result;
}

/**
 *  Aligns the center of \a aWidget with the center of \a aRelative.
 *
 *  If necessary, \a aWidget's position is adjusted to make it fully visible
 *  within the available desktop area. If \a aWidget is bigger then this area,
 *  it will also be resized unless \a aCanResize is false or there is an
 *  inappropriate minimum size limit (in which case the top left corner will be
 *  simply aligned with the top left corner of the available desktop area).
 *
 *  \a aWidget must be a top-level widget. \a aRelative may be any widget, but
 *  if it's not top-level itself, its top-level widget will be used for
 *  calculations. \a aRelative can also be NULL, in which case \a aWidget will
 *  be centered relative to the available desktop area.
 */
/* static */
void NemuGlobal::centerWidget (QWidget *aWidget, QWidget *aRelative,
                               bool aCanResize /* = true */)
{
    AssertReturnVoid (aWidget);
    AssertReturnVoid (aWidget->isTopLevel());

    QRect deskGeo, parentGeo;
    QWidget *w = aRelative;
    if (w)
    {
        w = w->window();
        deskGeo = QApplication::desktop()->availableGeometry (w);
        parentGeo = w->frameGeometry();
        /* On X11/Gnome, geo/frameGeo.x() and y() are always 0 for top level
         * widgets with parents, what a shame. Use mapToGlobal() to workaround. */
        QPoint d = w->mapToGlobal (QPoint (0, 0));
        d.rx() -= w->geometry().x() - w->x();
        d.ry() -= w->geometry().y() - w->y();
        parentGeo.moveTopLeft (d);
    }
    else
    {
        deskGeo = QApplication::desktop()->availableGeometry();
        parentGeo = deskGeo;
    }

    /* On X11, there is no way to determine frame geometry (including WM
     * decorations) before the widget is shown for the first time. Stupidly
     * enumerate other top level widgets to find the thickest frame. The code
     * is based on the idea taken from QDialog::adjustPositionInternal(). */

    int extraw = 0, extrah = 0;

    QWidgetList list = QApplication::topLevelWidgets();
    QListIterator<QWidget*> it (list);
    while ((extraw == 0 || extrah == 0) && it.hasNext())
    {
        int framew, frameh;
        QWidget *current = it.next();
        if (!current->isVisible())
            continue;

        framew = current->frameGeometry().width() - current->width();
        frameh = current->frameGeometry().height() - current->height();

        extraw = qMax (extraw, framew);
        extrah = qMax (extrah, frameh);
    }

    /// @todo (r=dmik) not sure if we really need this
#if 0
    /* sanity check for decoration frames. With embedding, we
     * might get extraordinary values */
    if (extraw == 0 || extrah == 0 || extraw > 20 || extrah > 50)
    {
        extrah = 50;
        extraw = 20;
    }
#endif

    /* On non-X11 platforms, the following would be enough instead of the
     * above workaround: */
    // QRect geo = frameGeometry();
    QRect geo = QRect (0, 0, aWidget->width() + extraw,
                             aWidget->height() + extrah);

    geo.moveCenter (QPoint (parentGeo.x() + (parentGeo.width() - 1) / 2,
                            parentGeo.y() + (parentGeo.height() - 1) / 2));

    /* ensure the widget is within the available desktop area */
    QRect newGeo = normalizeGeometry (geo, deskGeo, aCanResize);
#ifdef Q_WS_MAC
    /* No idea why, but Qt doesn't respect if there is a unified toolbar on the
     * ::move call. So manually add the height of the toolbar before setting
     * the position. */
    if (w)
        newGeo.translate (0, ::darwinWindowToolBarHeight (aWidget));
#endif /* Q_WS_MAC */

    aWidget->move (newGeo.topLeft());

    if (aCanResize &&
        (geo.width() != newGeo.width() || geo.height() != newGeo.height()))
        aWidget->resize (newGeo.width() - extraw, newGeo.height() - extrah);
}

/**
 *  Returns the decimal separator for the current locale.
 */
/* static */
QChar NemuGlobal::decimalSep()
{
    return QLocale::system().decimalPoint();
}

/**
 *  Returns the regexp string that defines the format of the human-readable
 *  size representation, <tt>####[.##] B|KB|MB|GB|TB|PB</tt>.
 *
 *  This regexp will capture 5 groups of text:
 *  - cap(1): integer number in case when no decimal point is present
 *            (if empty, it means that decimal point is present)
 *  - cap(2): size suffix in case when no decimal point is present (may be empty)
 *  - cap(3): integer number in case when decimal point is present (may be empty)
 *  - cap(4): fraction number (hundredth) in case when decimal point is present
 *  - cap(5): size suffix in case when decimal point is present (note that
 *            B cannot appear there)
 */
/* static */
QString NemuGlobal::sizeRegexp()
{
    QString regexp =
        QString ("^(?:(?:(\\d+)(?:\\s?(%2|%3|%4|%5|%6|%7))?)|(?:(\\d*)%1(\\d{1,2})(?:\\s?(%3|%4|%5|%6|%7))))$")
            .arg (decimalSep())
            .arg (tr ("B", "size suffix Bytes"))
            .arg (tr ("KB", "size suffix KBytes=1024 Bytes"))
            .arg (tr ("MB", "size suffix MBytes=1024 KBytes"))
            .arg (tr ("GB", "size suffix GBytes=1024 MBytes"))
            .arg (tr ("TB", "size suffix TBytes=1024 GBytes"))
            .arg (tr ("PB", "size suffix PBytes=1024 TBytes"));
    return regexp;
}

/**
 *  Parses the given size string that should be in form of
 *  <tt>####[.##] B|KB|MB|GB|TB|PB</tt> and returns
 *  the size value in bytes. Zero is returned on error.
 */
/* static */
quint64 NemuGlobal::parseSize (const QString &aText)
{
    QRegExp regexp (sizeRegexp());
    int pos = regexp.indexIn (aText);
    if (pos != -1)
    {
        QString intgS = regexp.cap (1);
        QString hundS;
        QString suff = regexp.cap (2);
        if (intgS.isEmpty())
        {
            intgS = regexp.cap (3);
            hundS = regexp.cap (4);
            suff = regexp.cap (5);
        }

        quint64 denom = 0;
        if (suff.isEmpty() || suff == tr ("B", "size suffix Bytes"))
            denom = 1;
        else if (suff == tr ("KB", "size suffix KBytes=1024 Bytes"))
            denom = _1K;
        else if (suff == tr ("MB", "size suffix MBytes=1024 KBytes"))
            denom = _1M;
        else if (suff == tr ("GB", "size suffix GBytes=1024 MBytes"))
            denom = _1G;
        else if (suff == tr ("TB", "size suffix TBytes=1024 GBytes"))
            denom = _1T;
        else if (suff == tr ("PB", "size suffix PBytes=1024 TBytes"))
            denom = _1P;

        quint64 intg = intgS.toULongLong();
        if (denom == 1)
            return intg;

        quint64 hund = hundS.leftJustified (2, '0').toULongLong();
        hund = hund * denom / 100;
        intg = intg * denom + hund;
        return intg;
    }
    else
        return 0;
}

/**
 * Formats the given @a aSize value in bytes to a human readable string
 * in form of <tt>####[.##] B|KB|MB|GB|TB|PB</tt>.
 *
 * The @a aMode and @a aDecimal parameters are used for rounding the resulting
 * number when converting the size value to KB, MB, etc gives a fractional part:
 * <ul>
 * <li>When \a aMode is FormatSize_Round, the result is rounded to the
 *     closest number containing \a aDecimal decimal digits.
 * </li>
 * <li>When \a aMode is FormatSize_RoundDown, the result is rounded to the
 *     largest number with \a aDecimal decimal digits that is not greater than
 *     the result. This guarantees that converting the resulting string back to
 *     the integer value in bytes will not produce a value greater that the
 *     initial size parameter.
 * </li>
 * <li>When \a aMode is FormatSize_RoundUp, the result is rounded to the
 *     smallest number with \a aDecimal decimal digits that is not less than the
 *     result. This guarantees that converting the resulting string back to the
 *     integer value in bytes will not produce a value less that the initial
 *     size parameter.
 * </li>
 * </ul>
 *
 * @param aSize     Size value in bytes.
 * @param aMode     Conversion mode.
 * @param aDecimal  Number of decimal digits in result.
 * @return          Human-readable size string.
 */
/* static */
QString NemuGlobal::formatSize (quint64 aSize, uint aDecimal /* = 2 */,
                                FormatSize aMode /* = FormatSize_Round */)
{
    quint64 denom = 0;
    int suffix = 0;

    if (aSize < _1K)
    {
        denom = 1;
        suffix = 0;
    }
    else if (aSize < _1M)
    {
        denom = _1K;
        suffix = 1;
    }
    else if (aSize < _1G)
    {
        denom = _1M;
        suffix = 2;
    }
    else if (aSize < _1T)
    {
        denom = _1G;
        suffix = 3;
    }
    else if (aSize < _1P)
    {
        denom = _1T;
        suffix = 4;
    }
    else
    {
        denom = _1P;
        suffix = 5;
    }

    quint64 intg = aSize / denom;
    quint64 decm = aSize % denom;
    quint64 mult = 1;
    for (uint i = 0; i < aDecimal; ++ i) mult *= 10;

    QString number;
    if (denom > 1)
    {
        if (decm)
        {
            decm *= mult;
            /* not greater */
            if (aMode == FormatSize_RoundDown)
                decm = decm / denom;
            /* not less */
            else if (aMode == FormatSize_RoundUp)
                decm = (decm + denom - 1) / denom;
            /* nearest */
            else decm = (decm + denom / 2) / denom;
        }
        /* check for the fractional part overflow due to rounding */
        if (decm == mult)
        {
            decm = 0;
            ++ intg;
            /* check if we've got 1024 XB after rounding and scale down if so */
            if (intg == 1024 && suffix + 1 < (int)SizeSuffix_Max)
            {
                intg /= 1024;
                ++ suffix;
            }
        }
        number = QString::number (intg);
        if (aDecimal) number += QString ("%1%2").arg (decimalSep())
            .arg (QString::number (decm).rightJustified (aDecimal, '0'));
    }
    else
    {
        number = QString::number (intg);
    }

    return QString ("%1 %2").arg (number).arg (gpConverter->toString(static_cast<SizeSuffix>(suffix)));
}

/**
 *  Returns the required video memory in bytes for the current desktop
 *  resolution at maximum possible screen depth in bpp.
 */
/* static */
quint64 NemuGlobal::requiredVideoMemory(const QString &strGuestOSTypeId, int cMonitors /* = 1 */)
{
    QDesktopWidget *pDW = QApplication::desktop();
    /* We create a list of the size of all available host monitors. This list
     * is sorted by value and by starting with the biggest one, we calculate
     * the memory requirements for every guest screen. This is of course not
     * correct, but as we can't predict on which host screens the user will
     * open the guest windows, this is the best assumption we can do, cause it
     * is the worst case. */
    const int cHostScreens = pDW->screenCount();
    QVector<int> screenSize(qMax(cMonitors, cHostScreens), 0);
    for (int i = 0; i < cHostScreens; ++i)
    {
        QRect r = pDW->screenGeometry(i);
        screenSize[i] = r.width() * r.height();
    }
    /* Now sort the vector */
    qSort(screenSize.begin(), screenSize.end(), qGreater<int>());
    /* For the case that there are more guest screens configured then host
     * screens available, replace all zeros with the greatest value in the
     * vector. */
    for (int i = 0; i < screenSize.size(); ++i)
        if (screenSize.at(i) == 0)
            screenSize.replace(i, screenSize.at(0));

    quint64 needBits = 0;
    for (int i = 0; i < cMonitors; ++i)
    {
        /* Calculate summary required memory amount in bits */
        needBits += (screenSize.at(i) * /* with x height */
                     32 + /* we will take the maximum possible bpp for now */
                     8 * _1M) + /* current cache per screen - may be changed in future */
                    8 * 4096; /* adapter info */
    }
    /* Translate value into megabytes with rounding to highest side */
    quint64 needMBytes = needBits % (8 * _1M) ? needBits / (8 * _1M) + 1 :
                         needBits / (8 * _1M) /* convert to megabytes */;

    if (strGuestOSTypeId.startsWith("Windows"))
    {
       /* Windows guests need offscreen VRAM too for graphics acceleration features: */
#ifdef NEMU_WITH_CRHGSMI
       if (isWddmCompatibleOsType(strGuestOSTypeId))
       {
           /* wddm mode, there are two surfaces for each screen: shadow & primary */
           needMBytes *= 3;
       }
       else
#endif /* NEMU_WITH_CRHGSMI */
       {
           needMBytes *= 2;
       }
    }

    return needMBytes * _1M;
}

/**
 * Puts soft hyphens after every path component in the given file name.
 *
 * @param aFileName File name (must be a full path name).
 */
/* static */
QString NemuGlobal::locationForHTML (const QString &aFileName)
{
/// @todo (dmik) remove?
//    QString result = QDir::toNativeSeparators (fn);
//#ifdef Q_OS_LINUX
//    result.replace ('/', "/<font color=red>&shy;</font>");
//#else
//    result.replace ('\\', "\\<font color=red>&shy;</font>");
//#endif
//    return result;
    QFileInfo fi (aFileName);
    return fi.fileName();
}

/**
 *  Reformats the input string @a aStr so that:
 *  - strings in single quotes will be put inside <nobr> and marked
 *    with blue color;
 *  - UUIDs be put inside <nobr> and marked
 *    with green color;
 *  - replaces new line chars with </p><p> constructs to form paragraphs
 *    (note that <p> and </p> are not appended to the beginning and to the
 *     end of the string respectively, to allow the result be appended
 *     or prepended to the existing paragraph).
 *
 *  If @a aToolTip is true, colouring is not applied, only the <nobr> tag
 *  is added. Also, new line chars are replaced with <br> instead of <p>.
 */
/* static */
QString NemuGlobal::highlight (const QString &aStr, bool aToolTip /* = false */)
{
    QString strFont;
    QString uuidFont;
    QString endFont;
    if (!aToolTip)
    {
        strFont = "<font color=#0000CC>";
        uuidFont = "<font color=#008000>";
        endFont = "</font>";
    }

    QString text = aStr;

    /* replace special entities, '&' -- first! */
    text.replace ('&', "&amp;");
    text.replace ('<', "&lt;");
    text.replace ('>', "&gt;");
    text.replace ('\"', "&quot;");

    /* mark strings in single quotes with color */
    QRegExp rx = QRegExp ("((?:^|\\s)[(]?)'([^']*)'(?=[:.-!);]?(?:\\s|$))");
    rx.setMinimal (true);
    text.replace (rx,
        QString ("\\1%1<nobr>'\\2'</nobr>%2").arg (strFont).arg (endFont));

    /* mark UUIDs with color */
    text.replace (QRegExp (
        "((?:^|\\s)[(]?)"
        "(\\{[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\\})"
        "(?=[:.-!);]?(?:\\s|$))"),
        QString ("\\1%1<nobr>\\2</nobr>%2").arg (uuidFont).arg (endFont));

    /* split to paragraphs at \n chars */
    if (!aToolTip)
        text.replace ('\n', "</p><p>");
    else
        text.replace ('\n', "<br>");

    return text;
}

/* static */
QString NemuGlobal::replaceHtmlEntities(QString strText)
{
    return strText
        .replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('\"', "&quot;");
}

/**
 *  Reformats the input string @a aStr so that:
 *  - strings in single quotes will be put inside <nobr> and marked
 *    with bold style;
 *  - UUIDs be put inside <nobr> and marked
 *    with italic style;
 *  - replaces new line chars with </p><p> constructs to form paragraphs
 *    (note that <p> and </p> are not appended to the beginning and to the
 *     end of the string respectively, to allow the result be appended
 *     or prepended to the existing paragraph).
 */
/* static */
QString NemuGlobal::emphasize (const QString &aStr)
{
    QString strEmphStart ("<b>");
    QString strEmphEnd ("</b>");
    QString uuidEmphStart ("<i>");
    QString uuidEmphEnd ("</i>");

    QString text = aStr;

    /* replace special entities, '&' -- first! */
    text.replace ('&', "&amp;");
    text.replace ('<', "&lt;");
    text.replace ('>', "&gt;");
    text.replace ('\"', "&quot;");

    /* mark strings in single quotes with bold style */
    QRegExp rx = QRegExp ("((?:^|\\s)[(]?)'([^']*)'(?=[:.-!);]?(?:\\s|$))");
    rx.setMinimal (true);
    text.replace (rx,
        QString ("\\1%1<nobr>'\\2'</nobr>%2").arg (strEmphStart).arg (strEmphEnd));

    /* mark UUIDs with italic style */
    text.replace (QRegExp (
        "((?:^|\\s)[(]?)"
        "(\\{[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\\})"
        "(?=[:.-!);]?(?:\\s|$))"),
        QString ("\\1%1<nobr>\\2</nobr>%2").arg (uuidEmphStart).arg (uuidEmphEnd));

    /* split to paragraphs at \n chars */
    text.replace ('\n', "</p><p>");

    return text;
}

/**
 *  This does exactly the same as QLocale::system().name() but corrects its
 *  wrong behavior on Linux systems (LC_NUMERIC for some strange reason takes
 *  precedence over any other locale setting in the QLocale::system()
 *  implementation). This implementation first looks at LC_ALL (as defined by
 *  SUS), then looks at LC_MESSAGES which is designed to define a language for
 *  program messages in case if it differs from the language for other locale
 *  categories. Then it looks for LANG and finally falls back to
 *  QLocale::system().name().
 *
 *  The order of precedence is well defined here:
 *  http://opengroup.org/onlinepubs/007908799/xbd/envvar.html
 *
 *  @note This method will return "C" when the requested locale is invalid or
 *  when the "C" locale is set explicitly.
 */
/* static */
QString NemuGlobal::systemLanguageId()
{
#if defined (Q_WS_MAC)
    /* QLocale return the right id only if the user select the format of the
     * language also. So we use our own implementation */
    return ::darwinSystemLanguage();
#elif defined (Q_OS_UNIX)
    const char *s = RTEnvGet ("LC_ALL");
    if (s == 0)
        s = RTEnvGet ("LC_MESSAGES");
    if (s == 0)
        s = RTEnvGet ("LANG");
    if (s != 0)
        return QLocale (s).name();
#endif
    return  QLocale::system().name();
}

#if defined (Q_WS_X11)

static char *XXGetProperty (Display *aDpy, Window aWnd,
                            Atom aPropType, const char *aPropName)
{
    Atom propNameAtom = XInternAtom (aDpy, aPropName,
                                     True /* only_if_exists */);
    if (propNameAtom == None)
        return NULL;

    Atom actTypeAtom = None;
    int actFmt = 0;
    unsigned long nItems = 0;
    unsigned long nBytesAfter = 0;
    unsigned char *propVal = NULL;
    int rc = XGetWindowProperty (aDpy, aWnd, propNameAtom,
                                 0, LONG_MAX, False /* delete */,
                                 aPropType, &actTypeAtom, &actFmt,
                                 &nItems, &nBytesAfter, &propVal);
    if (rc != Success)
        return NULL;

    return reinterpret_cast <char *> (propVal);
}

static Bool XXSendClientMessage (Display *aDpy, Window aWnd, const char *aMsg,
                                 unsigned long aData0 = 0, unsigned long aData1 = 0,
                                 unsigned long aData2 = 0, unsigned long aData3 = 0,
                                 unsigned long aData4 = 0)
{
    Atom msgAtom = XInternAtom (aDpy, aMsg, True /* only_if_exists */);
    if (msgAtom == None)
        return False;

    XEvent ev;

    ev.xclient.type = ClientMessage;
    ev.xclient.serial = 0;
    ev.xclient.send_event = True;
    ev.xclient.display = aDpy;
    ev.xclient.window = aWnd;
    ev.xclient.message_type = msgAtom;

    /* always send as 32 bit for now */
    ev.xclient.format = 32;
    ev.xclient.data.l [0] = aData0;
    ev.xclient.data.l [1] = aData1;
    ev.xclient.data.l [2] = aData2;
    ev.xclient.data.l [3] = aData3;
    ev.xclient.data.l [4] = aData4;

    return XSendEvent (aDpy, DefaultRootWindow (aDpy), False,
                       SubstructureRedirectMask, &ev) != 0;
}

#endif

/**
 * Activates the specified window. If necessary, the window will be
 * de-iconified activation.
 *
 * @note On X11, it is implied that @a aWid represents a window of the same
 * display the application was started on.
 *
 * @param aWId              Window ID to activate.
 * @param aSwitchDesktop    @c true to switch to the window's desktop before
 *                          activation.
 *
 * @return @c true on success and @c false otherwise.
 */
/* static */
bool NemuGlobal::activateWindow (WId aWId, bool aSwitchDesktop /* = true */)
{
    bool result = true;

#if defined (Q_WS_WIN32)

    if (IsIconic (aWId))
        result &= !!ShowWindow (aWId, SW_RESTORE);
    else if (!IsWindowVisible (aWId))
        result &= !!ShowWindow (aWId, SW_SHOW);

    result &= !!SetForegroundWindow (aWId);

#elif defined (Q_WS_X11)

    Display *dpy = QX11Info::display();

    if (aSwitchDesktop)
    {
        /* try to find the desktop ID using the NetWM property */
        CARD32 *desktop = (CARD32 *) XXGetProperty (dpy, aWId, XA_CARDINAL,
                                                    "_NET_WM_DESKTOP");
        if (desktop == NULL)
            /* if the NetWM properly is not supported try to find the desktop
             * ID using the GNOME WM property */
            desktop = (CARD32 *) XXGetProperty (dpy, aWId, XA_CARDINAL,
                                                "_WIN_WORKSPACE");

        if (desktop != NULL)
        {
            Bool ok = XXSendClientMessage (dpy, DefaultRootWindow (dpy),
                                           "_NET_CURRENT_DESKTOP",
                                           *desktop);
            if (!ok)
            {
                Log1WarningFunc(("Couldn't switch to desktop=%08X\n", desktop));
                result = false;
            }
            XFree (desktop);
        }
        else
        {
            Log1WarningFunc(("Couldn't find a desktop ID for aWId=%08X\n", aWId));
            result = false;
        }
    }

    Bool ok = XXSendClientMessage (dpy, aWId, "_NET_ACTIVE_WINDOW");
    result &= !!ok;

    XRaiseWindow (dpy, aWId);

#else

    NOREF (aWId);
    NOREF (aSwitchDesktop);
    AssertFailed();
    result = false;

#endif

    if (!result)
        Log1WarningFunc(("Couldn't activate aWId=%08X\n", aWId));

    return result;
}

#ifdef Q_WS_X11
/* This method tests whether the current X11 window manager supports full-screen mode as we need it.
 * Unfortunately the EWMH specification was not fully clear about whether we can expect to find
 * all of these atoms on the _NET_SUPPORTED root window property, so we have to test with all
 * interesting window managers. If this fails for a user when you think it should succeed
 * they should try executing:
 * xprop -root | egrep -w '_NET_WM_FULLSCREEN_MONITORS|_NET_WM_STATE|_NET_WM_STATE_FULLSCREEN'
 * in an X11 terminal window.
 * All three strings should be found under a property called "_NET_SUPPORTED(ATOM)". */
/* static */
bool NemuGlobal::supportsFullScreenMonitorsProtocolX11()
{
    /* Using a global to get at the display does not feel right, but that is
     * how it is done elsewhere in the code. */
    Display *pDisplay = QX11Info::display();
    Atom atomSupported            = XInternAtom(pDisplay, "_NET_SUPPORTED",
                                                True /* only_if_exists */);
    Atom atomWMFullScreenMonitors = XInternAtom(pDisplay,
                                                "_NET_WM_FULLSCREEN_MONITORS",
                                                True /* only_if_exists */);
    Atom atomWMState              = XInternAtom(pDisplay,
                                                "_NET_WM_STATE",
                                                True /* only_if_exists */);
    Atom atomWMStateFullScreen    = XInternAtom(pDisplay,
                                                "_NET_WM_STATE_FULLSCREEN",
                                                True /* only_if_exists */);
    bool fFoundFullScreenMonitors = false;
    bool fFoundState              = false;
    bool fFoundStateFullScreen    = false;
    Atom atomType;
    int cFormat;
    unsigned long cItems;
    unsigned long cbLeft;
    Atom *pAtomHints;
    int rc;
    unsigned i;

    if (   atomSupported == None || atomWMFullScreenMonitors == None
        || atomWMState == None || atomWMStateFullScreen == None)
        return false;
    /* Get atom value: */
    rc = XGetWindowProperty(pDisplay, DefaultRootWindow(pDisplay),
                            atomSupported, 0, 0x7fffffff /*LONG_MAX*/,
                            False /* delete */, XA_ATOM, &atomType,
                            &cFormat, &cItems, &cbLeft,
                            (unsigned char **)&pAtomHints);
    if (rc != Success)
        return false;
    if (pAtomHints == NULL)
        return false;
    if (atomType == XA_ATOM && cFormat == 32 && cbLeft == 0)
        for (i = 0; i < cItems; ++i)
        {
            if (pAtomHints[i] == atomWMFullScreenMonitors)
                fFoundFullScreenMonitors = true;
            if (pAtomHints[i] == atomWMState)
                fFoundState = true;
            if (pAtomHints[i] == atomWMStateFullScreen)
                fFoundStateFullScreen = true;
        }
    XFree(pAtomHints);
    return fFoundFullScreenMonitors && fFoundState && fFoundStateFullScreen;
}

/* static */
bool NemuGlobal::setFullScreenMonitorX11(QWidget *pWidget, ulong uScreenId)
{
    return XXSendClientMessage(pWidget->x11Info().display(),
                               pWidget->window()->winId(),
                               "_NET_WM_FULLSCREEN_MONITORS",
                               uScreenId, uScreenId, uScreenId, uScreenId,
                               1 /* Source indication (1 = normal application) */);
}

/* static */
QVector<Atom> NemuGlobal::flagsNetWmState(QWidget *pWidget)
{
    /* Get display: */
    Display *pDisplay = pWidget->x11Info().display();

    /* Prepare atoms: */
    QVector<Atom> resultNetWmState;
    Atom net_wm_state = XInternAtom(pDisplay, "_NET_WM_STATE", True /* only if exists */);

    /* Get the size of the property data: */
    Atom actual_type;
    int iActualFormat;
    ulong uPropertyLength;
    ulong uBytesLeft;
    uchar *pPropertyData = 0;
    if (XGetWindowProperty(pDisplay, pWidget->window()->winId(),
                           net_wm_state, 0, 0, False, XA_ATOM, &actual_type, &iActualFormat,
                           &uPropertyLength, &uBytesLeft, &pPropertyData) == Success &&
        actual_type == XA_ATOM && iActualFormat == 32)
    {
        resultNetWmState.resize(uBytesLeft / 4);
        XFree((char*)pPropertyData);
        pPropertyData = 0;

        /* Fetch all data: */
        if (XGetWindowProperty(pDisplay, pWidget->window()->winId(),
                               net_wm_state, 0, resultNetWmState.size(), False, XA_ATOM, &actual_type, &iActualFormat,
                               &uPropertyLength, &uBytesLeft, &pPropertyData) != Success)
            resultNetWmState.clear();
        else if (uPropertyLength != (ulong)resultNetWmState.size())
            resultNetWmState.resize(uPropertyLength);

        /* Put it into resultNetWmState: */
        if (!resultNetWmState.isEmpty())
            memcpy(resultNetWmState.data(), pPropertyData, resultNetWmState.size() * sizeof(Atom));
        if (pPropertyData)
            XFree((char*)pPropertyData);
    }

    /* Return result: */
    return resultNetWmState;
}

/* static */
bool NemuGlobal::isFullScreenFlagSet(QWidget *pWidget)
{
    /* Get display: */
    Display *pDisplay = pWidget->x11Info().display();

    /* Prepare atoms: */
    Atom net_wm_state_fullscreen = XInternAtom(pDisplay, "_NET_WM_STATE_FULLSCREEN", True /* only if exists */);

    /* Check if flagsNetWmState(pWidget) contains full-screen flag: */
    return flagsNetWmState(pWidget).contains(net_wm_state_fullscreen);
}

/* static */
void NemuGlobal::setFullScreenFlag(QWidget *pWidget)
{
    /* Get display: */
    Display *pDisplay = pWidget->x11Info().display();

    /* Prepare atoms: */
    QVector<Atom> resultNetWmState = flagsNetWmState(pWidget);
    Atom net_wm_state = XInternAtom(pDisplay, "_NET_WM_STATE", True /* only if exists */);
    Atom net_wm_state_fullscreen = XInternAtom(pDisplay, "_NET_WM_STATE_FULLSCREEN", True /* only if exists */);

    /* Append resultNetWmState with full-screen flag if necessary: */
    if (!resultNetWmState.contains(net_wm_state_fullscreen))
    {
        resultNetWmState.append(net_wm_state_fullscreen);
        /* Apply property to widget again: */
        XChangeProperty(pDisplay, pWidget->window()->winId(),
                        net_wm_state, XA_ATOM, 32, PropModeReplace,
                        (unsigned char*)resultNetWmState.data(), resultNetWmState.size());
    }
}
#endif /* Q_WS_X11 */

/**
 *  Removes the accelerator mark (the ampersand symbol) from the given string
 *  and returns the result. The string is supposed to be a menu item's text
 *  that may (or may not) contain the accelerator mark.
 *
 *  In order to support accelerators used in non-alphabet languages
 *  (e.g. Japanese) that has a form of "(&<L>)" (where <L> is a latin letter),
 *  this method first searches for this pattern and, if found, removes it as a
 *  whole. If such a pattern is not found, then the '&' character is simply
 *  removed from the string.
 *
 *  @note This function removes only the first occurrence of the accelerator
 *  mark.
 *
 *  @param aText Menu item's text to remove the accelerator mark from.
 *
 *  @return The resulting string.
 */
/* static */
QString NemuGlobal::removeAccelMark (const QString &aText)
{
    QString result = aText;

    QRegExp accel ("\\(&[a-zA-Z]\\)");
    int pos = accel.indexIn (result);
    if (pos >= 0)
        result.remove (pos, accel.cap().length());
    else
    {
        pos = result.indexOf ('&');
        if (pos >= 0)
            result.remove (pos, 1);
    }

    return result;
}

/* static */
QString NemuGlobal::insertKeyToActionText(const QString &strText, const QString &strKey)
{
#ifdef Q_WS_MAC
    QString pattern("%1 (Host+%2)");
#else
    QString pattern("%1 \tHost+%2");
#endif
    if (   strKey.isEmpty()
        || strKey.compare("None", Qt::CaseInsensitive) == 0)
        return strText;
    else
        return pattern.arg(strText).arg(QKeySequence(strKey).toString(QKeySequence::NativeText));
}

/**
 * Joins two pixmaps horizontally with 2px space between them and returns the
 * result.
 *
 * @param aPM1 Left pixmap.
 * @param aPM2 Right pixmap.
 */
/* static */
QPixmap NemuGlobal::joinPixmaps (const QPixmap &aPM1, const QPixmap &aPM2)
{
    if (aPM1.isNull())
        return aPM2;
    if (aPM2.isNull())
        return aPM1;

    QPixmap result (aPM1.width() + aPM2.width() + 2,
                    qMax (aPM1.height(), aPM2.height()));
    result.fill (Qt::transparent);

    QPainter painter (&result);
    painter.drawPixmap (0, 0, aPM1);
    painter.drawPixmap (aPM1.width() + 2, result.height() - aPM2.height(), aPM2);
    painter.end();

    return result;
}

/**
 *  Searches for a widget that with @a aName (if it is not NULL) which inherits
 *  @a aClassName (if it is not NULL) and among children of @a aParent. If @a
 *  aParent is NULL, all top-level widgets are searched. If @a aRecursive is
 *  true, child widgets are recursively searched as well.
 */
/* static */
QWidget *NemuGlobal::findWidget (QWidget *aParent, const char *aName,
                                 const char *aClassName /* = NULL */,
                                 bool aRecursive /* = false */)
{
    if (aParent == NULL)
    {
        QWidgetList list = QApplication::topLevelWidgets();
        foreach(QWidget *w, list)
        {
            if ((!aName || strcmp (w->objectName().toAscii().constData(), aName) == 0) &&
                (!aClassName || strcmp (w->metaObject()->className(), aClassName) == 0))
                return w;
            if (aRecursive)
            {
                w = findWidget (w, aName, aClassName, aRecursive);
                if (w)
                    return w;
            }
        }
        return NULL;
    }

    /* Find the first children of aParent with the appropriate properties.
     * Please note that this call is recursively. */
    QList<QWidget *> list = qFindChildren<QWidget *> (aParent, aName);
    foreach(QWidget *child, list)
    {
        if (!aClassName || strcmp (child->metaObject()->className(), aClassName) == 0)
            return child;
    }
    return NULL;
}

/**
 * Figures out which medium formats are currently supported by VirtualBox for
 * the given device type.
 * Returned is a list of pairs with the form
 *   <tt>{"Backend Name", "*.suffix1 .suffix2 ..."}</tt>.
 */
/* static */
QList <QPair <QString, QString> > NemuGlobal::MediumBackends(KDeviceType enmType)
{
    CSystemProperties systemProperties = nemuGlobal().virtualBox().GetSystemProperties();
    QVector<CMediumFormat> mediumFormats = systemProperties.GetMediumFormats();
    QList< QPair<QString, QString> > backendPropList;
    for (int i = 0; i < mediumFormats.size(); ++ i)
    {
        /* File extensions */
        QVector <QString> fileExtensions;
        QVector <KDeviceType> deviceTypes;

        mediumFormats [i].DescribeFileExtensions(fileExtensions, deviceTypes);

        QStringList f;
        for (int a = 0; a < fileExtensions.size(); ++ a)
            if (deviceTypes [a] == enmType)
                f << QString ("*.%1").arg (fileExtensions [a]);
        /* Create a pair out of the backend description and all suffix's. */
        if (!f.isEmpty())
            backendPropList << QPair<QString, QString> (mediumFormats [i].GetName(), f.join(" "));
    }
    return backendPropList;
}

/**
 * Figures out which hard disk formats are currently supported by VirtualBox.
 * Returned is a list of pairs with the form
 *   <tt>{"Backend Name", "*.suffix1 .suffix2 ..."}</tt>.
 */
/* static */
QList <QPair <QString, QString> > NemuGlobal::HDDBackends()
{
    return MediumBackends(KDeviceType_HardDisk);
}

/**
 * Figures out which optical disk formats are currently supported by VirtualBox.
 * Returned is a list of pairs with the form
 *   <tt>{"Backend Name", "*.suffix1 .suffix2 ..."}</tt>.
 */
/* static */
QList <QPair <QString, QString> > NemuGlobal::DVDBackends()
{
    return MediumBackends(KDeviceType_DVD);
}

/**
 * Figures out which floppy disk formats are currently supported by VirtualBox.
 * Returned is a list of pairs with the form
 *   <tt>{"Backend Name", "*.suffix1 .suffix2 ..."}</tt>.
 */
/* static */
QList <QPair <QString, QString> > NemuGlobal::FloppyBackends()
{
    return MediumBackends(KDeviceType_Floppy);
}

/* static */
QString NemuGlobal::documentsPath()
{
    QString path = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
    QDir dir(path);
    if (dir.exists())
        return QDir::cleanPath(dir.canonicalPath());
    else
    {
        dir.setPath(QDir::homePath() + "/Documents");
        if (dir.exists())
            return QDir::cleanPath(dir.canonicalPath());
        else
            return QDir::homePath();
    }
}

#ifdef NEMU_WITH_VIDEOHWACCEL
/* static */
bool NemuGlobal::isAcceleration2DVideoAvailable()
{
    return NemuQGLOverlay::isAcceleration2DVideoAvailable();
}

/** additional video memory required for the best 2D support performance
 *  total amount of VRAM required is thus calculated as requiredVideoMemory + required2DOffscreenVideoMemory  */
/* static */
quint64 NemuGlobal::required2DOffscreenVideoMemory()
{
    return NemuQGLOverlay::required2DOffscreenVideoMemory();
}

#endif

#ifdef NEMU_WITH_CRHGSMI
/* static */
quint64 NemuGlobal::required3DWddmOffscreenVideoMemory(const QString &strGuestOSTypeId, int cMonitors /* = 1 */)
{
    cMonitors = RT_MAX(cMonitors, 1);
    quint64 cbSize = NemuGlobal::requiredVideoMemory(strGuestOSTypeId, 1); /* why not cMonitors? */
    cbSize += 64 * _1M;
    return cbSize;
}

/* static */
bool NemuGlobal::isWddmCompatibleOsType(const QString &strGuestOSTypeId)
{
    return    strGuestOSTypeId.startsWith("WindowsVista")
           || strGuestOSTypeId.startsWith("Windows7")
           || strGuestOSTypeId.startsWith("Windows8")
           || strGuestOSTypeId.startsWith("Windows81")
           || strGuestOSTypeId.startsWith("Windows10")
           || strGuestOSTypeId.startsWith("Windows2008")
           || strGuestOSTypeId.startsWith("Windows2012");
}
#endif /* NEMU_WITH_CRHGSMI */

/* static */
QString NemuGlobal::fullMediumFormatName(const QString &strBaseMediumFormatName)
{
    if (strBaseMediumFormatName == "VDI")
        return tr("VDI (VirtualBox Disk Image)");
    else if (strBaseMediumFormatName == "VMDK")
        return tr("VMDK (Virtual Machine Disk)");
    else if (strBaseMediumFormatName == "VHD")
        return tr("VHD (Virtual Hard Disk)");
    else if (strBaseMediumFormatName == "Parallels")
        return tr("HDD (Parallels Hard Disk)");
    else if (strBaseMediumFormatName == "QED")
        return tr("QED (QEMU enhanced disk)");
    else if (strBaseMediumFormatName == "QCOW")
        return tr("QCOW (QEMU Copy-On-Write)");
    return strBaseMediumFormatName;
}

#ifdef RT_OS_LINUX
/* static */
void NemuGlobal::checkForWrongUSBMounted()
{
    /* Make sure '/proc/mounts' exists and can be opened: */
    QFile file("/proc/mounts");
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    /* Fetch contents: */
    QStringList contents;
    for (;;)
    {
        QByteArray line = file.readLine();
        if (line.isEmpty())
            break;
        contents << line;
    }
    /* Grep contents for usbfs presence: */
    QStringList grep1(contents.filter("/sys/bus/usb/drivers"));
    QStringList grep2(grep1.filter("usbfs"));
    if (grep2.isEmpty())
        return;

    /* Show corresponding warning: */
    msgCenter().warnAboutWrongUSBMounted();
}
#endif /* RT_OS_LINUX */

/* static */
void NemuGlobal::setMinimumWidthAccordingSymbolCount(QSpinBox *pSpinBox, int cCount)
{
    /* Load options: */
    QStyleOptionSpinBox option;
    option.initFrom(pSpinBox);

    /* Acquire edit-field rectangle: */
    QRect rect = pSpinBox->style()->subControlRect(QStyle::CC_SpinBox,
                                                   &option,
                                                   QStyle::SC_SpinBoxEditField,
                                                   pSpinBox);

    /* Calculate minimum-width magic: */
    int iSpinBoxWidth = pSpinBox->width();
    int iSpinBoxEditFieldWidth = rect.width();
    int iSpinBoxDelta = qMax(0, iSpinBoxWidth - iSpinBoxEditFieldWidth);
    QFontMetrics metrics(pSpinBox->font(), pSpinBox);
    QString strDummy(cCount, '0');
    int iTextWidth = metrics.width(strDummy);

    /* Tune spin-box minimum-width: */
    pSpinBox->setMinimumWidth(iTextWidth + iSpinBoxDelta);
}

// Public slots
////////////////////////////////////////////////////////////////////////////////

/**
 * Opens the specified URL using OS/Desktop capabilities.
 *
 * @param aURL URL to open
 *
 * @return true on success and false otherwise
 */
bool NemuGlobal::openURL (const QString &aURL)
{
    /* Service event */
    class ServiceEvent : public QEvent
    {
        public:

            ServiceEvent (bool aResult) : QEvent (QEvent::User), mResult (aResult) {}

            bool result() const { return mResult; }

        private:

            bool mResult;
    };

    /* Service-Client object */
    class ServiceClient : public QEventLoop
    {
        public:

            ServiceClient() : mResult (false) {}

            bool result() const { return mResult; }

        private:

            bool event (QEvent *aEvent)
            {
                if (aEvent->type() == QEvent::User)
                {
                    ServiceEvent *pEvent = static_cast <ServiceEvent*> (aEvent);
                    mResult = pEvent->result();
                    pEvent->accept();
                    quit();
                    return true;
                }
                return false;
            }

            bool mResult;
    };

    /* Service-Server object */
    class ServiceServer : public QThread
    {
        public:

            ServiceServer (ServiceClient &aClient, const QString &sURL)
                : mClient (aClient), mURL (sURL) {}

        private:

            void run()
            {
                QApplication::postEvent (&mClient, new ServiceEvent (QDesktopServices::openUrl (mURL)));
            }

            ServiceClient &mClient;
            const QString &mURL;
    };

    ServiceClient client;
    ServiceServer server (client, aURL);
    server.start();
    client.exec();
    server.wait();

    bool result = client.result();

    if (!result)
        msgCenter().cannotOpenURL (aURL);

    return result;
}

void NemuGlobal::sltGUILanguageChange(QString strLang)
{
    /* Make sure medium-enumeration is not in progress! */
    AssertReturnVoid(!isMediumEnumerationInProgress());
    /* Load passed language: */
    loadLanguage(strLang);
}

// Protected members
////////////////////////////////////////////////////////////////////////////////

bool NemuGlobal::eventFilter (QObject *aObject, QEvent *aEvent)
{
    if (aEvent->type() == QEvent::LanguageChange &&
        aObject->isWidgetType() &&
        static_cast <QWidget *> (aObject)->isTopLevel())
    {
        /* Catch the language change event before any other widget gets it in
         * order to invalidate cached string resources (like the details view
         * templates) that may be used by other widgets. */
        QWidgetList list = QApplication::topLevelWidgets();
        if (list.first() == aObject)
        {
            /* call this only once per every language change (see
             * QApplication::installTranslator() for details) */
            retranslateUi();
        }
    }

    return QObject::eventFilter (aObject, aEvent);
}

#ifdef NEMU_WITH_DEBUGGER_GUI

bool NemuGlobal::isDebuggerEnabled() const
{
    return isDebuggerWorker(&m_fDbgEnabled, GUI_Dbg_Enabled);
}

bool NemuGlobal::isDebuggerAutoShowEnabled() const
{
    return isDebuggerWorker(&m_fDbgAutoShow, GUI_Dbg_AutoShow);
}

bool NemuGlobal::isDebuggerAutoShowCommandLineEnabled() const
{
    return isDebuggerWorker(&m_fDbgAutoShowCommandLine, GUI_Dbg_AutoShow);
}

bool NemuGlobal::isDebuggerAutoShowStatisticsEnabled() const
{
    return isDebuggerWorker(&m_fDbgAutoShowStatistics, GUI_Dbg_AutoShow);
}

#endif /* NEMU_WITH_DEBUGGER_GUI */

// Private members
////////////////////////////////////////////////////////////////////////////////

bool NemuGlobal::processArgs()
{
    bool fResult = false;
    QStringList args = qApp->arguments();
    QList<QUrl> list;
    for (int i = 1; i < args.size(); ++i)
    {
        /* We break out after the first parameter, cause there could be
           parameters with arguments (e.g. --comment comment). */
        if (args.at(i).startsWith("-"))
            break;
#ifdef Q_WS_MAC
        QString strArg = ::darwinResolveAlias(args.at(i));
#else /* Q_WS_MAC */
        QString strArg = args.at(i);
#endif /* !Q_WS_MAC */
        if (   !strArg.isEmpty()
            && QFile::exists(strArg))
            list << QUrl::fromLocalFile(strArg);
    }
    if (!list.isEmpty())
    {
        for (int i = 0; i < list.size(); ++i)
        {
            const QString& strFile = list.at(i).toLocalFile();
            if (NemuGlobal::hasAllowedExtension(strFile, NemuFileExts))
            {
                CVirtualBox nemu = virtualBox();
                CMachine machine = nemu.FindMachine(strFile);
                if (!machine.isNull())
                {
                    fResult = true;
                    launchMachine(machine);
                    /* Remove from the arg list. */
                    list.removeAll(strFile);
                }
            }
        }
    }
    if (!list.isEmpty())
    {
        m_ArgUrlList = list;
        UISelectorWindow::create();
        QTimer::singleShot(0, gpSelectorWindow, SLOT(sltOpenUrls()));
    }
    return fResult;
}

void NemuGlobal::prepare()
{
    /* Make sure QApplication cleanup us on exit: */
    connect(qApp, SIGNAL(aboutToQuit()), this, SLOT(cleanup()));

#ifdef Q_WS_MAC
    /* Determine OS release early: */
    m_osRelease = determineOsRelease();
#endif /* Q_WS_MAC */

    /* Create message-center: */
    UIMessageCenter::create();
    /* Create popup-center: */
    UIPopupCenter::create();

    /* Prepare general icon-pool: */
    m_pIconPool = new UIIconPoolGeneral;

    /* Load translation based on the current locale: */
    loadLanguage();

#ifdef DEBUG
    mVerString += " [DEBUG]";
#endif

    HRESULT rc = COMBase::InitializeCOM(true);
    if (FAILED (rc))
    {
#ifdef NEMU_WITH_XPCOM
        if (rc == NS_ERROR_FILE_ACCESS_DENIED)
        {
            char szHome[RTPATH_MAX] = "";
            com::GetNemuUserHomeDirectory(szHome, sizeof(szHome));
            msgCenter().cannotInitUserHome(QString(szHome));
        }
        else
#endif
            msgCenter().cannotInitCOM(rc);
        return;
    }

    /* Create VirtualBox client instance: */
    m_client.createInstance(CLSID_VirtualBoxClient);
    /* And make sure it was created: */
    if (!m_client.isOk())
    {
        msgCenter().cannotCreateVirtualBoxClient(m_client);
        return;
    }
    /* Init wrappers: */
    comWrappersReinit();

    /* Watch for the NemuSVC availability changes: */
    connect(gNemuEvents, SIGNAL(sigNemuSVCAvailabilityChange(bool)),
            this, SLOT(sltHandleNemuSVCAvailabilityChange(bool)));

    /* Prepare thread-pool instance: */
    m_pThreadPool = new UIThreadPool(3 /* worker count */, 5000 /* worker timeout */);

    /* create default non-null global settings */
    gset = NemuGlobalSettings (false);

    /* try to load global settings */
    gset.load(m_nemu);
    if (!m_nemu.isOk() || !gset)
    {
        msgCenter().cannotLoadGlobalConfig(m_nemu, gset.lastError());
        return;
    }

    /* Load translation based on the user settings: */
    QString sLanguageId = gset.languageId();
    if (!sLanguageId.isNull())
        loadLanguage (sLanguageId);

    retranslateUi();

    connect(gEDataManager, SIGNAL(sigLanguageChange(QString)),
            this, SLOT(sltGUILanguageChange(QString)));

    qApp->installEventFilter (this);

    /* process command line */

    UIVisualStateType visualStateType = UIVisualStateType_Invalid;

#ifdef Q_WS_X11
    /* Acquire current Window Manager type: */
    m_enmWindowManagerType = X11WindowManagerType();

    /* Create desktop-widget watchdog instance: */
    m_pDesktopWidgetWatchdog = new UIDesktopWidgetWatchdog(this);
#endif /* Q_WS_X11 */

#ifdef NEMU_WITH_DEBUGGER_GUI
# ifdef NEMU_WITH_DEBUGGER_GUI_MENU
    initDebuggerVar(&m_fDbgEnabled, "NEMU_GUI_DBG_ENABLED", GUI_Dbg_Enabled, true);
# else
    initDebuggerVar(&m_fDbgEnabled, "NEMU_GUI_DBG_ENABLED", GUI_Dbg_Enabled, false);
# endif
    initDebuggerVar(&m_fDbgAutoShow, "NEMU_GUI_DBG_AUTO_SHOW", GUI_Dbg_AutoShow, false);
    m_fDbgAutoShowCommandLine = m_fDbgAutoShowStatistics = m_fDbgAutoShow;
    m_enmStartRunning = StartRunning_Default;
#endif

    mShowStartVMErrors = true;
    bool startVM = false;
    bool fSeparateProcess = false;
    QString vmNameOrUuid;

    int argc = qApp->argc();
    int i = 1;
    while (i < argc)
    {
        const char *arg = qApp->argv() [i];
        /* NOTE: the check here must match the corresponding check for the
         * options to start a VM in main.cpp and hardenedmain.cpp exactly,
         * otherwise there will be weird error messages. */
        if (   !::strcmp (arg, "--startvm")
            || !::strcmp (arg, "-startvm"))
        {
            if (++i < argc)
            {
                vmNameOrUuid = QString (qApp->argv() [i]);
                startVM = true;
            }
        }
        else if (!::strcmp (arg, "-separate") || !::strcmp (arg, "--separate"))
        {
            fSeparateProcess = true;
        }
#ifdef NEMU_GUI_WITH_PIDFILE
        else if (!::strcmp(arg, "-pidfile") || !::strcmp(arg, "--pidfile"))
        {
            if (++i < argc)
                m_strPidfile = QString(qApp->argv()[i]);
        }
#endif /* NEMU_GUI_WITH_PIDFILE */
        /* Visual state type options: */
        else if (!::strcmp(arg, "-normal") || !::strcmp(arg, "--normal"))
            visualStateType = UIVisualStateType_Normal;
        else if (!::strcmp(arg, "-fullscreen") || !::strcmp(arg, "--fullscreen"))
            visualStateType = UIVisualStateType_Fullscreen;
        else if (!::strcmp(arg, "-seamless") || !::strcmp(arg, "--seamless"))
            visualStateType = UIVisualStateType_Seamless;
        else if (!::strcmp(arg, "-scale") || !::strcmp(arg, "--scale"))
            visualStateType = UIVisualStateType_Scale;
        /* Passwords: */
        else if (!::strcmp (arg, "--settingspw"))
        {
            if (++i < argc)
            {
                RTStrCopy(mSettingsPw, sizeof(mSettingsPw), qApp->argv() [i]);
                mSettingsPwSet = true;
            }
        }
        else if (!::strcmp (arg, "--settingspwfile"))
        {
            if (++i < argc)
            {
                size_t cbFile;
                char *pszFile = qApp->argv() [i];
                bool fStdIn = !::strcmp(pszFile, "stdin");
                int vrc = VINF_SUCCESS;
                PRTSTREAM pStrm;
                if (!fStdIn)
                    vrc = RTStrmOpen(pszFile, "r", &pStrm);
                else
                    pStrm = g_pStdIn;
                if (RT_SUCCESS(vrc))
                {
                    vrc = RTStrmReadEx(pStrm, mSettingsPw, sizeof(mSettingsPw)-1, &cbFile);
                    if (RT_SUCCESS(vrc))
                    {
                        if (cbFile >= sizeof(mSettingsPw)-1)
                            continue;
                        else
                        {
                            unsigned i;
                            for (i = 0; i < cbFile && !RT_C_IS_CNTRL(mSettingsPw[i]); i++)
                                ;
                            mSettingsPw[i] = '\0';
                            mSettingsPwSet = true;
                        }
                    }
                    if (!fStdIn)
                        RTStrmClose(pStrm);
                }
            }
        }
        /* Misc options: */
        else if (!::strcmp (arg, "-comment") || !::strcmp (arg, "--comment"))
            ++i;
        else if (!::strcmp(arg, "--no-startvm-errormsgbox"))
            mShowStartVMErrors = false;
        else if (!::strcmp(arg, "--aggressive-caching"))
            mAgressiveCaching = true;
        else if (!::strcmp(arg, "--no-aggressive-caching"))
            mAgressiveCaching = false;
        else if (!::strcmp(arg, "--restore-current"))
            mRestoreCurrentSnapshot = true;
        /* Ad hoc VM reconfig options: */
        else if (!::strcmp(arg, "--fda"))
        {
            if (++i < argc)
                m_strFloppyImage = qApp->argv()[i];
        }
        else if (!::strcmp(arg, "--dvd") || !::strcmp(arg, "--cdrom"))
        {
            if (++i < argc)
                m_strDvdImage = qApp->argv()[i];
        }
        /* VMM Options: */
        else if (!::strcmp(arg, "--disable-patm"))
            mDisablePatm = true;
        else if (!::strcmp(arg, "--disable-csam"))
            mDisableCsam = true;
        else if (!::strcmp(arg, "--recompile-supervisor"))
            mRecompileSupervisor = true;
        else if (!::strcmp(arg, "--recompile-user"))
            mRecompileUser = true;
        else if (!::strcmp(arg, "--recompile-all"))
            mDisablePatm = mDisableCsam = mRecompileSupervisor = mRecompileUser = true;
        else if (!::strcmp(arg, "--execute-all-in-iem"))
            mDisablePatm = mDisableCsam = mExecuteAllInIem = true;
        else if (!::strcmp(arg, "--warp-pct"))
        {
            if (++i < argc)
                mWarpPct = RTStrToUInt32(qApp->argv() [i]);
        }
#ifdef NEMU_WITH_DEBUGGER_GUI
        /* Debugger/Debugging options: */
        else if (!::strcmp(arg, "-dbg") || !::strcmp (arg, "--dbg"))
            setDebuggerVar(&m_fDbgEnabled, true);
        else if (!::strcmp( arg, "-debug") || !::strcmp (arg, "--debug"))
        {
            setDebuggerVar(&m_fDbgEnabled, true);
            setDebuggerVar(&m_fDbgAutoShow, true);
            setDebuggerVar(&m_fDbgAutoShowCommandLine, true);
            setDebuggerVar(&m_fDbgAutoShowStatistics, true);
        }
        else if (!::strcmp(arg, "--debug-command-line"))
        {
            setDebuggerVar(&m_fDbgEnabled, true);
            setDebuggerVar(&m_fDbgAutoShow, true);
            setDebuggerVar(&m_fDbgAutoShowCommandLine, true);
        }
        else if (!::strcmp(arg, "--debug-statistics"))
        {
            setDebuggerVar(&m_fDbgEnabled, true);
            setDebuggerVar(&m_fDbgAutoShow, true);
            setDebuggerVar(&m_fDbgAutoShowStatistics, true);
        }
        else if (!::strcmp(arg, "-no-debug") || !::strcmp(arg, "--no-debug"))
        {
            setDebuggerVar(&m_fDbgEnabled, false);
            setDebuggerVar(&m_fDbgAutoShow, false);
            setDebuggerVar(&m_fDbgAutoShowCommandLine, false);
            setDebuggerVar(&m_fDbgAutoShowStatistics, false);
        }
        /* Not quite debug options, but they're only useful with the debugger bits. */
        else if (!::strcmp(arg, "--start-paused"))
            m_enmStartRunning = StartRunning_No;
        else if (!::strcmp(arg, "--start-running"))
            m_enmStartRunning = StartRunning_Yes;
#endif
        /** @todo add an else { msgbox(syntax error); exit(1); } here, pretty please... */
        i++;
    }

    if (startVM)
    {
        /* m_fSeparateProcess makes sense only if a VM is started. */
        m_fSeparateProcess = fSeparateProcess;

        /* Search for corresponding VM: */
        QUuid uuid = QUuid(vmNameOrUuid);
        const CMachine machine = m_nemu.FindMachine(vmNameOrUuid);
        if (!uuid.isNull())
        {
            if (machine.isNull() && showStartVMErrors())
                return msgCenter().cannotFindMachineById(m_nemu, vmNameOrUuid);
        }
        else
        {
            if (machine.isNull() && showStartVMErrors())
                return msgCenter().cannotFindMachineByName(m_nemu, vmNameOrUuid);
        }
        vmUuid = machine.GetId();
    }

    /* After initializing *vmUuid* we already know if that is VM process or not: */
    if (!isVMConsoleProcess())
    {
        /* We should create separate logging file for VM selector: */
        char szLogFile[RTPATH_MAX];
        const char *pszLogFile = NULL;
        com::GetNemuUserHomeDirectory(szLogFile, sizeof(szLogFile));
        RTPathAppend(szLogFile, sizeof(szLogFile), "selectorwindow.log");
        pszLogFile = szLogFile;
        /* Create release logger, to file: */
        char szError[RTPATH_MAX + 128];
        com::NemuLogRelCreate("GUI VM Selector Window",
                              pszLogFile,
                              RTLOGFLAGS_PREFIX_TIME_PROG,
                              "all",
                              "NEMU_GUI_SELECTORWINDOW_RELEASE_LOG",
                              RTLOGDEST_FILE,
                              UINT32_MAX,
                              1,
                              60 * 60,
                              _1M,
                              szError,
                              sizeof(szError));
    }

    if (mSettingsPwSet)
        m_nemu.SetSettingsSecret(mSettingsPw);

    if (visualStateType != UIVisualStateType_Invalid && !vmUuid.isEmpty())
        gEDataManager->setRequestedVisualState(visualStateType, vmUuid);

#ifdef NEMU_WITH_DEBUGGER_GUI
    /* setup the debugger gui. */
    if (RTEnvExist("NEMU_GUI_NO_DEBUGGER"))
        m_fDbgEnabled = m_fDbgAutoShow =  m_fDbgAutoShowCommandLine = m_fDbgAutoShowStatistics = false;
    if (m_fDbgEnabled)
    {
        RTERRINFOSTATIC ErrInfo;
        RTErrInfoInitStatic(&ErrInfo);
        int vrc = SUPR3HardenedLdrLoadAppPriv("NemuDbg", &m_hNemuDbg, RTLDRLOAD_FLAGS_LOCAL, &ErrInfo.Core);
        if (RT_FAILURE(vrc))
        {
            m_hNemuDbg = NIL_RTLDRMOD;
            m_fDbgAutoShow = m_fDbgAutoShowCommandLine = m_fDbgAutoShowStatistics = false;
            LogRel(("Failed to load NemuDbg, rc=%Rrc - %s\n", vrc, ErrInfo.Core.pszMsg));
        }
    }
#endif

    mValid = true;

    UIConverter::prepare();

    /* Cache IMedium data.
     * There could be no used mediums at all,
     * but this method should be run anyway just to enumerate null UIMedium object,
     * used by some Nemu smart widgets, like NemuMediaComboBox: */
    m_pMediumEnumerator = new UIMediumEnumerator;
    {
        /* Prepare medium-enumerator: */
        connect(m_pMediumEnumerator, SIGNAL(sigMediumCreated(const QString&)),
                this, SIGNAL(sigMediumCreated(const QString&)));
        connect(m_pMediumEnumerator, SIGNAL(sigMediumDeleted(const QString&)),
                this, SIGNAL(sigMediumDeleted(const QString&)));
        connect(m_pMediumEnumerator, SIGNAL(sigMediumEnumerationStarted()),
                this, SIGNAL(sigMediumEnumerationStarted()));
        connect(m_pMediumEnumerator, SIGNAL(sigMediumEnumerated(const QString&)),
                this, SIGNAL(sigMediumEnumerated(const QString&)));
        connect(m_pMediumEnumerator, SIGNAL(sigMediumEnumerationFinished()),
                this, SIGNAL(sigMediumEnumerationFinished()));
    }
    if (agressiveCaching())
        startMediumEnumeration();

    /* Create shortcut pool: */
    UIShortcutPool::create();

#ifdef NEMU_GUI_WITH_NETWORK_MANAGER
    /* Create network manager: */
    UINetworkManager::create();

    /* Schedule update manager: */
    UIUpdateManager::schedule();
#endif /* NEMU_GUI_WITH_NETWORK_MANAGER */

#ifdef RT_OS_LINUX
    /* Make sure no wrong USB mounted: */
    checkForWrongUSBMounted();
#endif /* RT_OS_LINUX */
}

void NemuGlobal::cleanup()
{
    /* Preventing some unwanted stuff
     * which could de called from the other threads: */
    m_sfCleanupInProgress = true;

#ifdef NEMU_GUI_WITH_NETWORK_MANAGER
    /* Shutdown update manager: */
    UIUpdateManager::shutdown();

    /* Destroy network manager: */
    UINetworkManager::destroy();
#endif /* NEMU_GUI_WITH_NETWORK_MANAGER */

    /* Destroy shortcut pool: */
    UIShortcutPool::destroy();

#ifdef NEMU_GUI_WITH_PIDFILE
    deletePidfile();
#endif

    /* Destroy the GUI root windows _BEFORE_ the media-mess, because there is
       code in the GUI that's using the media code an will be racing us! */
    if (gpSelectorWindow)
        UISelectorWindow::destroy();
    if (gpMachine)
        UIMachine::destroy();

    /* Cleanup medium-enumerator: */
    m_mediumEnumeratorDtorRwLock.lockForWrite();
    delete m_pMediumEnumerator;
    m_pMediumEnumerator = 0;
    m_mediumEnumeratorDtorRwLock.unlock();

    /* Destroy extra-data manager: */
    UIExtraDataManager::destroy();

    /* Destroy whatever this converter stuff is: */
    UIConverter::cleanup();

    /* Cleanup thread-pool: */
    delete m_pThreadPool;
    m_pThreadPool = 0;
    /* Cleanup general icon-pool: */
    delete m_pIconPool;
    m_pIconPool = 0;

    /* ensure CGuestOSType objects are no longer used */
    m_guestOSFamilyIDs.clear();
    m_guestOSTypes.clear();

    /* Starting COM cleanup: */
    m_comCleanupProtectionToken.lockForWrite();
    {
        /* First, make sure we don't use COM any more: */
        m_host.detach();
        m_nemu.detach();
        m_client.detach();

        /* There may be UIMedium(s)EnumeratedEvent instances still in the message
         * queue which reference COM objects. Remove them to release those objects
         * before uninitializing the COM subsystem. */
        QApplication::removePostedEvents(this);

        /* Finally cleanup COM itself: */
        COMBase::CleanupCOM();
    }
    /* Finishing COM cleanup: */
    m_comCleanupProtectionToken.unlock();

    /* Destroy popup-center: */
    UIPopupCenter::destroy();
    /* Destroy message-center: */
    UIMessageCenter::destroy();

    mValid = false;
}

void NemuGlobal::sltHandleNemuSVCAvailabilityChange(bool fAvailable)
{
    /* Make sure the NemuSVC availability changed: */
    if (m_fNemuSVCAvailable == fAvailable)
        return;

    /* Cache the new NemuSVC availability value: */
    m_fNemuSVCAvailable = fAvailable;

    /* If NemuSVC is not available: */
    if (!m_fNemuSVCAvailable)
    {
        /* Mark wrappers invalid: */
        m_fWrappersValid = false;
        /* Re-fetch corresponding CVirtualBox to restart NemuSVC: */
        // CVirtualBox is still NULL in current Main implementation,
        // and this call do not restart anything, so we are waiting
        // for subsequent event about NemuSVC is available again.
        m_nemu = virtualBoxClient().GetVirtualBox();
    }
    /* If NemuSVC is available: */
    else
    {
        if (!m_fWrappersValid)
        {
            /* Re-init wrappers: */
            comWrappersReinit();

            /* If that is Selector UI: */
            if (!isVMConsoleProcess())
            {
                /* Recreate/show selector-window: */
                UISelectorWindow::destroy();
                UISelectorWindow::create();
            }
        }
    }

    /* Notify listeners about the NemuSVC availability change: */
    emit sigNemuSVCAvailabilityChange();
}

void NemuGlobal::comWrappersReinit()
{
    /* Re-fetch corresponding objects/values: */
    m_nemu = virtualBoxClient().GetVirtualBox();
    m_host = virtualBox().GetHost();
    m_strHomeFolder = virtualBox().GetHomeFolder();

    /* Re-initialize guest OS Type list: */
    m_guestOSFamilyIDs.clear();
    m_guestOSTypes.clear();
    const CGuestOSTypeVector guestOSTypes = m_nemu.GetGuestOSTypes();
    const int cGuestOSTypeCount = guestOSTypes.size();
    AssertMsg(cGuestOSTypeCount > 0, ("Number of OS types must not be zero"));
    if (cGuestOSTypeCount > 0)
    {
        /* Here we ASSUME the 'Other' types are always the first,
         * so we remember them and will append them to the list when finished.
         * We do a two pass, first adding the specific types, then the two 'Other' types. */
        for (int j = 0; j < 2; ++j)
        {
            int cMax = j == 0 ? cGuestOSTypeCount : RT_MIN(2, cGuestOSTypeCount);
            for (int i = j == 0 ? 2 : 0; i < cMax; ++i)
            {
                const CGuestOSType os = guestOSTypes.at(i);
                const QString strFamilyID = os.GetFamilyId();
                if (!m_guestOSFamilyIDs.contains(strFamilyID))
                {
                    m_guestOSFamilyIDs << strFamilyID;
                    m_guestOSTypes << QList<CGuestOSType>();
                }
                m_guestOSTypes[m_guestOSFamilyIDs.indexOf(strFamilyID)].append(os);
            }
        }
    }

    /* Mark wrappers valid: */
    m_fWrappersValid = true;
}

#ifdef NEMU_WITH_DEBUGGER_GUI

# define NEMUGLOBAL_DBG_CFG_VAR_FALSE       (0)
# define NEMUGLOBAL_DBG_CFG_VAR_TRUE        (1)
# define NEMUGLOBAL_DBG_CFG_VAR_MASK        (1)
# define NEMUGLOBAL_DBG_CFG_VAR_CMD_LINE    RT_BIT(3)
# define NEMUGLOBAL_DBG_CFG_VAR_DONE        RT_BIT(4)

/**
 * Initialize a debugger config variable.
 *
 * @param   piDbgCfgVar         The debugger config variable to init.
 * @param   pszEnvVar           The environment variable name relating to this
 *                              variable.
 * @param   pszExtraDataName    The extra data name relating to this variable.
 * @param   fDefault            The default value.
 */
void NemuGlobal::initDebuggerVar(int *piDbgCfgVar, const char *pszEnvVar, const char *pszExtraDataName, bool fDefault)
{
    QString strEnvValue;
    char    szEnvValue[256];
    int rc = RTEnvGetEx(RTENV_DEFAULT, pszEnvVar, szEnvValue, sizeof(szEnvValue), NULL);
    if (RT_SUCCESS(rc))
    {
        strEnvValue = QString::fromUtf8(&szEnvValue[0]).toLower().trimmed();
        if (strEnvValue.isEmpty())
            strEnvValue = "yes";
    }
    else if (rc != VERR_ENV_VAR_NOT_FOUND)
        strEnvValue = "veto";

    QString strExtraValue = m_nemu.GetExtraData(pszExtraDataName).toLower().trimmed();
    if (strExtraValue.isEmpty())
        strExtraValue = QString();

    if ( strEnvValue.contains("veto") || strExtraValue.contains("veto"))
        *piDbgCfgVar = NEMUGLOBAL_DBG_CFG_VAR_DONE | NEMUGLOBAL_DBG_CFG_VAR_FALSE;
    else if (strEnvValue.isNull() && strExtraValue.isNull())
        *piDbgCfgVar = fDefault ? NEMUGLOBAL_DBG_CFG_VAR_TRUE : NEMUGLOBAL_DBG_CFG_VAR_FALSE;
    else
    {
        QString *pStr = !strEnvValue.isEmpty() ? &strEnvValue : &strExtraValue;
        if (   pStr->startsWith("y")  // yes
            || pStr->startsWith("e")  // enabled
            || pStr->startsWith("t")  // true
            || pStr->startsWith("on")
            || pStr->toLongLong() != 0)
            *piDbgCfgVar = NEMUGLOBAL_DBG_CFG_VAR_TRUE;
        else if (   pStr->startsWith("n")  // o
                 || pStr->startsWith("d")  // disable
                 || pStr->startsWith("f")  // false
                 || pStr->startsWith("off")
                 || pStr->contains("veto") /* paranoia */
                 || pStr->toLongLong() == 0)
            *piDbgCfgVar = NEMUGLOBAL_DBG_CFG_VAR_FALSE;
        else
        {
            LogFunc(("Ignoring unknown value '%s' for '%s'\n", pStr->toAscii().constData(), pStr == &strEnvValue ? pszEnvVar : pszExtraDataName));
            *piDbgCfgVar = fDefault ? NEMUGLOBAL_DBG_CFG_VAR_TRUE : NEMUGLOBAL_DBG_CFG_VAR_FALSE;
        }
    }
}

/**
 * Set a debugger config variable according according to start up argument.
 *
 * @param   piDbgCfgVar         The debugger config variable to set.
 * @param   fState              The value from the command line.
 */
void NemuGlobal::setDebuggerVar(int *piDbgCfgVar, bool fState)
{
    if (!(*piDbgCfgVar & NEMUGLOBAL_DBG_CFG_VAR_DONE))
        *piDbgCfgVar = (fState ? NEMUGLOBAL_DBG_CFG_VAR_TRUE : NEMUGLOBAL_DBG_CFG_VAR_FALSE)
                     | NEMUGLOBAL_DBG_CFG_VAR_CMD_LINE;
}

/**
 * Checks the state of a debugger config variable, updating it with the machine
 * settings on the first invocation.
 *
 * @returns true / false.
 * @param   piDbgCfgVar         The debugger config variable to consult.
 * @param   pszExtraDataName    The extra data name relating to this variable.
 */
bool NemuGlobal::isDebuggerWorker(int *piDbgCfgVar, const char *pszExtraDataName) const
{
    if (!(*piDbgCfgVar & NEMUGLOBAL_DBG_CFG_VAR_DONE))
    {
        const QString str = gEDataManager->debugFlagValue(pszExtraDataName);
        if (str.contains("veto"))
            *piDbgCfgVar = NEMUGLOBAL_DBG_CFG_VAR_DONE | NEMUGLOBAL_DBG_CFG_VAR_FALSE;
        else if (str.isEmpty() || (*piDbgCfgVar & NEMUGLOBAL_DBG_CFG_VAR_CMD_LINE))
            *piDbgCfgVar |= NEMUGLOBAL_DBG_CFG_VAR_DONE;
        else if (   str.startsWith("y")  // yes
                 || str.startsWith("e")  // enabled
                 || str.startsWith("t")  // true
                 || str.startsWith("on")
                 || str.toLongLong() != 0)
            *piDbgCfgVar = NEMUGLOBAL_DBG_CFG_VAR_DONE | NEMUGLOBAL_DBG_CFG_VAR_TRUE;
        else if (   str.startsWith("n")  // no
                 || str.startsWith("d")  // disable
                 || str.startsWith("f")  // false
                 || str.toLongLong() == 0)
            *piDbgCfgVar = NEMUGLOBAL_DBG_CFG_VAR_DONE | NEMUGLOBAL_DBG_CFG_VAR_FALSE;
        else
            *piDbgCfgVar |= NEMUGLOBAL_DBG_CFG_VAR_DONE;
    }

    return (*piDbgCfgVar & NEMUGLOBAL_DBG_CFG_VAR_MASK) == NEMUGLOBAL_DBG_CFG_VAR_TRUE;
}

#endif /* NEMU_WITH_DEBUGGER_GUI */

bool NemuGlobal::showUI()
{
    /* Load application settings: */
    NemuGlobalSettings appSettings = settings();

    /* Show Selector UI: */
    if (!isVMConsoleProcess())
    {
        /* Make sure Selector UI is permitted: */
        if (appSettings.isFeatureActive("noSelector"))
        {
            msgCenter().cannotStartSelector();
            return false;
        }

#ifdef NEMU_BLEEDING_EDGE
        /* Show EXPERIMENTAL BUILD warning: */
        msgCenter().showExperimentalBuildWarning();
#else /* !NEMU_BLEEDING_EDGE */
# ifndef DEBUG
        /* Show BETA warning if necessary: */
        const QString nemuVersion(nemuGlobal().virtualBox().GetVersion());
        if (   nemuVersion.contains("BETA")
            && gEDataManager->preventBetaBuildWarningForVersion() != nemuVersion)
            msgCenter().showBetaBuildWarning();
# endif /* !DEBUG */
#endif /* !NEMU_BLEEDING_EDGE */

        /* Create/show selector-window: */
        UISelectorWindow::create();
    }
    /* Show Runtime UI: */
    else
    {
        /* Make sure machine is started: */
        if (!UIMachine::startMachine(nemuGlobal().managedVMUuid()))
            return false;
    }

    /* True by default: */
    return true;
}

bool NemuGlobal::switchToMachine(CMachine &machine)
{
#ifdef Q_WS_MAC
    ULONG64 id = machine.ShowConsoleWindow();
#else
    WId id = (WId) machine.ShowConsoleWindow();
#endif
    AssertWrapperOk(machine);
    if (!machine.isOk())
        return false;

    /* winId = 0 it means the console window has already done everything
     * necessary to implement the "show window" semantics. */
    if (id == 0)
        return true;

#if defined (Q_WS_WIN32) || defined (Q_WS_X11)

    return nemuGlobal().activateWindow(id, true);

#elif defined (Q_WS_MAC)
    /*
     * This is just for the case were the other process cannot steal
     * the focus from us. It will send us a PSN so we can try.
     */
    ProcessSerialNumber psn;
    psn.highLongOfPSN = id >> 32;
    psn.lowLongOfPSN = (UInt32)id;
    OSErr rc = ::SetFrontProcess(&psn);
    if (!rc)
        Log(("GUI: %#RX64 couldn't do SetFrontProcess on itself, the selector (we) had to do it...\n", id));
    else
        Log(("GUI: Failed to bring %#RX64 to front. rc=%#x\n", id, rc));
    return !rc;

#endif

    return false;

    /// @todo Below is the old method of switching to the console window
    //  based on the process ID of the console process. It should go away
    //  after the new (callback-based) method is fully tested.
#if 0

    if (!canSwitchTo())
        return false;

#if defined (Q_WS_WIN32)

    HWND hwnd = mWinId;

    /* if there are blockers (modal and modeless dialogs, etc), find the
     * topmost one */
    HWND hwndAbove = NULL;
    do
    {
        hwndAbove = GetNextWindow(hwnd, GW_HWNDPREV);
        HWND hwndOwner;
        if (hwndAbove != NULL &&
            ((hwndOwner = GetWindow(hwndAbove, GW_OWNER)) == hwnd ||
             hwndOwner  == hwndAbove))
            hwnd = hwndAbove;
        else
            break;
    }
    while (1);

    /* first, check that the primary window is visible */
    if (IsIconic(mWinId))
        ShowWindow(mWinId, SW_RESTORE);
    else if (!IsWindowVisible(mWinId))
        ShowWindow(mWinId, SW_SHOW);

#if 0
    LogFlowFunc(("mWinId=%08X hwnd=%08X\n", mWinId, hwnd));
#endif

    /* then, activate the topmost in the group */
    AllowSetForegroundWindow(m_pid);
    SetForegroundWindow(hwnd);

    return true;

#elif defined (Q_WS_X11)

    return false;

#elif defined (Q_WS_MAC)

    ProcessSerialNumber psn;
    OSStatus rc = ::GetProcessForPID(m_pid, &psn);
    if (!rc)
    {
        rc = ::SetFrontProcess(&psn);

        if (!rc)
        {
            ShowHideProcess(&psn, true);
            return true;
        }
    }
    return false;

#else

    return false;

#endif

#endif
}

bool NemuGlobal::launchMachine(CMachine &machine, LaunchMode enmLaunchMode /* = LaunchMode_Default */)
{
    /* Switch to machine window(s) if possible: */
    if (   machine.GetSessionState() == KSessionState_Locked /* precondition for CanShowConsoleWindow() */
        && machine.CanShowConsoleWindow())
        return NemuGlobal::switchToMachine(machine);

    if (enmLaunchMode != LaunchMode_Separate)
    {
        /* Make sure machine-state is one of required: */
        KMachineState state = machine.GetState(); NOREF(state);
        AssertMsg(   state == KMachineState_PoweredOff
                  || state == KMachineState_Saved
                  || state == KMachineState_Teleported
                  || state == KMachineState_Aborted
                  , ("Machine must be PoweredOff/Saved/Teleported/Aborted (%d)", state));
    }

    /* Create empty session instance: */
    CSession session;
    session.createInstance(CLSID_Session);
    if (session.isNull())
    {
        msgCenter().cannotOpenSession(session);
        return false;
    }

    /* Configure environment: */
    QString strEnv;
#ifdef Q_OS_WIN
    /* Allow started VM process to be foreground window: */
    AllowSetForegroundWindow(ASFW_ANY);
#endif /* Q_OS_WIN */
#ifdef Q_WS_X11
    /* Make sure VM process will start on the same display as the VM selector: */
    const char *pDisplay = RTEnvGet("DISPLAY");
    if (pDisplay)
        strEnv.append(QString("DISPLAY=%1\n").arg(pDisplay));
    const char *pXauth = RTEnvGet("XAUTHORITY");
    if (pXauth)
        strEnv.append(QString("XAUTHORITY=%1\n").arg(pXauth));
#endif /* Q_WS_X11 */
    QString strType;
    switch (enmLaunchMode)
    {
        case LaunchMode_Default:  strType = ""; break;
        case LaunchMode_Separate: strType = nemuGlobal().isSeparateProcess() ? "headless" : "separate"; break;
        case LaunchMode_Headless: strType = "headless"; break;
    }

    /* Prepare "VM spawning" progress: */
    CProgress progress = machine.LaunchVMProcess(session, strType, strEnv);
    if (!machine.isOk())
    {
        /* If the VM is started separately and the VM process is already running, then it is OK. */
        if (enmLaunchMode == LaunchMode_Separate)
        {
            KMachineState state = machine.GetState();
            if (   state >= KMachineState_FirstOnline
                && state <= KMachineState_LastOnline)
            {
                /* Already running. */
                return true;
            }
        }

        msgCenter().cannotOpenSession(machine);
        return false;
    }

    /* Postpone showing "VM spawning" progress.
     * Hope 1 minute will be enough to spawn any running VM silently,
     * otherwise we better show the progress...
     * If starting separately, then show the progress now. */
    int iSpawningDuration = enmLaunchMode == LaunchMode_Separate ? 0 : 60000;
    msgCenter().showModalProgressDialog(progress, machine.GetName(),
                                        ":/progress_start_90px.png", 0, iSpawningDuration);
    if (!progress.isOk() || progress.GetResultCode() != 0)
        msgCenter().cannotOpenSession(progress, machine.GetName());

    /* Unlock machine, close session: */
    session.UnlockMachine();

    /* True finally: */
    return true;
}

