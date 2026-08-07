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
#ifndef XJSEMUL_H
#define XJSEMUL_H

#include "xjsinterpreter.h"

// XJSEmul: a self-contained (Qt-only, no XBinary dependency) JavaScript emulator for PDF/Acrobat scripts.
// It lexes, parses and *executes* the script through a tree-walking interpreter with an Acrobat DOM, so
// multi-stage obfuscation (eval chains, String.fromCharCode, unescape, concatenation, replace) is unwound
// and the dangerous API surface (exportDataObject, Collab.getIcon, util.printf, launchURL, ...) is captured.
// Execution is bounded (step / recursion / loop / string caps) so hostile scripts cannot hang or OOM.
class XJSEmul {
public:
    XJSEmul();

    // Analyse a JavaScript source string; returns the structured report.
    XJS::XJSReport analyze(const QString &sSource);
    XJS::XJSReport analyze(const QByteArray &baSource);

    // Analyse and return a human-readable multi-line summary (empty if nothing notable was found).
    QString analyzeToString(const QString &sSource);

    static QString reportToString(const XJS::XJSReport &report);

    // Tuning knobs (0 / negative keeps the default).
    void setStepLimit(qint64 nStepLimit);

private:
    qint64 m_nStepLimit;
};

#endif  // XJSEMUL_H
