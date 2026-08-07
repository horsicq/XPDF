/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef XJSINTERPRETER_H
#define XJSINTERPRETER_H

#include "xjsast.h"
#include <QMap>
#include <QSharedPointer>
#include <QString>
#include <QStringList>

namespace XJS {

class JSObject;
struct Environment;
class XJSParser;

// A JavaScript value (ECMAScript-like). Reference types (objects/arrays/functions/regex) share a
// JSObject through a QSharedPointer; primitives are held inline.
class XJSValue {
public:
    enum VType { V_UNDEFINED, V_NULL, V_BOOL, V_NUMBER, V_STRING, V_OBJECT };

    VType type;
    bool b;
    double n;
    QString s;
    QSharedPointer<JSObject> o;

    XJSValue() : type(V_UNDEFINED), b(false), n(0.0)
    {
    }

    static XJSValue undef();
    static XJSValue null();
    static XJSValue boolean(bool bValue);
    static XJSValue number(double nValue);
    static XJSValue string(const QString &sValue);
    static XJSValue object(const QSharedPointer<JSObject> &pObject);

    bool isObject() const
    {
        return (type == V_OBJECT) && !o.isNull();
    }
    bool isCallable() const;

    bool toBool() const;
    double toNumber() const;
    QString toStr() const;   // ECMAScript ToString
    QString typeOf() const;  // typeof operator
};

// Object / array / function / native / regex, distinguished by kind + className.
class JSObject {
public:
    enum OKind { O_PLAIN, O_ARRAY, O_FUNCTION, O_NATIVE, O_REGEX };

    OKind kind;
    QMap<QString, XJSValue> props;
    QList<XJSValue> elements;  // array storage
    Node *fnNode;              // user-function AST (N_FUNCTION); null for native
    Environment *closure;      // captured scope (arena-owned by the interpreter)
    QString nativeName;        // dispatch key for native functions
    QString className;         // "app","util","Doc","Collab","spell","console","Math","String","Array",...
    QString regexSource;
    QString regexFlags;

    JSObject() : kind(O_PLAIN), fnNode(nullptr), closure(nullptr)
    {
    }
};

// Lexical scope. Arena-owned raw parent pointer (all environments live for the whole run), which keeps
// closures cheap and sidesteps shared-pointer cycles.
struct Environment {
    QMap<QString, XJSValue> vars;
    Environment *parent;

    Environment() : parent(nullptr)
    {
    }
};

// Analysis result produced by a run.
struct XJSReport {
    QStringList apiCalls;        // notable Acrobat/JS API invocations (dangerous surface)
    QStringList evalArguments;   // deobfuscated payloads passed to eval/Function/setTimeout
    QStringList strings;         // other notable strings the script materialised
    QStringList indicators;      // heuristic exploit indicators
    QString consoleOutput;       // app.alert / console.println / print output
    bool bError;
    QString errorMessage;

    XJSReport() : bError(false)
    {
    }
};

// Tree-walking evaluator with an Acrobat/PDF DOM. Bounded by step/loop/recursion/string caps so hostile
// or non-terminating scripts cannot hang or exhaust memory.
class XJSInterpreter {
public:
    XJSInterpreter();
    ~XJSInterpreter();

    void run(Node *pProgram, XJSReport *pReport);
    void setStepLimit(qint64 nStepLimit);

private:
    enum Completion { C_NORMAL, C_RETURN, C_BREAK, C_CONTINUE, C_THROW };

    Environment *newEnv(Environment *pParent);
    QSharedPointer<JSObject> newObject(JSObject::OKind kind);
    QSharedPointer<JSObject> newArray();
    QSharedPointer<JSObject> newNative(const QString &sName);
    QSharedPointer<JSObject> newDom(const QString &sClassName);

    void buildGlobals();
    void hoist(Node *pNode, Environment *pEnv);

    void execStmt(Node *pNode, Environment *pEnv);
    void execBlock(Node *pNode, Environment *pEnv);

    XJSValue evalNode(Node *pNode, Environment *pEnv);
    XJSValue evalBinary(const QString &sOp, const XJSValue &l, const XJSValue &r);
    XJSValue evalCall(Node *pNode, Environment *pEnv);
    XJSValue evalAssign(Node *pNode, Environment *pEnv);
    XJSValue evalUnary(Node *pNode, Environment *pEnv);

    XJSValue callFunction(const XJSValue &fn, const XJSValue &thisVal, const QList<XJSValue> &args);
    XJSValue callNative(const QString &sName, const XJSValue &thisVal, const QList<XJSValue> &args);
    XJSValue callMethod(const XJSValue &base, const QString &sMethod, const QList<XJSValue> &args);

    XJSValue getMember(const XJSValue &base, const QString &sProp);
    void setMember(const XJSValue &base, const QString &sProp, const XJSValue &val);
    void assignTo(Node *pTarget, const XJSValue &val, Environment *pEnv);

    XJSValue lookup(const QString &sName, Environment *pEnv);
    void declare(const QString &sName, const XJSValue &val, Environment *pEnv);
    void setVar(const QString &sName, const XJSValue &val, Environment *pEnv);

    // Native helpers.
    XJSValue nativeStringMethod(const XJSValue &base, const QString &sMethod, const QList<XJSValue> &args);
    XJSValue nativeArrayMethod(const XJSValue &base, const QString &sMethod, const QList<XJSValue> &args);
    XJSValue jsUnescape(const QString &s);
    XJSValue jsEscape(const QString &s);
    QString regexReplace(const QString &sInput, const QString &sPattern, const QString &sFlags, const QString &sReplacement);

    void recordApi(const QString &sApi, const QList<XJSValue> &args);
    void addIndicator(const QString &sIndicator);
    bool budgetExceeded();
    void throwError(const QString &sMessage);

private:
    XJSValue runCode(const QString &sCode);  // eval / new Function / setTimeout(string)

    XJSReport *m_pReport;
    Environment *m_pGlobal;
    QList<Environment *> m_envArena;
    QList<XJSParser *> m_subParsers;  // keep eval'd ASTs alive for the run

    Completion m_completion;
    XJSValue m_completionValue;

    qint64 m_nSteps;
    qint64 m_nStepLimit;
    qint32 m_nCallDepth;
    qint32 m_nCallDepthLimit;
    qint64 m_nMaxStringLength;
};

}  // namespace XJS

#endif  // XJSINTERPRETER_H
