/* Copyright (c) 2022-2025 hors<horsicq@gmail.com>
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
#ifndef XPDF_H
#define XPDF_H

#include "xbinary.h"
#include "xpdf_def.h"  // TODO remove

class XPDF : public XBinary {
    Q_OBJECT

public:
    enum TYPE {
        TYPE_UNKNOWN = 0,
        TYPE_DOCUMENT
    };

    struct STARTHREF {
        qint64 nXrefOffset;
        qint64 nFooterOffset;
        qint64 nFooterSize;
        bool bIsXref;
        bool bIsObject;
    };

    struct OBJECT {
        quint64 nID;
        qint64 nOffset;
        qint64 nSize;
    };

    struct STREAM {
        qint64 nOffset;
        qint64 nSize;
    };

    struct XPART {
        quint64 nID;
        qint64 nOffset;
        qint64 nSize;
        QList<QString> listParts;
        QList<STREAM> listStreams;
    };

    XPDF(QIODevice *pDevice);

    virtual bool isValid(PDSTRUCT *pPdStruct = nullptr);
    virtual QString getVersion();
    virtual FT getFileType();
    virtual ENDIAN getEndian();
    virtual qint64 getFileFormatSize(PDSTRUCT *pPdStruct);
    virtual QString getFileFormatExt();
    virtual QString getFileFormatExtsString();
    virtual MODE getMode();
    virtual QString getMIMEString();

    virtual QList<MAPMODE> getMapModesList();
    virtual _MEMORY_MAP getMemoryMap(MAPMODE mapMode = MAPMODE_UNKNOWN, PDSTRUCT *pPdStruct = nullptr);

    QList<STARTHREF> findStartxrefs(qint64 nOffset, PDSTRUCT *pPdStruct);
    QList<OBJECT> getObjectsFromStartxref(STARTHREF *pStartxref, PDSTRUCT *pPdStruct);
    QList<OBJECT> findObjects(qint64 nOffset, qint64 nSize, bool bDeepScan, PDSTRUCT *pPdStruct);
    OS_STRING _readPDFString(qint64 nOffset, qint64 nSize);
    OS_STRING _readPDFStringPart_title(qint64 nOffset, qint64 nSize);
    OS_STRING _readPDFStringPart(qint64 nOffset);
    OS_STRING _readPDFStringPart_const(qint64 nOffset);
    OS_STRING _readPDFStringPart_str(qint64 nOffset);
    OS_STRING _readPDFStringPart_val(qint64 nOffset);
    OS_STRING _readPDFStringPart_hex(qint64 nOffset);
    qint32 skipPDFEnding(qint64 *pnOffset);
    qint32 skipPDFSpace(qint64 *pnOffset);
    qint32 skipPDFString(qint64 *pnOffset);
    XPART handleXpart(qint64 nOffset, qint32 nID, qint32 nPartLimit, PDSTRUCT *pPdStruct);
    static bool _isObject(const QString &sString);
    static bool _isString(const QString &sString);
    static bool _isHex(const QString &sString);
    static bool _isDateTime(const QString &sString);
    static bool _isEndObject(const QString &sString);
    static bool _isComment(const QString &sString);
    static bool _isXref(const QString &sString);
    static QString _getCommentString(const QString &sString);
    static QString _getString(const QString &sString);
    static QString _getHex(const QString &sString);
    static QDateTime _getDateTime(const QString &sString);
    static qint32 getObjectID(const QString &sString);

    QList<XPART> getParts(qint32 nPartLimit, PDSTRUCT *pPdStruct = nullptr);
    static QList<XVARIANT> getValuesByKey(QList<XPART> *pListObjects, const QString &sKey, PDSTRUCT *pPdStruct = nullptr);

    virtual qint32 getType();
    virtual QString typeIdToString(qint32 nType);

    QString getHeaderCommentAsHex();

    virtual QList<FPART> getFileParts(quint32 nFileParts, qint32 nLimit = -1, PDSTRUCT *pPdStruct = nullptr);
};

#endif  // XPDF_H
