/* Copyright (c) 2022 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
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

XPDF::XPDF(QIODevice *pDevice): XBinary(pDevice)
{

}

bool XPDF::isValid()
{
    bool bResult=false;

    // TODO more checks
    if(getSize()>4)
    {
        if(read_uint32(0)==0x46445025) // '%PDF'
        {
            bResult=true;
        }
    }

    return bResult;
}

QString XPDF::getVersion()
{
    QString sResult;

    sResult=read_ansiString(5,4);

    return sResult;
}

XBinary::FT XPDF::getFileType()
{
    return FT_PDF;
}

bool XPDF::isBigEndian()
{
    return false;
}

qint64 XPDF::getFileFormatSize()
{
    qint64 nResult=0;
    qint64 nOffset=0;

    while(true)
    {
        qint64 nCurrent=find_signature(nOffset,-1,"'startxref'");

        if(nCurrent!=-1)
        {
            OS_STRING osStartXref=readPDFString(nCurrent);

            nCurrent+=osStartXref.nSize;

            OS_STRING osOffset=readPDFString(nCurrent);

            qint64 _nOffset=osOffset.sString.toLongLong();

            if((_nOffset>0)&&(_nOffset<nCurrent))
            {
                nCurrent+=osOffset.nSize;

                OS_STRING osEnd=readPDFString(nCurrent);

                if(osEnd.sString=="%%EOF")
                {
                    nCurrent+=osEnd.nSize;
                    nResult=nCurrent;

                    break;
                }
            }
        }
        else
        {
            nResult=0;
            break;
        }

        nOffset=nCurrent+10;
    }

    return nResult;
}

qint64 XPDF::findStartxref()
{
    qint64 nResult=-1;
    qint64 nSize=getSize();

    qint64 nOffset=qMax((qint64)0,nSize-0x1000);  // TODO const

    bool bFound=false;

    while(true)
    {
        qint64 nCurrent=find_signature(nOffset,-1,"'startxref'"); // \n \r

        if(nCurrent==-1)
        {
            break;
        }

        bFound=true;

        nOffset=nCurrent+10; // Get the last
    }

    if(bFound)
    {
        QString sOffset=readPDFString(nOffset).sString;

        nResult=sOffset.toULongLong();

        if(nResult==0)
        {
            nResult=-1;
        }
    }

    return nResult;
}

QMap<QString,QString> XPDF::readTrailer()
{
    QMap<QString,QString> mapResult;

    qint64 nSize=getSize();

    qint64 nOffset=qMax((qint64)0,nSize-0x1000);  // TODO const

    bool bFound=false;

    while(true)
    {
        qint64 nCurrent=find_signature(nOffset,-1,"'trailer'");

        if(nCurrent==-1)
        {
            break;
        }

        bFound=true;

        nOffset=nCurrent+8; // Get the last
    }

    if(bFound)
    {
        bool bValid=false;

        while(true)
        {
            QString sRecord=readPDFString(nOffset).sString;

            if(sRecord=="<<")
            {
                bValid=true;
            }
            else if(bValid&&XBinary::isRegExpPresent("^\\/",sRecord))
            {
                QString _sRecord=sRecord.section("/",1,-1);
                QString sName=_sRecord.section(" ",0,0);
                QString sValue=_sRecord.section(" ",1,-1);

                mapResult.insert(sName,sValue);
            }
            else if((sRecord=="")||(sRecord==">>"))
            {
                break;
            }

            nOffset+=sRecord.size()+1;
        }
    }

    return mapResult;
}

XBinary::OS_STRING XPDF::readPDFString(qint64 nOffset)
{
    OS_STRING result={};

    result.nOffset=nOffset;

    // TODO optimize
    for(qint32 i=0;i<65535;i++)
    {
        QString sSymbol=read_ansiString(nOffset+i,1);

        if(sSymbol!="")
        {
            result.nSize++;
        }

        if((sSymbol=="")||(sSymbol=="\r")||(sSymbol=="\n")) // TODO more checks
        {
            break;
        }

        result.sString.append(sSymbol);
    }

    return result;
}

void XPDF::getInfo()
{
    // TODO all streams
    QMap<QString,QString> mapTrailer=readTrailer();
    qint64 nStartxref=findStartxref();

    QList<XPDF_DEF::OBJECT> listObjects;

    if(nStartxref!=-1)
    {
        // TODO "xref"
        qint64 nOffset=nStartxref;

        bool bValid=false;

//        while(true)
        while(true) // TODO size from trailer
        {
            OS_STRING osRecord=readPDFString(nOffset);

            if(osRecord.sString=="")
            {
                break;
            }

            if(osRecord.sString=="xref")
            {
                bValid=true;
            }
            else
            {
                QString sID=osRecord.sString.section(" ",0,0);
                qint32 nNumberOfObjects=osRecord.sString.section(" ",1,1).toUInt();

                bool bLast=false;

                if(sID==mapTrailer.value("Size"))
                {
                    bLast=true;
                }

                nOffset+=osRecord.nSize;

                for(qint32 i=0;i<nNumberOfObjects;i++)
                {
                    OS_STRING osString=readPDFString(nOffset);

                    XPDF_DEF::OBJECT record={};

                    record.nOffset=osString.sString.section(" ",0,0).toULongLong();
                    record.nID=sID.toULongLong()+osString.sString.section(" ",1,1).toULongLong();

                    QString sStatus=osString.sString.section(" ",2,2);

                    if(sStatus=="f")
                    {
                        record.bIsFree=true;
                    }

                    record.nSize=getObjectSize(record.nOffset);

                    listObjects.append(record);

                    qDebug("%s",osString.sString.toLatin1().data());

                    nOffset+=osString.nSize;
                }

                if(bLast)
                {
                    break;
                }
            }

            qDebug("%s",osRecord.sString.toLatin1().data());

            nOffset+=osRecord.nSize; // Check
        }
    }

    qint32 nNumberOfRecords=listObjects.count();

    for(qint32 i=0;i<nNumberOfRecords;i++)
    {
        QString sRecord=QString("%1 %2 %3").arg(QString::number(i),QString::number(listObjects.at(i).nOffset),QString::number(listObjects.at(i).nSize));

        qDebug("%s",sRecord.toLatin1().data());
    }
}

qint64 XPDF::getObjectSize(qint64 nOffset)
{
    qint64 _nOffset=nOffset;

    while(true)
    {
        OS_STRING osString=readPDFString(_nOffset);
        _nOffset+=osString.nSize;

        if(osString.sString=="")
        {
            break;
        }
        // TODO XXX XXX "obj"
        if((osString.sString=="endobj"))
        {
            break;
        }
    }

    return _nOffset-nOffset;
}

