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
#include "xjsparser.h"

namespace XJS {

XJSParser::XJSParser() : m_pos(0), m_error(false)
{
    m_eof.type = TT_EOF;
}

XJSParser::~XJSParser()
{
    for (qint32 i = 0; i < m_arena.count(); ++i) {
        delete m_arena.at(i);
    }
    m_arena.clear();
}

Node *XJSParser::newNode(NodeType type)
{
    Node *pNode = new Node();
    pNode->type = type;
    m_arena.append(pNode);
    return pNode;
}

const Token &XJSParser::peek() const
{
    if (m_pos < m_tokens.count()) {
        return m_tokens.at(m_pos);
    }
    return m_eof;
}

const Token &XJSParser::peekAt(qint32 nAhead) const
{
    const qint32 nIndex = m_pos + nAhead;
    if ((nIndex >= 0) && (nIndex < m_tokens.count())) {
        return m_tokens.at(nIndex);
    }
    return m_eof;
}

bool XJSParser::isPunct(const QString &s) const
{
    const Token &t = peek();
    return (t.type == TT_PUNCT) && (t.text == s);
}

bool XJSParser::isKeyword(const QString &s) const
{
    const Token &t = peek();
    return (t.type == TT_KEYWORD) && (t.text == s);
}

bool XJSParser::eatPunct(const QString &s)
{
    if (isPunct(s)) {
        ++m_pos;
        return true;
    }
    return false;
}

bool XJSParser::eatKeyword(const QString &s)
{
    if (isKeyword(s)) {
        ++m_pos;
        return true;
    }
    return false;
}

void XJSParser::expectPunct(const QString &s)
{
    if (!eatPunct(s)) {
        m_error = true;
        // Tolerant recovery: do not advance blindly; the caller's loop conditions bound progress.
    }
}

qint32 XJSParser::binaryPrecedence(const QString &sOp)
{
    if (sOp == QLatin1String("||")) return 1;
    if (sOp == QLatin1String("&&")) return 2;
    if (sOp == QLatin1String("|")) return 3;
    if (sOp == QLatin1String("^")) return 4;
    if (sOp == QLatin1String("&")) return 5;
    if ((sOp == QLatin1String("==")) || (sOp == QLatin1String("!=")) || (sOp == QLatin1String("===")) || (sOp == QLatin1String("!=="))) return 6;
    if ((sOp == QLatin1String("<")) || (sOp == QLatin1String(">")) || (sOp == QLatin1String("<=")) || (sOp == QLatin1String(">=")) ||
        (sOp == QLatin1String("instanceof")) || (sOp == QLatin1String("in")))
        return 7;
    if ((sOp == QLatin1String("<<")) || (sOp == QLatin1String(">>")) || (sOp == QLatin1String(">>>"))) return 8;
    if ((sOp == QLatin1String("+")) || (sOp == QLatin1String("-"))) return 9;
    if ((sOp == QLatin1String("*")) || (sOp == QLatin1String("/")) || (sOp == QLatin1String("%"))) return 10;
    if (sOp == QLatin1String("**")) return 11;
    return -1;
}

Node *XJSParser::parse(const QString &sSource, bool *pbError)
{
    XJSLexer lexer;
    bool bLexError = false;
    m_tokens = lexer.tokenize(sSource, &bLexError);
    m_pos = 0;
    m_error = false;

    Node *pProgram = newNode(N_PROGRAM);

    qint32 nGuard = 0;
    const qint32 nMaxStatements = 2000000;
    while ((peek().type != TT_EOF) && (nGuard < nMaxStatements)) {
        const qint32 nBefore = m_pos;
        Node *pStmt = parseStatement();
        if (pStmt) {
            pProgram->list.append(pStmt);
        }
        if (m_pos == nBefore) {
            ++m_pos;  // guarantee progress on an unrecognised token
        }
        ++nGuard;
    }

    if (pbError) {
        *pbError = m_error || bLexError;
    }

    return pProgram;
}

Node *XJSParser::parseStatement()
{
    const Token &t = peek();

    if (t.type == TT_PUNCT) {
        if (t.text == QLatin1String("{")) {
            return parseBlock();
        }
        if (t.text == QLatin1String(";")) {
            ++m_pos;
            return newNode(N_EMPTY);
        }
    }

    if (t.type == TT_KEYWORD) {
        if ((t.text == QLatin1String("var")) || (t.text == QLatin1String("let")) || (t.text == QLatin1String("const"))) {
            Node *pDecl = parseVar();
            eatPunct(";");
            return pDecl;
        }
        if (t.text == QLatin1String("function")) {
            return parseFunction(false);
        }
        if (t.text == QLatin1String("if")) {
            return parseIf();
        }
        if (t.text == QLatin1String("for")) {
            return parseFor();
        }
        if (t.text == QLatin1String("while")) {
            return parseWhile();
        }
        if (t.text == QLatin1String("do")) {
            return parseDoWhile();
        }
        if (t.text == QLatin1String("try")) {
            return parseTry();
        }
        if (t.text == QLatin1String("switch")) {
            return parseSwitch();
        }
        if (t.text == QLatin1String("return")) {
            ++m_pos;
            Node *pRet = newNode(N_RETURN);
            if (!isPunct(";") && !isPunct("}") && (peek().type != TT_EOF)) {
                pRet->a = parseExpression();
            }
            eatPunct(";");
            return pRet;
        }
        if (t.text == QLatin1String("throw")) {
            ++m_pos;
            Node *pThrow = newNode(N_THROW);
            if (!isPunct(";") && !isPunct("}") && (peek().type != TT_EOF)) {
                pThrow->a = parseExpression();
            }
            eatPunct(";");
            return pThrow;
        }
        if (t.text == QLatin1String("break")) {
            ++m_pos;
            if ((peek().type == TT_IDENT)) {
                ++m_pos;  // ignore label
            }
            eatPunct(";");
            return newNode(N_BREAK);
        }
        if (t.text == QLatin1String("continue")) {
            ++m_pos;
            if ((peek().type == TT_IDENT)) {
                ++m_pos;
            }
            eatPunct(";");
            return newNode(N_CONTINUE);
        }
    }

    // Expression statement.
    Node *pStmt = newNode(N_EXPRSTMT);
    pStmt->a = parseExpression();
    eatPunct(";");
    return pStmt;
}

Node *XJSParser::parseBlock()
{
    expectPunct("{");
    Node *pBlock = newNode(N_BLOCK);

    qint32 nGuard = 0;
    while (!isPunct("}") && (peek().type != TT_EOF) && (nGuard < 1000000)) {
        const qint32 nBefore = m_pos;
        Node *pStmt = parseStatement();
        if (pStmt) {
            pBlock->list.append(pStmt);
        }
        if (m_pos == nBefore) {
            ++m_pos;
        }
        ++nGuard;
    }
    expectPunct("}");
    return pBlock;
}

Node *XJSParser::parseVar()
{
    ++m_pos;  // var / let / const
    Node *pDecl = newNode(N_VARDECL);

    qint32 nGuard = 0;
    do {
        const Token &t = peek();
        if (t.type != TT_IDENT) {
            break;
        }
        Node *pItem = newNode(N_VARITEM);
        pItem->sval = t.text;
        ++m_pos;
        if (eatPunct("=")) {
            pItem->a = parseAssignment();
        }
        pDecl->list.append(pItem);
        ++nGuard;
    } while (eatPunct(",") && (nGuard < 100000));

    return pDecl;
}

Node *XJSParser::parseFunction(bool bExpression)
{
    Q_UNUSED(bExpression)
    ++m_pos;  // 'function'
    Node *pFunc = newNode(N_FUNCTION);

    if (peek().type == TT_IDENT) {
        pFunc->sval = peek().text;
        ++m_pos;
    }

    expectPunct("(");
    qint32 nGuard = 0;
    while (!isPunct(")") && (peek().type != TT_EOF) && (nGuard < 100000)) {
        if (peek().type == TT_IDENT) {
            pFunc->params.append(peek().text);
            ++m_pos;
        } else {
            ++m_pos;  // skip unexpected token
        }
        if (!eatPunct(",")) {
            break;
        }
        ++nGuard;
    }
    expectPunct(")");

    pFunc->a = parseBlock();
    return pFunc;
}

Node *XJSParser::parseIf()
{
    ++m_pos;  // 'if'
    Node *pIf = newNode(N_IF);
    expectPunct("(");
    pIf->a = parseExpression();
    expectPunct(")");
    pIf->b = parseStatement();
    if (eatKeyword("else")) {
        pIf->c = parseStatement();
    }
    return pIf;
}

Node *XJSParser::parseFor()
{
    ++m_pos;  // 'for'
    expectPunct("(");

    Node *pInit = nullptr;
    bool bForIn = false;
    QString sForInVar;
    Node *pForInVarExpr = nullptr;

    if (isPunct(";")) {
        // no init
    } else if (isKeyword("var") || isKeyword("let") || isKeyword("const")) {
        ++m_pos;
        if (peek().type == TT_IDENT) {
            const QString sName = peek().text;
            ++m_pos;
            if (isKeyword("in")) {
                bForIn = true;
                sForInVar = sName;
            } else {
                Node *pDecl = newNode(N_VARDECL);
                Node *pItem = newNode(N_VARITEM);
                pItem->sval = sName;
                if (eatPunct("=")) {
                    pItem->a = parseAssignment();
                }
                pDecl->list.append(pItem);
                qint32 nGuard = 0;
                while (eatPunct(",") && (nGuard < 100000)) {
                    if (peek().type != TT_IDENT) {
                        break;
                    }
                    Node *pMore = newNode(N_VARITEM);
                    pMore->sval = peek().text;
                    ++m_pos;
                    if (eatPunct("=")) {
                        pMore->a = parseAssignment();
                    }
                    pDecl->list.append(pMore);
                    ++nGuard;
                }
                pInit = pDecl;
            }
        }
    } else {
        Node *pExpr = parseAssignment();
        if (isKeyword("in") && pExpr && (pExpr->type == N_IDENT)) {
            bForIn = true;
            pForInVarExpr = pExpr;
            sForInVar = pExpr->sval;
        } else {
            // allow comma sequence in a plain for-init
            while (eatPunct(",")) {
                Node *pSeq = newNode(N_SEQ);
                pSeq->a = pExpr;
                pSeq->b = parseAssignment();
                pExpr = pSeq;
            }
            Node *pStmt = newNode(N_EXPRSTMT);
            pStmt->a = pExpr;
            pInit = pStmt;
        }
    }

    if (bForIn) {
        Q_UNUSED(pForInVarExpr)
        eatKeyword("in");
        Node *pForIn = newNode(N_FORIN);
        pForIn->sval = sForInVar;
        pForIn->a = parseAssignment();
        expectPunct(")");
        pForIn->b = parseStatement();
        return pForIn;
    }

    Node *pFor = newNode(N_FOR);
    pFor->a = pInit;

    expectPunct(";");
    if (!isPunct(";")) {
        pFor->b = parseExpression();
    }
    expectPunct(";");
    if (!isPunct(")")) {
        pFor->c = parseExpression();
    }
    expectPunct(")");
    pFor->list.append(parseStatement());
    return pFor;
}

Node *XJSParser::parseWhile()
{
    ++m_pos;  // 'while'
    Node *pWhile = newNode(N_WHILE);
    expectPunct("(");
    pWhile->a = parseExpression();
    expectPunct(")");
    pWhile->b = parseStatement();
    return pWhile;
}

Node *XJSParser::parseDoWhile()
{
    ++m_pos;  // 'do'
    Node *pDo = newNode(N_DOWHILE);
    pDo->a = parseStatement();
    eatKeyword("while");
    expectPunct("(");
    pDo->b = parseExpression();
    expectPunct(")");
    eatPunct(";");
    return pDo;
}

Node *XJSParser::parseTry()
{
    ++m_pos;  // 'try'
    Node *pTry = newNode(N_TRY);
    pTry->a = parseBlock();

    if (eatKeyword("catch")) {
        if (eatPunct("(")) {
            if (peek().type == TT_IDENT) {
                pTry->sval = peek().text;
                ++m_pos;
            }
            expectPunct(")");
        }
        pTry->b = parseBlock();
    }

    if (eatKeyword("finally")) {
        pTry->c = parseBlock();
    }

    return pTry;
}

Node *XJSParser::parseSwitch()
{
    ++m_pos;  // 'switch'
    Node *pSwitch = newNode(N_SWITCH);
    expectPunct("(");
    pSwitch->a = parseExpression();
    expectPunct(")");
    expectPunct("{");

    qint32 nGuard = 0;
    while (!isPunct("}") && (peek().type != TT_EOF) && (nGuard < 100000)) {
        Node *pTest = nullptr;
        if (eatKeyword("case")) {
            pTest = parseExpression();
        } else if (eatKeyword("default")) {
            pTest = nullptr;
        } else {
            ++m_pos;  // recover
            ++nGuard;
            continue;
        }
        expectPunct(":");

        Node *pBody = newNode(N_BLOCK);
        qint32 nInner = 0;
        while (!isKeyword("case") && !isKeyword("default") && !isPunct("}") && (peek().type != TT_EOF) && (nInner < 1000000)) {
            const qint32 nBefore = m_pos;
            Node *pStmt = parseStatement();
            if (pStmt) {
                pBody->list.append(pStmt);
            }
            if (m_pos == nBefore) {
                ++m_pos;
            }
            ++nInner;
        }

        pSwitch->list.append(pTest);  // may be null for default
        pSwitch->list.append(pBody);
        ++nGuard;
    }
    expectPunct("}");
    return pSwitch;
}

Node *XJSParser::parseExpression()
{
    Node *pExpr = parseAssignment();
    while (eatPunct(",")) {
        Node *pSeq = newNode(N_SEQ);
        pSeq->a = pExpr;
        pSeq->b = parseAssignment();
        pExpr = pSeq;
    }
    return pExpr;
}

Node *XJSParser::parseAssignment()
{
    Node *pLeft = parseConditional();

    const Token &t = peek();
    if (t.type == TT_PUNCT) {
        if ((t.text == QLatin1String("=")) || (t.text == QLatin1String("+=")) || (t.text == QLatin1String("-=")) || (t.text == QLatin1String("*=")) ||
            (t.text == QLatin1String("/=")) || (t.text == QLatin1String("%=")) || (t.text == QLatin1String("&=")) || (t.text == QLatin1String("|=")) ||
            (t.text == QLatin1String("^=")) || (t.text == QLatin1String("<<=")) || (t.text == QLatin1String(">>=")) || (t.text == QLatin1String("**="))) {
            const QString sOp = t.text;
            ++m_pos;
            Node *pAssign = newNode(N_ASSIGN);
            pAssign->sval = sOp;
            pAssign->a = pLeft;
            pAssign->b = parseAssignment();
            return pAssign;
        }
    }

    return pLeft;
}

Node *XJSParser::parseConditional()
{
    Node *pTest = parseBinary(1);
    if (eatPunct("?")) {
        Node *pCond = newNode(N_COND);
        pCond->a = pTest;
        pCond->b = parseAssignment();
        expectPunct(":");
        pCond->c = parseAssignment();
        return pCond;
    }
    return pTest;
}

Node *XJSParser::parseBinary(qint32 nMinPrec)
{
    Node *pLeft = parseUnary();

    qint32 nGuard = 0;
    while (nGuard < 1000000) {
        const Token &t = peek();
        QString sOp;
        if (t.type == TT_PUNCT) {
            sOp = t.text;
        } else if ((t.type == TT_KEYWORD) && ((t.text == QLatin1String("instanceof")) || (t.text == QLatin1String("in")))) {
            sOp = t.text;
        } else {
            break;
        }

        const qint32 nPrec = binaryPrecedence(sOp);
        if ((nPrec < 0) || (nPrec < nMinPrec)) {
            break;
        }

        ++m_pos;
        const bool bRightAssoc = (sOp == QLatin1String("**"));
        Node *pRight = parseBinary(bRightAssoc ? nPrec : (nPrec + 1));

        Node *pNode = newNode(((sOp == QLatin1String("&&")) || (sOp == QLatin1String("||"))) ? N_LOGICAL : N_BINARY);
        pNode->sval = sOp;
        pNode->a = pLeft;
        pNode->b = pRight;
        pLeft = pNode;
        ++nGuard;
    }

    return pLeft;
}

Node *XJSParser::parseUnary()
{
    const Token &t = peek();

    bool bIsPrefixOp = false;
    if (t.type == TT_PUNCT) {
        bIsPrefixOp = (t.text == QLatin1String("!")) || (t.text == QLatin1String("~")) || (t.text == QLatin1String("+")) || (t.text == QLatin1String("-")) ||
                      (t.text == QLatin1String("++")) || (t.text == QLatin1String("--"));
    } else if (t.type == TT_KEYWORD) {
        bIsPrefixOp = (t.text == QLatin1String("typeof")) || (t.text == QLatin1String("void")) || (t.text == QLatin1String("delete"));
    }

    if (bIsPrefixOp) {
        const QString sOp = t.text;
        ++m_pos;
        Node *pUnary = newNode(N_UNARY);
        pUnary->sval = sOp;
        pUnary->ival = 1;  // prefix
        pUnary->a = parseUnary();
        return pUnary;
    }

    return parsePostfix();
}

Node *XJSParser::parsePostfix()
{
    Node *pExpr = parseCallMember();

    const Token &t = peek();
    if ((t.type == TT_PUNCT) && ((t.text == QLatin1String("++")) || (t.text == QLatin1String("--")))) {
        Node *pUnary = newNode(N_UNARY);
        pUnary->sval = t.text;
        pUnary->ival = 0;  // postfix
        pUnary->a = pExpr;
        ++m_pos;
        return pUnary;
    }

    return pExpr;
}

Node *XJSParser::parseArguments(Node *pCallee, bool bNew)
{
    Node *pNode = newNode(bNew ? N_NEW : N_CALL);
    pNode->a = pCallee;
    expectPunct("(");

    qint32 nGuard = 0;
    while (!isPunct(")") && (peek().type != TT_EOF) && (nGuard < 100000)) {
        pNode->list.append(parseAssignment());
        if (!eatPunct(",")) {
            break;
        }
        ++nGuard;
    }
    expectPunct(")");
    return pNode;
}

Node *XJSParser::parseCallMember()
{
    Node *pExpr = nullptr;

    if (isKeyword("new")) {
        ++m_pos;
        Node *pCallee = parsePrimary();
        // member chain (no call) for the constructor target
        qint32 nGuard = 0;
        while (nGuard < 100000) {
            if (eatPunct(".")) {
                Node *pMember = newNode(N_MEMBER);
                pMember->a = pCallee;
                pMember->sval = peek().text;
                ++m_pos;
                pCallee = pMember;
            } else if (eatPunct("[")) {
                Node *pIndex = newNode(N_INDEX);
                pIndex->a = pCallee;
                pIndex->b = parseExpression();
                expectPunct("]");
                pCallee = pIndex;
            } else {
                break;
            }
            ++nGuard;
        }
        if (isPunct("(")) {
            pExpr = parseArguments(pCallee, true);
        } else {
            Node *pNew = newNode(N_NEW);
            pNew->a = pCallee;
            pExpr = pNew;
        }
    } else {
        pExpr = parsePrimary();
    }

    qint32 nGuard = 0;
    while (nGuard < 1000000) {
        if (eatPunct(".")) {
            Node *pMember = newNode(N_MEMBER);
            pMember->a = pExpr;
            pMember->sval = peek().text;
            ++m_pos;
            pExpr = pMember;
        } else if (eatPunct("[")) {
            Node *pIndex = newNode(N_INDEX);
            pIndex->a = pExpr;
            pIndex->b = parseExpression();
            expectPunct("]");
            pExpr = pIndex;
        } else if (isPunct("(")) {
            pExpr = parseArguments(pExpr, false);
        } else {
            break;
        }
        ++nGuard;
    }

    return pExpr;
}

Node *XJSParser::parsePrimary()
{
    const Token &t = peek();

    if (t.type == TT_NUMBER) {
        Node *pNode = newNode(N_NUMBER);
        pNode->num = t.num;
        ++m_pos;
        return pNode;
    }

    if (t.type == TT_STRING) {
        Node *pNode = newNode(N_STRING);
        pNode->sval = t.text;
        ++m_pos;
        return pNode;
    }

    if (t.type == TT_REGEX) {
        Node *pNode = newNode(N_REGEX);
        pNode->sval = t.text;
        pNode->params.append(t.flags);
        ++m_pos;
        return pNode;
    }

    if (t.type == TT_IDENT) {
        Node *pNode = newNode(N_IDENT);
        pNode->sval = t.text;
        ++m_pos;
        return pNode;
    }

    if (t.type == TT_KEYWORD) {
        if (t.text == QLatin1String("true")) {
            ++m_pos;
            Node *pNode = newNode(N_BOOL);
            pNode->bval = true;
            return pNode;
        }
        if (t.text == QLatin1String("false")) {
            ++m_pos;
            Node *pNode = newNode(N_BOOL);
            pNode->bval = false;
            return pNode;
        }
        if (t.text == QLatin1String("null")) {
            ++m_pos;
            return newNode(N_NULL);
        }
        if (t.text == QLatin1String("undefined")) {
            ++m_pos;
            return newNode(N_UNDEFINED);
        }
        if (t.text == QLatin1String("this")) {
            ++m_pos;
            return newNode(N_THIS);
        }
        if (t.text == QLatin1String("function")) {
            return parseFunction(true);
        }
    }

    if (t.type == TT_PUNCT) {
        if (t.text == QLatin1String("(")) {
            ++m_pos;
            Node *pExpr = parseExpression();
            expectPunct(")");
            return pExpr;
        }
        if (t.text == QLatin1String("[")) {
            ++m_pos;
            Node *pArray = newNode(N_ARRAY);
            qint32 nGuard = 0;
            while (!isPunct("]") && (peek().type != TT_EOF) && (nGuard < 1000000)) {
                if (isPunct(",")) {
                    // elision
                    pArray->list.append(newNode(N_UNDEFINED));
                    ++m_pos;
                    ++nGuard;
                    continue;
                }
                pArray->list.append(parseAssignment());
                if (!eatPunct(",")) {
                    break;
                }
                ++nGuard;
            }
            expectPunct("]");
            return pArray;
        }
        if (t.text == QLatin1String("{")) {
            ++m_pos;
            Node *pObject = newNode(N_OBJECT);
            qint32 nGuard = 0;
            while (!isPunct("}") && (peek().type != TT_EOF) && (nGuard < 1000000)) {
                const Token &k = peek();
                QString sKey;
                if ((k.type == TT_IDENT) || (k.type == TT_KEYWORD)) {
                    sKey = k.text;
                    ++m_pos;
                } else if (k.type == TT_STRING) {
                    sKey = k.text;
                    ++m_pos;
                } else if (k.type == TT_NUMBER) {
                    sKey = k.text;
                    ++m_pos;
                } else {
                    ++m_pos;  // recover
                    ++nGuard;
                    continue;
                }

                Node *pProp = newNode(N_PROPERTY);
                pProp->sval = sKey;
                if (eatPunct(":")) {
                    pProp->a = parseAssignment();
                } else {
                    Node *pShort = newNode(N_IDENT);
                    pShort->sval = sKey;
                    pProp->a = pShort;
                }
                pObject->list.append(pProp);

                if (!eatPunct(",")) {
                    break;
                }
                ++nGuard;
            }
            expectPunct("}");
            return pObject;
        }
    }

    // Unknown token: consume and yield undefined so parsing continues.
    m_error = true;
    ++m_pos;
    return newNode(N_UNDEFINED);
}

}  // namespace XJS
