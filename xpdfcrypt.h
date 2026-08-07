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
#ifndef XPDFCRYPT_H
#define XPDFCRYPT_H

#include <QByteArray>

// PDF Standard security handler (ISO 32000 7.6.3/7.6.4): file-key derivation and per-object decryption
// for revisions 2-6 (RC4 40/128-bit, AESV2/AES-128, AESV3/AES-256). Pure algorithm: MD5/SHA via
// QCryptographicHash, AES-CBC via XAESDecoder, RC4 inline. No parsing here -- XPDF fills SECURITY.
class XPDFCrypt {
public:
    struct SECURITY {
        qint32 nV;
        qint32 nR;
        qint32 nKeyBytes;  // key length in bytes (Length/8; 5 for R2, 16 for R3/R4, 32 for R6)
        qint64 nP;         // permissions bitmask (signed 32-bit)
        bool bAES;         // AESV2 or AESV3
        bool bAES256;      // AESV3 (R6/V5)
        bool bEncryptMetadata;
        QByteArray baO;    // /O (>=32 bytes; R6: 48)
        QByteArray baU;    // /U (>=32 bytes; R6: 48)
        QByteArray baOE;   // /OE (R6)
        QByteArray baUE;   // /UE (R6)
        QByteArray baID;   // trailer /ID[0]

        SECURITY() : nV(0), nR(0), nKeyBytes(5), nP(0), bAES(false), bAES256(false), bEncryptMetadata(true)
        {
        }
    };

    // Derive the file encryption key from the (user) password. Sets *pbPasswordOk to whether the password
    // validates against /U. Returns the key (empty on unsupported/failed derivation).
    static QByteArray computeFileKey(const SECURITY &security, const QByteArray &baPassword, bool *pbPasswordOk);

    // Decrypt one object's string/stream bytes given the derived file key.
    static QByteArray decryptObject(const QByteArray &baData, quint64 nObjNum, quint32 nGeneration, const SECURITY &security, const QByteArray &baFileKey);

    // Primitives (exposed for testing).
    static QByteArray rc4(const QByteArray &baKey, const QByteArray &baData);
    static QByteArray aesCbcDecrypt(const QByteArray &baKey, const QByteArray &baIV, const QByteArray &baCipher, bool bStripPadding);

private:
    static QByteArray padPassword(const QByteArray &baPassword);
    static bool validateUserPassword(const SECURITY &security, const QByteArray &baKey);
    static QByteArray hashR6(const QByteArray &baPassword, const QByteArray &baSalt, const QByteArray &baUserData);
    static QByteArray aesCbcEncryptNoPad(const QByteArray &baKey, const QByteArray &baIV, const QByteArray &baPlain);
};

#endif  // XPDFCRYPT_H
