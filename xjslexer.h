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
#ifndef XJSLEXER_H
#define XJSLEXER_H

#include <QList>
#include <QString>

namespace XJS {

enum TokType {
    TT_EOF = 0,
    TT_NUMBER,
    TT_STRING,
    TT_IDENT,
    TT_KEYWORD,
    TT_PUNCT,
    TT_REGEX
};

struct Token {
    TokType type;
    QString text;   // identifier/keyword/punctuator text, decoded string value, or regex source
    QString flags;  // regex flags
    double num;     // numeric value
    qint32 pos;     // source offset
    qint32 line;

    Token() : type(TT_EOF), num(0.0), pos(0), line(1)
    {
    }
};

// A tolerant ECMAScript lexer (subset sufficient for PDF/Acrobat JavaScript). Decodes string escapes
// (\xHH \uHHHH \n \t octal ...), recognises regex literals via previous-token context, and never throws.
class XJSLexer {
public:
    XJSLexer();

    QList<Token> tokenize(const QString &sSource, bool *pbError = nullptr);

private:
    static bool isIdentStart(QChar c);
    static bool isIdentPart(QChar c);
    static bool isKeyword(const QString &s);
    // Whether a '/' at the current position begins a regex (true) or is a divide operator (false),
    // decided from the previous significant token.
    static bool regexAllowed(const Token *pPrev);

    QString readString(const QString &s, qint32 &i, QChar quote, bool *pbError);
    QString readRegex(const QString &s, qint32 &i, QString *pFlags, bool *pbError);
};

}  // namespace XJS

#endif  // XJSLEXER_H
