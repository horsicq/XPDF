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
#ifndef XJSPARSER_H
#define XJSPARSER_H

#include "xjsast.h"
#include "xjslexer.h"

namespace XJS {

// Recursive-descent / precedence-climbing parser for the supported ECMAScript subset. Tolerant: on an
// unexpected token it records an error and attempts to recover so obfuscated scripts still yield a tree.
// All nodes are arena-owned; the tree is valid only for the Parser's lifetime.
class XJSParser {
public:
    XJSParser();
    ~XJSParser();

    Node *parse(const QString &sSource, bool *pbError = nullptr);

private:
    Node *newNode(NodeType type);

    const Token &peek() const;
    const Token &peekAt(qint32 nAhead) const;
    bool isPunct(const QString &s) const;
    bool isKeyword(const QString &s) const;
    bool eatPunct(const QString &s);
    bool eatKeyword(const QString &s);
    void expectPunct(const QString &s);

    Node *parseStatement();
    Node *parseBlock();
    Node *parseVar();
    Node *parseFunction(bool bExpression);
    Node *parseIf();
    Node *parseFor();
    Node *parseWhile();
    Node *parseDoWhile();
    Node *parseTry();
    Node *parseSwitch();

    Node *parseExpression();      // full expression (allows comma sequence)
    Node *parseAssignment();
    Node *parseConditional();
    Node *parseBinary(qint32 nMinPrec);
    Node *parseUnary();
    Node *parsePostfix();
    Node *parseCallMember();
    Node *parsePrimary();
    Node *parseArguments(Node *pCallee, bool bNew);

    static qint32 binaryPrecedence(const QString &sOp);

private:
    QList<Token> m_tokens;
    qint32 m_pos;
    bool m_error;
    QList<Node *> m_arena;
    Token m_eof;
};

}  // namespace XJS

#endif  // XJSPARSER_H
