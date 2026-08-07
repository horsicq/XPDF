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
#include "xjsinterpreter.h"
#include "xjsparser.h"

#include <QRegularExpression>
#include <cmath>

namespace XJS {

// ---------------------------------------------------------------------------
// XJSValue
// ---------------------------------------------------------------------------

XJSValue XJSValue::undef()
{
    return XJSValue();
}

XJSValue XJSValue::null()
{
    XJSValue v;
    v.type = V_NULL;
    return v;
}

XJSValue XJSValue::boolean(bool bValue)
{
    XJSValue v;
    v.type = V_BOOL;
    v.b = bValue;
    return v;
}

XJSValue XJSValue::number(double nValue)
{
    XJSValue v;
    v.type = V_NUMBER;
    v.n = nValue;
    return v;
}

XJSValue XJSValue::string(const QString &sValue)
{
    XJSValue v;
    v.type = V_STRING;
    v.s = sValue;
    return v;
}

XJSValue XJSValue::object(const QSharedPointer<JSObject> &pObject)
{
    XJSValue v;
    v.type = V_OBJECT;
    v.o = pObject;
    return v;
}

bool XJSValue::isCallable() const
{
    return isObject() && ((o->kind == JSObject::O_FUNCTION) || (o->kind == JSObject::O_NATIVE));
}

bool XJSValue::toBool() const
{
    if (type == V_UNDEFINED) return false;
    if (type == V_NULL) return false;
    if (type == V_BOOL) return b;
    if (type == V_NUMBER) return (n != 0.0) && !std::isnan(n);
    if (type == V_STRING) return !s.isEmpty();
    return true;
}

static QString numberToJsString(double n)
{
    if (std::isnan(n)) return QStringLiteral("NaN");
    if (std::isinf(n)) return (n < 0) ? QStringLiteral("-Infinity") : QStringLiteral("Infinity");
    if (n == 0.0) return QStringLiteral("0");

    const double nRounded = std::floor(n);
    if ((nRounded == n) && (std::fabs(n) < 9.007199254740992e15)) {
        return QString::number(static_cast<qint64>(n));
    }
    return QString::number(n, 'g', 15);
}

double XJSValue::toNumber() const
{
    if (type == V_UNDEFINED) return std::nan("");
    if (type == V_NULL) return 0.0;
    if (type == V_BOOL) return b ? 1.0 : 0.0;
    if (type == V_NUMBER) return n;
    if (type == V_STRING) {
        const QString sTrim = s.trimmed();
        if (sTrim.isEmpty()) return 0.0;
        bool bOk = false;
        if (sTrim.startsWith(QLatin1String("0x")) || sTrim.startsWith(QLatin1String("0X"))) {
            const qulonglong nHex = sTrim.mid(2).toULongLong(&bOk, 16);
            return bOk ? static_cast<double>(nHex) : std::nan("");
        }
        const double d = sTrim.toDouble(&bOk);
        return bOk ? d : std::nan("");
    }
    // Object -> primitive (approximate via string).
    return XJSValue::string(toStr()).toNumber();
}

QString XJSValue::toStr() const
{
    if (type == V_UNDEFINED) return QStringLiteral("undefined");
    if (type == V_NULL) return QStringLiteral("null");
    if (type == V_BOOL) return b ? QStringLiteral("true") : QStringLiteral("false");
    if (type == V_NUMBER) return numberToJsString(n);
    if (type == V_STRING) return s;

    if (o->kind == JSObject::O_ARRAY) {
        QStringList list;
        for (qint32 i = 0; i < o->elements.count(); ++i) {
            const XJSValue &e = o->elements.at(i);
            list.append((e.type == V_UNDEFINED || e.type == V_NULL) ? QString() : e.toStr());
        }
        return list.join(QLatin1Char(','));
    }
    if (o->kind == JSObject::O_REGEX) {
        return QLatin1Char('/') + o->regexSource + QLatin1Char('/') + o->regexFlags;
    }
    if ((o->kind == JSObject::O_FUNCTION) || (o->kind == JSObject::O_NATIVE)) {
        return QStringLiteral("function () { [code] }");
    }
    return QStringLiteral("[object Object]");
}

QString XJSValue::typeOf() const
{
    if (type == V_UNDEFINED) return QStringLiteral("undefined");
    if (type == V_NULL) return QStringLiteral("object");
    if (type == V_BOOL) return QStringLiteral("boolean");
    if (type == V_NUMBER) return QStringLiteral("number");
    if (type == V_STRING) return QStringLiteral("string");
    return isCallable() ? QStringLiteral("function") : QStringLiteral("object");
}

// ---------------------------------------------------------------------------
// Int coercions
// ---------------------------------------------------------------------------

static qint32 toInt32(double d)
{
    if (std::isnan(d) || std::isinf(d)) return 0;
    const double m = std::fmod(std::trunc(d), 4294967296.0);
    quint32 u = static_cast<quint32>(m < 0 ? m + 4294967296.0 : m);
    return static_cast<qint32>(u);
}

static quint32 toUint32(double d)
{
    if (std::isnan(d) || std::isinf(d)) return 0;
    const double m = std::fmod(std::trunc(d), 4294967296.0);
    return static_cast<quint32>(m < 0 ? m + 4294967296.0 : m);
}

// ---------------------------------------------------------------------------
// XJSInterpreter
// ---------------------------------------------------------------------------

XJSInterpreter::XJSInterpreter()
    : m_pReport(nullptr), m_pGlobal(nullptr), m_completion(C_NORMAL), m_nSteps(0), m_nStepLimit(5000000), m_nCallDepth(0),
      m_nCallDepthLimit(200), m_nMaxStringLength(16 * 1024 * 1024)
{
}

void XJSInterpreter::setStepLimit(qint64 nStepLimit)
{
    if (nStepLimit > 0) {
        m_nStepLimit = nStepLimit;
    }
}

XJSInterpreter::~XJSInterpreter()
{
    for (qint32 i = 0; i < m_envArena.count(); ++i) {
        delete m_envArena.at(i);
    }
    m_envArena.clear();

    for (qint32 i = 0; i < m_subParsers.count(); ++i) {
        delete m_subParsers.at(i);
    }
    m_subParsers.clear();
}

Environment *XJSInterpreter::newEnv(Environment *pParent)
{
    Environment *pEnv = new Environment();
    pEnv->parent = pParent;
    m_envArena.append(pEnv);
    return pEnv;
}

QSharedPointer<JSObject> XJSInterpreter::newObject(JSObject::OKind kind)
{
    QSharedPointer<JSObject> p(new JSObject());
    p->kind = kind;
    return p;
}

QSharedPointer<JSObject> XJSInterpreter::newArray()
{
    QSharedPointer<JSObject> p = newObject(JSObject::O_ARRAY);
    p->className = QStringLiteral("Array");
    return p;
}

QSharedPointer<JSObject> XJSInterpreter::newNative(const QString &sName)
{
    QSharedPointer<JSObject> p = newObject(JSObject::O_NATIVE);
    p->nativeName = sName;
    p->className = QStringLiteral("Function");
    return p;
}

QSharedPointer<JSObject> XJSInterpreter::newDom(const QString &sClassName)
{
    QSharedPointer<JSObject> p = newObject(JSObject::O_PLAIN);
    p->className = sClassName;
    return p;
}

bool XJSInterpreter::budgetExceeded()
{
    return (m_nSteps > m_nStepLimit) || (m_completion == C_THROW);
}

void XJSInterpreter::throwError(const QString &sMessage)
{
    m_completion = C_THROW;
    m_completionValue = XJSValue::string(sMessage);
}

void XJSInterpreter::addIndicator(const QString &sIndicator)
{
    if (m_pReport && !m_pReport->indicators.contains(sIndicator)) {
        m_pReport->indicators.append(sIndicator);
    }
}

void XJSInterpreter::recordApi(const QString &sApi, const QList<XJSValue> &args)
{
    if (!m_pReport) {
        return;
    }

    QStringList argStrings;
    for (qint32 i = 0; (i < args.count()) && (i < 6); ++i) {
        QString sArg = args.at(i).toStr();
        if (sArg.length() > 120) {
            sArg = sArg.left(120) + QStringLiteral("...");
        }
        argStrings.append(sArg);
    }

    const QString sLine = sApi + QLatin1Char('(') + argStrings.join(QLatin1String(", ")) + QLatin1Char(')');
    if (!m_pReport->apiCalls.contains(sLine)) {
        m_pReport->apiCalls.append(sLine);
    }
}

// ---------------------------------------------------------------------------
// Scope
// ---------------------------------------------------------------------------

XJSValue XJSInterpreter::lookup(const QString &sName, Environment *pEnv)
{
    Environment *p = pEnv;
    while (p) {
        if (p->vars.contains(sName)) {
            return p->vars.value(sName);
        }
        p = p->parent;
    }
    return XJSValue::undef();
}

void XJSInterpreter::declare(const QString &sName, const XJSValue &val, Environment *pEnv)
{
    pEnv->vars.insert(sName, val);
}

void XJSInterpreter::setVar(const QString &sName, const XJSValue &val, Environment *pEnv)
{
    Environment *p = pEnv;
    while (p) {
        if (p->vars.contains(sName)) {
            p->vars.insert(sName, val);
            return;
        }
        p = p->parent;
    }
    // Implicit global.
    m_pGlobal->vars.insert(sName, val);
}

// ---------------------------------------------------------------------------
// Run + globals
// ---------------------------------------------------------------------------

void XJSInterpreter::run(Node *pProgram, XJSReport *pReport)
{
    m_pReport = pReport;
    m_pGlobal = newEnv(nullptr);
    m_completion = C_NORMAL;
    m_nSteps = 0;

    buildGlobals();

    if (!pProgram) {
        return;
    }

    hoist(pProgram, m_pGlobal);

    for (qint32 i = 0; (i < pProgram->list.count()) && (m_completion == C_NORMAL) && !budgetExceeded(); ++i) {
        execStmt(pProgram->list.at(i), m_pGlobal);
    }

    if ((m_completion == C_THROW) && m_pReport) {
        const QString sThrown = m_completionValue.toStr();
        if (!sThrown.isEmpty() && (sThrown != QStringLiteral("undefined"))) {
            m_pReport->indicators.append(QStringLiteral("Uncaught throw: ") + sThrown.left(120));
        }
    }

    if (m_nSteps > m_nStepLimit) {
        m_pReport->indicators.append(QStringLiteral("Execution budget exceeded (possible anti-analysis loop)"));
    }
}

void XJSInterpreter::buildGlobals()
{
    // Global functions.
    static const char *const sGlobals[] = {"eval",      "unescape", "escape",     "encodeURIComponent", "decodeURIComponent", "encodeURI",
                                           "decodeURI", "parseInt", "parseFloat", "isNaN",              "isFinite",           "String",
                                           "Number",    "Boolean",  "print",      "alert",              "setTimeout",         "setInterval",
                                           "Function",  "importScripts"};
    for (qint32 i = 0; i < 20; ++i) {
        const QString sName = QString::fromLatin1(sGlobals[i]);
        declare(sName, XJSValue::object(newNative(sName)), m_pGlobal);
    }

    // DOM objects.
    declare(QStringLiteral("app"), XJSValue::object(newDom(QStringLiteral("app"))), m_pGlobal);
    declare(QStringLiteral("util"), XJSValue::object(newDom(QStringLiteral("util"))), m_pGlobal);
    declare(QStringLiteral("Collab"), XJSValue::object(newDom(QStringLiteral("Collab"))), m_pGlobal);
    declare(QStringLiteral("spell"), XJSValue::object(newDom(QStringLiteral("spell"))), m_pGlobal);
    declare(QStringLiteral("console"), XJSValue::object(newDom(QStringLiteral("console"))), m_pGlobal);
    declare(QStringLiteral("event"), XJSValue::object(newDom(QStringLiteral("event"))), m_pGlobal);
    declare(QStringLiteral("Math"), XJSValue::object(newDom(QStringLiteral("Math"))), m_pGlobal);
    declare(QStringLiteral("global"), XJSValue::object(newDom(QStringLiteral("global"))), m_pGlobal);

    // String / Array / Object / RegExp / Date constructors reachable as objects too.
    QSharedPointer<JSObject> pStringCtor = newDom(QStringLiteral("String"));
    pStringCtor->kind = JSObject::O_NATIVE;
    pStringCtor->nativeName = QStringLiteral("String");
    declare(QStringLiteral("String"), XJSValue::object(pStringCtor), m_pGlobal);

    declare(QStringLiteral("Array"), XJSValue::object(newNative(QStringLiteral("Array"))), m_pGlobal);
    declare(QStringLiteral("RegExp"), XJSValue::object(newNative(QStringLiteral("RegExp"))), m_pGlobal);
    declare(QStringLiteral("Date"), XJSValue::object(newNative(QStringLiteral("Date"))), m_pGlobal);
    declare(QStringLiteral("Object"), XJSValue::object(newNative(QStringLiteral("Object"))), m_pGlobal);

    // Acrobat: top-level `this` is the Doc.
    QSharedPointer<JSObject> pDoc = newDom(QStringLiteral("Doc"));
    declare(QStringLiteral("this"), XJSValue::object(pDoc), m_pGlobal);
    // app.doc points at the same Doc.
    XJSValue vApp = lookup(QStringLiteral("app"), m_pGlobal);
    if (vApp.isObject()) {
        vApp.o->props.insert(QStringLiteral("doc"), XJSValue::object(pDoc));
    }
}

void XJSInterpreter::hoist(Node *pNode, Environment *pEnv)
{
    if (!pNode) {
        return;
    }

    // Function declarations become available before execution; var names are pre-declared as undefined.
    // Do NOT descend into nested function bodies (their own scope).
    if (pNode->type == N_FUNCTION) {
        if (!pNode->sval.isEmpty()) {
            QSharedPointer<JSObject> pFn = newObject(JSObject::O_FUNCTION);
            pFn->fnNode = pNode;
            pFn->closure = pEnv;
            declare(pNode->sval, XJSValue::object(pFn), pEnv);
        }
        return;
    }

    if (pNode->type == N_VARDECL) {
        for (qint32 i = 0; i < pNode->list.count(); ++i) {
            Node *pItem = pNode->list.at(i);
            if (pItem && !pEnv->vars.contains(pItem->sval)) {
                declare(pItem->sval, XJSValue::undef(), pEnv);
            }
        }
        return;
    }

    hoist(pNode->a, pEnv);
    hoist(pNode->b, pEnv);
    hoist(pNode->c, pEnv);
    for (qint32 i = 0; i < pNode->list.count(); ++i) {
        hoist(pNode->list.at(i), pEnv);
    }
}

// ---------------------------------------------------------------------------
// Statement execution
// ---------------------------------------------------------------------------

void XJSInterpreter::execBlock(Node *pNode, Environment *pEnv)
{
    for (qint32 i = 0; (i < pNode->list.count()) && (m_completion == C_NORMAL) && !budgetExceeded(); ++i) {
        execStmt(pNode->list.at(i), pEnv);
    }
}

void XJSInterpreter::execStmt(Node *pNode, Environment *pEnv)
{
    if (!pNode || budgetExceeded()) {
        return;
    }

    ++m_nSteps;
    if (m_nSteps > m_nStepLimit) {
        return;
    }

    switch (pNode->type) {
        case N_EMPTY:
        case N_FUNCTION:  // hoisted already
            break;

        case N_BLOCK:
            execBlock(pNode, pEnv);
            break;

        case N_EXPRSTMT:
            evalNode(pNode->a, pEnv);
            break;

        case N_VARDECL: {
            for (qint32 i = 0; (i < pNode->list.count()) && (m_completion == C_NORMAL); ++i) {
                Node *pItem = pNode->list.at(i);
                if (!pItem) continue;
                if (pItem->a) {
                    const XJSValue v = evalNode(pItem->a, pEnv);
                    declare(pItem->sval, v, pEnv);
                } else if (!pEnv->vars.contains(pItem->sval)) {
                    declare(pItem->sval, XJSValue::undef(), pEnv);
                }
            }
            break;
        }

        case N_IF: {
            const XJSValue v = evalNode(pNode->a, pEnv);
            if (v.toBool()) {
                execStmt(pNode->b, pEnv);
            } else if (pNode->c) {
                execStmt(pNode->c, pEnv);
            }
            break;
        }

        case N_WHILE: {
            qint64 nIter = 0;
            while ((m_completion == C_NORMAL) && !budgetExceeded() && evalNode(pNode->a, pEnv).toBool()) {
                execStmt(pNode->b, pEnv);
                if (m_completion == C_BREAK) {
                    m_completion = C_NORMAL;
                    break;
                }
                if (m_completion == C_CONTINUE) {
                    m_completion = C_NORMAL;
                }
                if (++nIter > 5000000) break;
            }
            break;
        }

        case N_DOWHILE: {
            qint64 nIter = 0;
            do {
                execStmt(pNode->a, pEnv);
                if (m_completion == C_BREAK) {
                    m_completion = C_NORMAL;
                    break;
                }
                if (m_completion == C_CONTINUE) {
                    m_completion = C_NORMAL;
                }
                if (budgetExceeded() || (m_completion != C_NORMAL)) break;
                if (++nIter > 5000000) break;
            } while (evalNode(pNode->b, pEnv).toBool());
            break;
        }

        case N_FOR: {
            if (pNode->a) {
                execStmt(pNode->a, pEnv);
            }
            qint64 nIter = 0;
            while ((m_completion == C_NORMAL) && !budgetExceeded()) {
                if (pNode->b && !evalNode(pNode->b, pEnv).toBool()) {
                    break;
                }
                if (!pNode->list.isEmpty()) {
                    execStmt(pNode->list.at(0), pEnv);
                }
                if (m_completion == C_BREAK) {
                    m_completion = C_NORMAL;
                    break;
                }
                if (m_completion == C_CONTINUE) {
                    m_completion = C_NORMAL;
                }
                if (pNode->c) {
                    evalNode(pNode->c, pEnv);
                }
                if (++nIter > 5000000) break;
            }
            break;
        }

        case N_FORIN: {
            const XJSValue vObj = evalNode(pNode->a, pEnv);
            QStringList keys;
            if (vObj.isObject()) {
                if (vObj.o->kind == JSObject::O_ARRAY) {
                    for (qint32 i = 0; i < vObj.o->elements.count(); ++i) {
                        keys.append(QString::number(i));
                    }
                }
                keys.append(vObj.o->props.keys());
            }
            for (qint32 i = 0; (i < keys.count()) && (m_completion == C_NORMAL) && !budgetExceeded(); ++i) {
                setVar(pNode->sval, XJSValue::string(keys.at(i)), pEnv);
                execStmt(pNode->b, pEnv);
                if (m_completion == C_BREAK) {
                    m_completion = C_NORMAL;
                    break;
                }
                if (m_completion == C_CONTINUE) {
                    m_completion = C_NORMAL;
                }
            }
            break;
        }

        case N_RETURN:
            m_completionValue = pNode->a ? evalNode(pNode->a, pEnv) : XJSValue::undef();
            if (m_completion == C_NORMAL) {
                m_completion = C_RETURN;
            }
            break;

        case N_BREAK:
            m_completion = C_BREAK;
            break;

        case N_CONTINUE:
            m_completion = C_CONTINUE;
            break;

        case N_THROW: {
            const XJSValue v = pNode->a ? evalNode(pNode->a, pEnv) : XJSValue::undef();
            if (m_completion == C_NORMAL) {
                m_completion = C_THROW;
                m_completionValue = v;
            }
            break;
        }

        case N_TRY: {
            if (pNode->a) {
                execBlock(pNode->a, pEnv);
            }
            if ((m_completion == C_THROW) && pNode->b) {
                const XJSValue vThrown = m_completionValue;
                m_completion = C_NORMAL;
                Environment *pCatchEnv = newEnv(pEnv);
                if (!pNode->sval.isEmpty()) {
                    declare(pNode->sval, vThrown, pCatchEnv);
                }
                execBlock(pNode->b, pCatchEnv);
            }
            if (pNode->c) {
                const Completion saved = m_completion;
                const XJSValue savedVal = m_completionValue;
                m_completion = C_NORMAL;
                execBlock(pNode->c, pEnv);
                if (m_completion == C_NORMAL) {
                    m_completion = saved;
                    m_completionValue = savedVal;
                }
            }
            break;
        }

        case N_SWITCH: {
            const XJSValue vDisc = evalNode(pNode->a, pEnv);
            qint32 nMatch = -1;
            qint32 nDefault = -1;
            const qint32 nPairs = pNode->list.count() / 2;
            for (qint32 i = 0; i < nPairs; ++i) {
                Node *pTest = pNode->list.at(i * 2);
                if (!pTest) {
                    nDefault = i;
                    continue;
                }
                const XJSValue vTest = evalNode(pTest, pEnv);
                if (evalBinary(QStringLiteral("==="), vDisc, vTest).toBool()) {
                    nMatch = i;
                    break;
                }
            }
            qint32 nStart = (nMatch != -1) ? nMatch : nDefault;
            if (nStart != -1) {
                for (qint32 i = nStart; (i < nPairs) && (m_completion == C_NORMAL) && !budgetExceeded(); ++i) {
                    execBlock(pNode->list.at(i * 2 + 1), pEnv);
                }
                if (m_completion == C_BREAK) {
                    m_completion = C_NORMAL;
                }
            }
            break;
        }

        default:
            evalNode(pNode, pEnv);
            break;
    }
}

// ---------------------------------------------------------------------------
// Expression evaluation
// ---------------------------------------------------------------------------

XJSValue XJSInterpreter::evalNode(Node *pNode, Environment *pEnv)
{
    if (!pNode || budgetExceeded()) {
        return XJSValue::undef();
    }

    ++m_nSteps;
    if (m_nSteps > m_nStepLimit) {
        return XJSValue::undef();
    }

    switch (pNode->type) {
        case N_NUMBER:
            return XJSValue::number(pNode->num);
        case N_STRING:
            return XJSValue::string(pNode->sval);
        case N_BOOL:
            return XJSValue::boolean(pNode->bval);
        case N_NULL:
            return XJSValue::null();
        case N_UNDEFINED:
            return XJSValue::undef();
        case N_IDENT:
            return lookup(pNode->sval, pEnv);
        case N_THIS:
            return lookup(QStringLiteral("this"), pEnv);

        case N_REGEX: {
            QSharedPointer<JSObject> p = newObject(JSObject::O_REGEX);
            p->className = QStringLiteral("RegExp");
            p->regexSource = pNode->sval;
            p->regexFlags = pNode->params.isEmpty() ? QString() : pNode->params.at(0);
            return XJSValue::object(p);
        }

        case N_ARRAY: {
            QSharedPointer<JSObject> p = newArray();
            for (qint32 i = 0; i < pNode->list.count(); ++i) {
                p->elements.append(evalNode(pNode->list.at(i), pEnv));
            }
            return XJSValue::object(p);
        }

        case N_OBJECT: {
            QSharedPointer<JSObject> p = newObject(JSObject::O_PLAIN);
            for (qint32 i = 0; i < pNode->list.count(); ++i) {
                Node *pProp = pNode->list.at(i);
                if (pProp && (pProp->type == N_PROPERTY)) {
                    p->props.insert(pProp->sval, evalNode(pProp->a, pEnv));
                }
            }
            return XJSValue::object(p);
        }

        case N_FUNCTION: {
            QSharedPointer<JSObject> p = newObject(JSObject::O_FUNCTION);
            p->fnNode = pNode;
            p->closure = pEnv;
            return XJSValue::object(p);
        }

        case N_MEMBER:
            return getMember(evalNode(pNode->a, pEnv), pNode->sval);

        case N_INDEX: {
            const XJSValue base = evalNode(pNode->a, pEnv);
            const XJSValue key = evalNode(pNode->b, pEnv);
            return getMember(base, key.toStr());
        }

        case N_CALL:
            return evalCall(pNode, pEnv);

        case N_NEW: {
            // Constructors relevant to PDF JS: Array, RegExp, Function (== eval), Date/Object (plain).
            QList<XJSValue> args;
            for (qint32 i = 0; i < pNode->list.count(); ++i) {
                args.append(evalNode(pNode->list.at(i), pEnv));
            }
            const XJSValue callee = pNode->a ? evalNode(pNode->a, pEnv) : XJSValue::undef();
            QString sName;
            if (callee.isObject()) {
                sName = callee.o->nativeName.isEmpty() ? callee.o->className : callee.o->nativeName;
            }
            if (sName == QStringLiteral("Array")) {
                QSharedPointer<JSObject> p = newArray();
                if ((args.count() == 1) && (args.at(0).type == XJSValue::V_NUMBER)) {
                    // new Array(n): leave empty (length approximated by elements)
                } else {
                    p->elements = args;
                }
                return XJSValue::object(p);
            }
            if (sName == QStringLiteral("RegExp")) {
                QSharedPointer<JSObject> p = newObject(JSObject::O_REGEX);
                p->className = QStringLiteral("RegExp");
                p->regexSource = args.count() > 0 ? args.at(0).toStr() : QString();
                p->regexFlags = args.count() > 1 ? args.at(1).toStr() : QString();
                return XJSValue::object(p);
            }
            if (sName == QStringLiteral("Function")) {
                // new Function([args...], body) -> body is the last argument; behaves like eval.
                if (!args.isEmpty()) {
                    const QString sCode = args.last().toStr();
                    if (m_pReport && !m_pReport->evalArguments.contains(sCode)) {
                        m_pReport->evalArguments.append(sCode.left(4096));
                    }
                    addIndicator(QStringLiteral("Dynamic code via new Function()"));
                }
                return XJSValue::object(newObject(JSObject::O_FUNCTION));
            }
            if (callee.isCallable() && callee.isObject() && (callee.o->kind == JSObject::O_FUNCTION)) {
                QSharedPointer<JSObject> pThis = newObject(JSObject::O_PLAIN);
                const XJSValue vThis = XJSValue::object(pThis);
                const XJSValue vRet = callFunction(callee, vThis, args);
                return vRet.isObject() ? vRet : vThis;
            }
            return XJSValue::object(newObject(JSObject::O_PLAIN));
        }

        case N_UNARY:
            return evalUnary(pNode, pEnv);

        case N_BINARY:
            return evalBinary(pNode->sval, evalNode(pNode->a, pEnv), evalNode(pNode->b, pEnv));

        case N_LOGICAL: {
            const XJSValue l = evalNode(pNode->a, pEnv);
            if (pNode->sval == QLatin1String("&&")) {
                return l.toBool() ? evalNode(pNode->b, pEnv) : l;
            }
            return l.toBool() ? l : evalNode(pNode->b, pEnv);
        }

        case N_ASSIGN:
            return evalAssign(pNode, pEnv);

        case N_COND:
            return evalNode(pNode->a, pEnv).toBool() ? evalNode(pNode->b, pEnv) : evalNode(pNode->c, pEnv);

        case N_SEQ:
            evalNode(pNode->a, pEnv);
            return evalNode(pNode->b, pEnv);

        default:
            return XJSValue::undef();
    }
}

XJSValue XJSInterpreter::evalUnary(Node *pNode, Environment *pEnv)
{
    const QString &op = pNode->sval;

    if ((op == QLatin1String("++")) || (op == QLatin1String("--"))) {
        const XJSValue old = evalNode(pNode->a, pEnv);
        const double dOld = old.toNumber();
        const double dNew = (op == QLatin1String("++")) ? (dOld + 1.0) : (dOld - 1.0);
        assignTo(pNode->a, XJSValue::number(dNew), pEnv);
        return XJSValue::number(pNode->ival ? dNew : dOld);  // prefix returns new, postfix old
    }

    if (op == QLatin1String("typeof")) {
        // typeof of an undeclared identifier must not throw.
        if (pNode->a && (pNode->a->type == N_IDENT)) {
            return XJSValue::string(lookup(pNode->a->sval, pEnv).typeOf());
        }
        return XJSValue::string(evalNode(pNode->a, pEnv).typeOf());
    }

    const XJSValue v = evalNode(pNode->a, pEnv);
    if (op == QLatin1String("!")) return XJSValue::boolean(!v.toBool());
    if (op == QLatin1String("-")) return XJSValue::number(-v.toNumber());
    if (op == QLatin1String("+")) return XJSValue::number(v.toNumber());
    if (op == QLatin1String("~")) return XJSValue::number(static_cast<double>(~toInt32(v.toNumber())));
    if (op == QLatin1String("void")) return XJSValue::undef();
    if (op == QLatin1String("delete")) return XJSValue::boolean(true);

    return XJSValue::undef();
}

XJSValue XJSInterpreter::evalBinary(const QString &sOp, const XJSValue &l, const XJSValue &r)
{
    if (sOp == QLatin1String("+")) {
        if ((l.type == XJSValue::V_STRING) || (r.type == XJSValue::V_STRING) || l.isObject() || r.isObject()) {
            QString sResult = l.toStr() + r.toStr();
            if (sResult.length() > m_nMaxStringLength) {
                sResult.truncate(static_cast<qint32>(m_nMaxStringLength));
                addIndicator(QStringLiteral("String length cap hit (heap-spray style build?)"));
            }
            return XJSValue::string(sResult);
        }
        return XJSValue::number(l.toNumber() + r.toNumber());
    }
    if (sOp == QLatin1String("-")) return XJSValue::number(l.toNumber() - r.toNumber());
    if (sOp == QLatin1String("*")) return XJSValue::number(l.toNumber() * r.toNumber());
    if (sOp == QLatin1String("/")) return XJSValue::number(l.toNumber() / r.toNumber());
    if (sOp == QLatin1String("%")) return XJSValue::number(std::fmod(l.toNumber(), r.toNumber()));
    if (sOp == QLatin1String("**")) return XJSValue::number(std::pow(l.toNumber(), r.toNumber()));

    if (sOp == QLatin1String("<") || sOp == QLatin1String(">") || sOp == QLatin1String("<=") || sOp == QLatin1String(">=")) {
        if ((l.type == XJSValue::V_STRING) && (r.type == XJSValue::V_STRING)) {
            const int c = QString::compare(l.s, r.s);
            if (sOp == QLatin1String("<")) return XJSValue::boolean(c < 0);
            if (sOp == QLatin1String(">")) return XJSValue::boolean(c > 0);
            if (sOp == QLatin1String("<=")) return XJSValue::boolean(c <= 0);
            return XJSValue::boolean(c >= 0);
        }
        const double a = l.toNumber();
        const double b = r.toNumber();
        if (std::isnan(a) || std::isnan(b)) return XJSValue::boolean(false);
        if (sOp == QLatin1String("<")) return XJSValue::boolean(a < b);
        if (sOp == QLatin1String(">")) return XJSValue::boolean(a > b);
        if (sOp == QLatin1String("<=")) return XJSValue::boolean(a <= b);
        return XJSValue::boolean(a >= b);
    }

    if (sOp == QLatin1String("===") || sOp == QLatin1String("!==")) {
        bool bEq = false;
        if (l.type == r.type) {
            if (l.type == XJSValue::V_UNDEFINED || l.type == XJSValue::V_NULL) bEq = true;
            else if (l.type == XJSValue::V_BOOL) bEq = (l.b == r.b);
            else if (l.type == XJSValue::V_NUMBER) bEq = (l.n == r.n);
            else if (l.type == XJSValue::V_STRING) bEq = (l.s == r.s);
            else bEq = (l.o == r.o);
        }
        return XJSValue::boolean(sOp == QLatin1String("===") ? bEq : !bEq);
    }

    if (sOp == QLatin1String("==") || sOp == QLatin1String("!=")) {
        bool bEq = false;
        if (l.type == r.type) {
            bEq = evalBinary(QStringLiteral("==="), l, r).toBool();
        } else if ((l.type == XJSValue::V_NULL && r.type == XJSValue::V_UNDEFINED) || (l.type == XJSValue::V_UNDEFINED && r.type == XJSValue::V_NULL)) {
            bEq = true;
        } else if (l.isObject() && !r.isObject() && r.type != XJSValue::V_UNDEFINED && r.type != XJSValue::V_NULL) {
            bEq = (l.toStr() == r.toStr());
        } else if (r.isObject() && !l.isObject() && l.type != XJSValue::V_UNDEFINED && l.type != XJSValue::V_NULL) {
            bEq = (l.toStr() == r.toStr());
        } else if (l.type != XJSValue::V_UNDEFINED && l.type != XJSValue::V_NULL && r.type != XJSValue::V_UNDEFINED && r.type != XJSValue::V_NULL) {
            bEq = (l.toNumber() == r.toNumber());
        }
        return XJSValue::boolean(sOp == QLatin1String("==") ? bEq : !bEq);
    }

    if (sOp == QLatin1String("&")) return XJSValue::number(static_cast<double>(toInt32(l.toNumber()) & toInt32(r.toNumber())));
    if (sOp == QLatin1String("|")) return XJSValue::number(static_cast<double>(toInt32(l.toNumber()) | toInt32(r.toNumber())));
    if (sOp == QLatin1String("^")) return XJSValue::number(static_cast<double>(toInt32(l.toNumber()) ^ toInt32(r.toNumber())));
    if (sOp == QLatin1String("<<")) return XJSValue::number(static_cast<double>(toInt32(l.toNumber()) << (toUint32(r.toNumber()) & 31)));
    if (sOp == QLatin1String(">>")) return XJSValue::number(static_cast<double>(toInt32(l.toNumber()) >> (toUint32(r.toNumber()) & 31)));
    if (sOp == QLatin1String(">>>")) return XJSValue::number(static_cast<double>(toUint32(l.toNumber()) >> (toUint32(r.toNumber()) & 31)));

    if (sOp == QLatin1String("instanceof")) return XJSValue::boolean(false);
    if (sOp == QLatin1String("in")) {
        if (r.isObject()) {
            const QString sKey = l.toStr();
            if (r.o->props.contains(sKey)) return XJSValue::boolean(true);
        }
        return XJSValue::boolean(false);
    }

    return XJSValue::undef();
}

XJSValue XJSInterpreter::evalAssign(Node *pNode, Environment *pEnv)
{
    const QString &op = pNode->sval;
    XJSValue vNew;

    if (op == QLatin1String("=")) {
        vNew = evalNode(pNode->b, pEnv);
    } else {
        const XJSValue vOld = evalNode(pNode->a, pEnv);
        const XJSValue vRhs = evalNode(pNode->b, pEnv);
        QString sBin = op;
        sBin.chop(1);  // strip trailing '='
        vNew = evalBinary(sBin, vOld, vRhs);
    }

    assignTo(pNode->a, vNew, pEnv);
    return vNew;
}

void XJSInterpreter::assignTo(Node *pTarget, const XJSValue &val, Environment *pEnv)
{
    if (!pTarget) {
        return;
    }

    if (pTarget->type == N_IDENT) {
        setVar(pTarget->sval, val, pEnv);
    } else if (pTarget->type == N_MEMBER) {
        const XJSValue base = evalNode(pTarget->a, pEnv);
        setMember(base, pTarget->sval, val);
    } else if (pTarget->type == N_INDEX) {
        const XJSValue base = evalNode(pTarget->a, pEnv);
        const XJSValue key = evalNode(pTarget->b, pEnv);
        setMember(base, key.toStr(), val);
    }
}

// ---------------------------------------------------------------------------
// Member access
// ---------------------------------------------------------------------------

XJSValue XJSInterpreter::getMember(const XJSValue &base, const QString &sProp)
{
    if (base.type == XJSValue::V_STRING) {
        if (sProp == QLatin1String("length")) {
            return XJSValue::number(base.s.length());
        }
        bool bIdx = false;
        const qint32 nIdx = sProp.toInt(&bIdx);
        if (bIdx && (nIdx >= 0) && (nIdx < base.s.length())) {
            return XJSValue::string(QString(base.s.at(nIdx)));
        }
        return XJSValue::undef();
    }

    if (!base.isObject()) {
        return XJSValue::undef();
    }

    JSObject *pObj = base.o.data();

    if (pObj->kind == JSObject::O_ARRAY) {
        if (sProp == QLatin1String("length")) {
            return XJSValue::number(pObj->elements.count());
        }
        bool bIdx = false;
        const qint32 nIdx = sProp.toInt(&bIdx);
        if (bIdx && (nIdx >= 0) && (nIdx < pObj->elements.count())) {
            return pObj->elements.at(nIdx);
        }
        if (pObj->props.contains(sProp)) {
            return pObj->props.value(sProp);
        }
        return XJSValue::undef();
    }

    if (pObj->props.contains(sProp)) {
        return pObj->props.value(sProp);
    }

    // Known data-property reads on DOM objects.
    if (pObj->className == QLatin1String("app")) {
        if (sProp == QLatin1String("viewerVersion") || sProp == QLatin1String("viewerType")) return XJSValue::number(9.0);
        if (sProp == QLatin1String("platform")) return XJSValue::string(QStringLiteral("WIN"));
        if (sProp == QLatin1String("language")) return XJSValue::string(QStringLiteral("ENU"));
    }
    if (pObj->className == QLatin1String("Doc")) {
        if (sProp == QLatin1String("numPages")) return XJSValue::number(1.0);
        if (sProp == QLatin1String("info")) return XJSValue::object(newDom(QStringLiteral("Doc.info")));
    }

    // Any other DOM/native member is exposed as a callable native so `x.y()` and `x.y.apply()` work.
    if (!pObj->className.isEmpty() && (pObj->className != QLatin1String("Array"))) {
        return XJSValue::object(newNative(pObj->className + QLatin1Char('.') + sProp));
    }

    return XJSValue::undef();
}

void XJSInterpreter::setMember(const XJSValue &base, const QString &sProp, const XJSValue &val)
{
    if (!base.isObject()) {
        return;
    }

    JSObject *pObj = base.o.data();

    if (pObj->kind == JSObject::O_ARRAY) {
        bool bIdx = false;
        const qint32 nIdx = sProp.toInt(&bIdx);
        if (bIdx && (nIdx >= 0)) {
            if (nIdx < 1000000) {  // bound array growth
                while (pObj->elements.count() <= nIdx) {
                    pObj->elements.append(XJSValue::undef());
                }
                pObj->elements[nIdx] = val;
            }
            return;
        }
        if (sProp == QLatin1String("length")) {
            const qint32 nLen = static_cast<qint32>(val.toNumber());
            if ((nLen >= 0) && (nLen < 1000000)) {
                while (pObj->elements.count() > nLen) pObj->elements.removeLast();
                while (pObj->elements.count() < nLen) pObj->elements.append(XJSValue::undef());
            }
            return;
        }
    }

    pObj->props.insert(sProp, val);
}

// ---------------------------------------------------------------------------
// Calls
// ---------------------------------------------------------------------------

XJSValue XJSInterpreter::evalCall(Node *pNode, Environment *pEnv)
{
    Node *pCallee = pNode->a;

    QList<XJSValue> args;
    for (qint32 i = 0; i < pNode->list.count(); ++i) {
        args.append(evalNode(pNode->list.at(i), pEnv));
    }

    if (pCallee && (pCallee->type == N_MEMBER)) {
        const XJSValue base = evalNode(pCallee->a, pEnv);
        return callMethod(base, pCallee->sval, args);
    }

    if (pCallee && (pCallee->type == N_INDEX)) {
        const XJSValue base = evalNode(pCallee->a, pEnv);
        const XJSValue key = evalNode(pCallee->b, pEnv);
        return callMethod(base, key.toStr(), args);
    }

    const XJSValue fn = pCallee ? evalNode(pCallee, pEnv) : XJSValue::undef();
    return callFunction(fn, lookup(QStringLiteral("this"), pEnv), args);
}

XJSValue XJSInterpreter::callFunction(const XJSValue &fn, const XJSValue &thisVal, const QList<XJSValue> &args)
{
    if (!fn.isObject()) {
        return XJSValue::undef();
    }

    if (fn.o->kind == JSObject::O_NATIVE) {
        return callNative(fn.o->nativeName, thisVal, args);
    }

    if (fn.o->kind != JSObject::O_FUNCTION) {
        return XJSValue::undef();
    }

    if (m_nCallDepth >= m_nCallDepthLimit) {
        addIndicator(QStringLiteral("Call-depth limit reached (recursion)"));
        return XJSValue::undef();
    }

    Node *pFn = fn.o->fnNode;
    if (!pFn) {
        return XJSValue::undef();
    }

    ++m_nCallDepth;

    Environment *pFnEnv = newEnv(fn.o->closure ? fn.o->closure : m_pGlobal);

    for (qint32 i = 0; i < pFn->params.count(); ++i) {
        declare(pFn->params.at(i), (i < args.count()) ? args.at(i) : XJSValue::undef(), pFnEnv);
    }

    // arguments object.
    QSharedPointer<JSObject> pArgs = newArray();
    pArgs->elements = args;
    declare(QStringLiteral("arguments"), XJSValue::object(pArgs), pFnEnv);

    // this binding (Acrobat default this == Doc).
    declare(QStringLiteral("this"), thisVal.type == XJSValue::V_UNDEFINED ? lookup(QStringLiteral("this"), m_pGlobal) : thisVal, pFnEnv);

    if (pFn->a) {
        hoist(pFn->a, pFnEnv);
        execBlock(pFn->a, pFnEnv);
    }

    XJSValue vResult = XJSValue::undef();
    if (m_completion == C_RETURN) {
        vResult = m_completionValue;
        m_completion = C_NORMAL;
    } else if (m_completion == C_BREAK || m_completion == C_CONTINUE) {
        m_completion = C_NORMAL;  // do not leak out of the function
    }
    // C_THROW propagates.

    --m_nCallDepth;
    return vResult;
}

// ---------------------------------------------------------------------------
// Method dispatch (strings, arrays, functions, DOM)
// ---------------------------------------------------------------------------

XJSValue XJSInterpreter::callMethod(const XJSValue &base, const QString &sMethod, const QList<XJSValue> &args)
{
    if (base.type == XJSValue::V_STRING) {
        return nativeStringMethod(base, sMethod, args);
    }

    // Function.prototype apply/call/bind.
    if (base.isCallable()) {
        if (sMethod == QLatin1String("apply")) {
            const XJSValue vThis = args.count() > 0 ? args.at(0) : XJSValue::undef();
            QList<XJSValue> spread;
            if ((args.count() > 1) && args.at(1).isObject() && (args.at(1).o->kind == JSObject::O_ARRAY)) {
                spread = args.at(1).o->elements;
            }
            return callFunction(base, vThis, spread);
        }
        if (sMethod == QLatin1String("call")) {
            const XJSValue vThis = args.count() > 0 ? args.at(0) : XJSValue::undef();
            QList<XJSValue> rest = args.mid(1);
            return callFunction(base, vThis, rest);
        }
        if (sMethod == QLatin1String("bind")) {
            return base;  // approximation: ignore bound this/args
        }
    }

    if (base.isObject() && (base.o->kind == JSObject::O_ARRAY)) {
        return nativeArrayMethod(base, sMethod, args);
    }

    if (base.isObject()) {
        // Own callable property first.
        if (base.o->props.contains(sMethod)) {
            const XJSValue vProp = base.o->props.value(sMethod);
            if (vProp.isCallable()) {
                return callFunction(vProp, base, args);
            }
        }
        // DOM dispatch by class.method.
        if (!base.o->className.isEmpty()) {
            return callNative(base.o->className + QLatin1Char('.') + sMethod, base, args);
        }
    }

    return XJSValue::undef();
}

// ---------------------------------------------------------------------------
// Native / DOM implementations
// ---------------------------------------------------------------------------

XJSValue XJSInterpreter::runCode(const QString &sCode)
{
    if (sCode.isEmpty() || (sCode.length() > 4 * 1024 * 1024)) {
        return XJSValue::undef();
    }

    XJSParser *pParser = new XJSParser();
    m_subParsers.append(pParser);
    bool bErr = false;
    Node *pProgram = pParser->parse(sCode, &bErr);

    if (pProgram) {
        hoist(pProgram, m_pGlobal);
        for (qint32 i = 0; (i < pProgram->list.count()) && (m_completion == C_NORMAL) && !budgetExceeded(); ++i) {
            execStmt(pProgram->list.at(i), m_pGlobal);
        }
    }

    return XJSValue::undef();
}

XJSValue XJSInterpreter::callNative(const QString &sName, const XJSValue &thisVal, const QList<XJSValue> &args)
{
    Q_UNUSED(thisVal)

    const XJSValue a0 = args.count() > 0 ? args.at(0) : XJSValue::undef();
    const XJSValue a1 = args.count() > 1 ? args.at(1) : XJSValue::undef();

    // ---- global functions ----
    if (sName == QLatin1String("eval")) {
        const QString sCode = a0.toStr();
        if (m_pReport && (a0.type == XJSValue::V_STRING) && !m_pReport->evalArguments.contains(sCode)) {
            m_pReport->evalArguments.append(sCode.left(4096));
        }
        addIndicator(QStringLiteral("Dynamic code execution via eval()"));
        if (a0.type == XJSValue::V_STRING) {
            return runCode(sCode);
        }
        return a0;
    }
    if (sName == QLatin1String("unescape")) return jsUnescape(a0.toStr());
    if (sName == QLatin1String("escape")) return jsEscape(a0.toStr());
    if (sName == QLatin1String("decodeURIComponent") || sName == QLatin1String("decodeURI")) {
        return XJSValue::string(QString::fromUtf8(QByteArray::fromPercentEncoding(a0.toStr().toUtf8())));
    }
    if (sName == QLatin1String("encodeURIComponent") || sName == QLatin1String("encodeURI")) {
        return XJSValue::string(QString::fromLatin1(a0.toStr().toUtf8().toPercentEncoding()));
    }
    if (sName == QLatin1String("parseInt")) {
        QString s = a0.toStr().trimmed();
        int nRadix = (args.count() > 1) ? static_cast<int>(a1.toNumber()) : 10;
        bool bNeg = false;
        int i = 0;
        if ((i < s.length()) && ((s.at(i) == QChar('+')) || (s.at(i) == QChar('-')))) {
            bNeg = (s.at(i) == QChar('-'));
            ++i;
        }
        if (((nRadix == 16) || (nRadix == 0)) && (i + 1 < s.length()) && (s.at(i) == QChar('0')) && ((s.at(i + 1) == QChar('x')) || (s.at(i + 1) == QChar('X')))) {
            i += 2;
            nRadix = 16;
        }
        if (nRadix == 0) nRadix = 10;
        QString sDigits;
        while (i < s.length()) {
            const QChar c = s.at(i);
            int dv = -1;
            if (c.isDigit()) dv = c.digitValue();
            else if ((c.toLower() >= QChar('a')) && (c.toLower() <= QChar('z'))) dv = 10 + (c.toLower().unicode() - 'a');
            if ((dv < 0) || (dv >= nRadix)) break;
            sDigits.append(c);
            ++i;
        }
        if (sDigits.isEmpty()) return XJSValue::number(std::nan(""));
        bool bOk = false;
        const qulonglong v = sDigits.toULongLong(&bOk, nRadix);
        double d = static_cast<double>(v);
        return XJSValue::number(bNeg ? -d : d);
    }
    if (sName == QLatin1String("parseFloat")) {
        bool bOk = false;
        const double d = a0.toStr().trimmed().toDouble(&bOk);
        return XJSValue::number(bOk ? d : std::nan(""));
    }
    if (sName == QLatin1String("isNaN")) return XJSValue::boolean(std::isnan(a0.toNumber()));
    if (sName == QLatin1String("isFinite")) {
        const double d = a0.toNumber();
        return XJSValue::boolean(!std::isnan(d) && !std::isinf(d));
    }
    if (sName == QLatin1String("String")) return XJSValue::string(a0.toStr());
    if (sName == QLatin1String("Number")) return XJSValue::number(a0.toNumber());
    if (sName == QLatin1String("Boolean")) return XJSValue::boolean(a0.toBool());
    if (sName == QLatin1String("print") || sName == QLatin1String("alert")) {
        m_pReport->consoleOutput += a0.toStr() + QLatin1Char('\n');
        recordApi(sName, args);
        return XJSValue::undef();
    }
    if (sName == QLatin1String("setTimeout") || sName == QLatin1String("setInterval")) {
        recordApi(sName, args);
        addIndicator(QStringLiteral("Deferred execution via ") + sName + QStringLiteral("()"));
        if (a0.type == XJSValue::V_STRING) {
            if (m_pReport && !m_pReport->evalArguments.contains(a0.s)) {
                m_pReport->evalArguments.append(a0.s.left(4096));
            }
            return runCode(a0.s);
        }
        if (a0.isCallable()) {
            return callFunction(a0, XJSValue::undef(), QList<XJSValue>());
        }
        return XJSValue::undef();
    }
    if (sName == QLatin1String("Function")) {
        if (!args.isEmpty()) {
            const QString sCode = args.last().toStr();
            if (m_pReport && !m_pReport->evalArguments.contains(sCode)) {
                m_pReport->evalArguments.append(sCode.left(4096));
            }
            addIndicator(QStringLiteral("Dynamic code via Function()"));
        }
        return XJSValue::object(newObject(JSObject::O_FUNCTION));
    }
    if (sName == QLatin1String("Array")) {
        QSharedPointer<JSObject> p = newArray();
        if (!((args.count() == 1) && (a0.type == XJSValue::V_NUMBER))) {
            p->elements = args;
        }
        return XJSValue::object(p);
    }
    if (sName == QLatin1String("importScripts")) {
        recordApi(sName, args);
        return XJSValue::undef();
    }

    // ---- String static ----
    if (sName == QLatin1String("String.fromCharCode")) {
        QString sResult;
        for (qint32 i = 0; i < args.count(); ++i) {
            sResult.append(QChar(static_cast<ushort>(toUint32(args.at(i).toNumber()) & 0xFFFF)));
        }
        return XJSValue::string(sResult);
    }

    // ---- Math ----
    if (sName.startsWith(QLatin1String("Math."))) {
        const QString m = sName.mid(5);
        const double x = a0.toNumber();
        if (m == QLatin1String("floor")) return XJSValue::number(std::floor(x));
        if (m == QLatin1String("ceil")) return XJSValue::number(std::ceil(x));
        if (m == QLatin1String("round")) return XJSValue::number(std::floor(x + 0.5));
        if (m == QLatin1String("abs")) return XJSValue::number(std::fabs(x));
        if (m == QLatin1String("sqrt")) return XJSValue::number(std::sqrt(x));
        if (m == QLatin1String("pow")) return XJSValue::number(std::pow(x, a1.toNumber()));
        if (m == QLatin1String("max")) {
            double r = -INFINITY;
            for (qint32 i = 0; i < args.count(); ++i) r = qMax(r, args.at(i).toNumber());
            return XJSValue::number(r);
        }
        if (m == QLatin1String("min")) {
            double r = INFINITY;
            for (qint32 i = 0; i < args.count(); ++i) r = qMin(r, args.at(i).toNumber());
            return XJSValue::number(r);
        }
        if (m == QLatin1String("random")) return XJSValue::number(0.5);  // deterministic
        return XJSValue::number(std::nan(""));
    }

    // ---- Acrobat exploit-relevant surface: record + flag ----
    if (sName == QLatin1String("app.alert") || sName == QLatin1String("console.println") || sName == QLatin1String("Doc.println")) {
        m_pReport->consoleOutput += a0.toStr() + QLatin1Char('\n');
        recordApi(sName, args);
        return XJSValue::undef();
    }

    // Known CVE primitives.
    if (sName == QLatin1String("Collab.getIcon")) {
        recordApi(sName, args);
        addIndicator(QStringLiteral("Collab.getIcon (CVE-2009-0927 buffer overflow primitive)"));
        return XJSValue::undef();
    }
    if (sName == QLatin1String("spell.customDictionaryOpen")) {
        recordApi(sName, args);
        addIndicator(QStringLiteral("spell.customDictionaryOpen (CVE-2009-1493)"));
        return XJSValue::undef();
    }
    if (sName == QLatin1String("util.printf")) {
        recordApi(sName, args);
        addIndicator(QStringLiteral("util.printf (CVE-2008-2992 format-string primitive)"));
        // Best-effort format so a produced string is visible.
        return XJSValue::string(a0.toStr());
    }
    if (sName == QLatin1String("util.byteToChar")) {
        return XJSValue::string(QString(QChar(static_cast<ushort>(toUint32(a0.toNumber()) & 0xFFFF))));
    }
    if (sName == QLatin1String("Doc.exportDataObject")) {
        recordApi(sName, args);
        addIndicator(QStringLiteral("this.exportDataObject (drops/opens an embedded file)"));
        return XJSValue::undef();
    }
    if (sName == QLatin1String("Doc.getAnnots") || sName == QLatin1String("Doc.getAnnot")) {
        recordApi(sName, args);
        addIndicator(QStringLiteral("getAnnots (payload frequently hidden in annotation subject/contents)"));
        return XJSValue::object(newArray());
    }
    if (sName == QLatin1String("Doc.submitForm") || sName == QLatin1String("Doc.getURL") || sName == QLatin1String("app.launchURL") ||
        sName == QLatin1String("Doc.mailDoc") || sName == QLatin1String("Doc.mailForm")) {
        recordApi(sName, args);
        addIndicator(QStringLiteral("Network/exfil call: ") + sName);
        return XJSValue::undef();
    }
    if (sName == QLatin1String("Doc.importDataObject") || sName == QLatin1String("Doc.saveAs") || sName == QLatin1String("Doc.extractPages")) {
        recordApi(sName, args);
        addIndicator(QStringLiteral("File I/O: ") + sName);
        return XJSValue::undef();
    }
    if (sName == QLatin1String("app.beep") || sName == QLatin1String("app.execMenuItem") || sName == QLatin1String("app.newDoc") ||
        sName == QLatin1String("Doc.getField") || sName == QLatin1String("Doc.getNthFieldName")) {
        recordApi(sName, args);
        return sName.endsWith(QLatin1String("getField")) ? XJSValue::object(newDom(QStringLiteral("Field"))) : XJSValue::undef();
    }

    // Any other DOM method: record it so nothing is silently lost.
    if (sName.contains(QLatin1Char('.'))) {
        recordApi(sName, args);
    }

    return XJSValue::undef();
}

XJSValue XJSInterpreter::nativeStringMethod(const XJSValue &base, const QString &sMethod, const QList<XJSValue> &args)
{
    const QString s = base.s;
    const XJSValue a0 = args.count() > 0 ? args.at(0) : XJSValue::undef();
    const XJSValue a1 = args.count() > 1 ? args.at(1) : XJSValue::undef();

    if (sMethod == QLatin1String("charCodeAt")) {
        const int i = static_cast<int>(a0.toNumber());
        if ((i >= 0) && (i < s.length())) return XJSValue::number(s.at(i).unicode());
        return XJSValue::number(std::nan(""));
    }
    if (sMethod == QLatin1String("charAt")) {
        const int i = static_cast<int>(a0.toNumber());
        if ((i >= 0) && (i < s.length())) return XJSValue::string(QString(s.at(i)));
        return XJSValue::string(QString());
    }
    if (sMethod == QLatin1String("indexOf")) return XJSValue::number(s.indexOf(a0.toStr(), args.count() > 1 ? static_cast<int>(a1.toNumber()) : 0));
    if (sMethod == QLatin1String("lastIndexOf")) return XJSValue::number(s.lastIndexOf(a0.toStr()));
    if (sMethod == QLatin1String("substring")) {
        int start = qMax(0, static_cast<int>(a0.toNumber()));
        int end = (args.count() > 1) ? static_cast<int>(a1.toNumber()) : s.length();
        if (end < 0) end = 0;
        if (end > s.length()) end = s.length();
        if (start > s.length()) start = s.length();
        if (start > end) { const int t = start; start = end; end = t; }
        return XJSValue::string(s.mid(start, end - start));
    }
    if (sMethod == QLatin1String("substr")) {
        int start = static_cast<int>(a0.toNumber());
        if (start < 0) start = qMax(0, s.length() + start);
        const int len = (args.count() > 1) ? static_cast<int>(a1.toNumber()) : (s.length() - start);
        return XJSValue::string(s.mid(start, len));
    }
    if (sMethod == QLatin1String("slice")) {
        int start = static_cast<int>(a0.toNumber());
        int end = (args.count() > 1) ? static_cast<int>(a1.toNumber()) : s.length();
        if (start < 0) start = qMax(0, s.length() + start);
        if (end < 0) end = qMax(0, s.length() + end);
        if (end > s.length()) end = s.length();
        if (start > end) return XJSValue::string(QString());
        return XJSValue::string(s.mid(start, end - start));
    }
    if (sMethod == QLatin1String("toUpperCase")) return XJSValue::string(s.toUpper());
    if (sMethod == QLatin1String("toLowerCase")) return XJSValue::string(s.toLower());
    if (sMethod == QLatin1String("toString")) return base;
    if (sMethod == QLatin1String("trim")) return XJSValue::string(s.trimmed());
    if (sMethod == QLatin1String("concat")) {
        QString r = s;
        for (qint32 i = 0; i < args.count(); ++i) r += args.at(i).toStr();
        return XJSValue::string(r);
    }
    if (sMethod == QLatin1String("repeat")) {
        const int nCount = qBound(0, static_cast<int>(a0.toNumber()), 100000);
        if (static_cast<qint64>(nCount) * s.length() > m_nMaxStringLength) {
            addIndicator(QStringLiteral("String.repeat cap hit (heap-spray style build?)"));
            return XJSValue::string(s.repeated(qMax(1, static_cast<int>(m_nMaxStringLength / qMax(1, s.length())))));
        }
        return XJSValue::string(s.repeated(nCount));
    }
    if (sMethod == QLatin1String("split")) {
        QSharedPointer<JSObject> p = newArray();
        if (a0.type == XJSValue::V_UNDEFINED) {
            p->elements.append(base);
        } else {
            const QString sep = a0.toStr();
            if (sep.isEmpty()) {
                for (qint32 i = 0; i < s.length(); ++i) p->elements.append(XJSValue::string(QString(s.at(i))));
            } else {
                const QStringList parts = s.split(sep);
                for (qint32 i = 0; i < parts.count(); ++i) p->elements.append(XJSValue::string(parts.at(i)));
            }
        }
        return XJSValue::object(p);
    }
    if (sMethod == QLatin1String("replace")) {
        QString sPattern;
        QString sFlags;
        bool bRegex = false;
        if (a0.isObject() && (a0.o->kind == JSObject::O_REGEX)) {
            sPattern = a0.o->regexSource;
            sFlags = a0.o->regexFlags;
            bRegex = true;
        } else {
            sPattern = a0.toStr();
        }
        if (a1.isCallable()) {
            // Function replacer: fn(match, p1..pn, offset, string) per match. This IS common in
            // obfuscation (e.g. s.replace(/../g, function(m){return String.fromCharCode(parseInt(m,16))})).
            if (!bRegex) {
                const int nAt = s.indexOf(sPattern);
                if (nAt == -1) {
                    return base;
                }
                QList<XJSValue> cbArgs;
                cbArgs << XJSValue::string(sPattern) << XJSValue::number(nAt) << XJSValue::string(s);
                const XJSValue vRepl = callFunction(a1, XJSValue::undef(), cbArgs);
                return XJSValue::string(s.left(nAt) + vRepl.toStr() + s.mid(nAt + sPattern.length()));
            }

            QRegularExpression::PatternOptions opts = QRegularExpression::NoPatternOption;
            if (sFlags.contains(QLatin1Char('i'))) opts |= QRegularExpression::CaseInsensitiveOption;
            if (sFlags.contains(QLatin1Char('m'))) opts |= QRegularExpression::MultilineOption;
            if (sFlags.contains(QLatin1Char('s'))) opts |= QRegularExpression::DotMatchesEverythingOption;
            QRegularExpression re(sPattern, opts);
            if (!re.isValid()) {
                return base;
            }

            const bool bGlobal = sFlags.contains(QLatin1Char('g'));
            QString sOut;
            qint32 nLast = 0;
            qint32 nGuard = 0;
            QRegularExpressionMatchIterator it = re.globalMatch(s);
            while (it.hasNext() && !budgetExceeded()) {
                const QRegularExpressionMatch m = it.next();
                QList<XJSValue> cbArgs;
                cbArgs << XJSValue::string(m.captured(0));
                for (qint32 c = 1; c <= m.lastCapturedIndex(); ++c) {
                    cbArgs << XJSValue::string(m.captured(c));
                }
                cbArgs << XJSValue::number(m.capturedStart(0)) << XJSValue::string(s);
                const XJSValue vRepl = callFunction(a1, XJSValue::undef(), cbArgs);
                sOut += s.mid(nLast, m.capturedStart(0) - nLast) + vRepl.toStr();
                nLast = m.capturedEnd(0);
                if (!bGlobal || (++nGuard > 1000000)) {
                    break;
                }
            }
            sOut += s.mid(nLast);
            return XJSValue::string(sOut);
        }

        const QString sRepl = a1.toStr();
        if (bRegex) {
            return XJSValue::string(regexReplace(s, sPattern, sFlags, sRepl));
        }
        QString r = s;
        const int nAt = r.indexOf(sPattern);
        if (nAt != -1) {
            r.replace(nAt, sPattern.length(), sRepl);
        }
        return XJSValue::string(r);
    }
    if (sMethod == QLatin1String("match") || sMethod == QLatin1String("search")) {
        return XJSValue::null();
    }

    return XJSValue::undef();
}

XJSValue XJSInterpreter::nativeArrayMethod(const XJSValue &base, const QString &sMethod, const QList<XJSValue> &args)
{
    JSObject *pObj = base.o.data();

    if (sMethod == QLatin1String("push")) {
        for (qint32 i = 0; i < args.count(); ++i) pObj->elements.append(args.at(i));
        return XJSValue::number(pObj->elements.count());
    }
    if (sMethod == QLatin1String("pop")) {
        if (pObj->elements.isEmpty()) return XJSValue::undef();
        const XJSValue v = pObj->elements.last();
        pObj->elements.removeLast();
        return v;
    }
    if (sMethod == QLatin1String("shift")) {
        if (pObj->elements.isEmpty()) return XJSValue::undef();
        return pObj->elements.takeFirst();
    }
    if (sMethod == QLatin1String("unshift")) {
        for (qint32 i = args.count() - 1; i >= 0; --i) pObj->elements.prepend(args.at(i));
        return XJSValue::number(pObj->elements.count());
    }
    if (sMethod == QLatin1String("join")) {
        const QString sep = args.isEmpty() ? QStringLiteral(",") : args.at(0).toStr();
        QStringList list;
        for (qint32 i = 0; i < pObj->elements.count(); ++i) {
            const XJSValue &e = pObj->elements.at(i);
            list.append((e.type == XJSValue::V_UNDEFINED || e.type == XJSValue::V_NULL) ? QString() : e.toStr());
        }
        return XJSValue::string(list.join(sep));
    }
    if (sMethod == QLatin1String("reverse")) {
        QList<XJSValue> r;
        for (qint32 i = pObj->elements.count() - 1; i >= 0; --i) r.append(pObj->elements.at(i));
        pObj->elements = r;
        return base;
    }
    if (sMethod == QLatin1String("slice")) {
        QSharedPointer<JSObject> p = newArray();
        int start = args.count() > 0 ? static_cast<int>(args.at(0).toNumber()) : 0;
        int end = args.count() > 1 ? static_cast<int>(args.at(1).toNumber()) : pObj->elements.count();
        if (start < 0) start = qMax(0, pObj->elements.count() + start);
        if (end < 0) end = qMax(0, pObj->elements.count() + end);
        for (int i = start; (i < end) && (i < pObj->elements.count()); ++i) p->elements.append(pObj->elements.at(i));
        return XJSValue::object(p);
    }
    if (sMethod == QLatin1String("concat")) {
        QSharedPointer<JSObject> p = newArray();
        p->elements = pObj->elements;
        for (qint32 i = 0; i < args.count(); ++i) {
            const XJSValue &a = args.at(i);
            if (a.isObject() && (a.o->kind == JSObject::O_ARRAY)) p->elements.append(a.o->elements);
            else p->elements.append(a);
        }
        return XJSValue::object(p);
    }
    if (sMethod == QLatin1String("indexOf")) {
        const XJSValue needle = args.count() > 0 ? args.at(0) : XJSValue::undef();
        for (qint32 i = 0; i < pObj->elements.count(); ++i) {
            if (evalBinary(QStringLiteral("==="), pObj->elements.at(i), needle).toBool()) return XJSValue::number(i);
        }
        return XJSValue::number(-1);
    }
    if (sMethod == QLatin1String("toString")) {
        return XJSValue::string(base.toStr());
    }

    return XJSValue::undef();
}

XJSValue XJSInterpreter::jsUnescape(const QString &s)
{
    QString r;
    const qint32 n = s.length();
    qint32 i = 0;
    while (i < n) {
        const QChar c = s.at(i);
        if (c == QChar('%')) {
            if ((i + 5 < n) && ((s.at(i + 1) == QChar('u')) || (s.at(i + 1) == QChar('U')))) {
                bool bOk = false;
                const uint v = s.mid(i + 2, 4).toUInt(&bOk, 16);
                if (bOk) {
                    r.append(QChar(static_cast<ushort>(v)));
                    i += 6;
                    continue;
                }
            } else if (i + 2 < n) {
                bool bOk = false;
                const uint v = s.mid(i + 1, 2).toUInt(&bOk, 16);
                if (bOk) {
                    r.append(QChar(static_cast<ushort>(v)));
                    i += 3;
                    continue;
                }
            }
        }
        r.append(c);
        ++i;
    }
    return XJSValue::string(r);
}

XJSValue XJSInterpreter::jsEscape(const QString &s)
{
    QString r;
    for (qint32 i = 0; i < s.length(); ++i) {
        const ushort u = s.at(i).unicode();
        const QChar c = s.at(i);
        if (c.isLetterOrNumber() && (u < 128)) {
            r.append(c);
        } else if ((c == QChar('@')) || (c == QChar('*')) || (c == QChar('_')) || (c == QChar('+')) || (c == QChar('-')) || (c == QChar('.')) ||
                   (c == QChar('/'))) {
            r.append(c);
        } else if (u < 256) {
            r.append(QChar('%'));
            r.append(QString::asprintf("%02X", u));
        } else {
            r.append(QStringLiteral("%u"));
            r.append(QString::asprintf("%04X", u));
        }
    }
    return XJSValue::string(r);
}

QString XJSInterpreter::regexReplace(const QString &sInput, const QString &sPattern, const QString &sFlags, const QString &sReplacement)
{
    QRegularExpression::PatternOptions opts = QRegularExpression::NoPatternOption;
    if (sFlags.contains(QLatin1Char('i'))) opts |= QRegularExpression::CaseInsensitiveOption;
    if (sFlags.contains(QLatin1Char('m'))) opts |= QRegularExpression::MultilineOption;
    if (sFlags.contains(QLatin1Char('s'))) opts |= QRegularExpression::DotMatchesEverythingOption;

    QRegularExpression re(sPattern, opts);
    if (!re.isValid()) {
        return sInput;
    }

    // Translate JS replacement backreferences ($1, $&) to Qt's (\1, \0).
    QString sQtRepl;
    for (qint32 i = 0; i < sReplacement.length(); ++i) {
        const QChar c = sReplacement.at(i);
        if ((c == QChar('$')) && (i + 1 < sReplacement.length())) {
            const QChar d = sReplacement.at(i + 1);
            if (d == QChar('&')) {
                sQtRepl.append(QStringLiteral("\\0"));
                ++i;
                continue;
            }
            if (d.isDigit()) {
                sQtRepl.append(QLatin1Char('\\'));
                sQtRepl.append(d);
                ++i;
                continue;
            }
            if (d == QChar('$')) {
                sQtRepl.append(QLatin1Char('$'));
                ++i;
                continue;
            }
        }
        if (c == QChar('\\')) {
            sQtRepl.append(QStringLiteral("\\\\"));
            continue;
        }
        sQtRepl.append(c);
    }

    if (sFlags.contains(QLatin1Char('g'))) {
        QString sResult = sInput;
        sResult.replace(re, sQtRepl);
        return sResult;
    }

    // Non-global: replace only the first match, expanding $n / $& backreferences manually.
    const QRegularExpressionMatch m = re.match(sInput);
    if (!m.hasMatch()) {
        return sInput;
    }

    QString sExpanded;
    for (qint32 i = 0; i < sReplacement.length(); ++i) {
        const QChar c = sReplacement.at(i);
        if ((c == QChar('$')) && (i + 1 < sReplacement.length())) {
            const QChar d = sReplacement.at(i + 1);
            if (d == QChar('&')) {
                sExpanded.append(m.captured(0));
                ++i;
                continue;
            }
            if (d.isDigit()) {
                sExpanded.append(m.captured(d.digitValue()));
                ++i;
                continue;
            }
            if (d == QChar('$')) {
                sExpanded.append(QLatin1Char('$'));
                ++i;
                continue;
            }
        }
        sExpanded.append(c);
    }

    return sInput.left(m.capturedStart()) + sExpanded + sInput.mid(m.capturedStart() + m.capturedLength());
}

}  // namespace XJS
