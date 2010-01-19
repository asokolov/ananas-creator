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

#ifndef DEBUGGER_TRK_UTILS
#define DEBUGGER_TRK_UTILS

#include <QtCore/QByteArray>
#include <QtCore/QHash>
#include <QtCore/QString>
#include <QtCore/QVariant>

typedef unsigned char byte;

namespace trk {

enum Command {
    TrkPing = 0x00,
    TrkConnect = 0x01,
    TrkDisconnect = 0x02,
    TrkVersions = 0x04,
    TrkSupported = 0x05,
    TrkCpuType = 0x06,
    TrkHostVersions = 0x09,
    TrkContinue = 0x18,
    TrkCreateItem = 0x40,
    TrkDeleteItem = 0x41,

    TrkWriteFile = 0x48,
    TrkOpenFile = 0x4a,
    TrkCloseFile = 0x4b,
    TrkInstallFile = 0x4d,
    TrkInstallFile2 = 0x4e,

    TrkNotifyAck = 0x80,
    TrkNotifyNak = 0xff,
    TrkNotifyStopped = 0x90,
    TrkNotifyException = 0x91,
    TrkNotifyInternalError = 0x92,
    TrkNotifyCreated = 0xa0,
    TrkNotifyDeleted = 0xa1,
    TrkNotifyProcessorStarted = 0xa2,
    TrkNotifyProcessorStandBy = 0xa6,
    TrkNotifyProcessorReset = 0xa7
};

QByteArray decode7d(const QByteArray &ba);
QByteArray encode7d(const QByteArray &ba);

ushort extractShort(const char *data);
uint extractInt(const char *data);

QString quoteUnprintableLatin1(const QByteArray &ba);

// produces "xx "
QString stringFromByte(byte c);
// produces "xx xx xx "
QString stringFromArray(const QByteArray &ba, int maxLen = - 1);

enum Endianness
{
    LittleEndian,
    BigEndian,
    TargetByteOrder = BigEndian,
};

void appendByte(QByteArray *ba, byte b);
void appendShort(QByteArray *ba, ushort s, Endianness = TargetByteOrder);
void appendInt(QByteArray *ba, uint i, Endianness = TargetByteOrder);
void appendString(QByteArray *ba, const QByteArray &str, Endianness = TargetByteOrder, bool appendNullTerminator = true);

enum CodeMode
{
    ArmMode = 0,
    ThumbMode,
};

enum TargetConstants
{

    RegisterCount = 17,
    RegisterSP = 13, // Stack Pointer
    RegisterLR = 14, // Return address
    RegisterPC = 15, // Program counter
    RegisterPSGdb = 25, // gdb's view of the world
    RegisterPSTrk = 16, // TRK's view of the world

    MemoryChunkSize = 256
};

struct Session
{
    Session() {
        cpuMajor = 0;
        cpuMinor = 0;
        bigEndian = 0;
        defaultTypeSize = 0;
        fpTypeSize = 0;
        extended1TypeSize = 0;
        extended2TypeSize = 0;
        pid = 0;
        tid = 0;
        codeseg = 0;
        dataseg = 0;

        currentThread = 0;
    }

    // Trk feedback
    byte cpuMajor;
    byte cpuMinor;
    byte bigEndian;
    byte defaultTypeSize;
    byte fpTypeSize;
    byte extended1TypeSize;
    byte extended2TypeSize;
    uint pid;
    uint tid;
    uint codeseg;
    uint dataseg;
    QHash<uint, uint> tokenToBreakpointIndex;

    // Gdb request
    uint currentThread;
};

struct Snapshot
{
    uint registers[RegisterCount];
    typedef QHash<uint, QByteArray> Memory;
    Memory memory;
};

struct Breakpoint
{
    Breakpoint(uint offset_ = 0)
    {
        number = 0;
        offset = offset_;
        mode = ArmMode;
    }
    uint offset;
    ushort number;
    CodeMode mode;
};

struct TrkResult
{
    TrkResult() { code = token = 0; isDebugOutput = false; }
    QString toString() const;
    // 0 for no error.
    int errorCode() const;

    byte code;
    byte token;
    QByteArray data;
    QVariant cookie;
    bool isDebugOutput;
};

// returns a QByteArray containing 0x01 0x90 <len> 0x7e encoded7d(ba) 0x7e
QByteArray frameMessage(byte command, byte token, const QByteArray &data);
ushort isValidTrkResult(const QByteArray &buffer);
TrkResult extractResult(QByteArray *buffer);
QByteArray errorMessage(byte code);
QByteArray hexNumber(uint n, int digits = 0);
uint swapEndian(uint in);


} // namespace trk

#endif // DEBUGGER_TRK_UTILS
