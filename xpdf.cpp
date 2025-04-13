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
            qint64 nObjectSize = getObjectSize(nCurrentOffset, pPdStruct);

            XPDF::OBJECT record = {};

            record.nOffset = nCurrentOffset;
            record.nSize = nObjectSize;
            record.nID = getObjectID(osString.sString);

            listResult.append(record);
        } else if (_isComment(osString.sString)) {
            nCurrentOffset += osString.nSize;
        } else {
            break;
        }
    }

    return listResult;
}

void XPDF::skipPDFString(qint64 *pnOffset)
{
    OS_STRING osString = _readPDFString(*pnOffset, 20);
    *pnOffset += osString.nSize;
}

QList<XPDF::OBJECT> XPDF::getObjectsFromStartxref(STARTHREF *pStartxref, PDSTRUCT *pPdStruct)
{
    QList<XPDF::OBJECT> listResult;

    qint64 nCurrentOffset = pStartxref->nXrefOffset;

    skipPDFString(&nCurrentOffset);

    QList<qint64> listObjectOffsets;
    quint64 nID = 0;

    OS_STRING osSection = _readPDFString(nCurrentOffset, 20);

    // nID = osSection.sString.section(" ", 0, 0).toULongLong();

    quint64 nNumberOfObjects = osSection.sString.section(" ", 1, 1).toULongLong();

    nCurrentOffset += osSection.nSize;

    if (nNumberOfObjects) {
        for (quint64 i = 0; i < nNumberOfObjects; i++) {
            OS_STRING osObject = _readPDFString(nCurrentOffset, 20);

            if (i > 0) {
                qint64 nObjectOffset = osObject.sString.section(" ", 0, 0).toULongLong();

                listObjectOffsets.append(nObjectOffset);
            }

            nCurrentOffset += osObject.nSize;
        }
    }

    qint32 nNumberOfOffsets = listObjectOffsets.count();

    for (qint32 i = 0; i < nNumberOfOffsets; i++) {
        OBJECT record = {};

        record.nOffset = listObjectOffsets.at(i);
        record.nSize = getObjectSize(record.nOffset, pPdStruct);
        record.nID = getObjectID(_readPDFString(record.nOffset, 20).sString);

        listResult.append(record);
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

    bool bSubstring = false;
    bool bUnicode = false;

    for (qint64 i = 0; i < nSize; i++) {
        if (!bUnicode) {
            quint8 nChar = read_uint8(nOffset + i);

            if (nChar == 0) {
                break;
            }

            if (nChar == 0xFE) {
                if (read_uint8(nOffset + i + 1) == 0xFF) {
                    bUnicode = true;
                    result.nSize++;
                    i++;
                }
            }

            if (nChar == '(') {
                bSubstring = true;
            } else if (nChar == ')') {
                bSubstring = false;
                bUnicode = false;
            }

            result.nSize++;

            if (nChar == 10) {
                break;
            } else if (nChar == 13) {
                quint8 _nChar = read_uint8(nOffset + i + 1);

                if (_nChar == 10) {
                    result.nSize++;
                }

                break;
            } else {
                if (!bUnicode) {
                    result.sString.append((char)nChar);
                }
            }
        } else {
            quint16 nChar = read_uint16(nOffset + i, true);

            if (nChar == 0) {
                break;
            }

            if (nChar == 0x290A) {
                bSubstring = false;
                bUnicode = false;
                result.sString.append((char)0x29);
                result.nSize += 2;

                break;
            } else {
                result.sString.append(QChar(nChar));
                result.nSize += 2;
                i++;
            }
        }
    }

    return result;
}

QList<XBinary::MAPMODE> XPDF::getMapModesList()
{
    QList<MAPMODE> listResult;

    listResult.append(MAPMODE_REGIONS);

    return listResult;
}

XBinary::_MEMORY_MAP XPDF::getMemoryMap(MAPMODE mapMode, PDSTRUCT *pPdStruct)
{
    Q_UNUSED(mapMode)
    // TODO Check streams
    PDSTRUCT pdStructEmpty = {};

    if (!pPdStruct) {
        pdStructEmpty = XBinary::createPdStruct();
        pPdStruct = &pdStructEmpty;
    }

    XBinary::_MEMORY_MAP result = {};

    result.nBinarySize = getSize();

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

            nMaxOffset = record.nSize;
        }

        for (int j = 0; j < nNumberOfFrefs; j++) {
            STARTHREF startxref = listStrartHrefs.at(j);

            QList<OBJECT> listObject = getObjectsFromStartxref(&startxref, pPdStruct);

            qint32 nNumberOfObjects = listObject.count();

            for (qint32 i = 0; i < nNumberOfObjects; i++) {
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

        for (qint32 i = 0; i < nNumberOfObjects; i++) {
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

    return result;
}

QList<XPDF::STARTHREF> XPDF::findStartxrefs(qint64 nOffset, PDSTRUCT *pPdStruct)
{
    QList<XPDF::STARTHREF> listResult;

    XBinary::PDSTRUCT pdStructEmpty = {};

    if (!pPdStruct) {
        pdStructEmpty = XBinary::createPdStruct();
        pPdStruct = &pdStructEmpty;
    }

    while (!(pPdStruct->bIsStop)) {
        qint64 nStartXref = find_signature(nOffset, -1, "'startxref'", nullptr, pPdStruct);  // \n \r

        if (nStartXref != -1) {
            qint64 nCurrent = nStartXref;

            OS_STRING osStartXref = _readPDFString(nCurrent, 20);

            nCurrent += osStartXref.nSize;

            OS_STRING osOffset = _readPDFString(nCurrent, 20);

            qint64 _nOffset = osOffset.sString.toLongLong();

            OS_STRING osHref = _readPDFString(_nOffset, 20);

            if ((osHref.sString == "xref") && (_nOffset < nCurrent)) {
                nCurrent += osOffset.nSize;

                OS_STRING osEnd = _readPDFString(nCurrent, 20);

                if (osEnd.sString == "%%EOF") {
                    nCurrent += osEnd.nSize;

                    STARTHREF record = {};

                    record.nXrefOffset = _nOffset;
                    record.nFooterOffset = nStartXref;
                    record.nFooterSize = nCurrent - nStartXref;

                    listResult.append(record);

                    OS_STRING osAppend = _readPDFString(nCurrent, 20);

                    if ((!_isObject(osAppend.sString)) && (!_isComment(osAppend.sString))){
                        break;  // No append
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

QList<XPDF::TRAILERRECORD> XPDF::readTrailer(PDSTRUCT *pPdStruct)
{
    XBinary::PDSTRUCT pdStructEmpty = {};

    if (!pPdStruct) {
        pdStructEmpty = XBinary::createPdStruct();
        pPdStruct = &pdStructEmpty;
    }

    QList<XPDF::TRAILERRECORD> listResult;

    qint64 nSize = getSize();

    qint64 nOffset = qMax((qint64)0, nSize - 0x1000);  // TODO const

    bool bFound = false;

    while (true) {
        qint64 nCurrent = find_signature(nOffset, -1, "'trailer'", 0, pPdStruct);

        if (nCurrent == -1) {
            break;
        }

        bFound = true;

        nOffset = nCurrent + 8;  // Get the last
    }

    if (bFound) {
        bool bValid = false;

        while (true) {
            OS_STRING osString = _readPDFString(nOffset, 20);

            if (osString.sString == "<<") {
                bValid = true;
            } else if (bValid && XBinary::isRegExpPresent("^\\/", osString.sString)) {
                QString _sRecord = osString.sString.section("/", 1, -1);

                TRAILERRECORD record = {};

                record.sName = _sRecord.section(" ", 0, 0);
                record.sValue = _sRecord.section(" ", 1, -1);

                listResult.append(record);
            } else if ((osString.sString == "") || (osString.sString == ">>")) {
                break;
            }

            nOffset += osString.nSize;
        }
    }

    return listResult;
}

XBinary::OS_STRING XPDF::readPDFValue(qint64 nOffset)
{
    OS_STRING result = {};

    result.nOffset = nOffset;

    quint16 nBOF = read_uint16(nOffset);

    if (nBOF == 0xFFFE) {
        nOffset += 2;

        qint64 _nOffset = nOffset;

        while (true) {
            quint16 nWord = read_uint16(_nOffset, true);
            if ((nWord == 0) || (nWord == 0x290a)) {
                break;
            }

            _nOffset += 2;
        }

        qint32 nSize = (_nOffset - nOffset) / 2;

        result.sString = read_unicodeString(nOffset, nSize, true);
        result.nSize = 2 + result.sString.size() * 2;
    }

    return result;
}

void XPDF::getInfo()
{
}

qint64 XPDF::getObjectSize(qint64 nOffset, PDSTRUCT *pPdStruct)
{
    qint64 _nOffset = nOffset;

    QString sLength;

    while (!(pPdStruct->bIsStop)) {
        // TODO Read Object
        OS_STRING osString = _readPDFString(_nOffset, 60);
        _nOffset += osString.nSize;

        if (_getRecordName(osString.sString) == "Length") {
            sLength = _getRecordValue(osString.sString);
        } else if (osString.sString == "stream") {
            break; // TODO
        } else if (_isEndObject(osString.sString)) {
            break;
        } else if (osString.sString == "") {
            break;
        }
    }

    return _nOffset - nOffset;
}

QString XPDF::_getRecordName(const QString &sString)
{
    QString sResult;

    if (sString.size() > 0) {
        if (sString.at(0) == QChar('/')) {
            sResult = sString.section("/", 1, -1).section(" ", 0, 0);
        }
    }

    return sResult;
}

QString XPDF::_getRecordValue(const QString &sString)
{
    QString sResult;

    if (sString.size() > 0) {
        if (sString.at(0) == QChar('/')) {
            sResult = sString.section("/", 1, -1).section(" ", 1, -1);
        }
    }

    return sResult;
}

bool XPDF::_isObject(const QString &sString)
{
    return (sString.section(" ", 2, 2) == "obj");
}

bool XPDF::_isEndObject(const QString &sString)
{
    return (sString == "endobj");
}

bool XPDF::_isComment(const QString &sString)
{
    bool bResult = false;

    if (sString.size() > 0) {
        bResult = (sString.at(0) == QChar('%'));
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

qint32 XPDF::getObjectID(const QString &sString)
{
    return sString.section(" ", 0, 0).toInt();
}
