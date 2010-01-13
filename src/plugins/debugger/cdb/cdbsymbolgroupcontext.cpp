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

#include "cdbsymbolgroupcontext.h"
#include "cdbdebugengine_p.h"
#include "watchhandler.h"
#include "watchutils.h"

#include <QtCore/QTextStream>
#include <QtCore/QCoreApplication>
#include <QtCore/QRegExp>

enum { debug = 0 };
enum { debugInternalDumpers = 0 };

static inline QString msgSymbolNotFound(const QString &s)
{
    return QString::fromLatin1("The symbol '%1' could not be found.").arg(s);
}

static inline QString msgOutOfScope()
{
    return QCoreApplication::translate("SymbolGroup", "Out of scope");
}

static inline bool isTopLevelSymbol(const DEBUG_SYMBOL_PARAMETERS &p)
{
    return p.ParentSymbol == DEBUG_ANY_ID;
}

static inline void debugSymbolFlags(unsigned long f, QTextStream &str)
{
    if (f & DEBUG_SYMBOL_EXPANDED)
        str << "DEBUG_SYMBOL_EXPANDED";
    if (f & DEBUG_SYMBOL_READ_ONLY)
        str << "|DEBUG_SYMBOL_READ_ONLY";
    if (f & DEBUG_SYMBOL_IS_ARRAY)
        str << "|DEBUG_SYMBOL_IS_ARRAY";
    if (f & DEBUG_SYMBOL_IS_FLOAT)
        str << "|DEBUG_SYMBOL_IS_FLOAT";
    if (f & DEBUG_SYMBOL_IS_ARGUMENT)
        str << "|DEBUG_SYMBOL_IS_ARGUMENT";
    if (f & DEBUG_SYMBOL_IS_LOCAL)
        str << "|DEBUG_SYMBOL_IS_LOCAL";
}

QTextStream &operator<<(QTextStream &str, const DEBUG_SYMBOL_PARAMETERS &p)
{
    str << " Type=" << p.TypeId << " parent=";
    if (isTopLevelSymbol(p)) {
        str << "<ROOT>";
    } else {
        str << p.ParentSymbol;
    }
    str << " Subs=" << p.SubElements << " flags=" << p.Flags << '/';
    debugSymbolFlags(p.Flags, str);
    return str;
}

// A helper function to extract a string value from a member function of
// IDebugSymbolGroup2 taking the symbol index and a character buffer.
// Pass in the the member function as '&IDebugSymbolGroup2::GetSymbolNameWide'

typedef HRESULT  (__stdcall IDebugSymbolGroup2::*WideStringRetrievalFunction)(ULONG, PWSTR, ULONG, PULONG);

static inline QString getSymbolString(IDebugSymbolGroup2 *sg,
                                      WideStringRetrievalFunction wsf,
                                      unsigned long index)
{
    // Template type names can get quite long....
    enum { BufSize = 1024 };
    static WCHAR nameBuffer[BufSize + 1];
    // Name
    ULONG nameLength;
    const HRESULT hr = (sg->*wsf)(index, nameBuffer, BufSize, &nameLength);
    if (SUCCEEDED(hr)) {
        nameBuffer[nameLength] = 0;
        return QString::fromUtf16(reinterpret_cast<const ushort *>(nameBuffer));
    }
    return QString();
}

namespace Debugger {
namespace Internal {


CdbSymbolGroupRecursionContext::CdbSymbolGroupRecursionContext(CdbSymbolGroupContext *ctx,
                                                         int ido,
                                                         CIDebugDataSpaces *ds) :
    context(ctx),
    internalDumperOwner(ido),
    dataspaces(ds)
{
}

static inline CdbSymbolGroupContext::SymbolState getSymbolState(const DEBUG_SYMBOL_PARAMETERS &p)
{
    if (p.SubElements == 0u)
        return CdbSymbolGroupContext::LeafSymbol;
    return (p.Flags & DEBUG_SYMBOL_EXPANDED) ?
               CdbSymbolGroupContext::ExpandedSymbol :
               CdbSymbolGroupContext::CollapsedSymbol;
}

CdbSymbolGroupContext::CdbSymbolGroupContext(const QString &prefix,
                                             CIDebugSymbolGroup *symbolGroup) :
    m_prefix(prefix),
    m_nameDelimiter(QLatin1Char('.')),
    m_symbolGroup(symbolGroup),
    m_unnamedSymbolNumber(1)
{
}

CdbSymbolGroupContext::~CdbSymbolGroupContext()
{
    m_symbolGroup->Release();
}

CdbSymbolGroupContext *CdbSymbolGroupContext::create(const QString &prefix,
                                                     CIDebugSymbolGroup *symbolGroup,
                                                     QString *errorMessage)
{
    CdbSymbolGroupContext *rc = new CdbSymbolGroupContext(prefix, symbolGroup);
    if (!rc->init(errorMessage)) {
        delete rc;
        return 0;
    }
    return rc;
}

bool CdbSymbolGroupContext::init(QString *errorMessage)
{
    // retrieve the root symbols
    ULONG count;
    HRESULT hr = m_symbolGroup->GetNumberSymbols(&count);
    if (FAILED(hr)) {
        *errorMessage = msgComFailed("GetNumberSymbols", hr);
        return false;
    }

    if (count) {
        m_symbolParameters.reserve(3u * count);
        m_symbolParameters.resize(count);

        hr = m_symbolGroup->GetSymbolParameters(0, count, symbolParameters());
        if (FAILED(hr)) {
            *errorMessage = QString::fromLatin1("In %1: %2 (%3 symbols)").arg(QLatin1String(Q_FUNC_INFO), msgComFailed("GetSymbolParameters", hr)).arg(count);
            return false;
        }
        populateINameIndexMap(m_prefix, DEBUG_ANY_ID, 0, count);
    }
    if (debug)
        qDebug() << Q_FUNC_INFO << '\n'<< toString();
    return true;
}

void CdbSymbolGroupContext::populateINameIndexMap(const QString &prefix, unsigned long parentId,
                                                  unsigned long start, unsigned long count)
{
    // Make the entries for iname->index mapping. We might encounter
    // already expanded subitems when doing it for top-level, recurse in that case.
    const QString symbolPrefix = prefix + m_nameDelimiter;
    if (debug)
        qDebug() << Q_FUNC_INFO << '\n'<< symbolPrefix << start << count;
    const unsigned long end = m_symbolParameters.size();
    unsigned long seenChildren = 0;
    // Skip over expanded children
    for (unsigned long i = start; i < end && seenChildren < count; i++) {
        const DEBUG_SYMBOL_PARAMETERS &p = m_symbolParameters.at(i);
        if (parentId == p.ParentSymbol) {
            seenChildren++;
            // "__formal" occurs when someone writes "void foo(int /* x */)..."
            static const QString unnamedFormalParameter = QLatin1String("__formal");
            QString symbolName = getSymbolString(m_symbolGroup, &IDebugSymbolGroup2::GetSymbolNameWide, i);
            if (symbolName == unnamedFormalParameter) {
                symbolName = QLatin1String("<unnamed");
                symbolName += QString::number(m_unnamedSymbolNumber++);
                symbolName += QLatin1Char('>');
            }
            const QString name = symbolPrefix + symbolName;
            m_inameIndexMap.insert(name, i);
            if (getSymbolState(p) == ExpandedSymbol)
                populateINameIndexMap(name, i, i + 1, p.SubElements);
        }
    }
}

QString CdbSymbolGroupContext::toString(bool verbose) const
{
    QString rc;
    QTextStream str(&rc);
    const int count = m_symbolParameters.size();
    for (int i = 0; i < count; i++) {
        str << i << ' ';
        const DEBUG_SYMBOL_PARAMETERS &p = m_symbolParameters.at(i);
        if (!isTopLevelSymbol(p))
            str << "    ";
        str << getSymbolString(m_symbolGroup, &IDebugSymbolGroup2::GetSymbolNameWide, i);
        if (p.Flags & DEBUG_SYMBOL_IS_LOCAL)
            str << " '" << getSymbolString(m_symbolGroup, &IDebugSymbolGroup2::GetSymbolTypeNameWide, i);
        str << p << '\n';
    }
    if (verbose) {
        str << "NameIndexMap\n";
        NameIndexMap::const_iterator ncend = m_inameIndexMap.constEnd();
        for (NameIndexMap::const_iterator it = m_inameIndexMap.constBegin() ; it != ncend; ++it)
            str << it.key() << ' ' << it.value() << '\n';
    }
    return rc;
}

CdbSymbolGroupContext::SymbolState CdbSymbolGroupContext::symbolState(unsigned long index) const
{
    return getSymbolState(m_symbolParameters.at(index));
}

CdbSymbolGroupContext::SymbolState CdbSymbolGroupContext::symbolState(const QString &prefix) const
{
    if (prefix == m_prefix) // root
        return ExpandedSymbol;
    unsigned long index;
    if (!lookupPrefix(prefix, &index)) {
        qWarning("WARNING %s: %s\n", Q_FUNC_INFO, qPrintable(msgSymbolNotFound(prefix)));
        return LeafSymbol;
    }
    return symbolState(index);
}

// Find index of a prefix
bool CdbSymbolGroupContext::lookupPrefix(const QString &prefix, unsigned long *index) const
{
    *index = 0;
    const NameIndexMap::const_iterator it = m_inameIndexMap.constFind(prefix);
    if (it == m_inameIndexMap.constEnd())
        return false;
    *index = it.value();
    return true;
}

/* Retrieve children and get the position. */
bool CdbSymbolGroupContext::getChildSymbolsPosition(const QString &prefix,
                                                    unsigned long *start,
                                                    unsigned long *parentId,
                                                    QString *errorMessage)
{
    if (debug)
        qDebug() << Q_FUNC_INFO << '\n'<< prefix;

    *start = *parentId = 0;
    // Root item?
    if (prefix == m_prefix) {
        *start = 0;
        *parentId = DEBUG_ANY_ID;
        if (debug)
            qDebug() << '<' << prefix << "at" << *start;
        return true;
    }
    // Get parent index, make sure it is expanded
    NameIndexMap::const_iterator nit = m_inameIndexMap.constFind(prefix);
    if (nit == m_inameIndexMap.constEnd()) {
        *errorMessage = QString::fromLatin1("'%1' not found.").arg(prefix);
        return false;
    }
    *parentId = nit.value();
    *start = nit.value() + 1;
    if (!expandSymbol(prefix, *parentId, errorMessage))
        return false;
    if (debug)
        qDebug() << '<' << prefix << "at" << *start;
    return true;
}

static inline QString msgExpandFailed(const QString &prefix, unsigned long index, const QString &why)
{
    return QString::fromLatin1("Unable to expand '%1' %2: %3").arg(prefix).arg(index).arg(why);
}

// Expand a symbol using the symbol group interface.
bool CdbSymbolGroupContext::expandSymbol(const QString &prefix, unsigned long index, QString *errorMessage)
{
    if (debug)
        qDebug() << '>' << Q_FUNC_INFO << '\n' << prefix << index;

    switch (symbolState(index)) {
    case LeafSymbol:
        *errorMessage = QString::fromLatin1("Attempt to expand leaf node '%1' %2!").arg(prefix).arg(index);
        return false;
    case ExpandedSymbol:
        return true;
    case CollapsedSymbol:
        break;
    }

    HRESULT hr = m_symbolGroup->ExpandSymbol(index, TRUE);
    if (FAILED(hr)) {
        *errorMessage = msgExpandFailed(prefix, index, msgComFailed("ExpandSymbol", hr));
        return false;
    }
    // Hopefully, this will never fail, else data structure will be foobar.
    const ULONG oldSize = m_symbolParameters.size();
    ULONG newSize;
    hr = m_symbolGroup->GetNumberSymbols(&newSize);
    if (FAILED(hr)) {
        *errorMessage = msgExpandFailed(prefix, index, msgComFailed("GetNumberSymbols", hr));
        return false;
    }

    // Retrieve the new parameter structs which will be inserted
    // after the parents, offsetting consecutive indexes.
    m_symbolParameters.resize(newSize);

    hr = m_symbolGroup->GetSymbolParameters(0, newSize, symbolParameters());
    if (FAILED(hr)) {
        *errorMessage = msgExpandFailed(prefix, index, msgComFailed("GetSymbolParameters", hr));
        return false;
    }
    // The new symbols are inserted after the parent symbol.
    // We need to correct the following values in the name->index map
    const unsigned long newSymbolCount = newSize - oldSize;
    const NameIndexMap::iterator nend = m_inameIndexMap.end();
    for (NameIndexMap::iterator it = m_inameIndexMap.begin(); it != nend; ++it)
        if (it.value() > index)
            it.value() += newSymbolCount;
    // insert the new symbols
    populateINameIndexMap(prefix, index, index + 1, newSymbolCount);
    if (debug > 1)
        qDebug() << '<' << Q_FUNC_INFO << '\n' << prefix << index << '\n' << toString();
    return true;
}

void CdbSymbolGroupContext::clear()
{
    m_symbolParameters.clear();
    m_inameIndexMap.clear();
}

QString CdbSymbolGroupContext::symbolINameAt(unsigned long index) const
{
    return m_inameIndexMap.key(index);
}

static inline QString hexSymbolOffset(CIDebugSymbolGroup *sg, unsigned long index)
{
    ULONG64 rc = 0;
    if (FAILED(sg->GetSymbolOffset(index, &rc)))
        rc = 0;
    return QLatin1String("0x") + QString::number(rc, 16);
}

// check for "0x000", "0x000 class X"
static inline bool isNullPointer(const WatchData &wd)
{
    if (!isPointerType(wd.type))
        return false;
    static const QRegExp hexNullPattern(QLatin1String("0x0+"));
    Q_ASSERT(hexNullPattern.isValid());
    const int blankPos = wd.value.indexOf(QLatin1Char(' '));
    if (blankPos == -1)
        return hexNullPattern.exactMatch(wd.value);
    const QString addr = wd.value.mid(0, blankPos);
    return hexNullPattern.exactMatch(addr);
}

// Fix a symbol group value. It is set to the class type for
// expandable classes (type="class std::foo<..>[*]",
// value="std::foo<...>[*]". This is not desired
// as it widens the value column for complex std::template types.
// Remove the inner template type.

static inline QString removeInnerTemplateType(QString value)
{
    const int firstBracketPos = value.indexOf(QLatin1Char('<'));
    const int lastBracketPos = firstBracketPos != -1 ? value.lastIndexOf(QLatin1Char('>')) : -1;
    if (lastBracketPos != -1)
        value.replace(firstBracketPos + 1, lastBracketPos - firstBracketPos -1, QLatin1String("..."));
    return value;
}

static inline QString fixValue(const QString &value)
{
    if (value.size() < 20 || value.endsWith(QLatin1Char('"')))
        return value;
    return removeInnerTemplateType(value);
}

WatchData CdbSymbolGroupContext::symbolAt(unsigned long index) const
{
    WatchData wd;
    wd.iname = symbolINameAt(index);
    wd.exp = wd.iname;
    const int lastDelimiterPos = wd.iname.lastIndexOf(m_nameDelimiter);
    // For class hierarchies, we get sometimes complicated std::template types here.
    // Remove them for display
    wd.name = removeInnerTemplateType(lastDelimiterPos == -1 ? wd.iname : wd.iname.mid(lastDelimiterPos + 1));
    wd.addr = hexSymbolOffset(m_symbolGroup, index);
    const QString type = getSymbolString(m_symbolGroup, &IDebugSymbolGroup2::GetSymbolTypeNameWide, index);
    const QString value = getSymbolString(m_symbolGroup, &IDebugSymbolGroup2::GetSymbolValueTextWide, index);
    wd.setType(type);
    wd.setValue(fixValue(value));
    wd.setChildrenNeeded(); // compensate side effects of above setters
    // Figure out children. The SubElement is only a guess unless the symbol,
    // is expanded, so, we leave this as a guess for later updates.
    // If the symbol has children (expanded or not), we leave the 'Children' flag
    // in 'needed' state. Suppress 0-pointers right ("0x000 class X")
    // here as they only lead to children with memory access errors.
    const bool hasChildren = m_symbolParameters.at(index).SubElements && !isNullPointer(wd);
    wd.setHasChildren(hasChildren);
    if (debug > 1)
        qDebug() << Q_FUNC_INFO << index << '\n' << wd.toString();
    return wd;
}

WatchData CdbSymbolGroupContext::dumpSymbolAt(CIDebugDataSpaces *ds, unsigned long index)
{
    WatchData rc = symbolAt(index);
    dump(ds, &rc);
    return rc;
}

bool CdbSymbolGroupContext::assignValue(const QString &iname, const QString &value,
                                        QString *newValue, QString *errorMessage)
{
    if (debugCDB)
        qDebug() << Q_FUNC_INFO << '\n' << iname << value;
    const NameIndexMap::const_iterator it = m_inameIndexMap.constFind(iname);
    if (it == m_inameIndexMap.constEnd()) {
        *errorMessage = msgSymbolNotFound(iname);
        return false;
    }
    const unsigned long  index = it.value();
    const HRESULT hr = m_symbolGroup->WriteSymbolWide(index, reinterpret_cast<PCWSTR>(value.utf16()));
    if (FAILED(hr)) {
        *errorMessage = QString::fromLatin1("Unable to assign '%1' to '%2': %3").
                        arg(value, iname, msgComFailed("WriteSymbolWide", hr));
        return false;
    }
    if (newValue)
        *newValue = getSymbolString(m_symbolGroup, &IDebugSymbolGroup2::GetSymbolValueTextWide, index);
    return true;
}

// format an array of integers as "0x323, 0x2322, ..."
template <class Integer>
static QString formatArrayHelper(const Integer *array, int size, int base = 10)
{
    QString rc;
    const QString hexPrefix = QLatin1String("0x");
    const QString separator = QLatin1String(", ");
    const bool hex = base == 16;
    for (int i = 0; i < size; i++) {
        if (i)
            rc += separator;
        if (hex)
            rc += hexPrefix;
        rc += QString::number(array[i], base);
    }
    return rc;
}

QString CdbSymbolGroupContext::hexFormatArray(const unsigned short *array, int size)
{
    return formatArrayHelper(array, size, 16);
}

// Helper to format an integer with
// a hex prefix in case base = 16
template <class Integer>
        inline QString formatInteger(Integer value, int base)
{
    QString rc;
    if (base == 16)
        rc = QLatin1String("0x");
    rc += QString::number(value, base);
    return rc;
}

QString CdbSymbolGroupContext::debugValueToString(const DEBUG_VALUE &dv, CIDebugControl *ctl,
                                                  QString *qType,
                                                  int integerBase)
{
    switch (dv.Type) {
    case DEBUG_VALUE_INT8:
        if (qType)
            *qType = QLatin1String("char");
        return formatInteger(dv.I8, integerBase);
    case DEBUG_VALUE_INT16:
        if (qType)
            *qType = QLatin1String("short");
        return formatInteger(static_cast<short>(dv.I16), integerBase);
    case DEBUG_VALUE_INT32:
        if (qType)
            *qType = QLatin1String("long");
        return formatInteger(static_cast<long>(dv.I32), integerBase);
    case DEBUG_VALUE_INT64:
        if (qType)
            *qType = QLatin1String("long long");
        return formatInteger(static_cast<long long>(dv.I64), integerBase);
    case DEBUG_VALUE_FLOAT32:
        if (qType)
            *qType = QLatin1String("float");
        return QString::number(dv.F32);
    case DEBUG_VALUE_FLOAT64:
        if (qType)
            *qType = QLatin1String("double");
        return QString::number(dv.F64);
    case DEBUG_VALUE_FLOAT80:
    case DEBUG_VALUE_FLOAT128: { // Convert to double
            DEBUG_VALUE doubleValue;
            double d = 0.0;
            if (SUCCEEDED(ctl->CoerceValue(const_cast<DEBUG_VALUE*>(&dv), DEBUG_VALUE_FLOAT64, &doubleValue)))
                d = dv.F64;
            if (qType)
                *qType = QLatin1String(dv.Type == DEBUG_VALUE_FLOAT80 ? "80bit-float" : "128bit-float");
            return QString::number(d);
        }
    case DEBUG_VALUE_VECTOR64: {
            if (qType)
                *qType = QLatin1String("64bit-vector");
            QString rc = QLatin1String("bytes: ");
            rc += formatArrayHelper(dv.VI8, 8, integerBase);
            rc += QLatin1String(" long: ");
            rc += formatArrayHelper(dv.VI32, 2, integerBase);
            return rc;
        }
    case DEBUG_VALUE_VECTOR128: {
            if (qType)
                *qType = QLatin1String("128bit-vector");
            QString rc = QLatin1String("bytes: ");
            rc += formatArrayHelper(dv.VI8, 16, integerBase);
            rc += QLatin1String(" long long: ");
            rc += formatArrayHelper(dv.VI64, 2, integerBase);
            return rc;
        }
    }
    if (qType)
        *qType = QString::fromLatin1("Unknown type #%1:").arg(dv.Type);
    return formatArrayHelper(dv.RawBytes, 24, integerBase);
}

bool CdbSymbolGroupContext::debugValueToInteger(const DEBUG_VALUE &dv, qint64 *value)
{
    *value = 0;
    switch (dv.Type) {
    case DEBUG_VALUE_INT8:
        *value = dv.I8;
        return true;
    case DEBUG_VALUE_INT16:
        *value = static_cast<short>(dv.I16);
        return true;
    case DEBUG_VALUE_INT32:
        *value = static_cast<long>(dv.I32);
        return true;
    case DEBUG_VALUE_INT64:
        *value = static_cast<long long>(dv.I64);
        return true;
    default:
        break;
    }
    return false;
}

/* The special type dumpers have an integer return code meaning:
 *  0:  ok
 *  1:  Dereferencing or retrieving memory failed, this is out of scope,
 *      do not try to query further.
 * > 1: A structural error was encountered, that is, the implementation
 *      of the class changed (Qt or say, a different STL implementation).
 *      Visibly warn about it.
 * To add further types, have a look at the toString() output of the
 * symbol group. */

static QString msgStructuralError(const QString &type, int code)
{
    return QString::fromLatin1("Warning: Internal dumper for '%1' failed with %2.").arg(type).arg(code);
}

static inline bool isStdStringOrPointer(const QString &type)
{
#define STD_WSTRING "std::basic_string<unsigned short,std::char_traits<unsigned short>,std::allocator<unsigned short> >"
#define STD_STRING "std::basic_string<char,std::char_traits<char>,std::allocator<char> >"
    return type.endsWith(QLatin1String(STD_STRING))
            || type.endsWith(QLatin1String(STD_STRING" *"))
            || type.endsWith(QLatin1String(STD_WSTRING))
            || type.endsWith(QLatin1String(STD_WSTRING" *"));
#undef STD_WSTRING
#undef STD_STRING
}

CdbSymbolGroupContext::DumperResult
        CdbSymbolGroupContext::dump(CIDebugDataSpaces *ds, WatchData *wd)
{
    DumperResult rc = DumperNotHandled;
    do {
        // Is this a previously detected Null-Pointer?
        if (wd->isHasChildrenKnown() && !wd->hasChildren)
            break;
        // QString
        if (wd->type.endsWith(QLatin1String("QString")) || wd->type.endsWith(QLatin1String("QString *"))) {
            const int drc = dumpQString(ds, wd);
            switch (drc) {
            case 0:
                rc = DumperOk;
                break;
            case 1:
                rc = DumperError;
                break;
            default:
                qWarning("%s\n", qPrintable(msgStructuralError(wd->type, drc)));
                rc = DumperNotHandled;
                break;
            }
        }
        // StdString
        if (isStdStringOrPointer(wd->type)) {
            const int drc = dumpStdString(wd);
            switch (drc) {
            case 0:
                rc = DumperOk;
                break;
            case 1:
                rc = DumperError;
                break;
            default:
                qWarning("%s\n", qPrintable(msgStructuralError(wd->type, drc)));
                rc = DumperNotHandled;
                break;
            }

        }
    } while (false);
    if (debugInternalDumpers)
        qDebug() << "CdbSymbolGroupContext::dump" << rc << wd->toString();
    return rc;
}

// Get integer value of symbol group
static inline bool getIntValue(CIDebugSymbolGroup *sg, int index, int *value)
{
    const QString valueS = getSymbolString(sg, &IDebugSymbolGroup2::GetSymbolValueTextWide, index);
    bool ok;
    *value = valueS.toInt(&ok);
    return ok;
}

// Get pointer value of symbol group ("0xAAB")
static inline bool getPointerValue(CIDebugSymbolGroup *sg, int index, quint64 *value)
{
    *value = 0;
    QString valueS = getSymbolString(sg, &IDebugSymbolGroup2::GetSymbolValueTextWide, index);
    if (!valueS.startsWith(QLatin1String("0x")))
        return false;
    valueS.remove(0, 2);
    bool ok;
    *value = valueS.toULongLong(&ok, 16);
    return ok;
}

int CdbSymbolGroupContext::dumpQString(CIDebugDataSpaces *ds, WatchData *wd)
{
    const int maxLength = 40;
    QString errorMessage;
    unsigned long stringIndex;
    if (!lookupPrefix(wd->iname, &stringIndex))
        return 1;

    // Expand string and it's "d" (step over 'static null')
    if (!expandSymbol(wd->iname, stringIndex, &errorMessage))
        return 2;
    const unsigned long dIndex = stringIndex + 4;
    if (!expandSymbol(wd->iname, dIndex, &errorMessage))
        return 3;
    const unsigned long sizeIndex = dIndex + 3;
    const unsigned long arrayIndex = dIndex + 4;
    // Get size and pointer
    int size;
    if (!getIntValue(m_symbolGroup, sizeIndex, &size))
        return 4;
    quint64 array;
    if (!getPointerValue(m_symbolGroup, arrayIndex, &array))
        return 5;
    // Fetch
    const bool truncated = size > maxLength;
    if (truncated)
        size = maxLength;
    const QChar doubleQuote = QLatin1Char('"');
    QString value;
    if (size > 0) {
        value += doubleQuote;
        // Should this ever be a remote debugger, need to check byte order.
        unsigned short *buf =  new unsigned short[size + 1];
        unsigned long bytesRead;
        const HRESULT hr = ds->ReadVirtual(array, buf, size * sizeof(unsigned short), &bytesRead);
        if (FAILED(hr)) {
            delete [] buf;
            return 1;
        }
        buf[bytesRead / sizeof(unsigned short)] = 0;
        value += QString::fromUtf16(buf);
        delete [] buf;
        if (truncated)
            value += QLatin1String("...");
        value += doubleQuote;
    } else if (size == 0) {
        value = QString(doubleQuote) + doubleQuote;
    } else {
        value = "Invalid QString";
    }

    wd->setValue(value);
    wd->setHasChildren(false);
    return 0;
}

int CdbSymbolGroupContext::dumpStdString(WatchData *wd)
{
    const int maxLength = 40;
    QString errorMessage;
    unsigned long stringIndex;
    if (!lookupPrefix(wd->iname, &stringIndex))
        return 1;

    // Expand string ->string_val->_bx.
    if (!expandSymbol(wd->iname, stringIndex, &errorMessage))
        return 1;
    const unsigned long bxIndex = stringIndex + 3;
    if (!expandSymbol(wd->iname, bxIndex, &errorMessage))
        return 2;
    // Check if size is something sane
    const int sizeIndex = stringIndex + 6;
    int size;
    if (!getIntValue(m_symbolGroup, sizeIndex, &size))
        return 3;
    if (size < 0)
        return 1;
    // Just copy over the value of the buf[]-array, which should be the string
    const QChar doubleQuote = QLatin1Char('"');
    const int bufIndex = stringIndex + 4;
    QString bufValue = getSymbolString(m_symbolGroup, &IDebugSymbolGroup2::GetSymbolValueTextWide, bufIndex);
    const int quotePos = bufValue.indexOf(doubleQuote);
    if (quotePos == -1)
        return 1;
    bufValue.remove(0, quotePos);
    if (bufValue.size() > maxLength) {
        bufValue.truncate(maxLength);
        bufValue += QLatin1String("...\"");
    }
    wd->setValue(bufValue);
    wd->setHasChildren(false);
    return 0;
}

} // namespace Internal
} // namespace Debugger
