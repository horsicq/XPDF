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

    if(getSize()>4)
    {
        if(read_uint32(0)==0x46445025) // %PDF
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
        QString sOffset=readPDFValue(nOffset);

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
        qint64 nCurrent=find_signature(nOffset,-1,"'trailer'"); // \n \r

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
            QString sRecord=readPDFValue(nOffset);

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

QString XPDF::readPDFValue(qint64 nOffset)
{
    QString sResult;

    // TODO optimize
    for(qint32 i=0;i<65535;i++)
    {
        QString sSymbol=read_ansiString(nOffset+i,1);

        if((sSymbol=="")||(sSymbol=="\r")) // TODO more checks
        {
            break;
        }

        sResult.append(sSymbol);
    }

    return sResult;
}

void XPDF::getInfo()
{
    QMap<QString,QString> mapTrailer=readTrailer();
    qint64 nStartxref=findStartxref();

    if(nStartxref!=-1)
    {
        // TODO "xref"
        qint64 nOffset=nStartxref;
        QString sRecord=readPDFValue(nOffset);
        nOffset+=sRecord.size()+1;
    }
}

