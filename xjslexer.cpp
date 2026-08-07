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
#include "xjslexer.h"

namespace XJS {

XJSLexer::XJSLexer()
{
}

bool XJSLexer::isIdentStart(QChar c)
{
    return c.isLetter() || (c == QChar('_')) || (c == QChar('$'));
}

bool XJSLexer::isIdentPart(QChar c)
{
    return c.isLetterOrNumber() || (c == QChar('_')) || (c == QChar('$'));
}

bool XJSLexer::isKeyword(const QString &s)
{
    return (s == QLatin1String("var")) || (s == QLatin1String("let")) || (s == QLatin1String("const")) || (s == QLatin1String("function")) ||
           (s == QLatin1String("return")) || (s == QLatin1String("if")) || (s == QLatin1String("else")) || (s == QLatin1String("for")) ||
           (s == QLatin1String("while")) || (s == QLatin1String("do")) || (s == QLatin1String("break")) || (s == QLatin1String("continue")) ||
           (s == QLatin1String("new")) || (s == QLatin1String("delete")) || (s == QLatin1String("typeof")) || (s == QLatin1String("instanceof")) ||
           (s == QLatin1String("in")) || (s == QLatin1String("this")) || (s == QLatin1String("true")) || (s == QLatin1String("false")) ||
           (s == QLatin1String("null")) || (s == QLatin1String("undefined")) || (s == QLatin1String("void")) || (s == QLatin1String("switch")) ||
           (s == QLatin1String("case")) || (s == QLatin1String("default")) || (s == QLatin1String("try")) || (s == QLatin1String("catch")) ||
           (s == QLatin1String("finally")) || (s == QLatin1String("throw"));
}

bool XJSLexer::regexAllowed(const Token *pPrev)
{
    if (!pPrev) {
        return true;  // start of input
    }

    if (pPrev->type == TT_NUMBER || pPrev->type == TT_STRING || pPrev->type == TT_REGEX) {
        return false;
    }

    if (pPrev->type == TT_IDENT) {
        return false;  // identifier value -> '/' is division
    }

    if (pPrev->type == TT_KEYWORD) {
        // Value-producing keywords are followed by division; statement keywords allow a regex.
        if ((pPrev->text == QLatin1String("this")) || (pPrev->text == QLatin1String("true")) || (pPrev->text == QLatin1String("false")) ||
            (pPrev->text == QLatin1String("null")) || (pPrev->text == QLatin1String("undefined"))) {
            return false;
        }
        return true;
    }

    if (pPrev->type == TT_PUNCT) {
        // After a closing ) ] } or ++/-- a '/' is division; otherwise a regex may start.
        if ((pPrev->text == QLatin1String(")")) || (pPrev->text == QLatin1String("]")) || (pPrev->text == QLatin1String("}")) ||
            (pPrev->text == QLatin1String("++")) || (pPrev->text == QLatin1String("--"))) {
            return false;
        }
        return true;
    }

    return true;
}

QString XJSLexer::readString(const QString &s, qint32 &i, QChar quote, bool *pbError)
{
    QString sResult;
    const qint32 n = s.size();
    ++i;  // skip opening quote

    while (i < n) {
        const QChar c = s.at(i);

        if (c == quote) {
            ++i;
            return sResult;
        }

        if (c == QChar('\\')) {
            ++i;
            if (i >= n) {
                break;
            }
            const QChar e = s.at(i);
            if (e == QChar('n')) {
                sResult.append(QChar('\n'));
                ++i;
            } else if (e == QChar('t')) {
                sResult.append(QChar('\t'));
                ++i;
            } else if (e == QChar('r')) {
                sResult.append(QChar('\r'));
                ++i;
            } else if (e == QChar('b')) {
                sResult.append(QChar('\b'));
                ++i;
            } else if (e == QChar('f')) {
                sResult.append(QChar('\f'));
                ++i;
            } else if (e == QChar('v')) {
                sResult.append(QChar('\v'));
                ++i;
            } else if (e == QChar('0') && ((i + 1 >= n) || !s.at(i + 1).isDigit())) {
                sResult.append(QChar(QChar::Null));
                ++i;
            } else if (e == QChar('x')) {
                ++i;
                QString sHex = s.mid(i, 2);
                bool bOk = false;
                const uint nVal = sHex.toUInt(&bOk, 16);
                if (bOk) {
                    sResult.append(QChar(static_cast<ushort>(nVal)));
                    i += 2;
                } else {
                    sResult.append(e);
                }
            } else if (e == QChar('u')) {
                ++i;
                if ((i < n) && (s.at(i) == QChar('{'))) {
                    const qint32 nEnd = s.indexOf(QChar('}'), i);
                    if (nEnd != -1) {
                        QString sHex = s.mid(i + 1, nEnd - i - 1);
                        bool bOk = false;
                        const uint nVal = sHex.toUInt(&bOk, 16);
                        if (bOk) {
                            sResult.append(QChar(static_cast<ushort>(nVal & 0xFFFF)));
                        }
                        i = nEnd + 1;
                    }
                } else {
                    QString sHex = s.mid(i, 4);
                    bool bOk = false;
                    const uint nVal = sHex.toUInt(&bOk, 16);
                    if (bOk) {
                        sResult.append(QChar(static_cast<ushort>(nVal)));
                        i += 4;
                    } else {
                        sResult.append(e);
                    }
                }
            } else if (e == QChar('\n')) {
                ++i;  // line continuation
            } else if (e == QChar('\r')) {
                ++i;
                if ((i < n) && (s.at(i) == QChar('\n'))) {
                    ++i;
                }
            } else {
                sResult.append(e);
                ++i;
            }
        } else {
            sResult.append(c);
            ++i;
        }
    }

    if (pbError) {
        *pbError = true;  // unterminated string
    }
    return sResult;
}

QString XJSLexer::readRegex(const QString &s, qint32 &i, QString *pFlags, bool *pbError)
{
    QString sBody;
    const qint32 n = s.size();
    ++i;  // skip opening '/'
    bool bInClass = false;

    while (i < n) {
        const QChar c = s.at(i);
        if (c == QChar('\\')) {
            sBody.append(c);
            ++i;
            if (i < n) {
                sBody.append(s.at(i));
                ++i;
            }
            continue;
        }
        if (c == QChar('[')) {
            bInClass = true;
        } else if (c == QChar(']')) {
            bInClass = false;
        } else if ((c == QChar('/')) && !bInClass) {
            ++i;
            QString sFlags;
            while ((i < n) && isIdentPart(s.at(i))) {
                sFlags.append(s.at(i));
                ++i;
            }
            if (pFlags) {
                *pFlags = sFlags;
            }
            return sBody;
        } else if ((c == QChar('\n')) || (c == QChar('\r'))) {
            break;  // regex cannot span lines
        }
        sBody.append(c);
        ++i;
    }

    if (pbError) {
        *pbError = true;
    }
    return sBody;
}

QList<Token> XJSLexer::tokenize(const QString &sSource, bool *pbError)
{
    QList<Token> listResult;
    bool bError = false;

    const qint32 n = sSource.size();
    qint32 i = 0;
    qint32 nLine = 1;

    while (i < n) {
        const QChar c = sSource.at(i);

        // Whitespace / line tracking.
        if (c == QChar('\n')) {
            ++nLine;
            ++i;
            continue;
        }
        if (c.isSpace()) {
            ++i;
            continue;
        }

        // Comments.
        if ((c == QChar('/')) && (i + 1 < n) && (sSource.at(i + 1) == QChar('/'))) {
            i += 2;
            while ((i < n) && (sSource.at(i) != QChar('\n'))) {
                ++i;
            }
            continue;
        }
        if ((c == QChar('/')) && (i + 1 < n) && (sSource.at(i + 1) == QChar('*'))) {
            i += 2;
            while ((i + 1 < n) && !((sSource.at(i) == QChar('*')) && (sSource.at(i + 1) == QChar('/')))) {
                if (sSource.at(i) == QChar('\n')) {
                    ++nLine;
                }
                ++i;
            }
            i += 2;
            continue;
        }

        Token tok;
        tok.pos = i;
        tok.line = nLine;

        // String literal.
        if ((c == QChar('"')) || (c == QChar('\''))) {
            tok.type = TT_STRING;
            tok.text = readString(sSource, i, c, &bError);
            listResult.append(tok);
            continue;
        }

        // Number literal.
        if (c.isDigit() || ((c == QChar('.')) && (i + 1 < n) && sSource.at(i + 1).isDigit())) {
            qint32 nStart = i;
            bool bHex = false;
            if ((c == QChar('0')) && (i + 1 < n) && ((sSource.at(i + 1) == QChar('x')) || (sSource.at(i + 1) == QChar('X')))) {
                bHex = true;
                i += 2;
                while ((i < n) && (sSource.at(i).isDigit() || ((sSource.at(i).toLower() >= QChar('a')) && (sSource.at(i).toLower() <= QChar('f'))))) {
                    ++i;
                }
            } else {
                while ((i < n) && (sSource.at(i).isDigit() || (sSource.at(i) == QChar('.')) || (sSource.at(i) == QChar('e')) || (sSource.at(i) == QChar('E')) ||
                                   (((sSource.at(i) == QChar('+')) || (sSource.at(i) == QChar('-'))) && (i > nStart) &&
                                    ((sSource.at(i - 1) == QChar('e')) || (sSource.at(i - 1) == QChar('E')))))) {
                    ++i;
                }
            }
            const QString sNum = sSource.mid(nStart, i - nStart);
            bool bOk = false;
            tok.type = TT_NUMBER;
            if (bHex) {
                tok.num = static_cast<double>(sNum.mid(2).toULongLong(&bOk, 16));
            } else {
                tok.num = sNum.toDouble(&bOk);
            }
            tok.text = sNum;
            listResult.append(tok);
            continue;
        }

        // Identifier / keyword.
        if (isIdentStart(c)) {
            qint32 nStart = i;
            while ((i < n) && isIdentPart(sSource.at(i))) {
                ++i;
            }
            tok.text = sSource.mid(nStart, i - nStart);
            tok.type = isKeyword(tok.text) ? TT_KEYWORD : TT_IDENT;
            listResult.append(tok);
            continue;
        }

        // Regex literal vs division.
        if (c == QChar('/')) {
            const Token *pPrev = listResult.isEmpty() ? nullptr : &listResult.last();
            if (regexAllowed(pPrev)) {
                tok.type = TT_REGEX;
                QString sFlags;
                tok.text = readRegex(sSource, i, &sFlags, &bError);
                tok.flags = sFlags;
                listResult.append(tok);
                continue;
            }
        }

        // Punctuators (longest match first).
        static const char *const sPuncts3[] = {"===", "!==", ">>>", "**=", "<<=", ">>=", "..."};
        static const char *const sPuncts2[] = {"==", "!=", "<=", ">=", "&&", "||", "++", "--", "+=", "-=", "*=", "/=",
                                               "%=", "&=", "|=", "^=", "<<", ">>", "=>", "**"};
        bool bMatched = false;

        for (qint32 p = 0; p < 7; ++p) {
            const QString sP = QString::fromLatin1(sPuncts3[p]);
            if (sSource.mid(i, 3) == sP) {
                tok.type = TT_PUNCT;
                tok.text = sP;
                listResult.append(tok);
                i += 3;
                bMatched = true;
                break;
            }
        }
        if (bMatched) {
            continue;
        }

        for (qint32 p = 0; p < 20; ++p) {
            const QString sP = QString::fromLatin1(sPuncts2[p]);
            if (sSource.mid(i, 2) == sP) {
                tok.type = TT_PUNCT;
                tok.text = sP;
                listResult.append(tok);
                i += 2;
                bMatched = true;
                break;
            }
        }
        if (bMatched) {
            continue;
        }

        // Single-character punctuator.
        tok.type = TT_PUNCT;
        tok.text = QString(c);
        listResult.append(tok);
        ++i;
    }

    Token eof;
    eof.type = TT_EOF;
    eof.pos = n;
    eof.line = nLine;
    listResult.append(eof);

    if (pbError) {
        *pbError = bError;
    }

    return listResult;
}

}  // namespace XJS
