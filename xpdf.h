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
#ifndef XPDF_H
#define XPDF_H

#include "xbinary.h"
#include "xpdf_def.h"
#include "xarchive.h"
#include "xpdfcrypt.h"

class XPDF : public XBinary {
    Q_OBJECT

public:
    struct INTERNAL_INFO : XBinary::INTERNAL_INFO {};

    virtual bool handleInternalInfo(PDSTRUCT *pPdStruct) override;
    virtual void *getInternalInfo(PDSTRUCT *pPdStruct) override;
    virtual void setInternalInfo(void *pInternalInfo) override;

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

    explicit XPDF(QIODevice *pDevice);

    bool isValid(PDSTRUCT *pPdStruct = nullptr) override;
    static bool isValid(QIODevice *pDevice, PDSTRUCT *pPdStruct = nullptr);
    QString getVersion() override;
    FT getFileType() override;
    ENDIAN getEndian() override;
    qint64 getFileFormatSize(PDSTRUCT *pPdStruct) override;
    QString getFileFormatExt() override;
    QString getFileFormatExtsString() override;
    MODE getMode() override;
    QString getMIMEString() override;

    QList<MAPMODE> getMapModesList() override;
    _MEMORY_MAP getMemoryMap(MAPMODE mapMode = MAPMODE_UNKNOWN, PDSTRUCT *pPdStruct = nullptr) override;

    // Offset of the "%PDF-" header (may be non-zero: BOM / mail-mangled / embedded). -1 if absent.
    qint64 getHeaderOffset(PDSTRUCT *pPdStruct = nullptr);

    QList<STARTHREF> findStartxrefs(qint64 nOffset, PDSTRUCT *pPdStruct);
    QList<OBJECT> getObjectsFromStartxref(const STARTHREF *pStartxref, PDSTRUCT *pPdStruct);
    // Decode a PDF 1.5+ cross-reference stream (/Type /XRef). Fills *pbIsXrefStream when the target is a decodable xref stream.
    QList<OBJECT> getObjectsFromXrefStream(qint64 nXrefOffset, bool *pbIsXrefStream, PDSTRUCT *pPdStruct);
    QList<OBJECT> findObjects(qint64 nOffset, qint64 nSize, bool bDeepScan, PDSTRUCT *pPdStruct);
    OS_STRING _readPDFString(qint64 nOffset, qint64 nSize, PDSTRUCT *pPdStruct);
    OS_STRING _readPDFStringPart_title(qint64 nOffset, qint64 nSize, PDSTRUCT *pPdStruct);
    OS_STRING _readPDFStringPart(qint64 nOffset, PDSTRUCT *pPdStruct);
    OS_STRING _readPDFStringPart_const(qint64 nOffset, PDSTRUCT *pPdStruct);
    OS_STRING _readPDFStringPart_str(qint64 nOffset, PDSTRUCT *pPdStruct);
    OS_STRING _readPDFStringPart_val(qint64 nOffset, PDSTRUCT *pPdStruct);
    OS_STRING _readPDFStringPart_hex(qint64 nOffset, PDSTRUCT *pPdStruct);
    qint32 skipPDFEnding(qint64 *pnOffset, PDSTRUCT *pPdStruct);
    qint32 skipPDFSpace(qint64 *pnOffset, PDSTRUCT *pPdStruct);
    qint32 skipPDFString(qint64 *pnOffset, PDSTRUCT *pPdStruct);
    XPART handleXpart(qint64 nOffset, qint32 nID, qint32 nPartLimit, PDSTRUCT *pPdStruct, bool bResolveStreams = true,
                      const QMap<quint64, qint64> *pMapObjOffsets = nullptr);
    static bool _isObject(const QString &sString);
    static bool _isString(const QString &sString);
    static bool _isHex(const QString &sString);
    static bool _isDateTime(const QString &sString);
    static bool _isEndObject(const QString &sString);
    static bool _isComment(const QString &sString);
    static bool _isXref(const QString &sString);
    static bool _isIndirectRef(const QString &sString);  // "N G R" as a single fused token
    static QString _getCommentString(const QString &sString);
    static QString _getString(const QString &sString);
    static QString _getHex(const QString &sString);
    static QDateTime _getDateTime(const QString &sString);
    static qint32 getObjectID(const QString &sString);

    QList<XPART> getParts(qint32 nPartLimit, PDSTRUCT *pPdStruct = nullptr);
    static QList<XVARIANT> getValuesByKey(QList<XPART> *pListObjects, const QString &sKey, PDSTRUCT *pPdStruct = nullptr);
    static XVARIANT getFirstStringValueByKey(QList<QString> *pListStrings, const QString &sKey, PDSTRUCT *pPdStruct = nullptr);
    // Resolve a /Filter value (single name or array [...]) into an ordered list of filter names.
    static QStringList _resolveFilterList(const QList<QString> *pListParts, const QString &sKey, PDSTRUCT *pPdStruct = nullptr);
    // For a /Filespec dictionary: the declared file name (/UF preferred, else /F), and the embedded-file object id.
    static QString _resolveFilespecName(const QList<QString> *pListParts, PDSTRUCT *pPdStruct = nullptr);
    static qint32 _resolveEmbeddedRefId(const QList<QString> *pListParts, PDSTRUCT *pPdStruct = nullptr);

    qint32 getType() override;
    QString typeIdToString(qint32 nType) override;

    QString getHeaderCommentAsHex(PDSTRUCT *pPdStruct);

    QList<FPART> getFileParts(quint32 nFileParts, qint32 nLimit = -1, PDSTRUCT *pPdStruct = nullptr) override;

    QString getFilters(PDSTRUCT *pPdStruct = nullptr);
    QString getInfo(PDSTRUCT *pPdStruct = nullptr) override;

    // Forensic/triage summaries (reuse the parsed object list; safe on hostile input).
    QString getMetaInfoString(QList<XPART> *pListObjects, PDSTRUCT *pPdStruct = nullptr);
    QString getSuspiciousInfoString(QList<XPART> *pListObjects, PDSTRUCT *pPdStruct = nullptr);
    // Encryption: descriptor ("Standard V4 R4 128-bit AESV2 P=...") or empty. isEncrypted() overrides the
    // XBinary virtual so FILEFORMATINFO::bIsEncrypted (and thus Formats/SpecAbstract/DiE) reports it.
    QString getEncryption(PDSTRUCT *pPdStruct = nullptr);
    bool isEncrypted() override;

    // Decryption (Standard security handler). Derives the file key for the given user password (empty by
    // default -- covers owner-only-protected PDFs) and lets encrypted strings/streams be recovered.
    bool setupDecryption(const QByteArray &baUserPassword = QByteArray(), PDSTRUCT *pPdStruct = nullptr);
    bool isDecryptable() const;
    QByteArray decryptContent(const QByteArray &baData, quint64 nObjNum, quint32 nGeneration = 0);
    QString getDecryptedMetaInfoString(QList<XPART> *pListObjects, PDSTRUCT *pPdStruct = nullptr);
    // Extract embedded JavaScript (/JS inline strings, hex, or indirect streams) and emulate it via XJSEmul
    // (only when built with USE_PDFJSEMUL; otherwise returns empty). Reveals deobfuscated payloads + exploit APIs.
    QString getJavaScriptInfoString(QList<XPART> *pListObjects, PDSTRUCT *pPdStruct = nullptr);
    QString getEncryptionInfoString(QList<XPART> *pListObjects, PDSTRUCT *pPdStruct = nullptr);
    QString getLinearizedInfoString(QList<XPART> *pListObjects, PDSTRUCT *pPdStruct = nullptr);

    virtual QMap<UNPACK_PROP, QVariant> getDefaultUnpackProperties() override;
    bool initUnpack(UNPACK_STATE *pState, const QMap<UNPACK_PROP, QVariant> &mapProperties, PDSTRUCT *pPdStruct = nullptr) override;
    ARCHIVERECORD infoCurrent(UNPACK_STATE *pState, PDSTRUCT *pPdStruct = nullptr) override;
    bool unpackCurrent(UNPACK_STATE *pState, QIODevice *pDevice, PDSTRUCT *pPdStruct = nullptr) override;
    bool moveToNext(UNPACK_STATE *pState, PDSTRUCT *pPdStruct = nullptr) override;
    bool finishUnpack(UNPACK_STATE *pState, PDSTRUCT *pPdStruct = nullptr) override;

    QList<QString> getSearchSignatures() override;
    XBinary *createInstance(QIODevice *pDevice, bool bIsImage = false, XADDR nModuleAddress = -1) override;

    static XVARIANT _parseValue(const QString &sValue);

    // Apply a PDF predictor (PNG >=10 / TIFF 2) to decoded stream data in place. Returns true on success.
    static bool _applyPredictor(QByteArray *pbaData, qint32 nPredictor, qint32 nColumns, qint32 nColors, qint32 nBitsPerComponent);

private:
    struct UNPACK_CONTEXT {
        QList<XBinary::FPART> listStreams;
        qint32 nCurrentStreamIndex;
    };

    // Populate the per-instance structural cache (header offset, startxrefs, object list, id->offset index).
    void scanStructure(PDSTRUCT *pPdStruct);
    // Raw string-value bytes for a key ("(...)"/"<hex>" -> QByteArray); trailer /ID[0] bytes.
    static QByteArray _getRawBytesByKey(const QList<QString> *pListParts, const QString &sKey);
    QByteArray findTrailerID(PDSTRUCT *pPdStruct);
    // Resolve an object's file offset by id: index first, then a bounded scan. -1 if not found.
    qint64 resolveObjectOffset(quint64 nID, const QMap<quint64, qint64> *pMapObjOffsets, qint64 nHint, PDSTRUCT *pPdStruct);
    // Boundary-checked "N 0 obj" header search in a byte range (rejects prefix matches like 12 inside 112). -1 if absent.
    qint64 findObjectHeaderInRange(quint64 nID, qint64 nStart, qint64 nSize, PDSTRUCT *pPdStruct);

private:
    INTERNAL_INFO m_internalInfo;

    bool m_bStructScanned;
    qint64 m_nHeaderOffset;
    QList<STARTHREF> m_listStartHrefs;
    QList<OBJECT> m_listObjectsCache;
    QMap<quint64, qint64> m_mapObjIdOffset;

    bool m_bDecryptChecked;
    bool m_bDecryptReady;
    XPDFCrypt::SECURITY m_security;
    QByteArray m_baFileKey;
};

#endif  // XPDF_H
