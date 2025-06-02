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

    sResult = _readPDFString(5, 5).sString;

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

QList<XPDF::OBJECT> XPDF::findObjects(PDSTRUCT *pPdStruct)
{
    QList<XPDF::OBJECT> listResult;

    qint64 nCurrentOffset = 0;

    while (XBinary::isPdStructNotCanceled(pPdStruct)) {
        OS_STRING osString = _readPDFString(nCurrentOffset, 100);

        if (_isObject(osString.sString)) {
            quint64 nID = getObjectID(osString.sString);
            qint64 nEndObjOffset = find_ansiString(nCurrentOffset + osString.nSize, -1, "endobj", pPdStruct);

            if (nEndObjOffset != -1) {
                OS_STRING osEndObj = _readPDFString(nEndObjOffset, 100);

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
            skipPDFEnding(&nCurrentOffset);
        } else {
            break;
        }
    }

    return listResult;
}

qint32 XPDF::skipPDFString(qint64 *pnOffset)
{
    OS_STRING osString = _readPDFString(*pnOffset, 20);
    *pnOffset += osString.nSize;

    return osString.nSize;
}

qint32 XPDF::skipPDFEnding(qint64 *pnOffset)
{
    qint32 nOrigOffset = *pnOffset;
    qint64 nSize = getSize() - *pnOffset;

    while (nSize > 0) {
        quint8 nChar = read_uint8(*pnOffset);
        if (nChar == 10) {
            (*pnOffset)++;
            nSize--;
        } else if ((nSize > 1) && (nChar == 13)) {
            (*pnOffset)++;
            nSize--;
            if (read_uint8(*pnOffset) == 10) {
                (*pnOffset)++;
                nSize--;
            }
        } else {
            break;
        }
    }

    return *pnOffset - nOrigOffset;
}

qint32 XPDF::skipPDFSpace(qint64 *pnOffset)
{
    qint32 nOrigOffset = *pnOffset;
    qint64 nSize = getSize() - *pnOffset;

    while (nSize > 0) {
        quint8 nChar = read_uint8(*pnOffset);
        if (nChar == ' ') {
            (*pnOffset)++;
            nSize--;
        } else {
            break;
        }
    }

    return *pnOffset - nOrigOffset;
}

QList<XPDF::OBJECT> XPDF::getObjectsFromStartxref(STARTHREF *pStartxref, PDSTRUCT *pPdStruct)
{
    QList<XPDF::OBJECT> listResult;

    qint64 nTotalSize = getSize();

    qint64 nCurrentOffset = pStartxref->nXrefOffset;

    OS_STRING osStringHref = _readPDFString(nCurrentOffset, 20);

    if (_isXref(osStringHref.sString)) {
        nCurrentOffset += osStringHref.nSize;

        QMap<qint64, quint64> mapObjects;

        while (XBinary::isPdStructNotCanceled(pPdStruct)) {
            OS_STRING osSection = _readPDFString(nCurrentOffset, 20);

            if (osSection.sString != "") {
                quint64 nID = osSection.sString.section(" ", 0, 0).toULongLong();
                quint64 nNumberOfObjects = osSection.sString.section(" ", 1, 1).toULongLong();

                nCurrentOffset += osSection.nSize;

                if (nNumberOfObjects) {
                    qint32 _nFreeIndex = XBinary::getFreeIndex(pPdStruct);
                    XBinary::setPdStructInit(pPdStruct, _nFreeIndex, nNumberOfObjects);

                    for (quint64 i = 0; (i < nNumberOfObjects) && XBinary::isPdStructNotCanceled(pPdStruct); i++) {
                        OS_STRING osObject = _readPDFString(nCurrentOffset, 20);

                        if (osObject.sString.section(" ", 2, 2) == "n") {
                            qint64 nObjectOffset = osObject.sString.section(" ", 0, 0).toULongLong();

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
        while (iterator.hasNext()) {
            iterator.next();

            OBJECT object;
            object.nOffset = iterator.key();
            object.nID = iterator.value();
            object.nSize = 0;  // Will be calculated later

            listResult.append(object);
        }

        qint32 nNumberOfObjects = listResult.count();
        // Calculate sizes based on consecutive offsets
        for (int i = 0; i < nNumberOfObjects - 1; i++) {
            listResult[i].nSize = listResult[i + 1].nOffset - listResult[i].nOffset;
        }

        // Handle the last object's size using nXrefOffset
        if (!listResult.isEmpty()) {
            listResult.last().nSize = pStartxref->nXrefOffset - listResult.last().nOffset;
        }
    }

    return listResult;
}

XBinary::OS_STRING XPDF::_readPDFString(qint64 nOffset, qint64 nSize)
{
    XBinary::OS_STRING result = {};

    result.nOffset = nOffset;

    if (nSize == -1) {
        nSize = getSize() - nOffset;
    }

    if (nOffset + nSize > getSize()) {
        nSize = getSize() - nOffset;
    }

    for (qint64 i = 0; i < nSize; i++) {
        quint8 nChar = read_uint8(nOffset + i);

        if ((nChar == 0) || (nChar == 13) || (nChar == 10)) {
            break;
        }

        result.nSize++;

        result.sString.append((char)nChar);
    }

    nOffset += result.nSize;

    result.nSize += skipPDFEnding(&nOffset);

    return result;
}

XBinary::OS_STRING XPDF::_readPDFStringPart_title(qint64 nOffset, qint64 nSize)
{
    XBinary::OS_STRING result = {};

    result.nOffset = nOffset;

    if (nSize == -1) {
        nSize = getSize() - nOffset;
    }

    if (nOffset + nSize > getSize()) {
        nSize = getSize() - nOffset;
    }

    for (qint64 i = 0; i < nSize; i++) {
        quint8 nChar = read_uint8(nOffset + i);

        if ((nChar == 0) || (nChar == 13) || (nChar == 10) || (nChar == '<')) {
            break;
        }

        result.nSize++;

        result.sString.append((char)nChar);
    }

    nOffset += result.nSize;

    result.nSize += skipPDFEnding(&nOffset);

    return result;
}

XBinary::OS_STRING XPDF::_readPDFStringPart(qint64 nOffset)
{
    XBinary::OS_STRING result = {};

    result.nOffset = nOffset;

    qint64 nSize = getSize() - nOffset;

    if (nSize > 0) {
        quint8 nChar = read_uint8(nOffset);

        if (nChar == '/') {
            result = _readPDFStringPart_const(nOffset);
        } else if (nChar == '(') {
            result = _readPDFStringPart_str(nOffset);
        } else if (nChar == '<') {
            if (nSize > 1) {
                if (read_uint8(nOffset + 1) == '<') {
                    result.sString = "<<";
                    result.nSize = 2;
                } else {
                    result = _readPDFStringPart_hex(nOffset);
                }
            }
        } else if (nChar == '>') {
            if (nSize > 1) {
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
            result = _readPDFStringPart_val(nOffset);
        }
    }

    nOffset += result.nSize;
    result.nSize += skipPDFSpace(&nOffset);
    result.nSize += skipPDFEnding(&nOffset);

    return result;
}

XBinary::OS_STRING XPDF::_readPDFStringPart_const(qint64 nOffset)
{
    XBinary::OS_STRING result = {};

    result.nOffset = nOffset;

    qint64 nSize = getSize() - nOffset;

    for (qint64 i = 0; i < nSize; i++) {
        quint8 nChar = read_uint8(nOffset + i);

        if ((nChar == 0) || (nChar == 10) || (nChar == 13) || (nChar == '[') || (nChar == ']') || (nChar == '<') || (nChar == '>') || (nChar == ' ') || (nChar == '(')) {
            break;
        }

        if (i > 0) {
            if (nChar == '/') {
                break;
            }
        }

        result.nSize++;
        result.sString.append((char)nChar);
    }

    return result;
}

XBinary::OS_STRING XPDF::_readPDFStringPart_str(qint64 nOffset)
{
    XBinary::OS_STRING result = {};

    result.nOffset = nOffset;

    qint64 nSize = getSize() - nOffset;

    bool bStart = false;
    bool bEnd = false;
    bool bUnicode = false;
    bool bBSlash = false;

    for (qint64 i = 0; i < nSize; i++) {
        if (!bUnicode) {
            quint8 nChar = read_uint8(nOffset + i);

            if ((nChar == 0) || (nChar == 10) || (nChar == 13)) {
                break;
            }

            if ((i == 0) && (nChar == '(')) {
                bStart = true;
                if (nSize >= 3) {
                    if ((read_uint8(nOffset + 1) == 0xFE) && (read_uint8(nOffset + 2) == 0xFF)) {
                        bUnicode = true;
                        result.nSize += 2;
                        i += 2;
                    }
                }
                result.sString.append('(');
            } else if ((nChar == ')') && (!bBSlash)) {
                result.sString.append(')');
                bEnd = true;
            } else if (nChar == '\\') {
                bBSlash = true;
            } else {
                if (bBSlash) {
                    bBSlash = false;
                }
                result.sString.append((char)nChar);
            }
            result.nSize++;
        } else if (nSize - i >= 2) {
            quint16 nChar = read_uint16(nOffset + i, true);

            if (((nChar >> 8) == ')') && (!bBSlash)) {
                result.sString.append(')');
                result.nSize++;
                bEnd = true;
            } else if (nChar == '\\') {
                bBSlash = true;
                i++;
                result.nSize += 2;
            } else {
                if (bBSlash) {
                    bBSlash = false;
                }
                result.sString.append(QChar(nChar));
                i++;
                result.nSize += 2;
            }
        } else {
            break;
        }

        if (bStart && bEnd) {
            break;
        }
    }

    return result;
}

XBinary::OS_STRING XPDF::_readPDFStringPart_val(qint64 nOffset)
{
    XBinary::OS_STRING result = {};

    result.nOffset = nOffset;

    qint64 nSize = getSize() - nOffset;

    bool bSpace = false;

    for (qint64 i = 0; i < nSize; i++) {
        quint8 nChar = read_uint8(nOffset + i);

        if ((nChar == 0) || (nChar == 10) || (nChar == 13) || (nChar == '[') || (nChar == ']') || (nChar == '<') || (nChar == '>') || (nChar == '/')) {
            break;
        }

        result.nSize++;

        if (nChar == ' ') {
            bSpace = true;
            break;
        }

        result.sString.append((char)nChar);
    }

    if (bSpace) {
        if ((nSize - result.nSize) >= 3) {
            QString sSuffix = read_ansiString(nOffset + result.nSize, 3);
            if (sSuffix == "0 R") {
                result.sString.append(" " + sSuffix);
                result.nSize += 3;
            }
        }
    }

    return result;
}

XBinary::OS_STRING XPDF::_readPDFStringPart_hex(qint64 nOffset)
{
    XBinary::OS_STRING result = {};

    result.nOffset = nOffset;

    qint64 nSize = getSize() - nOffset;

    for (qint64 i = 0; i < nSize; i++) {
        quint8 nChar = read_uint8(nOffset + i);

        if ((i == 0) && (nChar != '<')) {
            break;
        }

        result.nSize++;
        result.sString.append((char)nChar);

        if (nChar == '>') {
            break;
        }
    }

    return result;
}

QList<XBinary::MAPMODE> XPDF::getMapModesList()
{
    QList<MAPMODE> listResult;

    listResult.append(MAPMODE_OBJECTS);

    return listResult;
}

XBinary::_MEMORY_MAP XPDF::getMemoryMap(MAPMODE mapMode, PDSTRUCT *pPdStruct)
{
    Q_UNUSED(mapMode)
    // TODO Check streams
    XBinary::_MEMORY_MAP result = {};

    result.nBinarySize = getSize();
    result.fileType = FT_PDF;
    result.endian = ENDIAN_UNKNOWN;
    result.mode = MODE_32;
    result.sArch = getArch();
    result.sType = getTypeAsString();

    qint32 nIndex = 0;
    qint64 nMaxOffset = 0;
    bool bValid = false;

    QList<STARTHREF> listStrartHrefs = findStartxrefs(0, pPdStruct);

    qint32 nNumberOfFrefs = listStrartHrefs.count();

    if (nNumberOfFrefs) {
        bValid = true;

        {
            OS_STRING osHeader = _readPDFString(0, 20);

            _MEMORY_RECORD record = {};

            record.nIndex = nIndex++;
            record.type = MMT_HEADER;
            record.nOffset = 0;
            record.nSize = osHeader.nSize;
            record.nAddress = -1;
            record.sName = tr("Header");

            result.listRecords.append(record);
        }

        for (int j = 0; (j < nNumberOfFrefs) && XBinary::isPdStructNotCanceled(pPdStruct); j++) {
            STARTHREF startxref = listStrartHrefs.at(j);

            if (startxref.bIsXref) {
                QList<OBJECT> listObject = getObjectsFromStartxref(&startxref, pPdStruct);

                qint32 nNumberOfObjects = listObject.count();

                for (qint32 i = 0; (i < nNumberOfObjects) && XBinary::isPdStructNotCanceled(pPdStruct); i++) {
                    _MEMORY_RECORD record = {};

                    record.nIndex = nIndex++;
                    record.type = MMT_OBJECT;
                    record.nOffset = listObject.at(i).nOffset;
                    record.nSize = listObject.at(i).nSize;
                    record.nID = listObject.at(i).nID;
                    record.nAddress = -1;
                    record.sName = QString("%1 %2").arg(tr("Object"), QString::number(record.nID));

                    result.listRecords.append(record);
                }

                {
                    _MEMORY_RECORD record = {};

                    record.nIndex = nIndex++;
                    record.type = MMT_DATA;
                    record.nOffset = startxref.nXrefOffset;
                    record.nSize = startxref.nFooterOffset - startxref.nXrefOffset;
                    record.nAddress = -1;
                    record.sName = QString("xref");

                    result.listRecords.append(record);
                }
            } else {
                // TODO
            }

            // if (startxref.nFooterOffset - nCurrentOffset > 0) {
            //     // Trailer
            // }

            {
                _MEMORY_RECORD record = {};

                record.nIndex = nIndex++;
                record.type = MMT_FOOTER;
                record.nOffset = startxref.nFooterOffset;
                record.nSize = startxref.nFooterSize;
                record.nAddress = -1;
                record.sName = tr("Footer");

                result.listRecords.append(record);
            }
        }

        nMaxOffset = listStrartHrefs.at(nNumberOfFrefs - 1).nFooterOffset + listStrartHrefs.at(nNumberOfFrefs - 1).nFooterSize;
    } else {
        // File damaged;
        QList<OBJECT> listObject = findObjects(pPdStruct);

        qint32 nNumberOfObjects = listObject.count();

        if (nNumberOfObjects) {
            bValid = true;

            {
                OS_STRING osHeader = _readPDFString(0, 20);

                _MEMORY_RECORD record = {};

                record.nIndex = nIndex++;
                record.type = MMT_HEADER;
                record.nOffset = 0;
                record.nSize = osHeader.nSize;
                record.nAddress = -1;
                record.sName = tr("Header");

                result.listRecords.append(record);

                nMaxOffset = record.nSize;
            }
        }

        for (qint32 i = 0; (i < nNumberOfObjects) && XBinary::isPdStructNotCanceled(pPdStruct); i++) {
            _MEMORY_RECORD record = {};

            record.nIndex = nIndex++;
            record.type = MMT_OBJECT;
            record.nOffset = listObject.at(i).nOffset;
            record.nSize = listObject.at(i).nSize;
            record.nID = listObject.at(i).nID;
            record.nAddress = -1;
            record.sName = QString("%1 %2").arg(tr("Object"), QString::number(record.nID));

            result.listRecords.append(record);

            nMaxOffset = listObject.at(i).nOffset + listObject.at(i).nSize;
        }
    }

    if (bValid) {
        if (nMaxOffset < result.nBinarySize) {
            _MEMORY_RECORD record = {};

            record.nIndex = nIndex++;
            record.type = MMT_OVERLAY;
            record.nOffset = nMaxOffset;
            record.nSize = result.nBinarySize - nMaxOffset;
            record.nAddress = -1;
            record.sName = tr("Overlay");

            result.listRecords.append(record);
        }
    }

    // std::sort(result.listRecords.begin(), result.listRecords.end(), compareMemoryMapRecord);

    return result;
}

QList<XPDF::STARTHREF> XPDF::findStartxrefs(qint64 nOffset, PDSTRUCT *pPdStruct)
{
    QList<XPDF::STARTHREF> listResult;

    while (XBinary::isPdStructNotCanceled(pPdStruct)) {
        qint64 nStartXref = find_signature(nOffset, -1, "'startxref'", nullptr, pPdStruct);  // \n \r

        if (nStartXref != -1) {
            qint64 nCurrent = nStartXref;

            OS_STRING osStartXref = _readPDFString(nCurrent, 20);

            nCurrent += osStartXref.nSize;

            OS_STRING osOffset = _readPDFString(nCurrent, 20);

            qint64 _nOffset = osOffset.sString.toLongLong();

            OS_STRING osHref = _readPDFString(_nOffset, 20);

            bool bIsXref = _isXref(osHref.sString);
            bool bIsObject = _isObject(osHref.sString);

            if (bIsXref || bIsObject) {
                if (_nOffset < nCurrent) {
                    nCurrent += osOffset.nSize;

                    OS_STRING osEnd = _readPDFString(nCurrent, 20);

                    QString _sEndOfFile = osEnd.sString;

                    _sEndOfFile.resize(5, QChar(' '));

                    if (_sEndOfFile == "%%EOF") {
                        nCurrent += 5;

                        if (read_uint8(nCurrent) == 13) {
                            nCurrent++;  // Skip \r
                        }

                        if (read_uint8(nCurrent) == 10) {
                            nCurrent++;  // Skip \n
                        }

                        STARTHREF record = {};

                        record.nXrefOffset = _nOffset;
                        record.nFooterOffset = nStartXref;
                        record.nFooterSize = nCurrent - nStartXref;
                        record.bIsObject = bIsObject;
                        record.bIsXref = bIsXref;

                        listResult.append(record);

                        if (osEnd.sString.size() != 5) {
                            break;
                        }

                        OS_STRING osAppend = _readPDFString(nCurrent, 20);

                        if ((!_isObject(osAppend.sString)) && (!_isComment(osAppend.sString)) && (!_isXref(osAppend.sString))) {
                            break;  // No append
                        }
                    }
                }
            }
        } else {
            break;
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
        // TODO Read Object
        bool bStop = false;
        OS_STRING osString = _readPDFStringPart_title(nOffset, 20);

        // qDebug("%llX", nOffset);

        if (result.nID == 0) {
            result.nID = getObjectID(osString.sString);
        }
        // #ifdef QT_DEBUG
        //         qDebug(">%s", osString.sString.toUtf8().data());
        //         bool bShowDebug = false;
        //         if (osString.sString == "16037 0 obj") {
        //             bShowDebug = true;
        //             qDebug("endobj");
        //         }
        // #endif
        nOffset += osString.nSize;

        if (_isObject(osString.sString)) {
            qint32 nObj = 0;
            qint32 nCol = 0;
            qint32 nPartCount = 0;
            while (XBinary::isPdStructNotCanceled(pPdStruct)) {
                OS_STRING osStringPart = _readPDFStringPart(nOffset);

                if ((nPartCount < nPartLimit) || (nPartLimit == -1)) {
                    result.listParts.append(osStringPart.sString);
                    nPartCount++;
                } else {
                    bStop = true;
                    break;
                }

                // #ifdef QT_DEBUG
                //                 if (bShowDebug) {
                //                     qDebug("#%s", osStringPart.sString.toUtf8().data());
                //                 }
                // #endif
                nOffset += osStringPart.nSize;

                if (osStringPart.sString == "") {
                    break;
                }

                if (osStringPart.sString == "<<") {
                    nObj++;
                } else if (osStringPart.sString == ">>") {
                    nObj--;
                } else if (osStringPart.sString == "[") {
                    nCol++;
                } else if (osStringPart.sString == "]") {
                    nCol--;
                } else if (osStringPart.sString == "/Length") {
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
        } else if (osString.sString == "stream") {
            STREAM stream = {};
            stream.nOffset = nOffset;
            if (sLength.toInt()) {
                stream.nSize = sLength.toInt();
            } else if (sLength.section(" ", 2, 2) == "R") {
                QString sPattern = sLength.replace("R", "obj");
                qint64 nObjectOffset = find_ansiString(nOffset, -1, sPattern, pPdStruct);

                if (nObjectOffset != -1) {
                    skipPDFString(&nObjectOffset);
                    OS_STRING osLen = _readPDFStringPart_val(nObjectOffset);

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
                skipPDFEnding(&nOffset);

                result.listStreams.append(stream);
            }
        } else if (osString.sString == "endstream") {
            // TODO
        } else if (_isEndObject(osString.sString)) {
            break;
        } else if (osString.sString == "") {
            break;
        }
    }

    result.nSize = nOffset - result.nOffset;

    return result;
}

bool XPDF::_isObject(const QString &sString)
{
    return (sString.section(" ", 2, 2) == "obj");
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
        if ((sString.mid(0, 3) == QString("(D:")) && (sString.at(nSize - 1) == QChar(')'))) {
            bResult = true;
        }
    }

    return bResult;
}

bool XPDF::_isEndObject(const QString &sString)
{
    return (sString.trimmed() == "endobj");
}

bool XPDF::_isComment(const QString &sString)
{
    bool bResult = false;

    if (sString.size() > 0) {
        bResult = (sString.at(0) == QChar('%'));
    }

    return bResult;
}

bool XPDF::_isXref(const QString &sString)
{
    bool bResult = false;

    if (sString.size() > 0) {
        bResult = (sString.section(" ", 0, 0) == "xref");
    }

    return bResult;
}

QString XPDF::_getCommentString(const QString &sString)
{
    QString sResult;

    if (sString.size() > 0) {
        if (sString.at(0) == QChar('%')) {
            sResult = sString.section("%", 1, -1);
        }
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
    return sString.section(" ", 0, 0).toInt();
}

QList<XBinary::XVARIANT> XPDF::getValuesByKey(QList<XPART> *pListObjects, const QString &sKey, PDSTRUCT *pPdStruct)
{
    QList<XVARIANT> listResult;
    QSet<QString> stVars;

    qint32 nNumberOfRecords = pListObjects->count();

    qint32 _nFreeIndex = XBinary::getFreeIndex(pPdStruct);
    XBinary::setPdStructInit(pPdStruct, _nFreeIndex, nNumberOfRecords);

    for (qint32 i = 0; (i < nNumberOfRecords) && XBinary::isPdStructNotCanceled(pPdStruct); i++) {
        XPART record = pListObjects->at(i);

        qint32 nNumberOfParts = record.listParts.count();

        for (qint32 j = 0; (j < nNumberOfParts) && XBinary::isPdStructNotCanceled(pPdStruct); j++) {
            QString sPart = record.listParts.at(j);

            if (sPart == sKey) {
                if ((j + 1) < nNumberOfParts) {
                    QString sValue = record.listParts.at(j + 1);
                    XVARIANT varValue;

                    if (sValue.toLongLong()) {
                        varValue.varType = XBinary::VT_INT64;
                        varValue.var = sValue.toLongLong();
                    } else if (_isDateTime(sValue)) {
                        varValue.varType = XBinary::VT_DATETIME;
                        varValue.var = _getDateTime(sValue);
                    } else if (_isString(sValue)) {
                        varValue.varType = XBinary::VT_STRING;
                        varValue.var = _getString(sValue);
                    } else if (_isHex(sValue)) {
                        varValue.varType = XBinary::VT_HEX;
                        varValue.var = _getHex(sValue);
                    }

                    if (!varValue.var.isNull()) {
                        if (!stVars.contains(varValue.var.toString())) {
                            listResult.append(varValue);
                            stVars.insert(varValue.var.toString());
                        }
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

QString XPDF::getHeaderCommentAsHex()
{
    QString sResult;

    qint64 nCurrentOffset = 0;

    OS_STRING osString = _readPDFString(nCurrentOffset, 100);
    nCurrentOffset += osString.nSize;
    skipPDFEnding(&nCurrentOffset);

    if (read_uint8(nCurrentOffset) == '%') {
        nCurrentOffset++;

        QByteArray baData;

        for (qint32 i = 0; i < 40; i++) {
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

QList<XBinary::FPART> XPDF::getFileParts(PDSTRUCT *pPdStruct)
{
    QList<XBinary::FPART> listResult;

    QList<XPDF::XPART> listParts = getParts(-1, pPdStruct);

    qint32 nNumberOfParts = listParts.count();
    qint32 nStreamNumber = 0;

    for (qint32 i = 0; i < nNumberOfParts; i++) {
        XPART xpart = listParts.at(i);

        qint32 nNumberOfStreams = xpart.listStreams.count();

        for (qint32 j = 0; j < nNumberOfStreams; j++) {
            STREAM stream = xpart.listStreams.at(j);

            XBinary::FPART fpart = {};
            fpart.nOffset = stream.nOffset;
            fpart.nSize = stream.nSize;
            fpart.sName = QString("%1 %2").arg(tr("Stream"), QString::number(nStreamNumber));

            listResult.append(fpart);

            nStreamNumber++;
        }
    }

    return listResult;
}

QList<XPDF::XPART> XPDF::getParts(qint32 nPartLimit, PDSTRUCT *pPdStruct)
{
    QList<XPART> listResult;

    QList<STARTHREF> listStrartHrefs = findStartxrefs(0, pPdStruct);

    qint32 nNumberOfHrefs = listStrartHrefs.count();

    QList<OBJECT> listObject;
    if (nNumberOfHrefs) {
        for (int i = 0; (i < nNumberOfHrefs) && XBinary::isPdStructNotCanceled(pPdStruct); i++) {
            STARTHREF startxref = listStrartHrefs.at(i);

            listObject = getObjectsFromStartxref(&startxref, pPdStruct);
        }
    } else {
        listObject = findObjects(pPdStruct);
    }

    qint32 nNumberOfObjects = listObject.count();

    if (nNumberOfObjects) {
        qint32 _nFreeIndex = XBinary::getFreeIndex(pPdStruct);
        XBinary::setPdStructInit(pPdStruct, _nFreeIndex, nNumberOfObjects);

        for (qint32 i = 0; (i < nNumberOfObjects) && XBinary::isPdStructNotCanceled(pPdStruct); i++) {
            OBJECT record = listObject.at(i);

            XPART xpart = handleXpart(record.nOffset, record.nID, nPartLimit, pPdStruct);

            listResult.append(xpart);

            XBinary::setPdStructCurrentIncrement(pPdStruct, _nFreeIndex);
        }

        XBinary::setPdStructFinished(pPdStruct, _nFreeIndex);
    }

    return listResult;
}

XBinary::FILEFORMATINFO XPDF::getFileFormatInfo(PDSTRUCT *pPdStruct)
{
    FILEFORMATINFO result = {};

    result.bIsValid = isValid(pPdStruct);

    if (result.bIsValid) {
        _MEMORY_MAP memoryMap = getMemoryMap(MAPMODE_OBJECTS, pPdStruct);

        result.nSize = _calculateRawSize(&memoryMap, pPdStruct);
        result.fileType = memoryMap.fileType;
        result.sExt = getFileFormatExt();
        result.sVersion = getVersion();
        result.sOptions = getOptions();
        result.osName = getOsName();
        result.sOsVersion = getOsVersion();
        result.sArch = memoryMap.sArch;
        result.mode = memoryMap.mode;
        result.sType = memoryMap.sType;
        result.endian = memoryMap.endian;
        result.nNumberOfRecords = XBinary::getNumberOfMemoryMapTypeRecords(&memoryMap, MMT_OBJECT);

        if (result.nSize == 0) {
            result.bIsValid = false;
        }
    }

    return result;
}
