/* Copyright (c) 2022-2026 hors<horsicq@gmail.com>
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
#include "xpdf.h"
#include "xdecompress.h"
#include <QBuffer>
#include <QRegularExpression>

namespace {
bool isPdfLineEnding(quint8 nChar)
{
    return (nChar == 10) || (nChar == 13);
}

bool isPdfStringTerminator(quint8 nChar)
{
    return (nChar == 0) || isPdfLineEnding(nChar);
}

bool isPdfTitleTerminator(quint8 nChar)
{
    return isPdfStringTerminator(nChar) || (nChar == '<');
}

bool isPdfStructuralDelimiter(quint8 nChar)
{
    return (nChar == '[') || (nChar == ']') || (nChar == '<') || (nChar == '>');
}

bool isPdfNameTerminator(quint8 nChar)
{
    return isPdfStringTerminator(nChar) || isPdfStructuralDelimiter(nChar) || (nChar == ' ') || (nChar == '(');
}

bool isPdfValueTerminator(quint8 nChar)
{
    return isPdfStringTerminator(nChar) || isPdfStructuralDelimiter(nChar) || (nChar == '/');
}

void skipBufferPdfSpace(const char *pData, qint32 nDataSize, qint32 *pnPos)
{
    while (*pnPos < nDataSize) {
        if (static_cast<quint8>(pData[*pnPos]) == ' ') {
            ++(*pnPos);
        } else {
            break;
        }
    }
}

void skipBufferPdfEnding(const char *pData, qint32 nDataSize, qint32 *pnPos)
{
    while (*pnPos < nDataSize) {
        const quint8 nChar = static_cast<quint8>(pData[*pnPos]);
        if (nChar == 10) {
            ++(*pnPos);
        } else if (nChar == 13) {
            ++(*pnPos);
            if ((*pnPos < nDataSize) && (static_cast<quint8>(pData[*pnPos]) == 10)) {
                ++(*pnPos);
            }
        } else {
            break;
        }
    }
}

XBinary::HANDLE_METHOD pdfFilterToHandleMethod(const QString &sFilter)
{
    XBinary::HANDLE_METHOD result = XBinary::HANDLE_METHOD_STORE;

    if (sFilter == QLatin1String("/FlateDecode")) {
        result = XBinary::HANDLE_METHOD_ZLIB;
    } else if (sFilter == QLatin1String("/LZWDecode")) {
        result = XBinary::HANDLE_METHOD_LZW_PDF;
    } else if (sFilter == QLatin1String("/ASCII85Decode")) {
        result = XBinary::HANDLE_METHOD_ASCII85;
    }

    return result;
}

bool isKnownStorePdfFilter(const QString &sFilter)
{
    return sFilter.isEmpty() || (sFilter == QLatin1String("/DCTDecode")) || (sFilter == QLatin1String("/CCITTFaxDecode"));
}

XBinary::FPART makeFilePart(XBinary::FILEPART filePart, qint64 nFileOffset, qint64 nFileSize, const QString &sName)
{
    XBinary::FPART record = {};

    record.filePart = filePart;
    record.nFileOffset = nFileOffset;
    record.nFileSize = nFileSize;
    record.nVirtualAddress = -1;
    record.sName = sName;

    return record;
}

bool decompressPdfBuffer(const QByteArray &baData, XBinary::HANDLE_METHOD handleMethod, qint64 nProcessedLimit, QByteArray *pbaResult, XBinary::PDSTRUCT *pPdStruct)
{
    if (pbaResult) {
        pbaResult->clear();
    }

    if (!pbaResult || (handleMethod == XBinary::HANDLE_METHOD_STORE) || baData.isEmpty()) {
        return false;
    }

    QBuffer sourceBuffer;
    sourceBuffer.setData(baData);

    QBuffer destBuffer;

    if (!sourceBuffer.open(QIODevice::ReadOnly) || !destBuffer.open(QIODevice::WriteOnly)) {
        sourceBuffer.close();
        destBuffer.close();
        return false;
    }

    XBinary::DATAPROCESS_STATE state = {};
    state.pDeviceInput = &sourceBuffer;
    state.pDeviceOutput = &destBuffer;
    state.nInputOffset = 0;
    state.nInputLimit = baData.size();
    state.nProcessedOffset = 0;
    state.nProcessedLimit = nProcessedLimit;
    state.bReadError = false;
    state.bWriteError = false;
    state.mapProperties.insert(XBinary::FPART_PROP_HANDLEMETHOD, static_cast<quint32>(handleMethod));

    XDecompress xDecompress;
    bool bResult = xDecompress.decompress(&state, pPdStruct);
    *pbaResult = destBuffer.data();

    sourceBuffer.close();
    destBuffer.close();

    return bResult;
}

quint32 readUInt32BE(const QByteArray &baData, qint32 nOffset)
{
    return (static_cast<quint32>(static_cast<quint8>(baData.at(nOffset))) << 24) | (static_cast<quint32>(static_cast<quint8>(baData.at(nOffset + 1))) << 16) |
           (static_cast<quint32>(static_cast<quint8>(baData.at(nOffset + 2))) << 8) | static_cast<quint32>(static_cast<quint8>(baData.at(nOffset + 3)));
}
}  // namespace

XPDF::XPDF(QIODevice *pDevice) : XBinary(pDevice)
{
}

bool XPDF::isValid(PDSTRUCT *pPdStruct)
{
    Q_UNUSED(pPdStruct)

    return (getSize() > 4) && (read_uint32(0) == 0x46445025);
}

bool XPDF::isValid(QIODevice *pDevice, PDSTRUCT *pPdStruct)
{
    XPDF pdf(pDevice);
    return pdf.isValid(pPdStruct);
}

QString XPDF::getVersion()
{
    return _readPDFString(5, 3, nullptr).sString;
}

XBinary::FT XPDF::getFileType()
{
    return FT_PDF;
}

XBinary::ENDIAN XPDF::getEndian()
{
    return ENDIAN_UNKNOWN;
}

qint64 XPDF::getFileFormatSize(PDSTRUCT *pPdStruct)
{
    return _calculateRawSize(pPdStruct);
}

QString XPDF::getFileFormatExt()
{
    return QStringLiteral("pdf");
}

QString XPDF::getFileFormatExtsString()
{
    return QStringLiteral("PDF (*.pdf)");
}

QString XPDF::getMIMEString()
{
    return QStringLiteral("application/pdf");
}

XBinary::MODE XPDF::getMode()
{
    return MODE_UNKNOWN;
}

QList<XPDF::OBJECT> XPDF::findObjects(qint64 nOffset, qint64 nSize, bool bDeepScan, PDSTRUCT *pPdStruct)
{
    qint64 nFileSize = getSize();
    if (nSize == -1) {
        nSize = nFileSize - nOffset;
    }

    QList<XPDF::OBJECT> listResult;

    qint64 nCurrentOffset = nOffset;
    qint64 nEndBound = nOffset + nSize;

    while (XBinary::isPdStructNotCanceled(pPdStruct) && (nCurrentOffset < nEndBound)) {
        OS_STRING osString = _readPDFString(nCurrentOffset, 64, pPdStruct);

        if (_isObject(osString.sString)) {
            quint64 nID = getObjectID(osString.sString);
            qint64 nSearchStart = nCurrentOffset + osString.nSize;
            qint64 nSearchLen = qMax<qint64>(0, nEndBound - nSearchStart);
            qint64 nEndObjOffset = (nSearchLen > 0) ? find_ansiString(nSearchStart, nSearchLen, "endobj", pPdStruct) : -1;

            if (nEndObjOffset != -1) {
                OS_STRING osEndObj = _readPDFString(nEndObjOffset, 32, pPdStruct);

                if (_isEndObject(osEndObj.sString)) {
                    OBJECT objectRecord = {};
                    objectRecord.nOffset = nCurrentOffset;
                    objectRecord.nID = nID;
                    objectRecord.nSize = (nEndObjOffset + osEndObj.nSize) - nCurrentOffset;

                    listResult.append(objectRecord);

                    nCurrentOffset = nEndObjOffset + osEndObj.nSize;
                } else {
                    break;
                }
            } else {
                break;
            }
        } else if (_isComment(osString.sString)) {
            nCurrentOffset += osString.nSize;
        } else {
            bool bContinue = false;
            if (bDeepScan) {
                qint64 nRemain = nEndBound - nCurrentOffset;
                nCurrentOffset = find_ansiString(nCurrentOffset, nRemain, " obj", pPdStruct);

                if (nCurrentOffset != -1) {
                    while ((nCurrentOffset > 0) && XBinary::isPdStructNotCanceled(pPdStruct)) {
                        quint8 nPrevChar = read_uint8(nCurrentOffset - 1);

                        if (!(((nPrevChar >= '0') && (nPrevChar <= '9')) || (nPrevChar == ' '))) {
                            break;
                        }

                        --nCurrentOffset;
                    }

                    bContinue = true;
                }
            }

            if (!bContinue) {
                break;
            }
        }
    }

    return listResult;
}

qint32 XPDF::skipPDFString(qint64 *pnOffset, PDSTRUCT *pPdStruct)
{
    qint32 nSize = _readPDFString(*pnOffset, 20, pPdStruct).nSize;
    *pnOffset += nSize;
    return nSize;
}

qint32 XPDF::skipPDFEnding(qint64 *pnOffset, PDSTRUCT *pPdStruct)
{
    qint64 nStartOffset = *pnOffset;
    qint64 nFileSize = getSize();
    qint64 nCurrentOffset = *pnOffset;

    while ((nCurrentOffset < nFileSize) && XBinary::isPdStructNotCanceled(pPdStruct)) {
        quint8 nChar = read_uint8(nCurrentOffset);
        if (nChar == 10) {
            ++nCurrentOffset;
        } else if (nChar == 13) {
            ++nCurrentOffset;
            if ((nCurrentOffset < nFileSize) && (read_uint8(nCurrentOffset) == 10)) {
                ++nCurrentOffset;
            }
        } else {
            break;
        }
    }

    *pnOffset = nCurrentOffset;
    return static_cast<qint32>(nCurrentOffset - nStartOffset);
}

qint32 XPDF::skipPDFSpace(qint64 *pnOffset, PDSTRUCT *pPdStruct)
{
    qint64 nStartOffset = *pnOffset;
    qint64 nFileSize = getSize();
    qint64 nCurrentOffset = *pnOffset;

    while ((nCurrentOffset < nFileSize) && XBinary::isPdStructNotCanceled(pPdStruct)) {
        if (read_uint8(nCurrentOffset) == ' ') {
            ++nCurrentOffset;
        } else {
            break;
        }
    }

    *pnOffset = nCurrentOffset;
    return static_cast<qint32>(nCurrentOffset - nStartOffset);
}

QList<XPDF::OBJECT> XPDF::getObjectsFromStartxref(const STARTHREF *pStartxref, PDSTRUCT *pPdStruct)
{
    QList<XPDF::OBJECT> listResult;

    qint64 nTotalSize = getSize();

    qint64 nCurrentOffset = pStartxref->nXrefOffset;

    OS_STRING osStringHref = _readPDFString(nCurrentOffset, 20, pPdStruct);

    if (_isXref(osStringHref.sString)) {
        nCurrentOffset += osStringHref.nSize;

        QMap<qint64, quint64> mapObjects;

        while (XBinary::isPdStructNotCanceled(pPdStruct)) {
            OS_STRING osSection = _readPDFString(nCurrentOffset, 20, pPdStruct);

            if (!osSection.sString.isEmpty()) {
                quint64 nID = osSection.sString.section(" ", 0, 0).toULongLong();
                quint64 nNumberOfObjects = osSection.sString.section(" ", 1, 1).toULongLong();

                nCurrentOffset += osSection.nSize;

                if (nNumberOfObjects) {
                    qint32 _nFreeIndex = XBinary::getFreeIndex(pPdStruct);
                    XBinary::setPdStructInit(pPdStruct, _nFreeIndex, static_cast<qint32>(nNumberOfObjects));

                    for (quint64 i = 0; (i < nNumberOfObjects) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
                        OS_STRING osObject = _readPDFString(nCurrentOffset, 20, pPdStruct);

                        if (osObject.sString.section(" ", 2, 2) == "n") {
                            qint64 nObjectOffset = osObject.sString.section(" ", 0, 0).toULongLong();

                            if ((nObjectOffset > 0) && (nObjectOffset < nTotalSize)) {
                                mapObjects.insert(nObjectOffset, nID + i);
                            }
                        }

                        nCurrentOffset += osObject.nSize;
                        XBinary::setPdStructCurrentIncrement(pPdStruct, _nFreeIndex);
                    }

                    XBinary::setPdStructFinished(pPdStruct, _nFreeIndex);
                } else {
                    break;
                }
            } else {
                break;
            }
        }

        QMapIterator<qint64, quint64> iterator(mapObjects);
        while (iterator.hasNext() && XBinary::isPdStructNotCanceled(pPdStruct)) {
            iterator.next();

            OBJECT object;
            object.nOffset = iterator.key();
            object.nID = iterator.value();
            object.nSize = 0;

            listResult.append(object);
        }

        qint32 nNumberOfObjects = listResult.count();
        for (qint32 i = 0; (i < nNumberOfObjects - 1) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
            listResult[i].nSize = listResult[i + 1].nOffset - listResult[i].nOffset;
        }

        if (!listResult.isEmpty()) {
            listResult.last().nSize = pStartxref->nXrefOffset - listResult.last().nOffset;
        }
    }

    return listResult;
}

XBinary::OS_STRING XPDF::_readPDFString(qint64 nOffset, qint64 nSize, PDSTRUCT *pPdStruct)
{
    XBinary::OS_STRING result = {};

    result.nOffset = nOffset;

    qint64 nFileSize = getSize();
    if (nOffset < 0 || nOffset >= nFileSize) {
        return result;
    }

    if (nSize == -1) {
        nSize = nFileSize - nOffset;
    }

    if (nOffset + nSize > nFileSize) {
        nSize = nFileSize - nOffset;
    }

    if (nSize <= 0) {
        return result;
    }

    QByteArray baData = read_array(nOffset, nSize);
    const char *pData = baData.constData();
    qint32 nDataSize = baData.size();

    qint32 nPos = 0;
    for (; (nPos < nDataSize) && XBinary::isPdStructNotCanceled(pPdStruct); ++nPos) {
        quint8 nChar = static_cast<quint8>(pData[nPos]);
        if (isPdfStringTerminator(nChar)) {
            break;
        }
    }

    result.sString = QString::fromLatin1(pData, nPos);
    result.nSize = nPos;

    qint64 nNewOffset = nOffset + nPos;
    result.nSize += skipPDFEnding(&nNewOffset, pPdStruct);

    return result;
}

XBinary::OS_STRING XPDF::_readPDFStringPart_title(qint64 nOffset, qint64 nSize, PDSTRUCT *pPdStruct)
{
    XBinary::OS_STRING result = {};

    result.nOffset = nOffset;

    qint64 nFileSize = getSize();
    if (nOffset < 0 || nOffset >= nFileSize) {
        return result;
    }

    if (nSize == -1) {
        nSize = nFileSize - nOffset;
    }

    if (nOffset + nSize > nFileSize) {
        nSize = nFileSize - nOffset;
    }

    if (nSize <= 0) {
        return result;
    }

    QByteArray baData = read_array(nOffset, nSize);
    const char *pData = baData.constData();
    qint32 nDataSize = baData.size();

    qint32 nPos = 0;
    for (; (nPos < nDataSize) && XBinary::isPdStructNotCanceled(pPdStruct); ++nPos) {
        quint8 nChar = static_cast<quint8>(pData[nPos]);
        if (isPdfTitleTerminator(nChar)) {
            break;
        }
    }

    result.sString = QString::fromLatin1(pData, nPos);
    result.nSize = nPos;

    qint64 nNewOffset = nOffset + nPos;
    result.nSize += skipPDFEnding(&nNewOffset, pPdStruct);

    return result;
}

XBinary::OS_STRING XPDF::_readPDFStringPart(qint64 nOffset, PDSTRUCT *pPdStruct)
{
    XBinary::OS_STRING result = {};

    result.nOffset = nOffset;

    const qint64 nFileSize = getSize();
    if (nOffset < 0 || nOffset >= nFileSize) {
        return result;
    }

    qint64 nRemaining = nFileSize - nOffset;
    if (nRemaining <= 0) {
        return result;
    }

    qint64 nChunkSize = qMin<qint64>(512, nRemaining);
    QByteArray baData = read_array(nOffset, nChunkSize);
    const char *pData = baData.constData();
    qint32 nDataSize = baData.size();

    if (nDataSize <= 0) {
        return result;
    }

    const quint8 nChar = static_cast<quint8>(pData[0]);
    qint32 nTokenEnd = 0;
    bool bNeedFallbackWhitespace = false;

    if (nChar == '/') {
        bool bIsFirst = true;
        qint32 nPos = 0;
        for (; (nPos < nDataSize) && XBinary::isPdStructNotCanceled(pPdStruct); ++nPos) {
            const quint8 c = static_cast<quint8>(pData[nPos]);
            if (isPdfNameTerminator(c)) {
                break;
            }
            if (!bIsFirst && (c == '/')) {
                break;
            }
            bIsFirst = false;
        }
        result.sString = QString::fromLatin1(pData, nPos);
        nTokenEnd = nPos;
    } else if (nChar == '(') {
        result = _readPDFStringPart_str(nOffset, pPdStruct);
        bNeedFallbackWhitespace = true;
    } else if (nChar == '<') {
        if (nDataSize > 1 && static_cast<quint8>(pData[1]) == '<') {
            result.sString = QStringLiteral("<<");
            nTokenEnd = 2;
        } else {
            result = _readPDFStringPart_hex(nOffset, pPdStruct);
            bNeedFallbackWhitespace = true;
        }
    } else if (nChar == '>') {
        if (nDataSize > 1 && static_cast<quint8>(pData[1]) == '>') {
            result.sString = QStringLiteral(">>");
            nTokenEnd = 2;
        }
    } else if (nChar == '[') {
        result.sString = QStringLiteral("[");
        nTokenEnd = 1;
    } else if (nChar == ']') {
        result.sString = QStringLiteral("]");
        nTokenEnd = 1;
    } else {
        bool bSpace = false;
        qint32 nPos = 0;
        for (; (nPos < nDataSize) && XBinary::isPdStructNotCanceled(pPdStruct); ++nPos) {
            const quint8 c = static_cast<quint8>(pData[nPos]);
            if (isPdfValueTerminator(c)) {
                break;
            }
            if (c == ' ') {
                ++nPos;
                bSpace = true;
                break;
            }
        }
        result.sString = QString::fromLatin1(pData, bSpace ? (nPos - 1) : nPos);
        nTokenEnd = nPos;

        if (bSpace) {
            if ((nPos + 2) < nDataSize) {
                if ((pData[nPos] == '0') && (pData[nPos + 1] == ' ') && (pData[nPos + 2] == 'R')) {
                    result.sString = QString::fromLatin1(pData, nPos + 3);
                    nTokenEnd = nPos + 3;
                }
            } else {
                qint64 nSuffixRemaining = nFileSize - (nOffset + nPos);
                if (nSuffixRemaining >= 3) {
                    QByteArray baSuffix = read_array(nOffset + nPos, 3);
                    if (baSuffix.size() >= 3 && baSuffix.at(0) == '0' && baSuffix.at(1) == ' ' && baSuffix.at(2) == 'R') {
                        result.sString.append(QStringLiteral(" 0 R"));
                        nTokenEnd = nPos + 3;
                    }
                }
            }
        }
    }

    if (bNeedFallbackWhitespace) {
        qint64 nNewOffset = nOffset + result.nSize;
        result.nSize += skipPDFSpace(&nNewOffset, pPdStruct);
        result.nSize += skipPDFEnding(&nNewOffset, pPdStruct);
        return result;
    }

    qint32 nPos = nTokenEnd;
    skipBufferPdfSpace(pData, nDataSize, &nPos);
    skipBufferPdfEnding(pData, nDataSize, &nPos);
    result.nSize = nPos;

    return result;
}

XBinary::OS_STRING XPDF::_readPDFStringPart_const(qint64 nOffset, PDSTRUCT *pPdStruct)
{
    XBinary::OS_STRING result = {};

    result.nOffset = nOffset;

    const qint64 nFileSize = getSize();
    if (nOffset < 0 || nOffset >= nFileSize) {
        return result;
    }

    qint64 nChunkSize = qMin<qint64>(256, nFileSize - nOffset);
    QByteArray baData = read_array(nOffset, nChunkSize);
    const char *pData = baData.constData();
    qint32 nDataSize = baData.size();

    qint32 nPos = 0;
    bool bIsFirst = true;
    for (; (nPos < nDataSize) && XBinary::isPdStructNotCanceled(pPdStruct); ++nPos) {
        const quint8 nChar = static_cast<quint8>(pData[nPos]);

        if (isPdfNameTerminator(nChar)) {
            break;
        }

        if (!bIsFirst && (nChar == '/')) {
            break;
        }

        bIsFirst = false;
    }

    result.sString = QString::fromLatin1(pData, nPos);
    result.nSize = nPos;

    return result;
}

XBinary::OS_STRING XPDF::_readPDFStringPart_str(qint64 nOffset, PDSTRUCT *pPdStruct)
{
    XBinary::OS_STRING result = {};

    result.nOffset = nOffset;

    const qint64 nFileSize = getSize();
    if (nOffset < 0 || nOffset >= nFileSize) {
        return result;
    }

    qint64 nRemaining = nFileSize - nOffset;
    if (nRemaining <= 0) {
        return result;
    }

    const qint64 CHUNK_SIZE = 4096;
    qint64 nChunkSize = qMin(CHUNK_SIZE, nRemaining);
    QByteArray baData = read_array(nOffset, nChunkSize);
    const quint8 *pBuf = reinterpret_cast<const quint8 *>(baData.constData());
    qint32 nBufSize = baData.size();
    qint64 nBufFileOffset = nOffset;
    qint32 nBufPos = 0;

    bool bStart = false;
    bool bEnd = false;
    bool bUnicode = false;
    bool bBSlash = false;

    while (XBinary::isPdStructNotCanceled(pPdStruct)) {
        if (nBufPos >= nBufSize) {
            qint64 nNewOffset = nBufFileOffset + nBufSize;
            qint64 nNewRemaining = nFileSize - nNewOffset;
            if (nNewRemaining <= 0) break;
            nChunkSize = qMin(CHUNK_SIZE, nNewRemaining);
            baData = read_array(nNewOffset, nChunkSize);
            pBuf = reinterpret_cast<const quint8 *>(baData.constData());
            nBufSize = baData.size();
            nBufFileOffset = nNewOffset;
            nBufPos = 0;
            if (nBufSize <= 0) break;
        }

        if (!bUnicode) {
            const quint8 nChar = pBuf[nBufPos];

            if (isPdfStringTerminator(nChar)) {
                break;
            }

            if (!bStart) {
                if (nChar == '(') {
                    bStart = true;
                    qint64 nBomFilePos = nBufFileOffset + nBufPos + 1;
                    if ((nBomFilePos + 1) < nFileSize) {
                        quint8 nByte1, nByte2;
                        if (nBufPos + 2 < nBufSize) {
                            nByte1 = pBuf[nBufPos + 1];
                            nByte2 = pBuf[nBufPos + 2];
                        } else {
                            QByteArray baBOM = read_array(nBomFilePos, 2);
                            nByte1 = (baBOM.size() >= 1) ? static_cast<quint8>(baBOM.at(0)) : 0;
                            nByte2 = (baBOM.size() >= 2) ? static_cast<quint8>(baBOM.at(1)) : 0;
                        }
                        if ((nByte1 == 0xFE) && (nByte2 == 0xFF)) {
                            bUnicode = true;
                            result.nSize += 2;
                            nBufPos += 2;
                        }
                    }
                    result.sString.append('(');
                } else {
                    if (bBSlash) {
                        bBSlash = false;
                    }
                    result.sString.append(QLatin1Char(static_cast<char>(nChar)));
                }
            } else if ((nChar == ')') && (!bBSlash)) {
                result.sString.append(')');
                bEnd = true;
            } else if (nChar == '\\') {
                bBSlash = true;
            } else {
                if (bBSlash) {
                    bBSlash = false;
                }
                result.sString.append(QLatin1Char(static_cast<char>(nChar)));
            }
            ++result.nSize;
            ++nBufPos;
        } else {
            if (nBufPos + 1 >= nBufSize) {
                qint64 nNewOffset = nBufFileOffset + nBufPos;
                qint64 nNewRemaining = nFileSize - nNewOffset;
                if (nNewRemaining < 2) break;
                nChunkSize = qMin(CHUNK_SIZE, nNewRemaining);
                baData = read_array(nNewOffset, nChunkSize);
                pBuf = reinterpret_cast<const quint8 *>(baData.constData());
                nBufSize = baData.size();
                nBufFileOffset = nNewOffset;
                nBufPos = 0;
                if (nBufSize < 2) break;
            }

            const quint16 nWord = (static_cast<quint16>(pBuf[nBufPos]) << 8) | pBuf[nBufPos + 1];

            if (((nWord >> 8) == ')') && (!bBSlash)) {
                result.sString.append(')');
                ++result.nSize;
                bEnd = true;
            } else if (nWord == '\\') {
                bBSlash = true;
                nBufPos += 2;
                result.nSize += 2;
                continue;
            } else if (bBSlash && (nWord == 0x6e29)) {  // 'n' ')'
                bBSlash = false;
                result.sString.append(')');
                ++result.nSize;
                bEnd = true;
            } else {
                if (bBSlash) {
                    bBSlash = false;
                }
                result.sString.append(QChar(nWord));
                nBufPos += 2;
                result.nSize += 2;
                continue;
            }

            ++nBufPos;
        }

        if (bStart && bEnd) {
            break;
        }
    }

    return result;
}

XBinary::OS_STRING XPDF::_readPDFStringPart_val(qint64 nOffset, PDSTRUCT *pPdStruct)
{
    XBinary::OS_STRING result = {};

    result.nOffset = nOffset;

    const qint64 nFileSize = getSize();
    if (nOffset < 0 || nOffset >= nFileSize) {
        return result;
    }

    qint64 nRemaining = nFileSize - nOffset;
    if (nRemaining <= 0) {
        return result;
    }

    qint64 nChunkSize = qMin<qint64>(256, nRemaining);
    QByteArray baData = read_array(nOffset, nChunkSize);
    const char *pData = baData.constData();
    qint32 nDataSize = baData.size();

    bool bSpace = false;
    qint32 nPos = 0;

    for (; (nPos < nDataSize) && XBinary::isPdStructNotCanceled(pPdStruct); ++nPos) {
        const quint8 nChar = static_cast<quint8>(pData[nPos]);

        if (isPdfValueTerminator(nChar)) {
            break;
        }

        if (nChar == ' ') {
            ++nPos;
            bSpace = true;
            break;
        }
    }

    result.sString = QString::fromLatin1(pData, bSpace ? (nPos - 1) : nPos);
    result.nSize = nPos;

    if (bSpace) {
        if ((nPos + 2) < nDataSize) {
            if ((pData[nPos] == '0') && (pData[nPos + 1] == ' ') && (pData[nPos + 2] == 'R')) {
                result.sString.append(QLatin1String(" 0 R"));
                result.nSize += 3;
            }
        } else {
            qint64 nSuffixRemaining = nFileSize - (nOffset + result.nSize);
            if (nSuffixRemaining >= 3) {
                const QString sSuffix = read_ansiString(nOffset + result.nSize, 3);
                if (sSuffix == QLatin1String("0 R")) {
                    result.sString.append(QLatin1String(" 0 R"));
                    result.nSize += 3;
                }
            }
        }
    }

    return result;
}

XBinary::OS_STRING XPDF::_readPDFStringPart_hex(qint64 nOffset, PDSTRUCT *pPdStruct)
{
    XBinary::OS_STRING result = {};

    result.nOffset = nOffset;

    const qint64 nFileSize = getSize();
    if (nOffset < 0 || nOffset >= nFileSize) {
        return result;
    }

    qint64 nRemaining = nFileSize - nOffset;
    if (nRemaining <= 0) {
        return result;
    }

    const quint8 nFirst = read_uint8(nOffset);
    if (nFirst != '<') {
        return result;
    }

    qint64 nChunkSize = qMin<qint64>(65536, nRemaining);
    QByteArray baData = read_array(nOffset, nChunkSize);
    const char *pData = baData.constData();
    qint32 nDataSize = baData.size();

    qint32 nPos = 0;
    for (; (nPos < nDataSize) && XBinary::isPdStructNotCanceled(pPdStruct); ++nPos) {
        if (static_cast<quint8>(pData[nPos]) == '>') {
            ++nPos;
            break;
        }
    }

    result.sString = QString::fromLatin1(pData, nPos);
    result.nSize = nPos;

    if ((nPos == nDataSize) && (static_cast<quint8>(pData[nDataSize - 1]) != '>')) {
        qint64 nNextOffset = nOffset + nChunkSize;
        while ((nNextOffset < nFileSize) && XBinary::isPdStructNotCanceled(pPdStruct)) {
            qint64 nNextChunk = qMin<qint64>(65536, nFileSize - nNextOffset);
            QByteArray baNext = read_array(nNextOffset, nNextChunk);
            const char *pNext = baNext.constData();
            qint32 nNextSize = baNext.size();

            qint32 nNextPos = 0;
            for (; nNextPos < nNextSize; ++nNextPos) {
                if (static_cast<quint8>(pNext[nNextPos]) == '>') {
                    ++nNextPos;
                    break;
                }
            }

            result.sString.append(QString::fromLatin1(pNext, nNextPos));
            result.nSize += nNextPos;

            if ((nNextPos < nNextSize) || (static_cast<quint8>(pNext[nNextPos - 1]) == '>')) {
                break;
            }

            nNextOffset += nNextChunk;
        }
    }

    return result;
}

QList<XBinary::MAPMODE> XPDF::getMapModesList()
{
    QList<MAPMODE> listResult;

    listResult.append(MAPMODE_DATA);
    listResult.append(MAPMODE_OBJECTS);
    listResult.append(MAPMODE_STREAMS);

    return listResult;
}

XBinary::_MEMORY_MAP XPDF::getMemoryMap(MAPMODE mapMode, PDSTRUCT *pPdStruct)
{
    XBinary::_MEMORY_MAP result = {};

    if (mapMode == MAPMODE_UNKNOWN) {
        mapMode = MAPMODE_DATA;
    }

    if (mapMode == MAPMODE_OBJECTS) {
        result = _getMemoryMap(FILEPART_SIGNATURE | FILEPART_OBJECT | FILEPART_FOOTER | FILEPART_TABLE | FILEPART_OVERLAY, pPdStruct);
    } else if (mapMode == MAPMODE_STREAMS) {
        result = _getMemoryMap(FILEPART_STREAM, pPdStruct);
    } else if (mapMode == MAPMODE_DATA) {
        result = _getMemoryMap(FILEPART_DATA | FILEPART_OVERLAY, pPdStruct);
    }

    return result;
}

QList<XPDF::STARTHREF> XPDF::findStartxrefs(qint64 nOffset, PDSTRUCT *pPdStruct)
{
    QList<XPDF::STARTHREF> listResult;

    const qint64 nFileSize = getSize();

    while (XBinary::isPdStructNotCanceled(pPdStruct)) {
        qint64 nStartXref = find_signature(nOffset, -1, "'startxref'", nullptr, pPdStruct);  // \n \r
        if (nStartXref == -1) {
            break;
        }

        qint64 nCurrent = nStartXref;

        OS_STRING osStartXref = _readPDFString(nCurrent, 20, pPdStruct);
        nCurrent += osStartXref.nSize;

        OS_STRING osOffset = _readPDFString(nCurrent, 20, pPdStruct);
        qint64 nTargetOffset = osOffset.sString.toLongLong();

        OS_STRING osHref = _readPDFString(nTargetOffset, 20, pPdStruct);
        bool bIsXref = _isXref(osHref.sString);
        bool bIsObject = _isObject(osHref.sString);

        if ((bIsXref || bIsObject) && (nTargetOffset < nCurrent)) {
            nCurrent += osOffset.nSize;

            OS_STRING osEnd = _readPDFString(nCurrent, 20, pPdStruct);

            if (osEnd.sString.startsWith(QStringLiteral("%%EOF"))) {
                nCurrent += 5;

                if ((nCurrent < nFileSize) && (read_uint8(nCurrent) == 13)) {
                    ++nCurrent;
                }
                if ((nCurrent < nFileSize) && (read_uint8(nCurrent) == 10)) {
                    ++nCurrent;
                }

                STARTHREF record = {};
                record.nXrefOffset = nTargetOffset;
                record.nFooterOffset = nStartXref;
                record.nFooterSize = nCurrent - nStartXref;
                record.bIsObject = bIsObject;
                record.bIsXref = bIsXref;
                listResult.append(record);

                if (osEnd.sString.size() != 5) {
                    break;
                }

                OS_STRING osAppend = _readPDFString(nCurrent, 20, pPdStruct);
                if ((!_isObject(osAppend.sString)) && (!_isComment(osAppend.sString)) && (!_isXref(osAppend.sString))) {
                    break;
                }
            }
        }

        nOffset = nStartXref + 10;
    }

    return listResult;
}

XPDF::XPART XPDF::handleXpart(qint64 nOffset, qint32 nID, qint32 nPartLimit, PDSTRUCT *pPdStruct)
{
    XPART result = {};
    result.nOffset = nOffset;
    result.nID = nID;

    QString sLength;
    bool bLength = false;

    const qint64 nFileSize = getSize();

    while (XBinary::isPdStructNotCanceled(pPdStruct)) {
        bool bStop = false;
        OS_STRING osString = _readPDFStringPart_title(nOffset, 20, pPdStruct);

        if (result.nID == 0) {
            result.nID = getObjectID(osString.sString);
        }

        nOffset += osString.nSize;

        if (_isObject(osString.sString)) {
            qint32 nObj = 0;
            qint32 nCol = 0;
            qint32 nPartCount = 0;
            result.listParts.reserve(32);
            while (XBinary::isPdStructNotCanceled(pPdStruct)) {
                OS_STRING osStringPart = _readPDFStringPart(nOffset, pPdStruct);

                if ((nPartCount < nPartLimit) || (nPartLimit == -1)) {
                    result.listParts.append(osStringPart.sString);
                    ++nPartCount;
                } else {
                    bStop = true;
                    break;
                }

                nOffset += osStringPart.nSize;

                if (osStringPart.sString.isEmpty()) {
                    break;
                }

                const qint32 nStrLen = osStringPart.sString.size();

                if (nStrLen == 1) {
                    const QChar ch = osStringPart.sString.at(0);
                    if (ch == QChar('[')) {
                        ++nCol;
                    } else if (ch == QChar(']')) {
                        --nCol;
                    }
                } else if (nStrLen == 2) {
                    const QChar ch0 = osStringPart.sString.at(0);
                    if (ch0 == QChar('<')) {
                        ++nObj;
                    } else if (ch0 == QChar('>')) {
                        --nObj;
                    }
                } else if (nStrLen == 7 && osStringPart.sString == QLatin1String("/Length")) {
                    bLength = true;
                } else if (bLength) {
                    bLength = false;
                    sLength = osStringPart.sString;
                }

                if ((nObj == 0) && (nCol == 0)) {
                    break;
                }
            }

            if (bStop) {
                break;
            }
        } else if (osString.sString == QLatin1String("stream")) {
            STREAM stream = {};
            stream.nOffset = nOffset;
            bool bHasStreamSize = false;

            bool bIndirectLength = false;
            bool bLengthTokenIsNumber = false;
            qint64 nLengthToken = sLength.section(" ", 0, 0).toLongLong(&bLengthTokenIsNumber);

            if (bLengthTokenIsNumber) {
                qint32 nPartsCount = result.listParts.count();
                for (qint32 nP = 0; nP < nPartsCount - 2; nP++) {
                    if ((result.listParts.at(nP) == QString::number(nLengthToken)) && (result.listParts.at(nP + 2) == QLatin1String("R"))) {
                        if ((nP > 0) && (result.listParts.at(nP - 1) == QLatin1String("/Length"))) {
                            bIndirectLength = true;
                            sLength = result.listParts.at(nP) + " " + result.listParts.at(nP + 1) + " R";
                            break;
                        }
                    }
                }
            }

            if (!bIndirectLength) {
                bool bOk = false;
                qint64 nStreamSize = sLength.toLongLong(&bOk);

                if (bOk && (nStreamSize >= 0)) {
                    stream.nSize = nStreamSize;
                    bHasStreamSize = true;
                }
            } else if (sLength.section(" ", 2, 2) == QLatin1String("R")) {
                QString sPattern = sLength;
                sPattern.replace("R", "obj");
                qint64 nSearchSize = qMin<qint64>(1048576, nFileSize - nOffset);
                qint64 nObjectOffset = find_ansiString(nOffset, nSearchSize, sPattern, pPdStruct);

                if (nObjectOffset == -1) {
                    nObjectOffset = find_ansiString(0, nOffset, sPattern, pPdStruct);
                }

                if (nObjectOffset == -1) {
                    nObjectOffset = find_ansiString(0, -1, sPattern, pPdStruct);
                }

                if (nObjectOffset != -1) {
                    qint64 nTmp = nObjectOffset;
                    skipPDFString(&nTmp, pPdStruct);
                    OS_STRING osLen = _readPDFStringPart_val(nTmp, pPdStruct);

                    bool bOk = false;
                    qint64 nStreamSize = osLen.sString.toLongLong(&bOk);

                    if (bOk && (nStreamSize >= 0)) {
                        stream.nSize = nStreamSize;
                        bHasStreamSize = true;
                    } else {
                        break;
                    }
                }
            } else {
                break;
            }

            if (bHasStreamSize) {
                nOffset += stream.nSize;
                skipPDFEnding(&nOffset, pPdStruct);
                result.listStreams.append(stream);
            }
        } else if (osString.sString == QLatin1String("endstream")) {
        } else if (_isEndObject(osString.sString)) {
            break;
        } else if (osString.sString.isEmpty()) {
            break;
        }
    }

    result.nSize = nOffset - result.nOffset;

    return result;
}

bool XPDF::_isObject(const QString &sString)
{
    qint32 nI = sString.size();
    while (nI > 0 && sString.at(nI - 1) == QChar(' ')) --nI;
    const qint32 nTokenLen = 3;
    if (nI < nTokenLen) return false;
    const qint32 nStart = nI - nTokenLen;
    if (!((nStart == 0) || (sString.at(nStart - 1) == QChar(' ')))) return false;
    return (sString.at(nStart) == QChar('o') && sString.at(nStart + 1) == QChar('b') && sString.at(nStart + 2) == QChar('j'));
}

bool XPDF::_isString(const QString &sString)
{
    return sString.size() >= 2 && sString.front() == QChar('(') && sString.back() == QChar(')');
}

bool XPDF::_isHex(const QString &sString)
{
    return sString.size() >= 2 && sString.front() == QChar('<') && sString.back() == QChar('>') && sString.at(0) != sString.at(1);
}

bool XPDF::_isDateTime(const QString &sString)
{
    bool bResult = false;

    qint32 nSize = sString.size();
    if (nSize >= 18) {
        if (sString.startsWith("(D:") && (sString.at(nSize - 1) == QChar(')'))) {
            bResult = true;
        }
    }

    return bResult;
}

bool XPDF::_isEndObject(const QString &sString)
{
    return sString.trimmed() == QLatin1String("endobj");
}

bool XPDF::_isComment(const QString &sString)
{
    return !sString.isEmpty() && sString.at(0) == QChar('%');
}

bool XPDF::_isXref(const QString &sString)
{
    return sString.startsWith(QLatin1String("xref")) && (sString.size() == 4 || sString.at(4) == QChar(' '));
}

QString XPDF::_getCommentString(const QString &sString)
{
    QString sResult;

    if (!sString.isEmpty() && (sString.at(0) == QChar('%'))) {
        sResult = sString.mid(1);
    }

    return sResult;
}

QString XPDF::_getString(const QString &sString)
{
    QString sResult;

    qint32 nSize = sString.size();
    if (nSize >= 2) {
        sResult = sString.mid(1, nSize - 2);
    }

    return sResult;
}

QString XPDF::_getHex(const QString &sString)
{
    QString sResult;

    qint32 nSize = sString.size();
    if (nSize >= 2) {
        sResult = sString.mid(1, nSize - 2);
    }

    return sResult;
}

QDateTime XPDF::_getDateTime(const QString &sString)
{
    QDateTime result;

    QString sDate = sString.section(":", 1, -1);
    sDate = sDate.section(")", 0, 0);
    sDate.remove(QChar('\''));
    sDate.replace(QChar('Z'), QChar('+'));
    result = QDateTime::fromString(sDate, "yyyyMMddhhmmsstt");

    return result;
}

qint32 XPDF::getObjectID(const QString &sString)
{
    qint64 n = 0;
    bool bNeg = false;
    qint32 nI = 0;
    const qint32 nLen = sString.size();
    if (nI < nLen && sString.at(nI) == QChar('-')) {
        bNeg = true;
        ++nI;
    }
    for (; nI < nLen; ++nI) {
        const QChar c = sString.at(nI);
        if (c < QChar('0') || c > QChar('9')) break;
        n = n * 10 + (c.unicode() - '0');
    }
    if (bNeg) n = -n;
    return static_cast<qint32>(n);
}

XBinary::XVARIANT XPDF::_parseValue(const QString &sValue)
{
    XVARIANT varValue;

    const qlonglong ll = sValue.toLongLong();
    if (ll) {
        varValue.varType = XBinary::VT_INT64;
        varValue.var = ll;
    } else if (_isDateTime(sValue)) {
        varValue.varType = XBinary::VT_DATETIME;
        varValue.var = _getDateTime(sValue);
    } else if (_isString(sValue)) {
        varValue.varType = XBinary::VT_STRING;
        varValue.var = _getString(sValue);
    } else if (_isHex(sValue)) {
        varValue.varType = XBinary::VT_HEX;
        varValue.var = _getHex(sValue);
    } else {
        varValue.varType = XBinary::VT_VALUE;
        varValue.var = sValue;
    }

    return varValue;
}

XBinary::XVARIANT XPDF::getFirstStringValueByKey(QList<QString> *pListStrings, const QString &sKey, PDSTRUCT *pPdStruct)
{
    XBinary::XVARIANT varResult;

    const qint32 nNumberOfParts = pListStrings->count();

    for (qint32 j = 0; (j + 1 < nNumberOfParts) && XBinary::isPdStructNotCanceled(pPdStruct); ++j) {
        const QString &sCurrentKey = pListStrings->at(j);

        if (sCurrentKey == sKey) {
            XVARIANT varValue = _parseValue(pListStrings->at(j + 1));

            if (!varValue.var.isNull()) {
                varResult = varValue;
            }
        }
    }

    return varResult;
}

QList<XPDF::XPART> XPDF::getParts(qint32 nPartLimit, PDSTRUCT *pPdStruct)
{
    QList<XPART> listResult;

    const QList<STARTHREF> listStrartHrefs = findStartxrefs(0, pPdStruct);

    qint32 nNumberOfHrefs = listStrartHrefs.count();

    QList<OBJECT> listObject;
    if (nNumberOfHrefs) {
        for (qint32 i = 0; (i < nNumberOfHrefs) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
            const STARTHREF &startxref = listStrartHrefs.at(i);

            if (startxref.bIsXref) {
                listObject.append(getObjectsFromStartxref(&startxref, pPdStruct));
            } else if (startxref.bIsObject) {
                listObject = findObjects(0, startxref.nFooterOffset, true, pPdStruct);
            }
        }
    } else {
        listObject = findObjects(0, -1, false, pPdStruct);
    }

    qint32 nNumberOfObjects = listObject.count();

    if (nNumberOfObjects) {
        const qint32 _nFreeIndex = XBinary::getFreeIndex(pPdStruct);
        XBinary::setPdStructInit(pPdStruct, _nFreeIndex, nNumberOfObjects);

        for (qint32 i = 0; (i < nNumberOfObjects) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
            const OBJECT &record = listObject.at(i);

            XPART xpart = handleXpart(record.nOffset, record.nID, nPartLimit, pPdStruct);

            listResult.append(xpart);

            XBinary::setPdStructCurrentIncrement(pPdStruct, _nFreeIndex);
        }

        XBinary::setPdStructFinished(pPdStruct, _nFreeIndex);
    }

    return listResult;
}

QList<XBinary::XVARIANT> XPDF::getValuesByKey(QList<XPART> *pListObjects, const QString &sKey, PDSTRUCT *pPdStruct)
{
    QList<XVARIANT> listResult;
    QSet<QString> stVars;

    qint32 nNumberOfRecords = pListObjects->count();

    const qint32 _nFreeIndex = XBinary::getFreeIndex(pPdStruct);
    XBinary::setPdStructInit(pPdStruct, _nFreeIndex, nNumberOfRecords);

    for (qint32 i = 0; (i < nNumberOfRecords) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
        const XPART &record = pListObjects->at(i);

        qint32 nNumberOfParts = record.listParts.count();

        for (qint32 j = 0; (j + 1 < nNumberOfParts) && XBinary::isPdStructNotCanceled(pPdStruct); ++j) {
            const QString &sPart = record.listParts.at(j);

            if (sPart == sKey) {
                XVARIANT varValue = _parseValue(record.listParts.at(j + 1));

                if (!varValue.var.isNull()) {
                    const QString key = varValue.var.toString();
                    if (!stVars.contains(key)) {
                        listResult.append(varValue);
                        stVars.insert(key);
                    }
                }
            }
        }

        XBinary::setPdStructCurrentIncrement(pPdStruct, _nFreeIndex);
    }

    XBinary::setPdStructFinished(pPdStruct, _nFreeIndex);

    return listResult;
}

qint32 XPDF::getType()
{
    return TYPE_DOCUMENT;
}

QString XPDF::typeIdToString(qint32 nType)
{
    QString sResult = tr("Unknown");

    switch (nType) {
        case TYPE_UNKNOWN: sResult = tr("Unknown"); break;
        case TYPE_DOCUMENT: sResult = tr("Document"); break;
    }

    return sResult;
}

QString XPDF::getHeaderCommentAsHex(PDSTRUCT *pPdStruct)
{
    QString sResult;

    const qint64 nFileSize = getSize();
    qint64 nCurrentOffset = 0;

    OS_STRING osString = _readPDFString(nCurrentOffset, 100, pPdStruct);
    nCurrentOffset += osString.nSize;

    if ((nCurrentOffset < nFileSize) && (read_uint8(nCurrentOffset) == '%')) {
        ++nCurrentOffset;

        const qint64 nMaxRead = qMin<qint64>(40, nFileSize - nCurrentOffset);
        QByteArray baData = read_array(nCurrentOffset, nMaxRead);

        qint32 nLen = 0;
        while (nLen < baData.size()) {
            quint8 nChar = static_cast<quint8>(baData.at(nLen));
            if (isPdfStringTerminator(nChar)) break;
            ++nLen;
        }

        sResult = baData.left(nLen).toHex();
    }

    return sResult;
}

QString XPDF::getFilters(PDSTRUCT *pPdStruct)
{
    QString sResult;

    QList<XPART> listParts = getParts(100, pPdStruct);
    const QList<XBinary::XVARIANT> listValues = getValuesByKey(&listParts, QLatin1String("/Filter"), pPdStruct);

    QStringList filters;

    const int nNumberOfValues = listValues.count();
    for (int i = 0; (i < nNumberOfValues) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
        const QString v = listValues.at(i).var.toString();
        if (!v.isEmpty()) filters.append(v);
    }

    sResult = filters.join(QLatin1String(", "));
    return sResult;
}

QString XPDF::getInfo(PDSTRUCT *pPdStruct)
{
    QString sResult;

    sResult = getFilters(pPdStruct);

    return sResult;
}

QList<XBinary::FPART> XPDF::getFileParts(quint32 nFileParts, qint32 nLimit, PDSTRUCT *pPdStruct)
{
    Q_UNUSED(nLimit)

    QList<XBinary::FPART> listResult;

    qint64 nMaxOffset = 0;
    QList<OBJECT> listObject;

    const QList<STARTHREF> listStrartHrefs = findStartxrefs(0, pPdStruct);
    const qint64 totalSize = getSize();

    qint32 nNumberOfFrefs = listStrartHrefs.count();

    if (nNumberOfFrefs) {
        if (nFileParts & FILEPART_SIGNATURE) {
            const OS_STRING osHeader = _readPDFString(0, 20, pPdStruct);

            listResult.append(makeFilePart(FILEPART_SIGNATURE, 0, osHeader.nSize, tr("Signature")));
        }

        for (int j = 0; (j < nNumberOfFrefs) && XBinary::isPdStructNotCanceled(pPdStruct); ++j) {
            const STARTHREF &startxref = listStrartHrefs.at(j);

            if (startxref.bIsXref) {
                if ((nFileParts & FILEPART_OBJECT) || (nFileParts & FILEPART_STREAM)) {
                    listObject.append(getObjectsFromStartxref(&startxref, pPdStruct));
                }

                if (nFileParts & FILEPART_TABLE) {
                    listResult.append(makeFilePart(FILEPART_DATA, startxref.nXrefOffset, startxref.nFooterOffset - startxref.nXrefOffset, QStringLiteral("xref")));
                }
            } else if (startxref.bIsObject) {
                if ((nFileParts & FILEPART_OBJECT) || (nFileParts & FILEPART_STREAM)) {
                    listObject.append(findObjects(0, startxref.nFooterOffset, true, pPdStruct));
                }
            }

            if (nFileParts & FILEPART_FOOTER) {
                listResult.append(makeFilePart(FILEPART_FOOTER, startxref.nFooterOffset, startxref.nFooterSize, tr("Footer")));
            }
        }

        nMaxOffset = listStrartHrefs.at(nNumberOfFrefs - 1).nFooterOffset + listStrartHrefs.at(nNumberOfFrefs - 1).nFooterSize;
    } else {
        listObject.append(findObjects(0, -1, false, pPdStruct));

        qint32 nNumberOfObjects = listObject.count();

        if (nNumberOfObjects) {
            const OS_STRING osHeader = _readPDFString(0, 20, pPdStruct);

            if (nFileParts & FILEPART_SIGNATURE) {
                listResult.append(makeFilePart(FILEPART_SIGNATURE, 0, osHeader.nSize, tr("Header")));
            }

            nMaxOffset = osHeader.nSize;
        }

        for (qint32 i = 0; (i < nNumberOfObjects) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
            const OBJECT &o = listObject.at(i);
            const qint64 end = o.nOffset + o.nSize;
            if (end > nMaxOffset) nMaxOffset = end;
        }
    }

    if (nFileParts & FILEPART_DATA) {
        listResult.append(makeFilePart(FILEPART_DATA, 0, nMaxOffset, tr("Data")));
    }

    if ((nFileParts & FILEPART_STREAM) || (nFileParts & FILEPART_OBJECT)) {
        qint32 nNumberOfObjects = listObject.count();
        QSet<qint32> stPaletteObjectIds;

        for (qint32 i = 0; (i < nNumberOfObjects) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
            const OBJECT &object = listObject.at(i);

            if (nFileParts & FILEPART_OBJECT) {
                listResult.append(makeFilePart(FILEPART_OBJECT, object.nOffset, object.nSize, QString("%1 %2").arg(tr("Object")).arg(QString::number(object.nID))));
            }

            if (nFileParts & FILEPART_STREAM) {
                XPART xpart = handleXpart(object.nOffset, object.nID, -1, pPdStruct);

                qint32 nNumberOfStreams = xpart.listStreams.count();

                for (qint32 j = 0; (j < nNumberOfStreams) && XBinary::isPdStructNotCanceled(pPdStruct); ++j) {
                    const STREAM &stream = xpart.listStreams.at(j);

                    XBinary::FPART record =
                        makeFilePart(XBinary::FILEPART_STREAM, stream.nOffset, stream.nSize, QString("%1 obj (%2)").arg(tr("Stream")).arg(QString::number(object.nID)));

                    const QString sFilter = getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/Filter"), pPdStruct).var.toString();
                    const QString sSubtype = getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/Subtype"), pPdStruct).var.toString();

                    if (!sFilter.isEmpty()) {
                        record.mapProperties.insert(FPART_PROP_FILTERNAME, sFilter);
                    }
                    if (!sSubtype.isEmpty()) {
                        record.mapProperties.insert(FPART_PROP_SUBTYPE, sSubtype);
                    }

                    const HANDLE_METHOD decompressMethod = pdfFilterToHandleMethod(sFilter);

                    if (sFilter == QLatin1String("[")) {
#ifdef QT_DEBUG
                        qDebug() << "Array filter:" << sFilter << xpart.listParts << record.sName;
#endif
                    } else if (!isKnownStorePdfFilter(sFilter) && (decompressMethod == HANDLE_METHOD_STORE)) {
#ifdef QT_DEBUG
                        qDebug() << "Unknown filter:" << sFilter << xpart.listParts << record.sName;
#endif
                    }

                    record.mapProperties.insert(FPART_PROP_HANDLEMETHOD, decompressMethod);

                    if (sSubtype == QLatin1String("/Image")) {
                        const qint32 nWidth = getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/Width"), pPdStruct).var.toInt();
                        const qint32 nHeight = getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/Height"), pPdStruct).var.toInt();
                        const qint32 nBitsPerComponent = getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/BitsPerComponent"), pPdStruct).var.toInt();

                        QString sColorSpace;
                        QString sBaseColorSpace;
                        QByteArray baPalette;

                        {
                            const qint32 nParts = xpart.listParts.count();

                            for (qint32 p = 0; (p + 1 < nParts) && XBinary::isPdStructNotCanceled(pPdStruct); ++p) {
                                if (xpart.listParts.at(p) == QLatin1String("/ColorSpace")) {
                                    const QString &sNext = xpart.listParts.at(p + 1);

                                    if (sNext == QLatin1String("[")) {
                                        QStringList listTokens;

                                        for (qint32 q = p + 2; q < nParts; ++q) {
                                            if (xpart.listParts.at(q) == QLatin1String("]")) {
                                                break;
                                            }

                                            listTokens.append(xpart.listParts.at(q));
                                        }

                                        if (listTokens.count() >= 1) {
                                            sColorSpace = listTokens.at(0);
                                        }

                                        if ((sColorSpace == QLatin1String("/Indexed")) && (listTokens.count() >= 3)) {
                                            sBaseColorSpace = listTokens.at(1);

                                            if (listTokens.count() >= 4) {
                                                QString sPaletteToken = listTokens.at(3);

                                                if (sPaletteToken.startsWith(QLatin1Char('<')) && sPaletteToken.endsWith(QLatin1Char('>'))) {
                                                    QString sPaletteHex = sPaletteToken.mid(1, sPaletteToken.length() - 2);
                                                    baPalette = QByteArray::fromHex(sPaletteHex.toLatin1());
                                                } else if (sPaletteToken.endsWith(QLatin1String(" R")) ||
                                                           ((listTokens.count() >= 6) && (listTokens.at(5) == QLatin1String("R")))) {
                                                    QString sObjRef;
                                                    if (sPaletteToken.endsWith(QLatin1String(" R"))) {
                                                        sObjRef = sPaletteToken;
                                                    } else {
                                                        sObjRef = sPaletteToken + " " + listTokens.at(4) + " R";
                                                    }
                                                    QString sObjPattern = sObjRef;
                                                    sObjPattern.replace(QLatin1String("R"), QLatin1String("obj"));
                                                    qint64 nPaletteObjOffset = find_ansiString(0, -1, sObjPattern, pPdStruct);

                                                    if (nPaletteObjOffset != -1) {
                                                        qint32 nPalObjId = sObjRef.section(" ", 0, 0).toInt();
                                                        stPaletteObjectIds.insert(nPalObjId);
                                                        XPART palPart = handleXpart(nPaletteObjOffset, nPalObjId, -1, pPdStruct);

                                                        if (palPart.listStreams.count() > 0) {
                                                            const STREAM &palStream = palPart.listStreams.at(0);
                                                            QByteArray baRawPalette = read_array_process(palStream.nOffset, palStream.nSize, pPdStruct);

                                                            QString sPalFilter;
                                                            qint32 nPalParts = palPart.listParts.count();
                                                            for (qint32 f = 0; f < nPalParts - 1; ++f) {
                                                                if (palPart.listParts.at(f) == QLatin1String("/Filter")) {
                                                                    sPalFilter = palPart.listParts.at(f + 1);
                                                                    break;
                                                                }
                                                            }

                                                            if (!sPalFilter.isEmpty() && (sPalFilter != QLatin1String("/None"))) {
                                                                const HANDLE_METHOD palMethod = pdfFilterToHandleMethod(sPalFilter);

                                                                if (palMethod != HANDLE_METHOD_STORE) {
                                                                    QByteArray baDecodedPalette;

                                                                    if (decompressPdfBuffer(baRawPalette, palMethod, -1, &baDecodedPalette, pPdStruct) &&
                                                                        !baDecodedPalette.isEmpty()) {
                                                                        baPalette = baDecodedPalette;
                                                                    }
                                                                }
                                                            } else {
                                                                baPalette = baRawPalette;
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    } else {
                                        sColorSpace = sNext;
                                    }

                                    break;
                                }
                            }
                        }

                        record.mapProperties.insert(FPART_PROP_WIDTH, nWidth);
                        record.mapProperties.insert(FPART_PROP_HEIGHT, nHeight);
                        record.mapProperties.insert(FPART_PROP_BITSPERCOMPONENT, nBitsPerComponent);

                        if (!sColorSpace.isEmpty()) {
                            record.mapProperties.insert(FPART_PROP_COLORSPACE, sColorSpace);
                        }

                        if (!sBaseColorSpace.isEmpty()) {
                            record.mapProperties.insert(FPART_PROP_BASECOLORSPACE, sBaseColorSpace);
                        }

                        if (!baPalette.isEmpty()) {
                            record.mapProperties.insert(FPART_PROP_PALETTE, baPalette);
                        }

                        if (sFilter == QLatin1String("/DCTDecode")) {
                            record.mapProperties.insert(FPART_PROP_FILETYPE, XBinary::FT_JPEG);
                            record.mapProperties.insert(FPART_PROP_EXT, QStringLiteral("jpg"));
                            record.mapProperties.insert(FPART_PROP_INFO, QString("%1 JPEG (%2 x %3) [%4] %5")
                                                                             .arg(tr("Image"))
                                                                             .arg(QString::number(nWidth))
                                                                             .arg(QString::number(nHeight))
                                                                             .arg(QString::number(nBitsPerComponent))
                                                                             .arg(sColorSpace));
                        } else if (sFilter == QLatin1String("/CCITTFaxDecode")) {
                            qint32 nCcittK = -1;
                            XBinary::XVARIANT varK = getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/K"), pPdStruct);
                            if (!varK.var.isNull()) {
                                nCcittK = varK.var.toInt();
                            }
                            record.mapProperties.insert(FPART_PROP_HANDLEMETHOD, HANDLE_METHOD_PDF_CCITTIMAGE);
                            record.mapProperties.insert(FPART_PROP_CCITTK, nCcittK);
                            record.mapProperties.insert(FPART_PROP_EXT, QStringLiteral("tif"));
                            record.mapProperties.insert(FPART_PROP_INFO, QString("%1 CCITT (%2 x %3) [%4]")
                                                                             .arg(tr("Image"))
                                                                             .arg(QString::number(nWidth))
                                                                             .arg(QString::number(nHeight))
                                                                             .arg(QString::number(nBitsPerComponent)));
                        } else {
                            record.mapProperties.insert(FPART_PROP_HANDLEMETHOD2, decompressMethod);
                            record.mapProperties.insert(FPART_PROP_HANDLEMETHOD, HANDLE_METHOD_PDF_IMAGEDATA);
                            record.mapProperties.insert(FPART_PROP_FILETYPE, XBinary::FT_PNG);
                            record.mapProperties.insert(FPART_PROP_EXT, QStringLiteral("png"));
                            record.mapProperties.insert(FPART_PROP_INFO, QString("%1 RAW (%2 x %3) [%4] %5 %6")
                                                                             .arg(tr("Image"))
                                                                             .arg(QString::number(nWidth))
                                                                             .arg(QString::number(nHeight))
                                                                             .arg(QString::number(nBitsPerComponent))
                                                                             .arg(sColorSpace)
                                                                             .arg(sFilter));
                        }
                    } else {
                        if (sSubtype == QLatin1String("/XML")) {
                            record.mapProperties.insert(FPART_PROP_EXT, QStringLiteral("xml"));
                            record.mapProperties.insert(FPART_PROP_FILETYPE, FT_XML);
                        } else if ((sSubtype == QLatin1String("/Type1C")) || (sSubtype == QLatin1String("/CIDFontType0C"))) {
                            record.mapProperties.insert(FPART_PROP_EXT, QStringLiteral("cff"));
                        } else if (!getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/Length1"), pPdStruct).var.isNull()) {
                            record.mapProperties.insert(FPART_PROP_EXT, QStringLiteral("ttf"));
                            record.mapProperties.insert(FPART_PROP_FILETYPE, FT_TTF);
                        } else if (sSubtype.isEmpty() && !getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/N"), pPdStruct).var.isNull()) {
                            bool bIsICC = false;

                            if (decompressMethod == HANDLE_METHOD_STORE) {
                                if (stream.nSize >= 40) {
                                    bIsICC = (read_uint32(stream.nOffset + 36, true) == 0x61637370);
                                }
                            } else if (stream.nSize >= 40) {
                                qint64 nReadSize = qMin(stream.nSize, (qint64)256);
                                QByteArray baRaw = read_array(stream.nOffset, nReadSize);
                                QByteArray baHeader;
                                decompressPdfBuffer(baRaw, decompressMethod, 40, &baHeader, pPdStruct);

                                if (baHeader.size() >= 40) {
                                    quint32 nSig = readUInt32BE(baHeader, 36);
                                    bIsICC = (nSig == 0x61637370);
                                }
                            }

                            if (bIsICC) {
                                record.mapProperties.insert(FPART_PROP_EXT, QStringLiteral("icc"));
                                record.mapProperties.insert(FPART_PROP_FILETYPE, FT_ICC);
                            } else if (decompressMethod != HANDLE_METHOD_STORE) {
                                record.mapProperties.insert(FPART_PROP_EXT, QStringLiteral("dat"));
                            } else {
                                record.mapProperties.insert(FPART_PROP_EXT, QStringLiteral("bin"));
                            }
                        } else if (sFilter == QLatin1String("/DCTDecode")) {
                            record.mapProperties.insert(FPART_PROP_EXT, QStringLiteral("jpg"));
                        } else if (sFilter == QLatin1String("/CCITTFaxDecode")) {
                            record.mapProperties.insert(FPART_PROP_EXT, QStringLiteral("bin"));
                        } else if ((decompressMethod == HANDLE_METHOD_STORE) && (stream.nSize >= 4)) {
                            quint32 nMagic = read_uint32(stream.nOffset, true);
                            if (nMagic == 0x00010000) {
                                record.mapProperties.insert(FPART_PROP_EXT, QStringLiteral("ttf"));
                                record.mapProperties.insert(FPART_PROP_FILETYPE, FT_TTF);
                            } else if ((stream.nSize >= 40) && (read_uint32(stream.nOffset + 36, true) == 0x61637370)) {
                                record.mapProperties.insert(FPART_PROP_EXT, QStringLiteral("icc"));
                                record.mapProperties.insert(FPART_PROP_FILETYPE, FT_ICC);
                            } else {
                                record.mapProperties.insert(FPART_PROP_EXT, QStringLiteral("bin"));
                            }
                        } else if (decompressMethod != HANDLE_METHOD_STORE) {
                            record.mapProperties.insert(FPART_PROP_EXT, QStringLiteral("dat"));
                        } else {
                            record.mapProperties.insert(FPART_PROP_EXT, QStringLiteral("bin"));
                        }

                        QStringList listInfo;
                        if (!sSubtype.isEmpty()) {
                            listInfo << sSubtype;
                        }
                        if (!sFilter.isEmpty()) {
                            listInfo << sFilter;
                        }
                        if (!listInfo.isEmpty()) {
                            record.mapProperties.insert(FPART_PROP_INFO, listInfo.join(QLatin1String(" ")));
                        }
                    }

                    listResult.append(record);
                }
            }
        }

        if (!stPaletteObjectIds.isEmpty()) {
            qint32 nResultCount = listResult.count();
            for (qint32 r = 0; r < nResultCount; ++r) {
                if (listResult.at(r).filePart == XBinary::FILEPART_STREAM) {
                    QString sName = listResult.at(r).sName;
                    qint32 nParenOpen = sName.indexOf(QLatin1Char('('));
                    qint32 nParenClose = sName.indexOf(QLatin1Char(')'));
                    if ((nParenOpen != -1) && (nParenClose != -1) && (nParenClose > nParenOpen)) {
                        qint32 nObjId = sName.mid(nParenOpen + 1, nParenClose - nParenOpen - 1).toInt();
                        if (stPaletteObjectIds.contains(nObjId)) {
                            listResult[r].mapProperties.insert(FPART_PROP_HANDLEMETHOD2, listResult[r].mapProperties.value(FPART_PROP_HANDLEMETHOD, HANDLE_METHOD_STORE));
                            listResult[r].mapProperties.insert(FPART_PROP_EXT, QStringLiteral("pal"));
                            listResult[r].mapProperties.insert(FPART_PROP_FILETYPE, FT_PAL);
                            listResult[r].mapProperties.insert(FPART_PROP_HANDLEMETHOD, HANDLE_METHOD_PDF_PALETTE);
                            listResult[r].mapProperties.insert(FPART_PROP_INFO, tr("Color palette") + QString(" (PAL)"));
                        }
                    }
                }
            }
        }
    }

    if (nFileParts & FILEPART_OVERLAY) {
        if (nMaxOffset < totalSize) {
            listResult.append(makeFilePart(FILEPART_OVERLAY, nMaxOffset, totalSize - nMaxOffset, tr("Overlay")));
        }
    }

    return listResult;
}

QMap<XBinary::UNPACK_PROP, QVariant> XPDF::getDefaultUnpackProperties()
{
    QMap<XBinary::UNPACK_PROP, QVariant> result = XBinary::getDefaultUnpackProperties();

    return result;
}

bool XPDF::initUnpack(UNPACK_STATE *pState, const QMap<UNPACK_PROP, QVariant> &mapProperties, PDSTRUCT *pPdStruct)
{
    Q_UNUSED(mapProperties)

    if (!pState) {
        return false;
    }

    pState->nCurrentOffset = 0;
    pState->nTotalSize = getSize();
    pState->nCurrentIndex = 0;
    pState->mapUnpackProperties = mapProperties;
    pState->pContext = nullptr;

    QList<XBinary::FPART> listStreams = getFileParts(FILEPART_STREAM, -1, pPdStruct);

    if (XBinary::isPdStructNotCanceled(pPdStruct)) {
        UNPACK_CONTEXT *pContext = new UNPACK_CONTEXT;
        pContext->listStreams = listStreams;
        pContext->nCurrentStreamIndex = 0;

        pState->pContext = pContext;
        pState->nNumberOfRecords = listStreams.count();

        return true;
    }

    return false;
}

XBinary::ARCHIVERECORD XPDF::infoCurrent(UNPACK_STATE *pState, PDSTRUCT *pPdStruct)
{
    Q_UNUSED(pPdStruct)

    XBinary::ARCHIVERECORD result = {};

    if (!pState || !pState->pContext) {
        return result;
    }

    UNPACK_CONTEXT *pContext = static_cast<UNPACK_CONTEXT *>(pState->pContext);

    if (pContext->nCurrentStreamIndex < 0 || pContext->nCurrentStreamIndex >= pContext->listStreams.count()) {
        return result;
    }

    const XBinary::FPART &stream = pContext->listStreams.at(pContext->nCurrentStreamIndex);

    result.nStreamOffset = stream.nFileOffset;
    result.nStreamSize = stream.nFileSize;

    QString sFileName = stream.sName;
    static const QRegularExpression reInvalidChars(QStringLiteral("[/\\\\:*?\"<>|]"));
    sFileName.replace(reInvalidChars, QStringLiteral("_"));

    HANDLE_METHOD primaryMethod = (HANDLE_METHOD)stream.mapProperties.value(FPART_PROP_HANDLEMETHOD, HANDLE_METHOD_STORE).toInt();
    QString sExt = stream.mapProperties.value(FPART_PROP_EXT).toString();
    if (sExt.isEmpty()) {
        if (primaryMethod == HANDLE_METHOD_STORE) {
            sExt = QStringLiteral("bin");
        } else {
            sExt = QStringLiteral("dat");
        }
    }

    sFileName = QString("%1_%2.%3").arg(sFileName).arg(QString::number(pContext->nCurrentStreamIndex)).arg(sExt);

    result.mapProperties.insert(FPART_PROP_ORIGINALNAME, sFileName);
    result.mapProperties.insert(FPART_PROP_UNCOMPRESSEDSIZE, stream.nFileSize);
    result.mapProperties.insert(FPART_PROP_COMPRESSEDSIZE, stream.nFileSize);

    QMapIterator<FPART_PROP, QVariant> it(stream.mapProperties);
    while (it.hasNext()) {
        it.next();
        result.mapProperties.insert(it.key(), it.value());
    }

    return result;
}

bool XPDF::unpackCurrent(UNPACK_STATE *pState, QIODevice *pDevice, PDSTRUCT *pPdStruct)
{
    bool bResult = false;

    if (pState && pDevice && pState->pContext) {
        ARCHIVERECORD archiveRecord = infoCurrent(pState, pPdStruct);

        XDecompress xDecompress;
        connect(&xDecompress, &XDecompress::errorMessage, this, &XBinary::errorMessage);
        connect(&xDecompress, &XDecompress::infoMessage, this, &XBinary::infoMessage);

        bResult = xDecompress.decompressArchiveRecord(archiveRecord, getDevice(), pDevice, pState->mapUnpackProperties, pPdStruct);
    }

    return bResult;
}

bool XPDF::moveToNext(UNPACK_STATE *pState, PDSTRUCT *pPdStruct)
{
    Q_UNUSED(pPdStruct)

    if (!pState || !pState->pContext) {
        return false;
    }

    UNPACK_CONTEXT *pContext = static_cast<UNPACK_CONTEXT *>(pState->pContext);

    pContext->nCurrentStreamIndex++;
    pState->nCurrentIndex = pContext->nCurrentStreamIndex;

    return true;
}

bool XPDF::finishUnpack(UNPACK_STATE *pState, PDSTRUCT *pPdStruct)
{
    Q_UNUSED(pPdStruct)

    if (!pState) {
        return false;
    }

    if (pState->pContext) {
        UNPACK_CONTEXT *pContext = static_cast<UNPACK_CONTEXT *>(pState->pContext);
        delete pContext;
        pState->pContext = nullptr;
    }

    pState->nCurrentIndex = 0;
    pState->nNumberOfRecords = 0;
    pState->nCurrentOffset = 0;

    return true;
}

QList<QString> XPDF::getSearchSignatures()
{
    QList<QString> listResult;

    listResult.append("'%PDF-'");

    return listResult;
}

XBinary *XPDF::createInstance(QIODevice *pDevice, bool bIsImage, XADDR nModuleAddress)
{
    Q_UNUSED(bIsImage)
    Q_UNUSED(nModuleAddress)

    return new XPDF(pDevice);
}

bool XPDF::handleInternalInfo(PDSTRUCT *pPdStruct)
{
    bool bResult = true;

    if (!isInternalInfoHandled()) {
        bResult = XBinary::handleInternalInfo(pPdStruct);

        if (bResult) {
            static_cast<XBinary::INTERNAL_INFO &>(m_internalInfo) =
                *static_cast<XBinary::INTERNAL_INFO *>(XBinary::getInternalInfo(pPdStruct));
            setIsInternalInfoHandled(true);
        }
    }

    return bResult;
}

void *XPDF::getInternalInfo(PDSTRUCT *pPdStruct)
{
    handleInternalInfo(pPdStruct);

    return &m_internalInfo;
}

void XPDF::setInternalInfo(void *pInternalInfo)
{
    if (pInternalInfo) {
        m_internalInfo = *static_cast<INTERNAL_INFO *>(pInternalInfo);
        XBinary::setInternalInfo(static_cast<XBinary::INTERNAL_INFO *>(&m_internalInfo));
        setIsInternalInfoHandled(true);
    } else {
        m_internalInfo = INTERNAL_INFO();
        XBinary::setInternalInfo(nullptr);
        setIsInternalInfoHandled(false);
    }
}
