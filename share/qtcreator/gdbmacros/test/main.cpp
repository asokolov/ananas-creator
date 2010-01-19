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

#include <QtCore/QStringList>
#include <QtCore/QVector>
#include <QtCore/QSharedPointer>
#include <QtCore/QTimer>
#include <QtCore/QMap>
#include <QtCore/QVariant>
#include <QtGui/QAction>

#include <string>
#include <list>
#include <vector>
#include <set>
#include <map>

#include <stdio.h>
#include <string.h>

// Test uninitialized variables allocing memory
bool optTestUninitialized = false;

template <class T>
        inline T* testAddress(T* in)
{
    return optTestUninitialized ?
        (reinterpret_cast<T*>(new char[sizeof(T)]))
        : in;
}

/* Test program for Dumper development/porting.
 * Takes the type as first argument. */

// --------------- Dumper symbols
extern char qDumpInBuffer[10000];
extern char qDumpOutBuffer[100000];

extern "C" void *qDumpObjectData440(
    int protocolVersion,
    int token,
    void *data,
#ifdef Q_CC_MSVC // CDB cannot handle boolean parameters
    int dumpChildren,
#else
    bool dumpChildren,
#endif
    int extraInt0, int extraInt1, int extraInt2, int extraInt3);

static void prepareInBuffer(const char *outerType,
                            const char *iname,
                            const char *expr,
                            const char *innerType)
{
    // Leave trailing '\0'
    char *ptr = qDumpInBuffer;
    strcpy(ptr, outerType);
    ptr += strlen(outerType);
    ptr++;
    strcpy(ptr, iname);
    ptr += strlen(iname);
    ptr++;
    strcpy(ptr, expr);
    ptr += strlen(expr);
    ptr++;
    strcpy(ptr, innerType);
    ptr += strlen(innerType);
    ptr++;
    strcpy(ptr, iname);
}

// ---------------  Qt types
static int dumpQString()
{
    QString test = QLatin1String("hallo");
    prepareInBuffer("QString", "local.qstring", "local.qstring", "");
    qDumpObjectData440(2, 42, testAddress(&test), 1, 0, 0, 0, 0);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    QString uninitialized;
    return 0;
}

static int dumpQSharedPointerQString()
{
    QSharedPointer<QString> test(new QString(QLatin1String("hallo")));
    prepareInBuffer("QSharedPointer", "local.sharedpointerqstring", "local.local.sharedpointerqstring", "QString");
    qDumpObjectData440(2, 42, testAddress(&test), 1, sizeof(QString), 0, 0, 0);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    QString uninitialized;
    return 0;
}

static int dumpQStringList()
{
    QStringList test = QStringList() << QLatin1String("item1") << QLatin1String("item2");
    prepareInBuffer("QList", "local.qstringlist", "local.qstringlist", "QString");
    qDumpObjectData440(2, 42, testAddress(&test), 1, sizeof(QString), 0, 0, 0);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    return 0;
}

static int dumpQIntList()
{
    QList<int> test = QList<int>() << 1 << 2;
    prepareInBuffer("QList", "local.qintlist", "local.qintlist", "int");
    qDumpObjectData440(2, 42, testAddress(&test), 1, sizeof(int), 0, 0, 0);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    return 0;
}

static int dumpQIntVector()
{
    QVector<int> test = QVector<int>() << 42 << 43;
    prepareInBuffer("QVector", "local.qintvector", "local.qintvector", "int");
    qDumpObjectData440(2, 42, testAddress(&test), 1, sizeof(int), 0, 0, 0);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    return 0;
}

static int dumpQMapIntInt()
{
    QMap<int,int> test;
    QMapNode<int,int> mapNode;
    const int valueOffset = (char*)&(mapNode.value) - (char*)&mapNode;
    test.insert(42, 43);
    test.insert(43, 44);
    prepareInBuffer("QMap", "local.qmapintint", "local.qmapintint", "int@int");
    qDumpObjectData440(2, 42, testAddress(&test), 1, sizeof(int), sizeof(int), sizeof(mapNode), valueOffset);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    return 0;
}

static int dumpQMapIntString()
{
    QMap<int,QString> test;
    QMapNode<int,QString> mapNode;
    const int valueOffset = (char*)&(mapNode.value) - (char*)&mapNode;
    test.insert(42, QLatin1String("fortytwo"));
    test.insert(43, QLatin1String("fortytree"));
    prepareInBuffer("QMap", "local.qmapintqstring", "local.qmapintqstring", "int@QString");
    qDumpObjectData440(2, 42, testAddress(&test), 1, sizeof(int), sizeof(QString), sizeof(mapNode), valueOffset);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    return 0;
}

static int dumpQVariant()
{
    QVariant test(QLatin1String("item"));
    prepareInBuffer("QVariant", "local.qvariant", "local.qvariant", "");
    qDumpObjectData440(2, 42, testAddress(&test), 1, 0, 0,0 ,0);
    fputs(qDumpOutBuffer, stdout);
    fputs("\n\n", stdout);
    test = QVariant(QStringList(QLatin1String("item1")));
    prepareInBuffer("QVariant", "local.qvariant", "local.qvariant", "");
    qDumpObjectData440(2, 42, testAddress(&test), 1, 0, 0,0 ,0);
    fputs(qDumpOutBuffer, stdout);
    test = QVariant(QRect(1,2, 3, 4));
    prepareInBuffer("QVariant", "local.qvariant", "local.qvariant", "");
    qDumpObjectData440(2, 42, testAddress(&test), 1, 0, 0,0 ,0);
    fputs(qDumpOutBuffer, stdout);
    return 0;
}

// ---------------  std types

static int dumpStdString()
{
    std::string test = "hallo";
    prepareInBuffer("std::string", "local.string", "local.string", "");
    qDumpObjectData440(2, 42, testAddress(&test), 1, 0, 0, 0, 0);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    return 0;
}

static int dumpStdWString()
{
    std::wstring test = L"hallo";
    prepareInBuffer("std::wstring", "local.wstring", "local.wstring", "");
    qDumpObjectData440(2, 42, testAddress(&test), 1, 0, 0, 0, 0);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    return 0;
}

static int dumpStdStringList()
{
    std::list<std::string> test;
    test.push_back("item1");
    test.push_back("item2");
    prepareInBuffer("std::list", "local.stringlist", "local.stringlist", "std::string");
    qDumpObjectData440(2, 42, testAddress(&test), 1, sizeof(std::string), sizeof(std::list<std::string>::allocator_type), 0, 0);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    return 0;
}

static int dumpStdStringQList()
{
    QList<std::string> test;
    test.push_back("item1");
    test.push_back("item2");
    prepareInBuffer("QList", "local.stringqlist", "local.stringqlist", "std::string");
    qDumpObjectData440(2, 42, testAddress(&test), 1, sizeof(std::string), 0, 0, 0);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    return 0;
}

static int dumpStdIntList()
{
    std::list<int> test;
    test.push_back(1);
    test.push_back(2);
    prepareInBuffer("std::list", "local.intlist", "local.intlist", "int");
    qDumpObjectData440(2, 42, testAddress(&test), 1, sizeof(int), sizeof(std::list<int>::allocator_type), 0, 0);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    return 0;
}

static int dumpStdIntVector()
{
    std::vector<int> test;
    test.push_back(1);
    test.push_back(2);
    prepareInBuffer("std::vector", "local.intvector", "local.intvector", "int");
    qDumpObjectData440(2, 42, testAddress(&test), 1, sizeof(int), sizeof(std::list<int>::allocator_type), 0, 0);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    return 0;
}

static int dumpStdStringVector()
{
    std::vector<std::string> test;
    test.push_back("item1");
    test.push_back("item2");
    prepareInBuffer("std::vector", "local.stringvector", "local.stringvector", "std::string");
    qDumpObjectData440(2, 42, testAddress(&test), 1, sizeof(std::string), sizeof(std::list<int>::allocator_type), 0, 0);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    return 0;
}

static int dumpStdWStringVector()
{
    std::vector<std::wstring> test;
    test.push_back(L"item1");
    test.push_back(L"item2");
    prepareInBuffer("std::vector", "local.wstringvector", "local.wstringvector", "std::wstring");
    qDumpObjectData440(2, 42, testAddress(&test), 1, sizeof(std::wstring), sizeof(std::list<int>::allocator_type), 0, 0);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    return 0;
}

static int dumpStdIntSet()
{
    std::set<int> test;
    test.insert(1);
    test.insert(2);
    prepareInBuffer("std::set", "local.intset", "local.intset", "int");
    qDumpObjectData440(2, 42, testAddress(&test), 1, sizeof(int), sizeof(std::list<int>::allocator_type), 0, 0);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    return 0;
}

static int dumpStdStringSet()
{
    std::set<std::string> test;
    test.insert("item1");
    test.insert("item2");
    prepareInBuffer("std::set", "local.stringset", "local.stringset", "std::string");
    qDumpObjectData440(2, 42, testAddress(&test), 1, sizeof(std::string), sizeof(std::list<int>::allocator_type), 0, 0);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    return 0;
}

static int dumpStdQStringSet()
{
    std::set<QString> test;
    test.insert(QLatin1String("item1"));
    test.insert(QLatin1String("item2"));
    prepareInBuffer("std::set", "local.stringset", "local.stringset", "QString");
    qDumpObjectData440(2, 42, testAddress(&test), 1, sizeof(QString), sizeof(std::list<int>::allocator_type), 0, 0);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    return 0;
}

static int dumpStdMapIntString()
{
    std::map<int,std::string> test;
    std::map<int,std::string>::value_type entry(42, std::string("fortytwo"));
    test.insert(entry);
    const int valueOffset = (char*)&(entry.second) - (char*)&entry;
    prepareInBuffer("std::map", "local.stdmapintstring", "local.stdmapintstring",
                    "int@std::basic_string<char,std::char_traits<char>,std::allocator<char> >@std::less<int>@std::allocator<std::pair<const int,std::basic_string<char,std::char_traits<char>,std::allocator<char> > > >");
    qDumpObjectData440(2, 42, testAddress(&test), 1, sizeof(int), sizeof(std::string), valueOffset, 0);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    return 0;
}

static int dumpStdMapStringString()
{
    typedef std::map<std::string,std::string> TestType;
    TestType test;
    const TestType::value_type entry("K", "V");
    test.insert(entry);
    const int valueOffset = (char*)&(entry.second) - (char*)&entry;
    prepareInBuffer("std::map", "local.stdmapstringstring", "local.stdmapstringstring",
                    "std::basic_string<char,std::char_traits<char>,std::allocator<char> >@std::basic_string<char,std::char_traits<char>,std::allocator<char> >@std::less<int>@std::allocator<std::pair<const std::basic_string<char,std::char_traits<char>,std::allocator<char> >,std::basic_string<char,std::char_traits<char>,std::allocator<char> > > >");
    qDumpObjectData440(2, 42, testAddress(&test), 1, sizeof(std::string), sizeof(std::string), valueOffset, 0);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    return 0;
}


static int dumpQObject()
{
    // Requires the childOffset to be know, but that is not critical
    QAction action(0);
    QObject x;
    QAction *a2= new QAction(&action);
    a2->setObjectName(QLatin1String("a2"));
    action.setObjectName(QLatin1String("action"));
    QObject::connect(&action, SIGNAL(triggered()), &x, SLOT(deleteLater()));
    prepareInBuffer("QObject", "local.qobject", "local.qobject", "");
    qDumpObjectData440(2, 42, testAddress(&action), 1, 0, 0, 0, 0);
    fputs(qDumpOutBuffer, stdout);
    fputs("\n\n", stdout);
    // Property list
    prepareInBuffer("QObjectPropertyList", "local.qobjectpropertylist", "local.qobjectpropertylist", "");
    qDumpObjectData440(2, 42, testAddress(&action), 1, 0, 0, 0, 0);
    fputs(qDumpOutBuffer, stdout);
    fputs("\n\n", stdout);
    // Signal list
    prepareInBuffer("QObjectSignalList", "local.qobjectsignallist", "local.qobjectsignallist", "");
    qDumpObjectData440(2, 42, testAddress(&action), 1, 0, 0, 0, 0);
    fputs(qDumpOutBuffer, stdout);
    // Slot list
    prepareInBuffer("QObjectSlotList", "local.qobjectslotlist", "local.qobjectslotlist", "");
    qDumpObjectData440(2, 42, testAddress(&action), 1, 0, 0, 0, 0);
    fputs(qDumpOutBuffer, stdout);
    fputs("\n\n", stdout);
    // Signal list
    prepareInBuffer("QObjectChildList", "local.qobjectchildlist", "local.qobjectchildlist", "");
    qDumpObjectData440(2, 42, testAddress(&action), 1, 0, 0, 0, 0);
    fputs(qDumpOutBuffer, stdout);
    return 0;
}

static int dumpQObjectList()
{
    // Requires the childOffset to be know, but that is not critical
    QObject *root = new QObject;
    root ->setObjectName("root");
    QTimer *t1 = new QTimer;
    t1 ->setObjectName("t1");
    QTimer *t2 = new QTimer;
    t2 ->setObjectName("t2");
    QObjectList test;
    test << root << t1 << t2;
    prepareInBuffer("QList", "local.qobjectlist", "local.qobjectlist", "QObject *");
    qDumpObjectData440(2, 42, testAddress(&test), sizeof(QObject*), 0, 0, 0, 0);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    delete root;
    return 0;
}

typedef int (*DumpFunction)();
typedef QMap<QString, DumpFunction> TypeDumpFunctionMap;

static TypeDumpFunctionMap registerTypes()
{
    TypeDumpFunctionMap rc;
    rc.insert("QString", dumpQString);
    rc.insert("QSharedPointer<QString>", dumpQSharedPointerQString);
    rc.insert("QStringList", dumpQStringList);
    rc.insert("QList<int>", dumpQIntList);
    rc.insert("QList<std::string>", dumpStdStringQList);
    rc.insert("QVector<int>", dumpQIntVector);
    rc.insert("QMap<int,QString>", dumpQMapIntString);
    rc.insert("QMap<int,int>", dumpQMapIntInt);
    rc.insert("string", dumpStdString);
    rc.insert("wstring", dumpStdWString);
    rc.insert("list<int>", dumpStdIntList);
    rc.insert("list<string>", dumpStdStringList);
    rc.insert("vector<int>", dumpStdIntVector);
    rc.insert("vector<string>", dumpStdStringVector);
    rc.insert("vector<wstring>", dumpStdWStringVector);
    rc.insert("set<int>", dumpStdIntSet);
    rc.insert("set<string>", dumpStdStringSet);
    rc.insert("set<QString>", dumpStdQStringSet);
    rc.insert("map<int,string>", dumpStdMapIntString);
    rc.insert("map<string,string>", dumpStdMapStringString);
    rc.insert("QObject", dumpQObject);
    rc.insert("QObjectList", dumpQObjectList);
    rc.insert("QVariant", dumpQVariant);
    return rc;
}

int main(int argc, char *argv[])
{
    printf("\nQt Creator Debugging Helper testing tool\n\n");
    printf("Running query protocol\n");
    qDumpObjectData440(1, 42, 0, 1, 0, 0, 0, 0);
    fputs(qDumpOutBuffer, stdout);
    fputc('\n', stdout);
    fputc('\n', stdout);

    const TypeDumpFunctionMap tdm = registerTypes();
    const TypeDumpFunctionMap::const_iterator cend = tdm.constEnd();

    if (argc < 2) {
        printf("Usage: %s [-a]|<type1> <type2..>\n", argv[0]);
        printf("Supported types: ");
        for (TypeDumpFunctionMap::const_iterator it = tdm.constBegin(); it != cend; ++it) {
            fputs(qPrintable(it.key()), stdout);
            fputc(' ', stdout);
        }
        fputc('\n', stdout);
        return 0;
    }

    int rc = 0;
    if (argc == 2 && !qstrcmp(argv[1], "-a")) {
        for (TypeDumpFunctionMap::const_iterator it = tdm.constBegin(); it != cend; ++it) {
            printf("\nTesting: %s\n", qPrintable(it.key()));
            rc += (*it.value())();
        }
    } else {
        for (int i = 1; i < argc; i++) {
            const char *arg = argv[i];
            printf("\nTesting: %s\n", arg);
            const TypeDumpFunctionMap::const_iterator it = tdm.constFind(QLatin1String(arg));
            if (it == cend) {
                rc = -1;
                fprintf(stderr, "\nUnhandled type: %s\n", argv[i]);
            } else {
                rc = (*it.value())();
            }
        }
    }
    return rc;
}
