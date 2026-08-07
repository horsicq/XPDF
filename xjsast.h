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
#ifndef XJSAST_H
#define XJSAST_H

#include <QList>
#include <QString>
#include <QStringList>

namespace XJS {

// Compact homogeneous AST node. A single struct (tagged union style) keeps the interpreter to one
// switch and avoids a large virtual-class hierarchy. Nodes are owned by the Parser's arena.
enum NodeType {
    N_PROGRAM,     // list = statements
    N_NUMBER,      // num
    N_STRING,      // sval
    N_BOOL,        // bval
    N_NULL,        //
    N_UNDEFINED,   //
    N_IDENT,       // sval = name
    N_THIS,        //
    N_ARRAY,       // list = elements
    N_OBJECT,      // list = N_PROPERTY nodes
    N_PROPERTY,    // sval = key, a = value
    N_REGEX,       // sval = pattern, flags (via 'sval2' packed in params[0])
    N_MEMBER,      // a = object, sval = property
    N_INDEX,       // a = object, b = index expr
    N_CALL,        // a = callee, list = args
    N_NEW,         // a = callee, list = args
    N_UNARY,       // sval = op, a = operand, ival: 1 = prefix, 0 = postfix (for ++/--)
    N_BINARY,      // sval = op, a = left, b = right
    N_LOGICAL,     // sval = op (&& ||), a = left, b = right
    N_ASSIGN,      // sval = op (= += ...), a = target, b = value
    N_COND,        // a ? b : c
    N_SEQ,         // a , b
    N_FUNCTION,    // sval = name (may be empty), params, a = body block
    N_VARDECL,     // list = N_VARITEM
    N_VARITEM,     // sval = name, a = init (may be null)
    N_BLOCK,       // list = statements
    N_IF,          // a = test, b = then, c = else (may be null)
    N_FOR,         // a = init, b = test, c = update, list[0] = body
    N_FORIN,       // sval = loop var name, a = object expr, b = body
    N_WHILE,       // a = test, b = body
    N_DOWHILE,     // a = body, b = test
    N_RETURN,      // a = argument (may be null)
    N_BREAK,       //
    N_CONTINUE,    //
    N_EMPTY,       //
    N_EXPRSTMT,    // a = expression
    N_TRY,         // a = try block, sval = catch param, b = catch block, c = finally block
    N_THROW,       // a = argument
    N_SWITCH       // a = discriminant, list = [case-test-or-null, case-body-block] flattened pairs
};

struct Node {
    NodeType type;
    double num;
    QString sval;
    bool bval;
    int ival;
    Node *a;
    Node *b;
    Node *c;
    QList<Node *> list;
    QStringList params;

    Node()
        : type(N_UNDEFINED), num(0.0), bval(false), ival(0), a(nullptr), b(nullptr), c(nullptr)
    {
    }
};

}  // namespace XJS

#endif  // XJSAST_H
