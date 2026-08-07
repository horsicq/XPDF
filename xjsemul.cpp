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
#include "xjsemul.h"
#include "xjsparser.h"

XJSEmul::XJSEmul() : m_nStepLimit(0)
{
}

void XJSEmul::setStepLimit(qint64 nStepLimit)
{
    m_nStepLimit = nStepLimit;
}

XJS::XJSReport XJSEmul::analyze(const QString &sSource)
{
    XJS::XJSReport report;

    if (sSource.trimmed().isEmpty()) {
        return report;
    }

    // The parser owns the AST arena; keep it alive for the whole synchronous run.
    XJS::XJSParser parser;
    bool bParseError = false;
    XJS::Node *pProgram = parser.parse(sSource, &bParseError);

    XJS::XJSInterpreter interpreter;
    interpreter.setStepLimit(m_nStepLimit);
    interpreter.run(pProgram, &report);

    if (bParseError && report.errorMessage.isEmpty()) {
        report.errorMessage = QStringLiteral("Parse warnings (tolerant recovery applied)");
    }

    return report;
}

XJS::XJSReport XJSEmul::analyze(const QByteArray &baSource)
{
    // PDF /JS strings are commonly PDFDocEncoded/Latin-1, or UTF-16 with a BOM.
    if ((baSource.size() >= 2) && (static_cast<quint8>(baSource.at(0)) == 0xFE) && (static_cast<quint8>(baSource.at(1)) == 0xFF)) {
        QString s;
        for (int i = 2; (i + 1) < baSource.size(); i += 2) {
            const ushort u = (static_cast<quint8>(baSource.at(i)) << 8) | static_cast<quint8>(baSource.at(i + 1));
            s.append(QChar(u));
        }
        return analyze(s);
    }
    return analyze(QString::fromLatin1(baSource));
}

QString XJSEmul::analyzeToString(const QString &sSource)
{
    const XJS::XJSReport report = analyze(sSource);
    return reportToString(report);
}

QString XJSEmul::reportToString(const XJS::XJSReport &report)
{
    QStringList lines;

    if (!report.indicators.isEmpty()) {
        lines << QStringLiteral("Indicators:");
        for (int i = 0; i < report.indicators.count(); ++i) {
            lines << (QStringLiteral("  - ") + report.indicators.at(i));
        }
    }

    if (!report.evalArguments.isEmpty()) {
        lines << QStringLiteral("Deobfuscated / dynamic payloads:");
        for (int i = 0; i < report.evalArguments.count(); ++i) {
            QString s = report.evalArguments.at(i);
            if (s.length() > 400) {
                s = s.left(400) + QStringLiteral(" ...");
            }
            lines << (QStringLiteral("  - ") + s);
        }
    }

    if (!report.apiCalls.isEmpty()) {
        lines << QStringLiteral("API calls:");
        for (int i = 0; i < report.apiCalls.count(); ++i) {
            lines << (QStringLiteral("  - ") + report.apiCalls.at(i));
        }
    }

    if (!report.consoleOutput.trimmed().isEmpty()) {
        lines << QStringLiteral("Console/alert output:");
        const QStringList outLines = report.consoleOutput.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (int i = 0; i < outLines.count(); ++i) {
            lines << (QStringLiteral("  ") + outLines.at(i));
        }
    }

    if (!report.strings.isEmpty()) {
        lines << QStringLiteral("Notable strings:");
        for (int i = 0; i < report.strings.count(); ++i) {
            lines << (QStringLiteral("  - ") + report.strings.at(i));
        }
    }

    return lines.join(QLatin1Char('\n'));
}
