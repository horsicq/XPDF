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

    sResult = _readPDFStringX(5, 5).sString;

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

    XPDF::OBJECT record = {};

    while (XBinary::isPdStructNotCanceled(pPdStruct)) {
        OS_STRING osString = _readPDFStringX(nCurrentOffset, 100);

        if (osString.sString.section(" ", 2, 2) == "obj") {
            record.nOffset = nCurrentOffset;
            record.nSize = 0;
            record.nID = osString.sString.section(" ", 0, 0).toULongLong();
        } else if (osString.sString == "endobj") {
            record.nSize = (nCurrentOffset + osString.nSize) - record.nOffset;
            listResult.append(record);
        }

        nCurrentOffset += osString.nSize;

        if (osString.nSize == 0) {
            break;
        }
    }

    // QList<qint64> listObjectOffsets;

    // for(qint32 i = 1; XBinary::isPdStructNotCanceled(pPdStruct); i++) {
    //     nOffset = find_ansiString(nOffset, -1, "obj", pPdStruct);
    // }

    return listResult;
}

void XPDF::skipPDFString(qint64 *pnOffset)
{
    OS_STRING osString = _readPDFStringX(*pnOffset, 20);
    *pnOffset += osString.nSize;
}

QList<XPDF::OBJECT> XPDF::getObjectsFromStartxref(STARTHREF *pStartxref, PDSTRUCT *pPdStruct)
{
    QList<XPDF::OBJECT> listResult;

    qint64 nCurrentOffset = pStartxref->nXrefOffset;

    skipPDFString(&nCurrentOffset);

    QList<qint64> listObjectOffsets;
    quint64 nID = 0;

    OS_STRING osSection = _readPDFStringX(nCurrentOffset, 20);

    // nID = osSection.sString.section(" ", 0, 0).toULongLong();

    quint64 nNumberOfObjects = osSection.sString.section(" ", 1, 1).toULongLong();

    nCurrentOffset += osSection.nSize;

    if (nNumberOfObjects) {
        for (quint64 i = 0; i < nNumberOfObjects; i++) {
            OS_STRING osObject = _readPDFStringX(nCurrentOffset, 20);

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

        if (i == (nNumberOfOffsets - 1)) {
            record.nSize = pStartxref->nXrefOffset - record.nOffset;
        } else {
            record.nSize = listObjectOffsets.at(i + 1) - record.nOffset;
        }

        record.nID = _readPDFStringX(record.nOffset, 20).sString.section(" ", 0, 0).toULongLong();

        listResult.append(record);
    }

    return listResult;
}

XBinary::OS_STRING XPDF::_readPDFStringX(qint64 nOffset, qint64 nSize)
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

    {
        OS_STRING osHeader = _readPDFStringX(0, 20);

        _MEMORY_RECORD record = {};

        record.nIndex = nIndex++;
        record.type = MMT_HEADER;
        record.nOffset = 0;
        record.nSize = osHeader.nSize + 1;
        record.nAddress = -1;
        record.sName = tr("Header");

        result.listRecords.append(record);

        nMaxOffset = record.nSize;
    }

    QList<STARTHREF> listStrartHrefs = findStartxrefs(0, pPdStruct);

    qint32 nNumberOfFrefs = listStrartHrefs.count();

    if (nNumberOfFrefs) {
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

            OS_STRING osStartXref = _readPDFStringX(nCurrent, 20);

            nCurrent += osStartXref.nSize;

            OS_STRING osOffset = _readPDFStringX(nCurrent, 20);

            qint64 _nOffset = osOffset.sString.toLongLong();

            OS_STRING osHref = _readPDFStringX(_nOffset, 20);

            if ((osHref.sString == "xref") && (_nOffset < nCurrent)) {
                nCurrent += osOffset.nSize;

                OS_STRING osEnd = _readPDFStringX(nCurrent, 20);

                if (osEnd.sString == "%%EOF") {
                    nCurrent += osEnd.nSize;

                    STARTHREF record = {};

                    record.nXrefOffset = _nOffset;
                    record.nFooterOffset = nStartXref;
                    record.nFooterSize = nCurrent - nStartXref;

                    listResult.append(record);

                    OS_STRING osAppend = _readPDFStringX(nCurrent, 20);

                    if (osAppend.sString.section(" ", 2, 2) != "obj") {
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
            OS_STRING osString = _readPDFStringX(nOffset, 20);

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

XBinary::OS_STRING XPDF::_readPDFString(qint64 nOffset)
{
    OS_STRING result = {};

    result.nOffset = nOffset;

    // TODO optimize
    for (qint32 i = 0; i < 65535; i++) {
        QString sSymbol = read_ansiString(nOffset + i, 1);

        if (sSymbol != "") {
            result.nSize++;
        }

        if ((sSymbol == "") || (sSymbol == "\n"))  // TODO more checks
        {
            break;
        }

        if (sSymbol == "\r") {
            QString _sSymbol = read_ansiString(nOffset + i + 1, 1);

            if (_sSymbol == "\n") {
                result.nSize++;
            }

            break;
        }

        result.sString.append(sSymbol);

        if (sSymbol == "(") {
            OS_STRING _unicode = readPDFValue(nOffset + i + 1);

            result.sString.append(_unicode.sString);
            i += _unicode.nSize;
            result.nSize += _unicode.nSize;
        }
    }

    return result;
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

    while (!(pPdStruct->bIsStop)) {
        // TODO Read Object
        OS_STRING osString = _readPDFStringX(_nOffset, 60);
        _nOffset += osString.nSize;

        if (osString.sString == "") {
            break;
        }
        // TODO XXX XXX "obj"
        if ((osString.sString == "endobj")) {
            break;
        }
    }

    return _nOffset - nOffset;
}
