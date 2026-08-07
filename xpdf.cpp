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
#include <QTimeZone>
#ifdef USE_XJS
#include "xjsemul.h"
#endif

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

// PDF whitespace per ISO 32000 7.2.3: NUL, TAB, LF, FF, CR, SP.
bool isPdfWhitespaceByte(quint8 nChar)
{
    return (nChar == 0) || (nChar == 9) || (nChar == 10) || (nChar == 12) || (nChar == 13) || (nChar == ' ');
}

bool isPdfNameTerminator(quint8 nChar)
{
    return isPdfStringTerminator(nChar) || isPdfStructuralDelimiter(nChar) || (nChar == ' ') || (nChar == 9) || (nChar == 12) || (nChar == '(');
}

bool isPdfValueTerminator(quint8 nChar)
{
    return isPdfStringTerminator(nChar) || isPdfStructuralDelimiter(nChar) || (nChar == '/');
}

void skipBufferPdfSpace(const char *pData, qint32 nDataSize, qint32 *pnPos)
{
    while (*pnPos < nDataSize) {
        const quint8 nChar = static_cast<quint8>(pData[*pnPos]);
        if ((nChar == ' ') || (nChar == 9) || (nChar == 12)) {
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
    } else if (sFilter == QLatin1String("/ASCIIHexDecode")) {
        result = XBinary::HANDLE_METHOD_ASCIIHEX;
    } else if (sFilter == QLatin1String("/RunLengthDecode")) {
        result = XBinary::HANDLE_METHOD_RUNLENGTH;
    }

    return result;
}

bool isKnownStorePdfFilter(const QString &sFilter)
{
    // Filters we intentionally leave stored (image codecs / encryption) rather than an unhandled-filter miss.
    // ASCIIHexDecode and RunLengthDecode are NOT here: they now have real decoders (pdfFilterToHandleMethod).
    return sFilter.isEmpty() || (sFilter == QLatin1String("/DCTDecode")) || (sFilter == QLatin1String("/CCITTFaxDecode")) ||
           (sFilter == QLatin1String("/JBIG2Decode")) || (sFilter == QLatin1String("/JPXDecode")) || (sFilter == QLatin1String("/Crypt"));
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

// Apply a filter chain (e.g. [/ASCII85Decode /FlateDecode]) in order. Stops at the first filter that has
// no decoder here (image codec / unknown), returning whatever was decoded so far. Returns true if any
// stage ran. This is what makes cascaded streams (a common obfuscation) decode instead of staying stored.
bool decompressPdfChain(const QByteArray &baData, const QStringList &listFilters, QByteArray *pbaResult, XBinary::PDSTRUCT *pPdStruct)
{
    QByteArray baCurrent = baData;
    bool bAny = false;

    for (qint32 i = 0; i < listFilters.count(); ++i) {
        const XBinary::HANDLE_METHOD handleMethod = pdfFilterToHandleMethod(listFilters.at(i));
        if (handleMethod == XBinary::HANDLE_METHOD_STORE) {
            break;  // no decoder for this stage (DCT/CCITT/JBIG2/unknown) -> stop the chain
        }

        QByteArray baNext;
        if (decompressPdfBuffer(baCurrent, handleMethod, -1, &baNext, pPdStruct)) {
            baCurrent = baNext;
            bAny = true;
        } else {
            break;
        }
    }

    *pbaResult = baCurrent;
    return bAny;
}

// Decode a single PDF literal "(...)" string starting at *pnPos (which must point at '('), honouring
// backslash escapes and balanced nested parentheses. Advances *pnPos past the closing ')'.
QString readPdfBufferLiteralString(const char *pData, qint32 nSize, qint32 *pnPos)
{
    QString sResult;
    qint32 i = *pnPos + 1;  // skip '('
    qint32 nDepth = 0;
    bool bBSlash = false;

    while (i < nSize) {
        const char c = pData[i];
        if (bBSlash) {
            switch (c) {
                case 'n': sResult.append(QChar('\n')); break;
                case 't': sResult.append(QChar('\t')); break;
                case 'r': sResult.append(QChar('\r')); break;
                case 'b': sResult.append(QChar('\b')); break;
                case 'f': sResult.append(QChar('\f')); break;
                default: sResult.append(QLatin1Char(c)); break;  // \( \) \\ and octal treated leniently
            }
            bBSlash = false;
            ++i;
            continue;
        }
        if (c == '\\') {
            bBSlash = true;
            ++i;
            continue;
        }
        if (c == '(') {
            ++nDepth;
            sResult.append(QLatin1Char(c));
            ++i;
            continue;
        }
        if (c == ')') {
            if (nDepth == 0) {
                ++i;
                break;
            }
            --nDepth;
            sResult.append(QLatin1Char(c));
            ++i;
            continue;
        }
        sResult.append(QLatin1Char(c));
        ++i;
    }

    *pnPos = i;
    return sResult;
}

// Scan a raw buffer (e.g. a decompressed object stream) for "/JS" keys with an inline literal or hex
// string value, returning the JavaScript source strings. Used to reach JS packed inside object streams.
QStringList extractJSStringsFromBuffer(const QByteArray &baData)
{
    QStringList listResult;

    const char *pData = baData.constData();
    const qint32 nSize = baData.size();
    qint32 nFrom = 0;

    while (nFrom < nSize) {
        const qint32 nIdx = baData.indexOf("/JS", nFrom);
        if (nIdx == -1) {
            break;
        }

        qint32 i = nIdx + 3;
        // Reject a longer name like "/JScript": /JS must be followed by a delimiter/whitespace/value.
        if ((i < nSize) && (QChar(QLatin1Char(pData[i])).isLetterOrNumber())) {
            nFrom = nIdx + 3;
            continue;
        }

        while ((i < nSize) && ((pData[i] == ' ') || (pData[i] == '\t') || (pData[i] == '\n') || (pData[i] == '\r') || (pData[i] == '\f') || (pData[i] == 0))) {
            ++i;
        }

        if ((i < nSize) && (pData[i] == '(')) {
            const QString sJs = readPdfBufferLiteralString(pData, nSize, &i);
            if (!sJs.trimmed().isEmpty() && !listResult.contains(sJs)) {
                listResult.append(sJs);
            }
            nFrom = i;
        } else if ((i + 1 < nSize) && (pData[i] == '<') && (pData[i + 1] != '<')) {
            const qint32 nEnd = baData.indexOf('>', i);
            if (nEnd != -1) {
                const QByteArray baHex = baData.mid(i + 1, nEnd - i - 1);
                const QString sJs = QString::fromLatin1(QByteArray::fromHex(baHex));
                if (!sJs.trimmed().isEmpty() && !listResult.contains(sJs)) {
                    listResult.append(sJs);
                }
                nFrom = nEnd + 1;
            } else {
                nFrom = nIdx + 3;
            }
        } else {
            nFrom = nIdx + 3;
        }
    }

    return listResult;
}

quint32 readUInt32BE(const QByteArray &baData, qint32 nOffset)
{
    return (static_cast<quint32>(static_cast<quint8>(baData.at(nOffset))) << 24) | (static_cast<quint32>(static_cast<quint8>(baData.at(nOffset + 1))) << 16) |
           (static_cast<quint32>(static_cast<quint8>(baData.at(nOffset + 2))) << 8) | static_cast<quint32>(static_cast<quint8>(baData.at(nOffset + 3)));
}

// Read a big-endian unsigned integer of nWidth bytes (0..8) at nOffset from a buffer. Width 0 -> default value per xref spec.
quint64 readBEValue(const quint8 *pData, qint64 nPos, qint32 nWidth)
{
    quint64 nResult = 0;

    for (qint32 i = 0; i < nWidth; ++i) {
        nResult = (nResult << 8) | pData[nPos + i];
    }

    return nResult;
}
}  // namespace

XPDF::XPDF(QIODevice *pDevice) : XBinary(pDevice)
{
    m_bStructScanned = false;
    m_nHeaderOffset = -2;  // -2: not computed, -1: no header, >=0: header offset
    m_bDecryptChecked = false;
    m_bDecryptReady = false;
}

qint64 XPDF::getHeaderOffset(PDSTRUCT *pPdStruct)
{
    if (m_nHeaderOffset != -2) {
        return m_nHeaderOffset;
    }

    qint64 nResult = -1;
    const qint64 nFileSize = getSize();

    if (nFileSize > 4) {
        if (read_uint32(0) == 0x46445025) {  // "%PDF" at offset 0 (fast path)
            nResult = 0;
        } else {
            // Adobe/ISO accept the "%PDF-" header anywhere in the first 1024 bytes (BOM, mail-mangling, embedding).
            const qint64 nScanSize = qMin<qint64>(nFileSize, 1024 + 5);
            nResult = find_signature(0, nScanSize, "'%PDF-'", nullptr, pPdStruct);
            if (nResult > 1024) {
                nResult = -1;
            }
        }
    }

    m_nHeaderOffset = nResult;

    return nResult;
}

bool XPDF::isValid(PDSTRUCT *pPdStruct)
{
    return getHeaderOffset(pPdStruct) != -1;
}

bool XPDF::isValid(QIODevice *pDevice, PDSTRUCT *pPdStruct)
{
    XPDF pdf(pDevice);
    return pdf.isValid(pPdStruct);
}

QString XPDF::getVersion()
{
    qint64 nHeader = getHeaderOffset(nullptr);
    if (nHeader < 0) {
        nHeader = 0;
    }

    return _readPDFString(nHeader + 5, 3, nullptr).sString;
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
        const qint64 nIterEntry = nCurrentOffset;
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
                qint64 nObjKeyword = find_ansiString(nCurrentOffset, nRemain, " obj", pPdStruct);

                if (nObjKeyword != -1) {
                    nCurrentOffset = nObjKeyword;

                    while ((nCurrentOffset > 0) && XBinary::isPdStructNotCanceled(pPdStruct)) {
                        quint8 nPrevChar = read_uint8(nCurrentOffset - 1);

                        if (!(((nPrevChar >= '0') && (nPrevChar <= '9')) || (nPrevChar == ' '))) {
                            break;
                        }

                        --nCurrentOffset;
                    }

                    // Guarantee forward progress: if the walk-back lands on (or before) the offset we
                    // already tried this iteration, resume past the matched " obj" instead of spinning.
                    if (nCurrentOffset <= nIterEntry) {
                        nCurrentOffset = nObjKeyword + 4;
                    }

                    bContinue = (nCurrentOffset < nEndBound);
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
    // 64 comfortably covers a full "NNNNNNNNNN GGGGG obj" header; the old 20-byte cap desynced on wide numbers.
    qint32 nSize = _readPDFString(*pnOffset, 64, pPdStruct).nSize;
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
        const quint8 nChar = read_uint8(nCurrentOffset);
        if ((nChar == ' ') || (nChar == 9) || (nChar == 12)) {
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

                        // No forward progress means we hit EOF: a hostile subsection count (e.g. "0 99999999999")
                        // would otherwise spin for billions of no-op iterations.
                        if (osObject.nSize <= 0) {
                            break;
                        }

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
            // The last (highest-offset) object ends at the xref table -- unless it physically lies past
            // the xref (linearized / early-xref layout), in which case bound it by the file size. Never negative.
            qint64 nEnd = pStartxref->nXrefOffset;
            if (nEnd <= listResult.last().nOffset) {
                nEnd = nTotalSize;
            }
            listResult.last().nSize = qMax<qint64>(0, nEnd - listResult.last().nOffset);
        }
    }

    return listResult;
}

void XPDF::scanStructure(PDSTRUCT *pPdStruct)
{
    if (m_bStructScanned) {
        return;
    }

    getHeaderOffset(pPdStruct);  // ensure the header offset is resolved and cached

    m_listStartHrefs = findStartxrefs(0, pPdStruct);
    m_listObjectsCache.clear();
    m_mapObjIdOffset.clear();

    const qint32 nNumberOfHrefs = m_listStartHrefs.count();
    QSet<qint64> stSeenOffsets;

    if (nNumberOfHrefs) {
        for (qint32 i = 0; (i < nNumberOfHrefs) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
            const STARTHREF &startxref = m_listStartHrefs.at(i);
            QList<OBJECT> listPart;

            if (startxref.bIsXref) {
                listPart = getObjectsFromStartxref(&startxref, pPdStruct);
            } else if (startxref.bIsObject) {
                bool bXrefStream = false;
                listPart = getObjectsFromXrefStream(startxref.nXrefOffset, &bXrefStream, pPdStruct);
                if (!bXrefStream) {
                    // Not a decodable cross-reference stream after all: fall back to the brute-force object scan.
                    listPart = findObjects(0, startxref.nFooterOffset, true, pPdStruct);
                }
            }

            const qint32 nCount = listPart.count();
            for (qint32 j = 0; j < nCount; ++j) {
                const OBJECT &o = listPart.at(j);
                if (!stSeenOffsets.contains(o.nOffset)) {
                    stSeenOffsets.insert(o.nOffset);
                    m_listObjectsCache.append(o);
                }
                // Later (newer) sections overwrite: the index resolves to the live revision, which is what
                // an indirect /Length lookup needs.
                m_mapObjIdOffset.insert(o.nID, o.nOffset);
            }
        }
    } else {
        m_listObjectsCache = findObjects(0, -1, false, pPdStruct);
        const qint32 nCount = m_listObjectsCache.count();
        for (qint32 j = 0; j < nCount; ++j) {
            m_mapObjIdOffset.insert(m_listObjectsCache.at(j).nID, m_listObjectsCache.at(j).nOffset);
        }
    }

    m_bStructScanned = true;
}

QList<XPDF::OBJECT> XPDF::getObjectsFromXrefStream(qint64 nXrefOffset, bool *pbIsXrefStream, PDSTRUCT *pPdStruct)
{
    QList<OBJECT> listResult;

    if (pbIsXrefStream) {
        *pbIsXrefStream = false;
    }

    const qint64 nFileSize = getSize();
    QSet<qint64> stVisited;
    QSet<quint64> stSeenIds;
    qint64 nCurrentXref = nXrefOffset;

    while ((nCurrentXref >= 0) && (nCurrentXref < nFileSize) && (!stVisited.contains(nCurrentXref)) && XBinary::isPdStructNotCanceled(pPdStruct)) {
        stVisited.insert(nCurrentXref);

        OS_STRING osHead = _readPDFString(nCurrentXref, 40, pPdStruct);
        if (!_isObject(osHead.sString)) {
            break;
        }

        XPART xpart = handleXpart(nCurrentXref, 0, -1, pPdStruct, true, &m_mapObjIdOffset);

        const QString sType = getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/Type"), pPdStruct).var.toString();
        if ((sType != QLatin1String("/XRef")) || xpart.listStreams.isEmpty()) {
            break;
        }

        // /W [ a b c ] field widths.
        qint32 nW0 = 0;
        qint32 nW1 = 0;
        qint32 nW2 = 0;
        {
            const qint32 nParts = xpart.listParts.count();
            const qint32 nWPos = xpart.listParts.indexOf(QLatin1String("/W"));
            if ((nWPos != -1) && ((nWPos + 1) < nParts) && (xpart.listParts.at(nWPos + 1) == QLatin1String("["))) {
                QList<qint32> listW;
                for (qint32 q = nWPos + 2; q < nParts; ++q) {
                    if (xpart.listParts.at(q) == QLatin1String("]")) {
                        break;
                    }
                    listW.append(xpart.listParts.at(q).toInt());
                }
                if (listW.count() >= 3) {
                    nW0 = listW.at(0);
                    nW1 = listW.at(1);
                    nW2 = listW.at(2);
                }
            }
        }

        const qint32 nRowLen = nW0 + nW1 + nW2;
        // Field widths are byte counts; anything over 8 is malformed (an offset never needs >8 bytes).
        if ((nRowLen <= 0) || (nW0 < 0) || (nW1 < 0) || (nW2 < 0) || (nW0 > 8) || (nW1 > 8) || (nW2 > 8)) {
            break;
        }

        const qint64 nSizeField = getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/Size"), pPdStruct).var.toLongLong();

        // /Index [ start count start count ... ] (default: [ 0 Size ]).
        QList<qint64> listIndex;
        {
            const qint32 nParts = xpart.listParts.count();
            const qint32 nIdxPos = xpart.listParts.indexOf(QLatin1String("/Index"));
            if ((nIdxPos != -1) && ((nIdxPos + 1) < nParts) && (xpart.listParts.at(nIdxPos + 1) == QLatin1String("["))) {
                for (qint32 q = nIdxPos + 2; q < nParts; ++q) {
                    if (xpart.listParts.at(q) == QLatin1String("]")) {
                        break;
                    }
                    listIndex.append(xpart.listParts.at(q).toLongLong());
                }
            }
        }
        if (listIndex.count() < 2) {
            listIndex.clear();
            listIndex.append(0);
            listIndex.append((nSizeField > 0) ? nSizeField : 0);
        }

        // Decompress the cross-reference stream body and undo any predictor.
        const QStringList listFilters = _resolveFilterList(&(xpart.listParts), QLatin1String("/Filter"), pPdStruct);
        const QString sFilter = listFilters.isEmpty() ? QString() : listFilters.last();
        const HANDLE_METHOD hm = pdfFilterToHandleMethod(sFilter);

        qint32 nPredictor = 1;
        qint32 nColumns = 1;
        qint32 nColors = 1;
        qint32 nBpc = 8;
        {
            const XVARIANT vP = getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/Predictor"), pPdStruct);
            if (!vP.var.isNull()) nPredictor = vP.var.toInt();
            const XVARIANT vC = getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/Columns"), pPdStruct);
            if (!vC.var.isNull()) nColumns = vC.var.toInt();
            const XVARIANT vCo = getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/Colors"), pPdStruct);
            if (!vCo.var.isNull()) nColors = vCo.var.toInt();
            const XVARIANT vB = getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/BitsPerComponent"), pPdStruct);
            if (!vB.var.isNull()) nBpc = vB.var.toInt();
        }

        const STREAM &stream = xpart.listStreams.at(0);
        QByteArray baData;
        if ((stream.nSize > 0) && (stream.nSize <= (nFileSize - stream.nOffset))) {
            QByteArray baRaw = read_array(stream.nOffset, stream.nSize);
            if (hm == HANDLE_METHOD_STORE) {
                baData = baRaw;
            } else {
                decompressPdfBuffer(baRaw, hm, -1, &baData, pPdStruct);
            }
        }

        if (nPredictor >= 2) {
            _applyPredictor(&baData, nPredictor, nColumns, nColors, nBpc);
        }

        const quint8 *pData = reinterpret_cast<const quint8 *>(baData.constData());
        const qint32 nDataSize = baData.size();
        qint64 nRow = 0;
        const qint32 nPairs = listIndex.count() / 2;

        for (qint32 s = 0; (s < nPairs) && XBinary::isPdStructNotCanceled(pPdStruct); ++s) {
            const qint64 nStartId = listIndex.at(s * 2);
            const qint64 nCountEntries = listIndex.at(s * 2 + 1);

            for (qint64 k = 0; k < nCountEntries; ++k) {
                if (((nRow + 1) * nRowLen) > nDataSize) {
                    break;
                }

                const qint64 nBase = nRow * nRowLen;
                const quint64 nField1 = (nW0 == 0) ? 1 : readBEValue(pData, nBase, nW0);           // entry type (default 1)
                const quint64 nField2 = readBEValue(pData, nBase + nW0, nW1);                       // offset (type 1) / objstm id (type 2)
                const quint64 nObjId = static_cast<quint64>(nStartId + k);

                if (nField1 == 1) {
                    const qint64 nObjOffset = static_cast<qint64>(nField2);
                    if ((nObjOffset > 0) && (nObjOffset < nFileSize) && (!stSeenIds.contains(nObjId))) {
                        OBJECT obj;
                        obj.nID = nObjId;
                        obj.nOffset = nObjOffset;
                        obj.nSize = 0;
                        listResult.append(obj);
                        stSeenIds.insert(nObjId);
                    }
                }
                // Type 0 (free) and type 2 (compressed in an object stream) carry no direct file offset.

                ++nRow;
            }
        }

        const XVARIANT vPrev = getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/Prev"), pPdStruct);
        nCurrentXref = (!vPrev.var.isNull()) ? vPrev.var.toLongLong() : -1;
    }

    // Only claim the file was handled as an xref stream when we actually recovered objects; otherwise
    // (corrupt/empty/undecodable body) the caller must still run the brute-force scan, which is always safe.
    if (pbIsXrefStream) {
        *pbIsXrefStream = !listResult.isEmpty();
    }

    // Order by file offset and derive per-object sizes (next-minus-this); the last stays 0 (unknown).
    if (!listResult.isEmpty()) {
        QMap<qint64, quint64> mapByOffset;
        const qint32 nCount = listResult.count();
        for (qint32 i = 0; i < nCount; ++i) {
            mapByOffset.insert(listResult.at(i).nOffset, listResult.at(i).nID);
        }

        listResult.clear();
        QMapIterator<qint64, quint64> iterator(mapByOffset);
        while (iterator.hasNext()) {
            iterator.next();
            OBJECT obj;
            obj.nOffset = iterator.key();
            obj.nID = iterator.value();
            obj.nSize = 0;
            listResult.append(obj);
        }

        const qint32 nFinal = listResult.count();
        for (qint32 i = 0; i < (nFinal - 1); ++i) {
            listResult[i].nSize = listResult[i + 1].nOffset - listResult[i].nOffset;
        }
    }

    return listResult;
}

bool XPDF::_applyPredictor(QByteArray *pbaData, qint32 nPredictor, qint32 nColumns, qint32 nColors, qint32 nBitsPerComponent)
{
    if (!pbaData) {
        return false;
    }

    if (nPredictor < 2) {
        return true;  // predictor 1 / none: nothing to undo
    }

    if ((nColumns <= 0) || (nColors <= 0) || (nBitsPerComponent <= 0)) {
        return false;
    }

    // Compute in 64-bit to avoid signed overflow on a hostile /Columns, then bound to a sane row size.
    const qint64 nBppWide = ((static_cast<qint64>(nColors) * nBitsPerComponent) + 7) / 8;
    const qint64 nRowLenWide = ((static_cast<qint64>(nColumns) * nColors * nBitsPerComponent) + 7) / 8;
    if ((nRowLenWide <= 0) || (nRowLenWide > (64 * 1024 * 1024))) {
        return false;
    }

    const qint32 nBpp = qMax<qint32>(1, static_cast<qint32>(nBppWide));
    const qint32 nRowLen = static_cast<qint32>(nRowLenWide);

    const QByteArray baIn = *pbaData;

    if (nPredictor == 2) {
        // TIFF predictor 2 (horizontal differencing); 8-bit components only.
        if (nBitsPerComponent != 8) {
            return false;
        }

        QByteArray baOut = baIn;
        const qint32 nRows = baIn.size() / nRowLen;
        for (qint32 r = 0; r < nRows; ++r) {
            const qint32 nRowBase = r * nRowLen;
            for (qint32 c = nBpp; c < nRowLen; ++c) {
                baOut[nRowBase + c] = static_cast<char>(static_cast<quint8>(baOut[nRowBase + c]) + static_cast<quint8>(baOut[nRowBase + c - nBpp]));
            }
        }

        *pbaData = baOut;
        return true;
    }

    // PNG predictors (>= 10): each row is prefixed by a one-byte filter type.
    const qint32 nStride = nRowLen + 1;
    const qint32 nRows = baIn.size() / nStride;

    QByteArray baOut;
    baOut.resize(nRows * nRowLen);

    QByteArray baPrev(nRowLen, '\0');

    for (qint32 r = 0; r < nRows; ++r) {
        const qint32 nInBase = r * nStride;
        const qint32 nFilter = static_cast<quint8>(baIn.at(nInBase));

        QByteArray baRow(nRowLen, '\0');
        for (qint32 c = 0; c < nRowLen; ++c) {
            baRow[c] = baIn.at(nInBase + 1 + c);
        }

        for (qint32 c = 0; c < nRowLen; ++c) {
            const qint32 nA = (c >= nBpp) ? static_cast<quint8>(baRow.at(c - nBpp)) : 0;
            const qint32 nB = static_cast<quint8>(baPrev.at(c));
            const qint32 nC = (c >= nBpp) ? static_cast<quint8>(baPrev.at(c - nBpp)) : 0;
            const qint32 nX = static_cast<quint8>(baRow.at(c));
            qint32 nV = nX;

            if (nFilter == 1) {
                nV = nX + nA;
            } else if (nFilter == 2) {
                nV = nX + nB;
            } else if (nFilter == 3) {
                nV = nX + ((nA + nB) >> 1);
            } else if (nFilter == 4) {
                const qint32 nP = nA + nB - nC;
                const qint32 nPa = qAbs(nP - nA);
                const qint32 nPb = qAbs(nP - nB);
                const qint32 nPc = qAbs(nP - nC);
                qint32 nPred = nA;
                if ((nPa <= nPb) && (nPa <= nPc)) {
                    nPred = nA;
                } else if (nPb <= nPc) {
                    nPred = nB;
                } else {
                    nPred = nC;
                }
                nV = nX + nPred;
            }

            baRow[c] = static_cast<char>(nV & 0xFF);
        }

        for (qint32 c = 0; c < nRowLen; ++c) {
            baOut[(r * nRowLen) + c] = baRow.at(c);
        }

        baPrev = baRow;
    }

    *pbaData = baOut;
    return true;
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
            if ((c == ' ') || (c == 9) || (c == 12)) {
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
    qint32 nDepth = 0;  // balanced nested-parenthesis depth (ISO 32000 7.3.4.2)

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
            } else if ((nChar == '(') && (!bBSlash)) {
                ++nDepth;
                result.sString.append('(');
            } else if ((nChar == ')') && (!bBSlash)) {
                result.sString.append(')');
                if (nDepth > 0) {
                    --nDepth;
                } else {
                    bEnd = true;
                }
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

            if (((nWord >> 8) == '(') && (!bBSlash)) {
                ++nDepth;
                result.sString.append('(');
                ++result.nSize;
            } else if (((nWord >> 8) == ')') && (!bBSlash)) {
                result.sString.append(')');
                ++result.nSize;
                if (nDepth > 0) {
                    --nDepth;
                } else {
                    bEnd = true;
                }
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

        if ((nChar == ' ') || (nChar == 9) || (nChar == 12)) {
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

            if (nNextSize <= 0) {
                break;
            }

            qint32 nNextPos = 0;
            for (; nNextPos < nNextSize; ++nNextPos) {
                if (static_cast<quint8>(pNext[nNextPos]) == '>') {
                    ++nNextPos;
                    break;
                }
            }

            result.sString.append(QString::fromLatin1(pNext, nNextPos));
            result.nSize += nNextPos;

            if ((nNextPos < nNextSize) || ((nNextPos > 0) && (static_cast<quint8>(pNext[nNextPos - 1]) == '>'))) {
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
    } else {
        // Any mode not advertised by getMapModesList still yields a valid single-region map instead of a blank one.
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

XPDF::XPART XPDF::handleXpart(qint64 nOffset, qint32 nID, qint32 nPartLimit, PDSTRUCT *pPdStruct, bool bResolveStreams, const QMap<quint64, qint64> *pMapObjOffsets)
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

                const QString &sTok = osStringPart.sString;

                // Dictionary/array nesting via exact delimiter tokens (matching only "<<"/">>" avoids
                // an empty hex string "<>" being mistaken for a dictionary open).
                if (sTok == QLatin1String("<<")) {
                    ++nObj;
                } else if (sTok == QLatin1String(">>")) {
                    --nObj;
                } else if (sTok == QLatin1String("[")) {
                    ++nCol;
                } else if (sTok == QLatin1String("]")) {
                    --nCol;
                }

                // Capture the token following /Length regardless of its shape (fixes 1-2 digit direct
                // lengths and the fused indirect "N G R" form, which the old length/depth chain skipped).
                if (bLength) {
                    bLength = false;
                    sLength = sTok;
                } else if (sTok == QLatin1String("/Length")) {
                    bLength = true;
                }

                if ((nObj == 0) && (nCol == 0)) {
                    break;
                }
            }

            if (bStop) {
                break;
            }
        } else if (osString.sString == QLatin1String("stream")) {
            // Dict-only callers (getParts -> getValuesByKey) have everything they need once the
            // dictionary is captured; skip the expensive stream body sizing entirely.
            if (!bResolveStreams) {
                break;
            }

            STREAM stream = {};
            stream.nOffset = nOffset;
            bool bHasStreamSize = false;

            bool bIndirectLength = false;

            if (_isIndirectRef(sLength)) {
                // Fused "N G R" token -- the common generation-0 case (e.g. "/Length 128 0 R"), which the
                // old split-token matcher never recognised, silently dropping the stream and derailing parsing.
                bIndirectLength = true;
            } else {
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
            }

            if (!bIndirectLength) {
                bool bOk = false;
                qint64 nStreamSize = sLength.toLongLong(&bOk);

                if (bOk && (nStreamSize >= 0)) {
                    stream.nSize = nStreamSize;
                    bHasStreamSize = true;
                }
            } else if (sLength.section(" ", 2, 2) == QLatin1String("R")) {
                quint64 nRefId = static_cast<quint64>(sLength.section(" ", 0, 0).toLongLong());
                qint64 nObjectOffset = resolveObjectOffset(nRefId, pMapObjOffsets, nOffset, pPdStruct);

                if (nObjectOffset != -1) {
                    qint64 nTmp = nObjectOffset;
                    skipPDFString(&nTmp, pPdStruct);
                    OS_STRING osLen = _readPDFStringPart_val(nTmp, pPdStruct);

                    bool bOk = false;
                    qint64 nStreamSize = osLen.sString.toLongLong(&bOk);

                    if (bOk && (nStreamSize >= 0)) {
                        stream.nSize = nStreamSize;
                        bHasStreamSize = true;
                    }
                }
            }

            // Validate the declared size against the real 'endstream' keyword. A wrong, overshooting or
            // unresolved /Length is repaired from 'endstream' instead of derailing the object parse.
            if (bHasStreamSize) {
                if (stream.nSize > (nFileSize - stream.nOffset)) {
                    bHasStreamSize = false;  // overshoot
                } else {
                    qint64 nProbe = stream.nOffset + stream.nSize;
                    qint64 nWindow = qMin<qint64>(nFileSize - nProbe, 40);
                    if ((nWindow <= 0) || (find_ansiString(nProbe, nWindow, "endstream", pPdStruct) == -1)) {
                        bHasStreamSize = false;  // /Length does not line up with endstream
                    }
                }
            }

            if (!bHasStreamSize) {
                qint64 nEndStream = find_ansiString(stream.nOffset, nFileSize - stream.nOffset, "endstream", pPdStruct);

                if (nEndStream != -1) {
                    qint64 nBodyEnd = nEndStream;
                    if ((nBodyEnd > stream.nOffset) && (read_uint8(nBodyEnd - 1) == 10)) --nBodyEnd;
                    if ((nBodyEnd > stream.nOffset) && (read_uint8(nBodyEnd - 1) == 13)) --nBodyEnd;

                    stream.nSize = nBodyEnd - stream.nOffset;
                    bHasStreamSize = true;

                    nOffset = nEndStream + 9;  // length of "endstream"
                    skipPDFEnding(&nOffset, pPdStruct);
                    result.listStreams.append(stream);
                    continue;
                } else {
                    break;
                }
            }

            nOffset += stream.nSize;
            skipPDFEnding(&nOffset, pPdStruct);
            result.listStreams.append(stream);
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

qint64 XPDF::resolveObjectOffset(quint64 nID, const QMap<quint64, qint64> *pMapObjOffsets, qint64 nHint, PDSTRUCT *pPdStruct)
{
    // Fast path: the object-number -> offset index built once per structural scan (O(log K)).
    if (pMapObjOffsets && pMapObjOffsets->contains(nID)) {
        return pMapObjOffsets->value(nID);
    }

    // Fallback: a bounded scan for "N 0 obj" near the hint (forward, then a bounded window before it).
    // The old code ended with an unconditional find_ansiString(0,-1) per stream, which is O(K*N).
    const qint64 nFileSize = getSize();
    const qint64 nHintClamped = qBound<qint64>(0, nHint, nFileSize);

    qint64 nForwardSize = qMin<qint64>(1048576, nFileSize - nHintClamped);
    qint64 nFound = findObjectHeaderInRange(nID, nHintClamped, nForwardSize, pPdStruct);

    if (nFound == -1) {
        const qint64 nBackStart = qMax<qint64>(0, nHintClamped - 1048576);
        const qint64 nBackSize = qMax<qint64>(0, nHintClamped - nBackStart);
        nFound = findObjectHeaderInRange(nID, nBackStart, nBackSize, pPdStruct);
    }

    return nFound;
}

qint64 XPDF::findObjectHeaderInRange(quint64 nID, qint64 nStart, qint64 nSize, PDSTRUCT *pPdStruct)
{
    if (nSize <= 0) {
        return -1;
    }

    const QString sNeedle = QString::number(nID) + " 0 obj";
    const qint64 nEnd = nStart + nSize;
    qint64 nSearchFrom = nStart;

    while (XBinary::isPdStructNotCanceled(pPdStruct)) {
        const qint64 nRemain = nEnd - nSearchFrom;
        if (nRemain <= 0) {
            break;
        }

        const qint64 nHit = find_ansiString(nSearchFrom, nRemain, sNeedle, pPdStruct);
        if (nHit == -1) {
            break;
        }

        // Reject a prefix match: the object-number digits must not be preceded by another digit
        // (so id 12 does not match inside "112 0 obj").
        bool bBoundary = (nHit == 0);
        if (!bBoundary) {
            const quint8 nPrev = read_uint8(nHit - 1);
            bBoundary = !((nPrev >= '0') && (nPrev <= '9'));
        }

        if (bBoundary) {
            return nHit;
        }

        nSearchFrom = nHit + 1;  // step past the false match and keep looking within the window
    }

    return -1;
}

bool XPDF::_isObject(const QString &sString)
{
    // Recognise an object header "N G obj" at the start of the string, even when the dictionary
    // is glued to the keyword on the same physical line ("1 0 obj<</Type/Page>>"). Rejects "endobj".
    const qint32 nLen = sString.size();
    qint32 nI = 0;

    while ((nI < nLen) && ((sString.at(nI) == QChar(' ')) || (sString.at(nI) == QChar('\t')))) ++nI;

    qint32 nDigits = 0;
    while ((nI < nLen) && (sString.at(nI) >= QChar('0')) && (sString.at(nI) <= QChar('9'))) {
        ++nI;
        ++nDigits;
    }
    if (nDigits == 0) return false;

    if ((nI >= nLen) || !((sString.at(nI) == QChar(' ')) || (sString.at(nI) == QChar('\t')))) return false;
    while ((nI < nLen) && ((sString.at(nI) == QChar(' ')) || (sString.at(nI) == QChar('\t')))) ++nI;

    nDigits = 0;
    while ((nI < nLen) && (sString.at(nI) >= QChar('0')) && (sString.at(nI) <= QChar('9'))) {
        ++nI;
        ++nDigits;
    }
    if (nDigits == 0) return false;

    if ((nI >= nLen) || !((sString.at(nI) == QChar(' ')) || (sString.at(nI) == QChar('\t')))) return false;
    while ((nI < nLen) && ((sString.at(nI) == QChar(' ')) || (sString.at(nI) == QChar('\t')))) ++nI;

    if ((nI + 3) > nLen) return false;
    if (!((sString.at(nI) == QChar('o')) && (sString.at(nI + 1) == QChar('b')) && (sString.at(nI + 2) == QChar('j')))) return false;
    nI += 3;

    if (nI < nLen) {
        const QChar cNext = sString.at(nI);
        const bool bBoundary = (cNext == QChar(' ')) || (cNext == QChar('\t')) || (cNext == QChar('\r')) || (cNext == QChar('\n')) || (cNext == QChar('<')) ||
                               (cNext == QChar('[')) || (cNext == QChar('(')) || (cNext == QChar('/')) || (cNext == QChar('%'));
        if (!bBoundary) return false;
    }

    return true;
}

bool XPDF::_isIndirectRef(const QString &sString)
{
    // "N G R" fused into a single token (e.g. "128 0 R").
    if (sString.isEmpty()) return false;
    if (!((sString.at(0) >= QChar('0')) && (sString.at(0) <= QChar('9')))) return false;
    if (sString.section(QChar(' '), 2, 2) != QLatin1String("R")) return false;

    bool bOkId = false;
    bool bOkGen = false;
    sString.section(QChar(' '), 0, 0).toLongLong(&bOkId);
    sString.section(QChar(' '), 1, 1).toLongLong(&bOkGen);

    return bOkId && bOkGen;
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

    // PDF date: (D:YYYYMMDDHHmmSSOHH'mm') where O in {+,-,Z}; everything after the year is optional.
    QString sDate = sString.section(QChar(':'), 1, -1);
    sDate = sDate.section(QChar(')'), 0, 0).trimmed();

    qint32 nTzSign = 0;
    qint32 nTzHour = 0;
    qint32 nTzMin = 0;
    qint32 nTzPos = -1;

    for (qint32 i = 0; i < sDate.size(); ++i) {
        const QChar c = sDate.at(i);
        if ((c == QChar('+')) || (c == QChar('-')) || (c == QChar('Z')) || (c == QChar('z'))) {
            nTzPos = i;
            break;
        }
    }

    QString sDigitsPart = sDate;

    if (nTzPos != -1) {
        sDigitsPart = sDate.left(nTzPos);
        const QChar cSign = sDate.at(nTzPos);
        if (cSign == QChar('+')) {
            nTzSign = 1;
        } else if (cSign == QChar('-')) {
            nTzSign = -1;
        }
        QString sTz = sDate.mid(nTzPos + 1);
        sTz.remove(QChar('\''));
        if (sTz.size() >= 2) nTzHour = sTz.left(2).toInt();
        if (sTz.size() >= 4) nTzMin = sTz.mid(2, 2).toInt();
    }

    QString sClean;
    for (qint32 i = 0; i < sDigitsPart.size(); ++i) {
        if (sDigitsPart.at(i).isDigit()) {
            sClean.append(sDigitsPart.at(i));
        }
    }

    if (sClean.size() < 4) {
        return result;
    }

    const qint32 nYear = sClean.mid(0, 4).toInt();
    qint32 nMonth = (sClean.size() >= 6) ? sClean.mid(4, 2).toInt() : 1;
    qint32 nDay = (sClean.size() >= 8) ? sClean.mid(6, 2).toInt() : 1;
    const qint32 nHour = (sClean.size() >= 10) ? sClean.mid(8, 2).toInt() : 0;
    const qint32 nMin = (sClean.size() >= 12) ? sClean.mid(10, 2).toInt() : 0;
    const qint32 nSec = (sClean.size() >= 14) ? sClean.mid(12, 2).toInt() : 0;

    if (nMonth < 1) nMonth = 1;
    if (nMonth > 12) nMonth = 12;
    if (nDay < 1) nDay = 1;
    if (nDay > 31) nDay = 31;

    const QDate date(nYear, nMonth, nDay);
    const QTime time(qBound(0, nHour, 23), qBound(0, nMin, 59), qBound(0, nSec, 59));

    if (!date.isValid() || !time.isValid()) {
        return result;
    }

    const qint32 nOffsetSec = nTzSign * ((nTzHour * 3600) + (nTzMin * 60));
    result = QDateTime(date, time, QTimeZone(nOffsetSec));  // fixed UTC-offset zone (available on Qt5 and Qt6)

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

    bool bOk = false;
    const qlonglong ll = sValue.toLongLong(&bOk);  // use the ok flag: literal "0" is a valid integer, not a fallthrough

    if (bOk) {
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
    } else if ((sValue == QLatin1String("true")) || (sValue == QLatin1String("false"))) {
        varValue.varType = XBinary::VT_VALUE;
        varValue.var = sValue;
    } else {
        bool bIsReal = false;
        const double dValue = sValue.toDouble(&bIsReal);
        if (bIsReal) {
            varValue.varType = XBinary::VT_DOUBLE;
            varValue.var = dValue;
        } else {
            varValue.varType = XBinary::VT_VALUE;
            varValue.var = sValue;
        }
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

QStringList XPDF::_resolveFilterList(const QList<QString> *pListParts, const QString &sKey, PDSTRUCT *pPdStruct)
{
    QStringList listResult;

    const qint32 nCount = pListParts->count();

    for (qint32 i = 0; (i + 1 < nCount) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
        if (pListParts->at(i) == sKey) {
            const QString &sNext = pListParts->at(i + 1);

            if (sNext == QLatin1String("[")) {
                for (qint32 q = i + 2; q < nCount; ++q) {
                    const QString &sTok = pListParts->at(q);
                    if (sTok == QLatin1String("]")) {
                        break;
                    }
                    if (sTok.startsWith(QLatin1Char('/'))) {
                        listResult.append(sTok);
                    }
                }
            } else if (sNext.startsWith(QLatin1Char('/'))) {
                listResult.append(sNext);
            }

            break;  // the first /Filter entry wins
        }
    }

    return listResult;
}

QString XPDF::_resolveFilespecName(const QList<QString> *pListParts, PDSTRUCT *pPdStruct)
{
    Q_UNUSED(pPdStruct)

    // Only a genuine file specification (carrying an embedded-file /EF dict) has a name worth surfacing.
    if (!pListParts->contains(QLatin1String("/EF"))) {
        return QString();
    }

    QString sName;
    const qint32 nCount = pListParts->count();

    for (qint32 i = 0; (i + 1) < nCount; ++i) {
        const QString &sKey = pListParts->at(i);
        if ((sKey == QLatin1String("/UF")) || (sKey == QLatin1String("/F"))) {
            const QString &sVal = pListParts->at(i + 1);
            if (_isString(sVal)) {  // the plain-string form is the file name; the /EF /F is an indirect ref, not a string
                const QString sCandidate = _getString(sVal);
                if (!sCandidate.isEmpty()) {
                    sName = sCandidate;
                    if (sKey == QLatin1String("/UF")) {
                        break;  // prefer the Unicode name
                    }
                }
            }
        }
    }

    return sName;
}

qint32 XPDF::_resolveEmbeddedRefId(const QList<QString> *pListParts, PDSTRUCT *pPdStruct)
{
    Q_UNUSED(pPdStruct)

    const qint32 nCount = pListParts->count();
    const qint32 nEfPos = pListParts->indexOf(QLatin1String("/EF"));
    if (nEfPos == -1) {
        return -1;
    }

    for (qint32 i = nEfPos + 1; (i + 1) < nCount; ++i) {
        const QString &sKey = pListParts->at(i);
        if ((sKey == QLatin1String("/F")) || (sKey == QLatin1String("/UF")) || (sKey == QLatin1String("/DOS")) || (sKey == QLatin1String("/Mac")) ||
            (sKey == QLatin1String("/Unix"))) {
            const QString &sVal = pListParts->at(i + 1);
            if (_isIndirectRef(sVal)) {
                return static_cast<qint32>(sVal.section(QChar(' '), 0, 0).toLongLong());
            }
        }
        if (sKey == QLatin1String(">>")) {
            break;  // end of the /EF dictionary
        }
    }

    return -1;
}

QList<XPDF::XPART> XPDF::getParts(qint32 nPartLimit, PDSTRUCT *pPdStruct)
{
    QList<XPART> listResult;

    scanStructure(pPdStruct);

    const qint32 nNumberOfObjects = m_listObjectsCache.count();

    if (nNumberOfObjects) {
        const qint32 _nFreeIndex = XBinary::getFreeIndex(pPdStruct);
        XBinary::setPdStructInit(pPdStruct, _nFreeIndex, nNumberOfObjects);

        for (qint32 i = 0; (i < nNumberOfObjects) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
            const OBJECT &record = m_listObjectsCache.at(i);

            // Dict-only: getParts consumers read listParts via getValuesByKey; stream bodies are not needed,
            // so skip the expensive per-stream /Length resolution entirely.
            XPART xpart = handleXpart(record.nOffset, record.nID, nPartLimit, pPdStruct, false, &m_mapObjIdOffset);

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
    qint64 nCurrentOffset = getHeaderOffset(pPdStruct);
    if (nCurrentOffset < 0) {
        nCurrentOffset = 0;
    }

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
    QList<XPART> listParts = getParts(100, pPdStruct);

    // Resolve each object's /Filter (single name or array) into clean, de-duplicated filter names.
    QSet<QString> stFilters;

    const qint32 nCount = listParts.count();
    for (qint32 i = 0; (i < nCount) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
        const QStringList listResolved = _resolveFilterList(&(listParts.at(i).listParts), QLatin1String("/Filter"), pPdStruct);
        for (qint32 f = 0; f < listResolved.count(); ++f) {
            stFilters.insert(listResolved.at(f));
        }
    }

    QStringList listFilterNames = stFilters.values();
    listFilterNames.sort();

    return listFilterNames.join(QLatin1String(", "));
}

QString XPDF::getInfo(PDSTRUCT *pPdStruct)
{
    QStringList listLines;

    // Parse the document once (dictionary tokens only; stream bodies are resolved lazily where needed).
    QList<XPART> listObjects = getParts(256, pPdStruct);

    // Filters actually used across the document.
    {
        QSet<QString> stFilters;
        const qint32 nCount = listObjects.count();
        for (qint32 i = 0; (i < nCount) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
            const QStringList listFilters = _resolveFilterList(&(listObjects.at(i).listParts), QLatin1String("/Filter"), pPdStruct);
            for (qint32 f = 0; f < listFilters.count(); ++f) {
                stFilters.insert(listFilters.at(f));
            }
        }
        if (!stFilters.isEmpty()) {
            QStringList listFilterNames = stFilters.values();
            listFilterNames.sort();
            listLines << (tr("Filters") + ": " + listFilterNames.join(QLatin1String(", ")));
        }
    }

    // Encryption first: when the document is encrypted, the Info-dictionary strings are ciphertext, so
    // showing them as "metadata" is misleading noise -- suppress them and say so instead.
    const QString sEncryption = getEncryptionInfoString(&listObjects, pPdStruct);
    if (!sEncryption.isEmpty()) {
        listLines << (tr("Encrypted") + ": " + sEncryption);
    }

    if (sEncryption.isEmpty()) {
        const QString sMeta = getMetaInfoString(&listObjects, pPdStruct);
        if (!sMeta.isEmpty()) {
            listLines << sMeta;
        }
    } else if (setupDecryption(QByteArray(), pPdStruct)) {
        // Owner-only-protected PDFs open with an empty user password: recover the metadata.
        listLines << (tr("Decrypted") + " (" + tr("empty user password") + ")");
        const QString sDecMeta = getDecryptedMetaInfoString(&listObjects, pPdStruct);
        if (!sDecMeta.isEmpty()) {
            listLines << sDecMeta;
        }
    } else {
        listLines << (tr("Metadata") + ": " + tr("encrypted"));
    }

    const QString sLinearized = getLinearizedInfoString(&listObjects, pPdStruct);
    if (!sLinearized.isEmpty()) {
        listLines << sLinearized;
    }

    const QString sSuspicious = getSuspiciousInfoString(&listObjects, pPdStruct);
    if (!sSuspicious.isEmpty()) {
        listLines << sSuspicious;
    }

    const QString sJavaScript = getJavaScriptInfoString(&listObjects, pPdStruct);
    if (!sJavaScript.isEmpty()) {
        listLines << sJavaScript;
    }

    // Flatten to a single line: getInfo() feeds the DIE detection tag / FILEFORMATINFO::sInfo, where an
    // embedded newline would break the "PDF(ver)[...]" rendering.
    QString sResult = listLines.join(QLatin1String("; "));
    sResult.replace(QLatin1Char('\n'), QLatin1String("; "));
    return sResult;
}

QString XPDF::getJavaScriptInfoString(QList<XPART> *pListObjects, PDSTRUCT *pPdStruct)
{
#ifdef USE_XJS
    QStringList listScripts;

    const qint32 nCount = pListObjects->count();
    for (qint32 i = 0; (i < nCount) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
        const XPART &part = pListObjects->at(i);
        const qint32 nParts = part.listParts.count();

        for (qint32 j = 0; (j + 1) < nParts; ++j) {
            if (part.listParts.at(j) != QLatin1String("/JS")) {
                continue;
            }

            const QString &sVal = part.listParts.at(j + 1);
            QString sJs;

            if (_isString(sVal)) {
                sJs = _getString(sVal);
            } else if (_isHex(sVal)) {
                sJs = QString::fromLatin1(QByteArray::fromHex(_getHex(sVal).toLatin1()));
            } else if (_isIndirectRef(sVal)) {
                // /JS points at an indirect object whose stream holds the script.
                const quint64 nId = static_cast<quint64>(sVal.section(QChar(' '), 0, 0).toLongLong());
                const qint64 nOffset = resolveObjectOffset(nId, &m_mapObjIdOffset, part.nOffset, pPdStruct);
                if (nOffset != -1) {
                    XPART jsPart = handleXpart(nOffset, static_cast<qint32>(nId), -1, pPdStruct, true, &m_mapObjIdOffset);
                    if (!jsPart.listStreams.isEmpty()) {
                        const STREAM &stream = jsPart.listStreams.at(0);
                        if ((stream.nSize > 0) && (stream.nSize <= (getSize() - stream.nOffset))) {
                            QByteArray baRaw = read_array(stream.nOffset, stream.nSize);
                            const QStringList listFilters = _resolveFilterList(&(jsPart.listParts), QLatin1String("/Filter"), pPdStruct);
                            QByteArray baDecoded;
                            // Apply the full filter chain so cascaded (e.g. ASCII85+Flate) scripts decode.
                            if (listFilters.isEmpty() || !decompressPdfChain(baRaw, listFilters, &baDecoded, pPdStruct)) {
                                baDecoded = baRaw;
                            }
                            sJs = QString::fromLatin1(baDecoded);
                        }
                    }
                }
            }

            if (!sJs.trimmed().isEmpty() && !listScripts.contains(sJs)) {
                listScripts.append(sJs);
            }
        }
    }

    // JavaScript is frequently packed inside a compressed object stream (/ObjStm). Decompress those and
    // pull inline /JS strings out of the decoded bytes so ObjStm-hidden scripts are emulated too.
    for (qint32 i = 0; (i < nCount) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
        const XPART &part = pListObjects->at(i);
        if (!part.listParts.contains(QLatin1String("/ObjStm"))) {
            continue;
        }

        XPART full = handleXpart(part.nOffset, part.nID, -1, pPdStruct, true, &m_mapObjIdOffset);
        if (full.listStreams.isEmpty()) {
            continue;
        }

        const STREAM &stream = full.listStreams.at(0);
        if ((stream.nSize <= 0) || (stream.nSize > (getSize() - stream.nOffset))) {
            continue;
        }

        QByteArray baRaw = read_array(stream.nOffset, stream.nSize);
        const QStringList listFilters = _resolveFilterList(&(full.listParts), QLatin1String("/Filter"), pPdStruct);
        QByteArray baDecoded;
        if (listFilters.isEmpty() || !decompressPdfChain(baRaw, listFilters, &baDecoded, pPdStruct)) {
            baDecoded = baRaw;
        }

        const QStringList listInner = extractJSStringsFromBuffer(baDecoded);
        for (qint32 k = 0; k < listInner.count(); ++k) {
            if (!listScripts.contains(listInner.at(k))) {
                listScripts.append(listInner.at(k));
            }
        }
    }

    if (listScripts.isEmpty()) {
        return QString();
    }

    QStringList listOut;
    XJSEmul emul;

    for (qint32 i = 0; (i < listScripts.count()) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
        const XJS::XJSReport report = emul.analyze(listScripts.at(i));

        for (qint32 k = 0; k < report.indicators.count(); ++k) {
            listOut << (tr("JavaScript") + ": " + report.indicators.at(k));
        }
        for (qint32 k = 0; k < report.evalArguments.count(); ++k) {
            QString sPayload = report.evalArguments.at(k);
            if (sPayload.length() > 200) {
                sPayload = sPayload.left(200) + QStringLiteral(" ...");
            }
            listOut << (tr("JavaScript payload") + ": " + sPayload);
        }
        for (qint32 k = 0; k < report.apiCalls.count(); ++k) {
            listOut << (tr("JavaScript API") + ": " + report.apiCalls.at(k));
        }
    }

    return listOut.join(QLatin1Char('\n'));
#else
    Q_UNUSED(pListObjects)
    Q_UNUSED(pPdStruct)
    return QString();
#endif
}

QString XPDF::getMetaInfoString(QList<XPART> *pListObjects, PDSTRUCT *pPdStruct)
{
    static const char *const sKeys[] = {"/Title", "/Author", "/Subject", "/Keywords", "/Creator", "/Producer", "/CreationDate", "/ModDate"};
    const qint32 nKeys = static_cast<qint32>(sizeof(sKeys) / sizeof(sKeys[0]));

    QStringList listLines;

    for (qint32 k = 0; (k < nKeys) && XBinary::isPdStructNotCanceled(pPdStruct); ++k) {
        const QString sKey = QString::fromLatin1(sKeys[k]);
        const QList<XVARIANT> listValues = getValuesByKey(pListObjects, sKey, pPdStruct);

        for (qint32 v = 0; v < listValues.count(); ++v) {
            const QString sValue = listValues.at(v).var.toString();
            if (!sValue.isEmpty()) {
                listLines << (sKey.mid(1) + ": " + sValue);
                break;  // first non-empty value per key
            }
        }
    }

    return listLines.join(QLatin1Char('\n'));
}

QString XPDF::getEncryptionInfoString(QList<XPART> *pListObjects, PDSTRUCT *pPdStruct)
{
    QString sResult;

    const qint32 nCount = pListObjects->count();

    for (qint32 i = 0; (i < nCount) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
        QList<QString> &listParts = (*pListObjects)[i].listParts;

        // The Standard security handler dictionary is identified by "/Filter /Standard".
        const qint32 nFilterPos = listParts.indexOf(QLatin1String("/Filter"));
        if ((nFilterPos == -1) || ((nFilterPos + 1) >= listParts.count()) || (listParts.at(nFilterPos + 1) != QLatin1String("/Standard"))) {
            continue;
        }

        qint32 nV = getFirstStringValueByKey(&listParts, QLatin1String("/V"), pPdStruct).var.toInt();
        const qint32 nR = getFirstStringValueByKey(&listParts, QLatin1String("/R"), pPdStruct).var.toInt();
        qint32 nLen = getFirstStringValueByKey(&listParts, QLatin1String("/Length"), pPdStruct).var.toInt();
        const XVARIANT vP = getFirstStringValueByKey(&listParts, QLatin1String("/P"), pPdStruct);
        QString sCFM = getFirstStringValueByKey(&listParts, QLatin1String("/CFM"), pPdStruct).var.toString();

        // /V can read 0 when its token falls past the binary /O//U strings; infer it from the revision.
        if (nV == 0) {
            if (nR >= 5) nV = 5;
            else if (nR == 4) nV = 4;
            else if (nR == 3) nV = 2;
            else if (nR == 2) nV = 1;
        }
        // The crypt-filter /Length is in BYTES (16=AES-128, 32=AES-256); the top-level /Length is in bits.
        // Normalise a small byte value to bits so the reported key length is correct.
        if ((nLen > 0) && (nLen <= 40)) {
            nLen = nLen * 8;
        }

        if (sCFM.startsWith(QLatin1Char('/'))) {
            sCFM = sCFM.mid(1);
        }
        if (sCFM.isEmpty()) {
            if (nV >= 5) {
                sCFM = QStringLiteral("AESV3");
            } else if (nV == 4) {
                sCFM = QStringLiteral("AESV2/RC4");
            } else {
                sCFM = QStringLiteral("RC4");
            }
        }

        // Return just the descriptor ("Standard V4 R4 128-bit AESV2 P=..."); callers add any label.
        sResult = QString("Standard V%1 R%2").arg(nV).arg(nR);
        if (nLen > 0) {
            sResult += QString(" %1-bit").arg(nLen);
        }
        sResult += QLatin1Char(' ') + sCFM;
        if (!vP.var.isNull()) {
            sResult += QString(" P=%1").arg(vP.var.toLongLong());
        }

        break;
    }

    return sResult;
}

QString XPDF::getEncryption(PDSTRUCT *pPdStruct)
{
    QList<XPART> listObjects = getParts(256, pPdStruct);
    return getEncryptionInfoString(&listObjects, pPdStruct);
}

bool XPDF::isEncrypted()
{
    // Overrides XBinary::isEncrypted() so FILEFORMATINFO::bIsEncrypted -> getFileFormatInfoString("[Encrypted]")
    // lights up across Formats, SpecAbstract (format record) and the DiE PDF signature automatically.
    return !getEncryption(nullptr).isEmpty();
}

QByteArray XPDF::_getRawBytesByKey(const QList<QString> *pListParts, const QString &sKey)
{
    const qint32 nCount = pListParts->count();
    for (qint32 i = 0; (i + 1) < nCount; ++i) {
        if (pListParts->at(i) == sKey) {
            const QString &sVal = pListParts->at(i + 1);
            if (_isString(sVal)) {
                return _getString(sVal).toLatin1();  // () literal content: bytes preserved as Latin1
            }
            if (_isHex(sVal)) {
                return QByteArray::fromHex(_getHex(sVal).toLatin1());
            }
            return QByteArray();
        }
    }
    return QByteArray();
}

QByteArray XPDF::findTrailerID(PDSTRUCT *pPdStruct)
{
    // /ID lives in the trailer dict: << ... /ID [ <hex1> <hex2> ] ... >>. Raw-scan for it (it is not an
    // enumerated object in classic-xref files). Returns the first ID element's bytes.
    const qint64 nFileSize = getSize();
    qint64 nFrom = 0;

    while (nFrom < nFileSize) {
        const qint64 nIdPos = find_ansiString(nFrom, nFileSize - nFrom, "/ID", pPdStruct);
        if (nIdPos == -1) {
            break;
        }

        const qint64 nChunk = qMin<qint64>(512, nFileSize - nIdPos);
        const QByteArray baChunk = read_array(nIdPos, nChunk);

        // Reject a longer name like "/IDTree": /ID must be followed by whitespace or '['.
        bool bBoundary = true;
        if (baChunk.size() > 3) {
            const char c = baChunk.at(3);
            bBoundary = (c == ' ') || (c == '\t') || (c == '\r') || (c == '\n') || (c == '[') || (c == '<');
        }

        if (bBoundary) {
            const qint32 nBr = baChunk.indexOf('[', 3);
            if (nBr != -1) {
                const qint32 nLt = baChunk.indexOf('<', nBr);
                if ((nLt != -1) && ((nLt + 1) < baChunk.size()) && (baChunk.at(nLt + 1) != '<')) {
                    const qint32 nGt = baChunk.indexOf('>', nLt);
                    if (nGt != -1) {
                        const QByteArray baID = QByteArray::fromHex(baChunk.mid(nLt + 1, nGt - nLt - 1).trimmed());
                        if (!baID.isEmpty()) {
                            return baID;
                        }
                    }
                }
            }
        }

        nFrom = nIdPos + 3;
    }

    return QByteArray();
}

bool XPDF::setupDecryption(const QByteArray &baUserPassword, PDSTRUCT *pPdStruct)
{
    if (m_bDecryptChecked) {
        return m_bDecryptReady;
    }
    m_bDecryptChecked = true;
    m_bDecryptReady = false;

    QList<XPART> listObjects = getParts(256, pPdStruct);

    const qint32 nCount = listObjects.count();
    for (qint32 i = 0; (i < nCount) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
        QList<QString> &listParts = listObjects[i].listParts;

        const qint32 nFilterPos = listParts.indexOf(QLatin1String("/Filter"));
        if ((nFilterPos == -1) || ((nFilterPos + 1) >= listParts.count()) || (listParts.at(nFilterPos + 1) != QLatin1String("/Standard"))) {
            continue;
        }

        XPDFCrypt::SECURITY security;
        security.nV = getFirstStringValueByKey(&listParts, QLatin1String("/V"), pPdStruct).var.toInt();
        security.nR = getFirstStringValueByKey(&listParts, QLatin1String("/R"), pPdStruct).var.toInt();
        security.nP = getFirstStringValueByKey(&listParts, QLatin1String("/P"), pPdStruct).var.toLongLong();
        security.baO = _getRawBytesByKey(&listParts, QLatin1String("/O"));
        security.baU = _getRawBytesByKey(&listParts, QLatin1String("/U"));
        security.baOE = _getRawBytesByKey(&listParts, QLatin1String("/OE"));
        security.baUE = _getRawBytesByKey(&listParts, QLatin1String("/UE"));

        QString sCFM = getFirstStringValueByKey(&listParts, QLatin1String("/CFM"), pPdStruct).var.toString();
        security.bAES = sCFM.contains(QLatin1String("AES"));
        security.bAES256 = (sCFM == QLatin1String("/AESV3")) || (security.nV >= 5);

        const QString sEncMeta = getFirstStringValueByKey(&listParts, QLatin1String("/EncryptMetadata"), pPdStruct).var.toString();
        security.bEncryptMetadata = (sEncMeta != QLatin1String("false"));

        const qint32 nLen = getFirstStringValueByKey(&listParts, QLatin1String("/Length"), pPdStruct).var.toInt();
        if (security.nR >= 5) {
            security.nKeyBytes = 32;
        } else if (nLen >= 40) {
            security.nKeyBytes = nLen / 8;  // /Length in bits (40/128)
        } else if (nLen > 0) {
            security.nKeyBytes = nLen;      // crypt-filter /Length already in bytes (5/16)
        } else {
            security.nKeyBytes = (security.nR == 2) ? 5 : 16;
        }

        security.baID = findTrailerID(pPdStruct);

        bool bPasswordOk = false;
        m_baFileKey = XPDFCrypt::computeFileKey(security, baUserPassword, &bPasswordOk);
        m_security = security;
        m_bDecryptReady = bPasswordOk && !m_baFileKey.isEmpty();
        break;
    }

    return m_bDecryptReady;
}

bool XPDF::isDecryptable() const
{
    return m_bDecryptReady;
}

QByteArray XPDF::decryptContent(const QByteArray &baData, quint64 nObjNum, quint32 nGeneration)
{
    if (!m_bDecryptReady) {
        return baData;
    }
    return XPDFCrypt::decryptObject(baData, nObjNum, nGeneration, m_security, m_baFileKey);
}

namespace {
QString decodePdfTextBytes(const QByteArray &baData)
{
    if ((baData.size() >= 2) && (static_cast<quint8>(baData.at(0)) == 0xFE) && (static_cast<quint8>(baData.at(1)) == 0xFF)) {
        QString sResult;
        for (qint32 i = 2; (i + 1) < baData.size(); i += 2) {
            sResult.append(QChar((static_cast<quint8>(baData.at(i)) << 8) | static_cast<quint8>(baData.at(i + 1))));
        }
        return sResult;
    }
    return QString::fromLatin1(baData);
}
}  // namespace

QString XPDF::getDecryptedMetaInfoString(QList<XPART> *pListObjects, PDSTRUCT *pPdStruct)
{
    if (!m_bDecryptReady) {
        return QString();
    }

    static const char *const sKeys[] = {"/Title", "/Author", "/Subject", "/Keywords", "/Creator", "/Producer", "/CreationDate", "/ModDate"};
    const qint32 nKeys = static_cast<qint32>(sizeof(sKeys) / sizeof(sKeys[0]));

    QStringList listLines;
    QSet<QString> stSeenKeys;

    const qint32 nCount = pListObjects->count();
    for (qint32 i = 0; (i < nCount) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
        const XPART &part = pListObjects->at(i);

        for (qint32 k = 0; k < nKeys; ++k) {
            const QString sKey = QString::fromLatin1(sKeys[k]);
            if (stSeenKeys.contains(sKey)) {
                continue;
            }

            const QByteArray baRaw = _getRawBytesByKey(&(part.listParts), sKey);
            if (baRaw.isEmpty()) {
                continue;
            }

            const QByteArray baPlain = decryptContent(baRaw, part.nID, 0);
            const QString sValue = decodePdfTextBytes(baPlain);
            if (!sValue.trimmed().isEmpty()) {
                listLines << (sKey.mid(1) + ": " + sValue);
                stSeenKeys.insert(sKey);
            }
        }
    }

    return listLines.join(QLatin1String("; "));
}

QString XPDF::getLinearizedInfoString(QList<XPART> *pListObjects, PDSTRUCT *pPdStruct)
{
    QString sResult;

    const qint32 nCount = pListObjects->count();

    for (qint32 i = 0; (i < nCount) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
        QList<QString> &listParts = (*pListObjects)[i].listParts;

        if (listParts.contains(QLatin1String("/Linearized"))) {
            sResult = tr("Linearized");

            const XVARIANT vL = getFirstStringValueByKey(&listParts, QLatin1String("/L"), pPdStruct);
            if (!vL.var.isNull()) {
                const qint64 nDeclared = vL.var.toLongLong();
                const qint64 nActual = getSize();
                if ((nDeclared > 0) && (nDeclared != nActual)) {
                    // A declared length that disagrees with the file size flags appended/tampered data.
                    sResult += QString(" (%1 L=%2, %3=%4)").arg(tr("declared")).arg(nDeclared).arg(tr("actual")).arg(nActual);
                }
            }

            break;
        }
    }

    return sResult;
}

QString XPDF::getSuspiciousInfoString(QList<XPART> *pListObjects, PDSTRUCT *pPdStruct)
{
    static const char *const sTriggers[] = {"/OpenAction", "/AA",       "/JavaScript", "/JS",       "/Launch",   "/EmbeddedFile",
                                            "/URI",        "/SubmitForm", "/GoToR",      "/RichMedia", "/AcroForm", "/XFA"};
    const qint32 nTriggers = static_cast<qint32>(sizeof(sTriggers) / sizeof(sTriggers[0]));

    QSet<QString> stFound;
    const qint32 nCount = pListObjects->count();

    // 1) Enumerated object dictionaries (dictionaries are never compressed, so their keys are visible here).
    for (qint32 i = 0; (i < nCount) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
        const QList<QString> &listParts = pListObjects->at(i).listParts;
        for (qint32 t = 0; t < nTriggers; ++t) {
            const QString sTrigger = QString::fromLatin1(sTriggers[t]);
            if (listParts.contains(sTrigger)) {
                stFound.insert(sTrigger);
            }
        }
    }

    // 2) Object streams (/ObjStm) hide compressed dictionaries; decompress and scan their bytes.
    for (qint32 i = 0; (i < nCount) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
        if (static_cast<qint32>(stFound.count()) >= nTriggers) {
            break;  // everything already found
        }

        const XPART &part = pListObjects->at(i);
        if (!part.listParts.contains(QLatin1String("/ObjStm"))) {
            continue;
        }

        XPART full = handleXpart(part.nOffset, part.nID, -1, pPdStruct, true, &m_mapObjIdOffset);
        if (full.listStreams.isEmpty()) {
            continue;
        }

        const QStringList listFilters = _resolveFilterList(&(full.listParts), QLatin1String("/Filter"), pPdStruct);
        const HANDLE_METHOD hm = listFilters.isEmpty() ? HANDLE_METHOD_STORE : pdfFilterToHandleMethod(listFilters.last());

        const STREAM &stream = full.listStreams.at(0);
        if (stream.nSize <= 0) {
            continue;
        }

        QByteArray baRaw = read_array(stream.nOffset, stream.nSize);
        QByteArray baDecoded;
        if (hm == HANDLE_METHOD_STORE) {
            baDecoded = baRaw;
        } else {
            decompressPdfBuffer(baRaw, hm, -1, &baDecoded, pPdStruct);
        }

        for (qint32 t = 0; t < nTriggers; ++t) {
            const QString sTrigger = QString::fromLatin1(sTriggers[t]);
            if (stFound.contains(sTrigger)) {
                continue;
            }
            if (baDecoded.contains(QByteArray(sTriggers[t]))) {
                stFound.insert(sTrigger);
            }
        }
    }

    if (stFound.isEmpty()) {
        return QString();
    }

    QStringList listOut;
    for (qint32 t = 0; t < nTriggers; ++t) {
        const QString sTrigger = QString::fromLatin1(sTriggers[t]);
        if (stFound.contains(sTrigger)) {
            listOut << sTrigger;
        }
    }

    return tr("Suspicious") + ": " + listOut.join(QLatin1String(", "));
}

QList<XBinary::FPART> XPDF::getFileParts(quint32 nFileParts, qint32 nLimit, PDSTRUCT *pPdStruct)
{
    QList<XBinary::FPART> listResult;

    const qint64 totalSize = getSize();

    scanStructure(pPdStruct);

    const qint64 nHeaderOffset = qMax<qint64>(0, m_nHeaderOffset);
    const qint32 nNumberOfFrefs = m_listStartHrefs.count();
    const qint32 nNumberOfObjects = m_listObjectsCache.count();

    qint64 nMaxOffset = 0;

    if (nFileParts & FILEPART_SIGNATURE) {
        const OS_STRING osHeader = _readPDFString(nHeaderOffset, 20, pPdStruct);
        listResult.append(makeFilePart(FILEPART_SIGNATURE, nHeaderOffset, osHeader.nSize, nNumberOfFrefs ? tr("Signature") : tr("Header")));
    }

    if (nNumberOfFrefs) {
        for (qint32 j = 0; (j < nNumberOfFrefs) && XBinary::isPdStructNotCanceled(pPdStruct); ++j) {
            const STARTHREF &startxref = m_listStartHrefs.at(j);

            if (startxref.bIsXref && (nFileParts & FILEPART_TABLE)) {
                listResult.append(makeFilePart(FILEPART_TABLE, startxref.nXrefOffset, startxref.nFooterOffset - startxref.nXrefOffset, QStringLiteral("xref")));
            }

            if (nFileParts & FILEPART_FOOTER) {
                listResult.append(makeFilePart(FILEPART_FOOTER, startxref.nFooterOffset, startxref.nFooterSize, tr("Footer")));
            }
        }

        nMaxOffset = m_listStartHrefs.at(nNumberOfFrefs - 1).nFooterOffset + m_listStartHrefs.at(nNumberOfFrefs - 1).nFooterSize;
    } else {
        const OS_STRING osHeader = _readPDFString(nHeaderOffset, 20, pPdStruct);
        nMaxOffset = nHeaderOffset + osHeader.nSize;
    }

    for (qint32 i = 0; (i < nNumberOfObjects) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
        const OBJECT &o = m_listObjectsCache.at(i);
        const qint64 nEnd = o.nOffset + o.nSize;
        if (nEnd > nMaxOffset) {
            nMaxOffset = nEnd;
        }
    }

    if (nMaxOffset > totalSize) {
        nMaxOffset = totalSize;
    }

    if (nFileParts & FILEPART_DATA) {
        listResult.append(makeFilePart(FILEPART_DATA, 0, nMaxOffset, tr("Data")));
    }

    if ((nFileParts & FILEPART_STREAM) || (nFileParts & FILEPART_OBJECT)) {
        QSet<qint32> stPaletteObjectIds;
        QMap<qint32, QString> mapEmbeddedNames;  // embedded-file object id -> declared name

        const qint32 _nFreeIndex = XBinary::getFreeIndex(pPdStruct);
        XBinary::setPdStructInit(pPdStruct, _nFreeIndex, nNumberOfObjects);

        for (qint32 i = 0; (i < nNumberOfObjects) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
            // Honour the part cap requested by _getMemoryMap (which passes 1000); the old code ignored nLimit.
            if ((nLimit != -1) && (listResult.count() >= nLimit)) {
                break;
            }

            const OBJECT &object = m_listObjectsCache.at(i);

            if (nFileParts & FILEPART_OBJECT) {
                listResult.append(makeFilePart(FILEPART_OBJECT, object.nOffset, object.nSize, QString("%1 %2").arg(tr("Object")).arg(QString::number(object.nID))));
            }

            if (nFileParts & FILEPART_STREAM) {
                XPART xpart = handleXpart(object.nOffset, object.nID, -1, pPdStruct, true, &m_mapObjIdOffset);

                // Collect /Filespec -> embedded-file name so streams can carry their real filename.
                {
                    const QString sFileName = _resolveFilespecName(&(xpart.listParts), pPdStruct);
                    if (!sFileName.isEmpty()) {
                        const qint32 nEfId = _resolveEmbeddedRefId(&(xpart.listParts), pPdStruct);
                        if (nEfId > 0) {
                            mapEmbeddedNames.insert(nEfId, sFileName);
                        }
                    }
                }

                qint32 nNumberOfStreams = xpart.listStreams.count();

                for (qint32 j = 0; (j < nNumberOfStreams) && XBinary::isPdStructNotCanceled(pPdStruct); ++j) {
                    const STREAM &stream = xpart.listStreams.at(j);

                    XBinary::FPART record =
                        makeFilePart(XBinary::FILEPART_STREAM, stream.nOffset, stream.nSize, QString("%1 obj (%2)").arg(tr("Stream")).arg(QString::number(object.nID)));

                    // Resolve /Filter as an ordered list (single name or array). The terminal filter drives
                    // the decode; a multi-filter cascade cannot be undone by a single method, so it stays stored.
                    const QStringList listFilters = _resolveFilterList(&(xpart.listParts), QLatin1String("/Filter"), pPdStruct);
                    const bool bCascade = (listFilters.count() > 1);
                    const QString sFilter = listFilters.isEmpty() ? QString() : listFilters.last();
                    const QString sFilterDisplay = listFilters.join(QLatin1Char(' '));
                    const QString sSubtype = getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/Subtype"), pPdStruct).var.toString();

                    if (!sFilterDisplay.isEmpty()) {
                        record.mapProperties.insert(FPART_PROP_FILTERNAME, sFilterDisplay);
                    }
                    if (!sSubtype.isEmpty()) {
                        record.mapProperties.insert(FPART_PROP_SUBTYPE, sSubtype);
                    }

                    const HANDLE_METHOD decompressMethod = bCascade ? HANDLE_METHOD_STORE : pdfFilterToHandleMethod(sFilter);

#ifdef QT_DEBUG
                    if (!bCascade && !isKnownStorePdfFilter(sFilter) && (decompressMethod == HANDLE_METHOD_STORE)) {
                        qDebug() << "Unknown filter:" << sFilter << xpart.listParts << record.sName;
                    }
#endif

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

                                    // Gather the colour-space array tokens from either an inline array or an
                                    // indirectly-referenced object ("/ColorSpace N 0 R").
                                    QStringList listTokens;
                                    QString sSimpleColorSpace;

                                    if (sNext == QLatin1String("[")) {
                                        for (qint32 q = p + 2; q < nParts; ++q) {
                                            if (xpart.listParts.at(q) == QLatin1String("]")) {
                                                break;
                                            }
                                            listTokens.append(xpart.listParts.at(q));
                                        }
                                    } else if (_isIndirectRef(sNext)) {
                                        const quint64 nCsId = static_cast<quint64>(sNext.section(QChar(' '), 0, 0).toLongLong());
                                        const qint64 nCsOffset = resolveObjectOffset(nCsId, &m_mapObjIdOffset, object.nOffset, pPdStruct);
                                        if (nCsOffset != -1) {
                                            XPART csPart = handleXpart(nCsOffset, static_cast<qint32>(nCsId), -1, pPdStruct, false, &m_mapObjIdOffset);
                                            const qint32 nCsParts = csPart.listParts.count();
                                            const qint32 nOpen = csPart.listParts.indexOf(QLatin1String("["));
                                            if (nOpen != -1) {
                                                for (qint32 q = nOpen + 1; q < nCsParts; ++q) {
                                                    if (csPart.listParts.at(q) == QLatin1String("]")) {
                                                        break;
                                                    }
                                                    listTokens.append(csPart.listParts.at(q));
                                                }
                                            }
                                        }
                                    } else {
                                        sSimpleColorSpace = sNext;
                                    }

                                    if (!listTokens.isEmpty()) {
                                        sColorSpace = listTokens.at(0);

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

                                                    const quint64 nPalId = static_cast<quint64>(sObjRef.section(QChar(' '), 0, 0).toLongLong());
                                                    // Resolve via the object index (O(log K)); the old code scanned the whole file per image.
                                                    qint64 nPaletteObjOffset = resolveObjectOffset(nPalId, &m_mapObjIdOffset, object.nOffset, pPdStruct);

                                                    if (nPaletteObjOffset != -1) {
                                                        const qint32 nPalObjId = static_cast<qint32>(nPalId);
                                                        stPaletteObjectIds.insert(nPalObjId);
                                                        XPART palPart = handleXpart(nPaletteObjOffset, nPalObjId, -1, pPdStruct, true, &m_mapObjIdOffset);

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
                                    } else if (!sSimpleColorSpace.isEmpty()) {
                                        sColorSpace = sSimpleColorSpace;
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
                            qint32 nCcittK = 0;  // ISO 32000 default for /K is 0 (Group 3 1-D), not Group 4
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
                                                                             .arg(sFilterDisplay));
                        }
                    } else {
                        const bool bHasLength1 = !getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/Length1"), pPdStruct).var.isNull();
                        const bool bHasLength2 = !getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/Length2"), pPdStruct).var.isNull();
                        const bool bHasLength3 = !getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/Length3"), pPdStruct).var.isNull();
                        const bool bEmbeddedFile = (getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/Type"), pPdStruct).var.toString() == QLatin1String("/EmbeddedFile"));

                        if (sSubtype == QLatin1String("/XML")) {
                            record.mapProperties.insert(FPART_PROP_EXT, QStringLiteral("xml"));
                            record.mapProperties.insert(FPART_PROP_FILETYPE, FT_XML);
                        } else if ((sSubtype == QLatin1String("/Type1C")) || (sSubtype == QLatin1String("/CIDFontType0C"))) {
                            record.mapProperties.insert(FPART_PROP_EXT, QStringLiteral("cff"));
                        } else if (bHasLength1 && bHasLength2 && bHasLength3) {
                            // Type1 (PFB/eexec) font program: /Length1 /Length2 /Length3, NOT an sfnt.
                            record.mapProperties.insert(FPART_PROP_EXT, QStringLiteral("pfb"));
                        } else if (bHasLength1) {
                            // FontFile2 -> TrueType/sfnt.
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
                        if (bEmbeddedFile) {
                            listInfo << tr("Embedded file");
                        }
                        if (!sSubtype.isEmpty()) {
                            listInfo << sSubtype;
                        }
                        if (!sFilterDisplay.isEmpty()) {
                            listInfo << sFilterDisplay;
                        }
                        if (!listInfo.isEmpty()) {
                            record.mapProperties.insert(FPART_PROP_INFO, listInfo.join(QLatin1String(" ")));
                        }
                    }

                    listResult.append(record);
                }
            }

            XBinary::setPdStructCurrentIncrement(pPdStruct, _nFreeIndex);
        }

        XBinary::setPdStructFinished(pPdStruct, _nFreeIndex);

        // Post-pass: relabel palette streams and attach embedded-file names, matching by object id.
        if (!stPaletteObjectIds.isEmpty() || !mapEmbeddedNames.isEmpty()) {
            qint32 nResultCount = listResult.count();
            for (qint32 r = 0; r < nResultCount; ++r) {
                if (listResult.at(r).filePart != XBinary::FILEPART_STREAM) {
                    continue;
                }

                const QString sName = listResult.at(r).sName;
                const qint32 nParenOpen = sName.indexOf(QLatin1Char('('));
                const qint32 nParenClose = sName.indexOf(QLatin1Char(')'));
                if ((nParenOpen == -1) || (nParenClose == -1) || (nParenClose <= nParenOpen)) {
                    continue;
                }

                const qint32 nObjId = sName.mid(nParenOpen + 1, nParenClose - nParenOpen - 1).toInt();

                if (stPaletteObjectIds.contains(nObjId)) {
                    listResult[r].mapProperties.insert(FPART_PROP_HANDLEMETHOD2, listResult[r].mapProperties.value(FPART_PROP_HANDLEMETHOD, HANDLE_METHOD_STORE));
                    listResult[r].mapProperties.insert(FPART_PROP_EXT, QStringLiteral("pal"));
                    listResult[r].mapProperties.insert(FPART_PROP_FILETYPE, FT_PAL);
                    listResult[r].mapProperties.insert(FPART_PROP_HANDLEMETHOD, HANDLE_METHOD_PDF_PALETTE);
                    listResult[r].mapProperties.insert(FPART_PROP_INFO, tr("Color palette") + QString(" (PAL)"));
                }

                if (mapEmbeddedNames.contains(nObjId)) {
                    listResult[r].mapProperties.insert(FPART_PROP_ORIGINALNAME, mapEmbeddedNames.value(nObjId));
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
    result.mapProperties.insert(FPART_PROP_COMPRESSEDSIZE, stream.nFileSize);
    // Only claim an uncompressed size when the stream is stored verbatim; otherwise the true inflated
    // size is unknown here and reporting the on-disk (compressed) size is misleading.
    if (primaryMethod == HANDLE_METHOD_STORE) {
        result.mapProperties.insert(FPART_PROP_UNCOMPRESSEDSIZE, stream.nFileSize);
    }

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

        // Multi-filter cascade (e.g. [/ASCII85Decode /FlateDecode]): the single-method decompressor can't
        // undo a chain, so when every stage is decodable, apply the chain in-memory and write the result.
        const QString sFilterName = archiveRecord.mapProperties.value(FPART_PROP_FILTERNAME).toString();
        const QStringList listFilters = sFilterName.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (listFilters.count() > 1) {
            bool bAllDecodable = true;
            for (qint32 i = 0; i < listFilters.count(); ++i) {
                if (pdfFilterToHandleMethod(listFilters.at(i)) == HANDLE_METHOD_STORE) {
                    bAllDecodable = false;
                    break;
                }
            }

            if (bAllDecodable && (archiveRecord.nStreamSize > 0)) {
                QByteArray baRaw = read_array(archiveRecord.nStreamOffset, archiveRecord.nStreamSize);
                QByteArray baDecoded;
                if (decompressPdfChain(baRaw, listFilters, &baDecoded, pPdStruct)) {
                    const qint64 nWritten = pDevice->write(baDecoded);
                    return (nWritten == static_cast<qint64>(baDecoded.size()));
                }
            }
        }

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

    // Report exhaustion so the generic folder-extract do-while terminates; returning true
    // unconditionally spun forever / wrote unbounded "file_N" once past the last stream.
    return (pContext->nCurrentStreamIndex < pContext->listStreams.count());
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
