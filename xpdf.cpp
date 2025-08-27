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
#include "xpdf.h"

XPDF::XPDF(QIODevice *pDevice) : XBinary(pDevice)
{
}

bool XPDF::isValid(PDSTRUCT *pPdStruct)
{
    Q_UNUSED(pPdStruct)

    bool bResult = false;

    // TODO more checks !!!
    // 1.0-2.0
    // %PDF-
    if (getSize() > 4) {
        if (read_uint32(0) == 0x46445025)  // '%PDF'
        {
            bResult = true;
        }
    }

    return bResult;
}

QString XPDF::getVersion()
{
    QString sResult;

    sResult = _readPDFString(5, 3, nullptr).sString;

    return sResult;
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
    return "pdf";
}

QString XPDF::getFileFormatExtsString()
{
    return "PDF(pdf)";
}

QString XPDF::getMIMEString()
{
    return "application/pdf";
}

XBinary::MODE XPDF::getMode()
{
    return MODE_UNKNOWN;  // PDF does not have a specific mode like 16/32/64
}

QList<XPDF::OBJECT> XPDF::findObjects(qint64 nOffset, qint64 nSize, bool bDeepScan, PDSTRUCT *pPdStruct)
{
    const qint64 fileSize = getSize();
    if (nSize == -1) {
        nSize = fileSize - nOffset;
    }

    QList<XPDF::OBJECT> listResult;

    qint64 nCurrentOffset = nOffset;
    const qint64 endBound = nOffset + nSize;

    while (XBinary::isPdStructNotCanceled(pPdStruct) && (nCurrentOffset < endBound)) {
        const OS_STRING osString = _readPDFString(nCurrentOffset, 100, pPdStruct);

        if (_isObject(osString.sString)) {
            const quint64 nID = getObjectID(osString.sString);
            const qint64 nEndObjOffset = find_ansiString(nCurrentOffset + osString.nSize, -1, "endobj", pPdStruct);

            if (nEndObjOffset != -1) {
                const OS_STRING osEndObj = _readPDFString(nEndObjOffset, 100, pPdStruct);

                if (_isEndObject(osEndObj.sString)) {
                    OBJECT record = {};
                    record.nOffset = nCurrentOffset;
                    record.nID = nID;
                    record.nSize = (nEndObjOffset + osEndObj.nSize) - nCurrentOffset;

                    listResult.append(record);

                    nCurrentOffset = nEndObjOffset + osEndObj.nSize;
                } else {
                    break;
                }
            } else {
                break;
            }
        } else if (_isComment(osString.sString)) {
            nCurrentOffset += osString.nSize;
            skipPDFEnding(&nCurrentOffset, pPdStruct);
        } else {
            bool bContinue = false;
            if (bDeepScan) {
                const qint64 remain = endBound - nCurrentOffset;
                nCurrentOffset = find_ansiString(nCurrentOffset, remain, " obj", pPdStruct);

                if (nCurrentOffset != -1) {
                    while ((nCurrentOffset > 0) && XBinary::isPdStructNotCanceled(pPdStruct)) {
                        const quint8 _nChar = read_uint8(nCurrentOffset - 1);

                        // If not number and not space
                        if (!(((_nChar >= '0') && (_nChar <= '9')) || (_nChar == ' '))) {
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
    const qint32 n = _readPDFString(*pnOffset, 20, pPdStruct).nSize;
    *pnOffset += n;
    return n;
}

qint32 XPDF::skipPDFEnding(qint64 *pnOffset, PDSTRUCT *pPdStruct)
{
    const qint64 start = *pnOffset;
    const qint64 fileSize = getSize();
    qint64 cur = *pnOffset;

    while ((cur < fileSize) && XBinary::isPdStructNotCanceled(pPdStruct)) {
        const quint8 ch = read_uint8(cur);
        if (ch == 10) {  // LF
            ++cur;
        } else if (ch == 13) {  // CR, optionally followed by LF
            ++cur;
            if ((cur < fileSize) && (read_uint8(cur) == 10)) {
                ++cur;
            }
        } else {
            break;
        }
    }

    *pnOffset = cur;
    return static_cast<qint32>(cur - start);
}

qint32 XPDF::skipPDFSpace(qint64 *pnOffset, PDSTRUCT *pPdStruct)
{
    const qint64 start = *pnOffset;
    const qint64 fileSize = getSize();
    qint64 cur = *pnOffset;

    while ((cur < fileSize) && XBinary::isPdStructNotCanceled(pPdStruct)) {
        if (read_uint8(cur) == ' ') {
            ++cur;
        } else {
            break;
        }
    }

    *pnOffset = cur;
    return static_cast<qint32>(cur - start);
}

QList<XPDF::OBJECT> XPDF::getObjectsFromStartxref(const STARTHREF *pStartxref, PDSTRUCT *pPdStruct)
{
    QList<XPDF::OBJECT> listResult;

    const qint64 nTotalSize = getSize();

    qint64 nCurrentOffset = pStartxref->nXrefOffset;

    const OS_STRING osStringHref = _readPDFString(nCurrentOffset, 20, pPdStruct);

    if (_isXref(osStringHref.sString)) {
        nCurrentOffset += osStringHref.nSize;

        QMap<qint64, quint64> mapObjects;

        while (XBinary::isPdStructNotCanceled(pPdStruct)) {
            const OS_STRING osSection = _readPDFString(nCurrentOffset, 20, pPdStruct);

            if (!osSection.sString.isEmpty()) {
                const quint64 nID = osSection.sString.section(" ", 0, 0).toULongLong();
                const quint64 nNumberOfObjects = osSection.sString.section(" ", 1, 1).toULongLong();

                nCurrentOffset += osSection.nSize;

                if (nNumberOfObjects) {
                    const qint32 _nFreeIndex = XBinary::getFreeIndex(pPdStruct);
                    XBinary::setPdStructInit(pPdStruct, _nFreeIndex, static_cast<qint32>(nNumberOfObjects));

                    for (quint64 i = 0; (i < nNumberOfObjects) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
                        const OS_STRING osObject = _readPDFString(nCurrentOffset, 20, pPdStruct);

                        if (osObject.sString.section(" ", 2, 2) == "n") {
                            const qint64 nObjectOffset = osObject.sString.section(" ", 0, 0).toULongLong();

                            // OS_STRING osTitle = _readPDFStringPart_title(nObjectOffset, 20);
                            // if (_isObject(osTitle.sString)) {
                            //      mapObjects.insert(nObjectOffset, nID + j);
                            // }
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
            object.nSize = 0;  // Will be calculated later

            listResult.append(object);
        }

        const qint32 nNumberOfObjects = listResult.count();
        // Calculate sizes based on consecutive offsets
        for (qint32 i = 0; (i < nNumberOfObjects - 1) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
            listResult[i].nSize = listResult[i + 1].nOffset - listResult[i].nOffset;
        }

        // Handle the last object's size using nXrefOffset
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

    const qint64 nFileSize = getSize();
    if (nOffset < 0 || nOffset >= nFileSize) {
        return result;  // Out of bounds
    }

    if (nSize == -1) {
        nSize = nFileSize - nOffset;
    }

    // Clamp read size to file end
    if (nOffset + nSize > nFileSize) {
        nSize = nFileSize - nOffset;
    }

    if (nSize <= 0) {
        return result;
    }

    const qint64 nStartOffset = nOffset;
    const qint64 nEndOffset = nOffset + nSize;
    for (; (nOffset < nEndOffset) && XBinary::isPdStructNotCanceled(pPdStruct); ++nOffset) {
        const quint8 nChar = read_uint8(nOffset);
        // Stop on NUL, CR, or LF
        if ((nChar == 0) || (nChar == 13) || (nChar == 10)) {
            break;
        }
        result.sString.append(QLatin1Char(static_cast<char>(nChar)));
    }

    // Bytes consumed for this string part
    result.nSize = nOffset - nStartOffset;

    // Include any trailing line ending in total size
    result.nSize += skipPDFEnding(&nOffset, pPdStruct);

    return result;
}

XBinary::OS_STRING XPDF::_readPDFStringPart_title(qint64 nOffset, qint64 nSize, PDSTRUCT *pPdStruct)
{
    XBinary::OS_STRING result = {};

    result.nOffset = nOffset;

    const qint64 nFileSize = getSize();
    if (nOffset < 0 || nOffset >= nFileSize) {
        return result;  // Out of bounds
    }

    if (nSize == -1) {
        nSize = nFileSize - nOffset;
    }

    // Clamp to file size
    if (nOffset + nSize > nFileSize) {
        nSize = nFileSize - nOffset;
    }

    if (nSize <= 0) {
        return result;
    }

    const qint64 nStartOffset = nOffset;
    const qint64 nEndOffset = nOffset + nSize;
    for (; (nOffset < nEndOffset) && XBinary::isPdStructNotCanceled(pPdStruct); ++nOffset) {
        const quint8 nChar = read_uint8(nOffset);
        // Stop on NUL, CR, LF, or '<'
        if ((nChar == 0) || (nChar == 13) || (nChar == 10) || (nChar == '<')) {
            break;
        }
        result.sString.append(QLatin1Char(static_cast<char>(nChar)));
    }

    // bytes consumed for the title part
    result.nSize = nOffset - nStartOffset;

    // Include any trailing line ending in total size
    result.nSize += skipPDFEnding(&nOffset, pPdStruct);

    return result;
}

XBinary::OS_STRING XPDF::_readPDFStringPart(qint64 nOffset, PDSTRUCT *pPdStruct)
{
    XBinary::OS_STRING result = {};

    result.nOffset = nOffset;

    const qint64 nFileSize = getSize();
    if (nOffset < 0 || nOffset >= nFileSize) {
        return result;  // Out of bounds
    }

    const qint64 remaining = nFileSize - nOffset;
    if (remaining <= 0) {
        return result;
    }

    const quint8 nChar = read_uint8(nOffset);

    if (nChar == '/') {
        result = _readPDFStringPart_const(nOffset, pPdStruct);
    } else if (nChar == '(') {
        result = _readPDFStringPart_str(nOffset, pPdStruct);
    } else if (nChar == '<') {
        if (remaining > 1) {
            if (read_uint8(nOffset + 1) == '<') {
                result.sString = "<<";
                result.nSize = 2;
            } else {
                result = _readPDFStringPart_hex(nOffset, pPdStruct);
            }
        }
    } else if (nChar == '>') {
        if (remaining > 1) {
            if (read_uint8(nOffset + 1) == '>') {
                result.sString = ">>";
                result.nSize = 2;
            }
        }
    } else if (nChar == '[') {
        result.sString = "[";
        result.nSize = 1;
    } else if (nChar == ']') {
        result.sString = "]";
        result.nSize = 1;
    } else {
        result = _readPDFStringPart_val(nOffset, pPdStruct);
    }

    // Post-consume trailing spaces and line endings
    nOffset += result.nSize;
    result.nSize += skipPDFSpace(&nOffset, pPdStruct);
    result.nSize += skipPDFEnding(&nOffset, pPdStruct);

    return result;
}

XBinary::OS_STRING XPDF::_readPDFStringPart_const(qint64 nOffset, PDSTRUCT *pPdStruct)
{
    XBinary::OS_STRING result = {};

    result.nOffset = nOffset;

    const qint64 nFileSize = getSize();
    if (nOffset < 0 || nOffset >= nFileSize) {
        return result;  // Out of bounds
    }

    const qint64 nEndOffset = nFileSize;  // read until file end or stop char
    bool isFirst = true;
    for (; (nOffset < nEndOffset) && XBinary::isPdStructNotCanceled(pPdStruct); ++nOffset) {
        const quint8 nChar = read_uint8(nOffset);

        // Stop on control/terminators or structural delimiters
        if ((nChar == 0) || (nChar == 10) || (nChar == 13) || (nChar == '[') || (nChar == ']') || (nChar == '<') || (nChar == '>') || (nChar == ' ') || (nChar == '(')) {
            break;
        }

        // Subsequent '/' starts a new token; include only the very first '/'
        if (!isFirst && (nChar == '/')) {
            break;
        }

        result.sString.append(QLatin1Char(static_cast<char>(nChar)));
        result.nSize++;
        isFirst = false;
    }

    return result;
}

XBinary::OS_STRING XPDF::_readPDFStringPart_str(qint64 nOffset, PDSTRUCT *pPdStruct)
{
    XBinary::OS_STRING result = {};

    result.nOffset = nOffset;

    const qint64 nFileSize = getSize();
    if (nOffset < 0 || nOffset >= nFileSize) {
        return result;  // Out of bounds
    }

    qint64 remaining = nFileSize - nOffset;
    if (remaining <= 0) {
        return result;
    }

    bool bStart = false;
    bool bEnd = false;
    bool bUnicode = false;
    bool bBSlash = false;

    // Cursor-based loop
    for (; (remaining > 0) && XBinary::isPdStructNotCanceled(pPdStruct);) {
        if (!bUnicode) {
            const quint8 ch = read_uint8(nOffset);

            // Stop on NUL, LF, CR
            if ((ch == 0) || (ch == 10) || (ch == 13)) {
                break;
            }

            if (!bStart) {
                if (ch == '(') {
                    bStart = true;
                    // Check UTF-16BE BOM after '('
                    if (remaining >= 3) {
                        if ((read_uint8(nOffset + 1) == 0xFE) && (read_uint8(nOffset + 2) == 0xFF)) {
                            bUnicode = true;
                            result.nSize += 2;  // count BOM bytes in size
                            nOffset += 2;
                            remaining -= 2;
                        }
                    }
                    result.sString.append('(');
                } else {
                    // If first char isn't '(', treat as plain char (legacy behavior)
                    if (bBSlash) {
                        bBSlash = false;
                    }
                    result.sString.append(QLatin1Char(static_cast<char>(ch)));
                }
            } else if ((ch == ')') && (!bBSlash)) {
                result.sString.append(')');
                bEnd = true;
            } else if (ch == '\\') {
                bBSlash = true;
            } else {
                if (bBSlash) {
                    bBSlash = false;
                }
                result.sString.append(QLatin1Char(static_cast<char>(ch)));
            }
            ++result.nSize;
            ++nOffset;
            --remaining;
        } else if (remaining >= 2) {
            const quint16 w = read_uint16(nOffset, true);

            if (((w >> 8) == ')') && (!bBSlash)) {
                result.sString.append(')');
                ++result.nSize;  // only one byte of ')'
                bEnd = true;
            } else if (w == '\\') {
                bBSlash = true;
                nOffset += 2;
                remaining -= 2;
                result.nSize += 2;
                continue;
            } else if (bBSlash && (w == 0x6e29)) {  // 'n' ')'
                bBSlash = false;
                result.sString.append(')');
                ++result.nSize;
                bEnd = true;
            } else {
                if (bBSlash) {
                    bBSlash = false;
                }
                result.sString.append(QChar(w));
                nOffset += 2;
                remaining -= 2;
                result.nSize += 2;
                continue;
            }

            // advance by one byte for the cases above where we consumed a single char
            ++nOffset;
            --remaining;
        } else {
            break;
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
        return result;  // Out of bounds
    }

    qint64 remaining = nFileSize - nOffset;
    if (remaining <= 0) {
        return result;
    }

    bool bSpace = false;

    for (; (remaining > 0) && XBinary::isPdStructNotCanceled(pPdStruct); ++nOffset, --remaining) {
        const quint8 ch = read_uint8(nOffset);

        if ((ch == 0) || (ch == 10) || (ch == 13) || (ch == '[') || (ch == ']') || (ch == '<') || (ch == '>') || (ch == '/')) {
            break;
        }

        ++result.nSize;

        if (ch == ' ') {
            bSpace = true;
            break;
        }

        result.sString.append(QLatin1Char(static_cast<char>(ch)));
    }

    if (bSpace) {
        if (remaining >= 3) {
            const QString sSuffix = read_ansiString(nOffset + result.nSize, 3);
            if (sSuffix == "0 R") {
                result.sString.append(" " + sSuffix);
                result.nSize += 3;
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
        return result;  // Out of bounds
    }

    qint64 remaining = nFileSize - nOffset;
    if (remaining <= 0) {
        return result;
    }

    // First byte must be '<'
    const quint8 first = read_uint8(nOffset);
    if (first != '<') {
        return result;
    }

    // Cursor-based copy until '>' inclusive
    for (; (remaining > 0) && XBinary::isPdStructNotCanceled(pPdStruct); ++nOffset, --remaining) {
        const quint8 ch = read_uint8(nOffset);
        result.sString.append(QLatin1Char(static_cast<char>(ch)));
        ++result.nSize;
        if (ch == '>') {
            break;
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
        mapMode = MAPMODE_DATA;  // Default mode
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
        const qint64 nStartXref = find_signature(nOffset, -1, "'startxref'", nullptr, pPdStruct);  // \n \r
        if (nStartXref == -1) {
            break;
        }

        qint64 nCurrent = nStartXref;

        OS_STRING osStartXref = _readPDFString(nCurrent, 20, pPdStruct);
        nCurrent += osStartXref.nSize;

        OS_STRING osOffset = _readPDFString(nCurrent, 20, pPdStruct);
        const qint64 targetOffset = osOffset.sString.toLongLong();

        OS_STRING osHref = _readPDFString(targetOffset, 20, pPdStruct);
        const bool bIsXref = _isXref(osHref.sString);
        const bool bIsObject = _isObject(osHref.sString);

        if ((bIsXref || bIsObject) && (targetOffset < nCurrent)) {
            nCurrent += osOffset.nSize;

            OS_STRING osEnd = _readPDFString(nCurrent, 20, pPdStruct);
            QString footerHead = osEnd.sString;
            footerHead.resize(5, QChar(' '));

            if (footerHead == "%%EOF") {
                nCurrent += 5;

                // Skip optional CR and LF, bounds-checked
                if ((nCurrent < nFileSize) && (read_uint8(nCurrent) == 13)) {
                    ++nCurrent;
                }
                if ((nCurrent < nFileSize) && (read_uint8(nCurrent) == 10)) {
                    ++nCurrent;
                }

                STARTHREF record = {};
                record.nXrefOffset = targetOffset;
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
                    break;  // No append
                }
            }
        }

        nOffset = nStartXref + 10;  // Get the last
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

    while (XBinary::isPdStructNotCanceled(pPdStruct)) {
        bool bStop = false;
        const OS_STRING osString = _readPDFStringPart_title(nOffset, 20, pPdStruct);

        if (result.nID == 0) {
            result.nID = getObjectID(osString.sString);
        }

        nOffset += osString.nSize;

        if (_isObject(osString.sString)) {
            qint32 nObj = 0;
            qint32 nCol = 0;
            qint32 nPartCount = 0;
            while (XBinary::isPdStructNotCanceled(pPdStruct)) {
                const OS_STRING osStringPart = _readPDFStringPart(nOffset, pPdStruct);

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

                if (osStringPart.sString == QLatin1String("<<")) {
                    ++nObj;
                } else if (osStringPart.sString == QLatin1String(">>")) {
                    --nObj;
                } else if (osStringPart.sString == QLatin1String("[")) {
                    ++nCol;
                } else if (osStringPart.sString == QLatin1String("]")) {
                    --nCol;
                } else if (osStringPart.sString == QLatin1String("/Length")) {
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
            if (sLength.toInt()) {
                stream.nSize = sLength.toInt();
            } else if (sLength.section(" ", 2, 2) == QLatin1String("R")) {
                QString sPattern = sLength;
                sPattern.replace("R", "obj");
                const qint64 nObjectOffset = find_ansiString(nOffset, -1, sPattern, pPdStruct);

                if (nObjectOffset != -1) {
                    qint64 tmp = nObjectOffset;
                    skipPDFString(&tmp, pPdStruct);
                    const OS_STRING osLen = _readPDFStringPart_val(tmp, pPdStruct);

                    if (osLen.sString.toInt()) {
                        stream.nSize = osLen.sString.toInt();
                    } else {
                        break;
                    }
                }
            } else {
                break;
            }

            if (stream.nSize) {
                nOffset += stream.nSize;
                skipPDFEnding(&nOffset, pPdStruct);
                result.listStreams.append(stream);
            }
        } else if (osString.sString == QLatin1String("endstream")) {
            // TODO
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
    // Fast check: last token equals "obj"
    int i = sString.size();
    while (i > 0 && sString.at(i - 1) == QChar(' ')) --i;  // trim right spaces
    const int tokenLen = 3;                                // "obj"
    if (i < tokenLen) return false;
    // Ensure boundary before token is start or space
    const int start = i - tokenLen;
    if (!((start == 0) || (sString.at(start - 1) == QChar(' ')))) return false;
    // Compare without allocating
    return (sString.at(start) == QChar('o') && sString.at(start + 1) == QChar('b') && sString.at(start + 2) == QChar('j'));
}

bool XPDF::_isString(const QString &sString)
{
    bool bResult = false;

    qint32 nSize = sString.size();
    if (nSize >= 2) {
        if ((sString.at(0) == QChar('(')) && (sString.at(nSize - 1) == QChar(')'))) {
            bResult = true;
        }
    }

    return bResult;
}

bool XPDF::_isHex(const QString &sString)
{
    bool bResult = false;

    qint32 nSize = sString.size();
    if (nSize >= 2) {
        if ((sString.at(0) == QChar('<')) && (sString.at(nSize - 1) == QChar('>'))) {
            if (sString.at(0) != sString.at(1)) {
                bResult = true;
            }
        }
    }

    return bResult;
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
    // Compare against "endobj" ignoring surrounding spaces without allocating
    int left = 0;
    int right = sString.size();
    while (left < right && sString.at(left) == QChar(' ')) ++left;
    while (right > left && sString.at(right - 1) == QChar(' ')) --right;
    if (right - left != 6) return false;
    return (sString.at(left) == QChar('e') && sString.at(left + 1) == QChar('n') && sString.at(left + 2) == QChar('d') && sString.at(left + 3) == QChar('o') &&
            sString.at(left + 4) == QChar('b') && sString.at(left + 5) == QChar('j'));
}

bool XPDF::_isComment(const QString &sString)
{
    bool bResult = false;

    if (!sString.isEmpty()) {
        bResult = (sString.at(0) == QChar('%'));
    }

    return bResult;
}

bool XPDF::_isXref(const QString &sString)
{
    bool bResult = false;

    if (!sString.isEmpty()) {
        // Fast path: check prefix "xref" and boundary
        const int tlen = 4;
        if (sString.size() >= tlen && sString.at(0) == QChar('x') && sString.at(1) == QChar('r') && sString.at(2) == QChar('e') && sString.at(3) == QChar('f')) {
            bResult = (sString.size() == tlen) || (sString.at(4) == QChar(' '));
        }
    }

    return bResult;
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
    // sDate.resize(14, QChar('0'));

    result = QDateTime::fromString(sDate, "yyyyMMddhhmmsstt");

    return result;
}

qint32 XPDF::getObjectID(const QString &sString)
{
    // Parse leading integer (until first space) without allocating
    qint64 n = 0;
    bool neg = false;
    int i = 0;
    const int len = sString.size();
    if (i < len && sString.at(i) == QChar('-')) {
        neg = true;
        ++i;
    }
    for (; i < len; ++i) {
        const QChar c = sString.at(i);
        if (c < QChar('0') || c > QChar('9')) break;
        n = n * 10 + (c.unicode() - '0');
    }
    if (neg) n = -n;
    return static_cast<qint32>(n);
}

XBinary::XVARIANT XPDF::getFirstStringValueByKey(QList<QString> *pListStrings, const QString &sKey, PDSTRUCT *pPdStruct)
{
    XBinary::XVARIANT varResult;

    const qint32 nNumberOfParts = pListStrings->count();

    for (qint32 j = 0; (j + 1 < nNumberOfParts) && XBinary::isPdStructNotCanceled(pPdStruct); ++j) {
        const QString &sCurrentKey = pListStrings->at(j);

        if (sCurrentKey == sKey) {
            const QString &sValue = pListStrings->at(j + 1);
            XVARIANT varValue;

            // Preserve original "0" behavior (not treated as number)
            qlonglong ll = sValue.toLongLong();
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
                // listObject = findObjects(startxref.nXrefOffset, startxref.nFooterOffset - startxref.nXrefOffset, true, pPdStruct);
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
                const QString &sValue = record.listParts.at(j + 1);
                XVARIANT varValue;

                qlonglong ll = sValue.toLongLong();
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

    qint64 nCurrentOffset = 0;

    OS_STRING osString = _readPDFString(nCurrentOffset, 100, nullptr);
    nCurrentOffset += osString.nSize;
    skipPDFEnding(&nCurrentOffset, pPdStruct);

    if (read_uint8(nCurrentOffset) == '%') {
        nCurrentOffset++;

        QByteArray baData;

        for (qint32 i = 0; (i < 40) && XBinary::isPdStructNotCanceled(pPdStruct); i++) {
            quint8 nChar = read_uint8(nCurrentOffset + i);

            if ((nChar == 13) || (nChar == 10) || (nChar == 0)) {
                break;
            }

            baData.append(nChar);
        }

        sResult = baData.toHex();
    }

    return sResult;
}

QString XPDF::getFilters(PDSTRUCT *pPdStruct)
{
    QString sResult;

    QList<XPART> listParts = getParts(100, pPdStruct);  // TODO limit
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
    // TODO limit

    QList<XBinary::FPART> listResult;

    qint64 nMaxOffset = 0;
    QList<OBJECT> listObject;

    const QList<STARTHREF> listStrartHrefs = findStartxrefs(0, pPdStruct);
    const qint64 totalSize = getSize();

    qint32 nNumberOfFrefs = listStrartHrefs.count();

    if (nNumberOfFrefs) {
        if (nFileParts & FILEPART_SIGNATURE) {
            const OS_STRING osHeader = _readPDFString(0, 20, pPdStruct);

            FPART record = {};

            record.filePart = FILEPART_SIGNATURE;
            record.nFileOffset = 0;
            record.nFileSize = osHeader.nSize;
            record.nVirtualAddress = -1;
            record.sName = tr("Signature");

            listResult.append(record);
        }

        for (int j = 0; (j < nNumberOfFrefs) && XBinary::isPdStructNotCanceled(pPdStruct); ++j) {
            const STARTHREF &startxref = listStrartHrefs.at(j);

            if (startxref.bIsXref) {
                if ((nFileParts & FILEPART_OBJECT) || (nFileParts & FILEPART_STREAM)) {
                    listObject.append(getObjectsFromStartxref(&startxref, pPdStruct));
                }

                if (nFileParts & FILEPART_TABLE) {
                    FPART record = {};

                    record.filePart = FILEPART_DATA;
                    record.nFileOffset = startxref.nXrefOffset;
                    record.nFileSize = startxref.nFooterOffset - startxref.nXrefOffset;
                    record.nVirtualAddress = -1;
                    record.sName = QStringLiteral("xref");

                    listResult.append(record);
                }
            } else if (startxref.bIsObject) {
                if ((nFileParts & FILEPART_OBJECT) || (nFileParts & FILEPART_STREAM)) {
                    listObject.append(findObjects(0, startxref.nFooterOffset, true, pPdStruct));
                }
            }

            // if (startxref.nFooterOffset - nCurrentOffset > 0) {
            //     // Trailer
            // }

            if (nFileParts & FILEPART_FOOTER) {
                FPART record = {};

                record.filePart = FILEPART_FOOTER;
                record.nFileOffset = startxref.nFooterOffset;
                record.nFileSize = startxref.nFooterSize;
                record.nVirtualAddress = -1;
                record.sName = tr("Footer");

                listResult.append(record);
            }
        }

        nMaxOffset = listStrartHrefs.at(nNumberOfFrefs - 1).nFooterOffset + listStrartHrefs.at(nNumberOfFrefs - 1).nFooterSize;
    } else {
        // File damaged;
        listObject.append(findObjects(0, -1, false, pPdStruct));

        qint32 nNumberOfObjects = listObject.count();

        if (nNumberOfObjects) {
            const OS_STRING osHeader = _readPDFString(0, 20, pPdStruct);

            if (nFileParts & FILEPART_SIGNATURE) {
                FPART record = {};

                record.filePart = FILEPART_SIGNATURE;
                record.nFileOffset = 0;
                record.nFileSize = osHeader.nSize;
                record.nVirtualAddress = -1;
                record.sName = tr("Header");

                listResult.append(record);
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
        FPART record = {};

        record.filePart = FILEPART_DATA;
        record.nFileOffset = 0;
        record.nFileSize = nMaxOffset;
        record.nVirtualAddress = -1;
        record.sName = tr("Data");

        listResult.append(record);
    }

    if ((nFileParts & FILEPART_STREAM) || (nFileParts & FILEPART_OBJECT)) {
        qint32 nNumberOfObjects = listObject.count();
        qint32 nStreamNumber = 0;

        for (qint32 i = 0; (i < nNumberOfObjects) && XBinary::isPdStructNotCanceled(pPdStruct); ++i) {
            const OBJECT &object = listObject.at(i);

            if (nFileParts & FILEPART_OBJECT) {
                FPART record = {};

                record.filePart = FILEPART_OBJECT;
                record.nFileOffset = object.nOffset;
                record.nFileSize = object.nSize;
                record.nVirtualAddress = -1;
                record.sName = QString("%1 %2").arg(tr("Object"), QString::number(object.nID));

                listResult.append(record);
            }

            if (nFileParts & FILEPART_STREAM) {
                XPART xpart = handleXpart(object.nOffset, object.nID, -1, pPdStruct);

                qint32 nNumberOfStreams = xpart.listStreams.count();

                for (qint32 j = 0; (j < nNumberOfStreams) && XBinary::isPdStructNotCanceled(pPdStruct); ++j) {
                    const STREAM &stream = xpart.listStreams.at(j);

                    XBinary::FPART record = {};
                    record.nFileOffset = stream.nOffset;
                    record.nFileSize = stream.nSize;
                    record.sName = QString("%1 obj (%2)").arg(tr("Stream"), QString::number(object.nID));
                    record.filePart = XBinary::FILEPART_STREAM;
                    record.nVirtualAddress = -1;

                    const QString sFilter = getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/Filter"), pPdStruct).var.toString();

                    if (sFilter == QLatin1String("/FlateDecode")) {
                        // ZLIB
                        record.mapProperties.insert(FPART_PROP_COMPRESSMETHOD, COMPRESS_METHOD_ZLIB);
                    } else if (sFilter == QLatin1String("/LZWDecode")) {
                        record.mapProperties.insert(FPART_PROP_COMPRESSMETHOD, COMPRESS_METHOD_LZW_PDF);
                        // record.mapProperties.insert(FPART_PROP_COMPRESSMETHOD, COMPRESS_METHOD_STORE);
                    } else if (sFilter == QLatin1String("/ASCII85Decode")) {
                        record.mapProperties.insert(FPART_PROP_COMPRESSMETHOD, COMPRESS_METHOD_ASCII85);
                    } else if (sFilter == QLatin1String("/DCTDecode")) {
                        // JPEG
                        record.mapProperties.insert(FPART_PROP_COMPRESSMETHOD, COMPRESS_METHOD_STORE);
                    } else if (sFilter == QLatin1String("/CCITTFaxDecode")) {
                        // JPEG
                        record.mapProperties.insert(FPART_PROP_COMPRESSMETHOD, COMPRESS_METHOD_STORE);
                    } else if (sFilter == QLatin1String("[")) {
#ifdef QT_DEBUG
                        qDebug() << "Unknown filter:" << sFilter << xpart.listParts << record.sName;
#endif
                    } else {
#ifdef QT_DEBUG
                        qDebug() << "Unknown filter:" << sFilter << xpart.listParts << record.sName;
#endif
                        // TODO
                        record.mapProperties.insert(FPART_PROP_COMPRESSMETHOD, COMPRESS_METHOD_STORE);
                    }

                    if (!sFilter.isEmpty()) {
                        if (getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/Subtype"), pPdStruct).var.toString() == QLatin1String("/Image")) {
                            const qint32 nWidth = getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/Width"), pPdStruct).var.toInt();
                            const qint32 nHeight = getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/Height"), pPdStruct).var.toInt();
                            const qint32 nBitsPerComponent = getFirstStringValueByKey(&(xpart.listParts), QLatin1String("/BitsPerComponent"), pPdStruct).var.toInt();

                            record.mapProperties.insert(FPART_PROP_FILETYPE, XBinary::FT_PNG);
                            record.mapProperties.insert(FPART_PROP_FILETYPE_EXTRA, XBinary::FT_DATA);
                            record.mapProperties.insert(FPART_PROP_WIDTH, nWidth);
                            record.mapProperties.insert(FPART_PROP_HEIGHT, nHeight);
                            record.mapProperties.insert(FPART_PROP_BITSPERCOMPONENT, nBitsPerComponent);
                            record.mapProperties.insert(FPART_PROP_EXT, QStringLiteral("png"));
                            record.mapProperties.insert(
                                FPART_PROP_INFO, QString("%1 (%2 x %3) [%4]")
                                                     .arg(tr("Raw image data"), QString::number(nWidth), QString::number(nHeight), QString::number(nBitsPerComponent)));
                        }
                    }

                    // qDebug() << "Filter:" << sFilter << xpart.listParts << record.sName;

                    // if (stream.nSize >= 6) {
                    //     quint16 nHeader = read_uint16(record.nFileOffset);
                    //     if ((nHeader == 0x5E78) || (nHeader == 0x9C78) || (nHeader == 0xDA78)) {

                    //         if (getFirstStringValueByKey(&(xpart.listParts), "/Subtype", pPdStruct).var.toString() == "/Image") {
                    //             qDebug() << xpart.listParts << record.sName;
                    //         }
                    //         compMethod = COMPRESS_METHOD_ZLIB;
                    //     }
                    // }

                    listResult.append(record);

                    nStreamNumber++;
                }
            }
        }
    }

    if (nFileParts & FILEPART_OVERLAY) {
        if (nMaxOffset < totalSize) {
            FPART record = {};

            record.filePart = FILEPART_OVERLAY;
            record.nFileOffset = nMaxOffset;
            record.nFileSize = totalSize - nMaxOffset;
            record.nVirtualAddress = -1;
            record.sName = tr("Overlay");

            listResult.append(record);
        }
    }

    return listResult;
}
