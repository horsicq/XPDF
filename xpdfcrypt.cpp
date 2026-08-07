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
#include "xpdfcrypt.h"
#include "xaesdecoder.h"

#include <QCryptographicHash>
#include <cstring>

namespace {
// PDF standard password padding string (ISO 32000 Table 20).
const quint8 g_pad[32] = {0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41, 0x64, 0x00, 0x4E, 0x56, 0xFF, 0xFA, 0x01, 0x08,
                          0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68, 0x3E, 0x80, 0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A};

QByteArray md5(const QByteArray &baData)
{
    return QCryptographicHash::hash(baData, QCryptographicHash::Md5);
}
}  // namespace

QByteArray XPDFCrypt::padPassword(const QByteArray &baPassword)
{
    QByteArray baResult = baPassword.left(32);
    const qint32 nNeed = 32 - baResult.size();
    if (nNeed > 0) {
        baResult.append(reinterpret_cast<const char *>(g_pad), nNeed);
    }
    return baResult;
}

QByteArray XPDFCrypt::rc4(const QByteArray &baKey, const QByteArray &baData)
{
    if (baKey.isEmpty()) {
        return baData;
    }

    quint8 s[256];
    for (qint32 i = 0; i < 256; ++i) {
        s[i] = static_cast<quint8>(i);
    }

    qint32 j = 0;
    const qint32 nKeyLen = baKey.size();
    for (qint32 i = 0; i < 256; ++i) {
        j = (j + s[i] + static_cast<quint8>(baKey.at(i % nKeyLen))) & 0xFF;
        const quint8 t = s[i];
        s[i] = s[j];
        s[j] = t;
    }

    QByteArray baResult(baData.size(), Qt::Uninitialized);
    qint32 a = 0;
    qint32 b = 0;
    for (qint32 k = 0; k < baData.size(); ++k) {
        a = (a + 1) & 0xFF;
        b = (b + s[a]) & 0xFF;
        const quint8 t = s[a];
        s[a] = s[b];
        s[b] = t;
        const quint8 nKeyStream = s[(s[a] + s[b]) & 0xFF];
        baResult[k] = static_cast<char>(static_cast<quint8>(baData.at(k)) ^ nKeyStream);
    }

    return baResult;
}

QByteArray XPDFCrypt::aesCbcDecrypt(const QByteArray &baKey, const QByteArray &baIV, const QByteArray &baCipher, bool bStripPadding)
{
    if (baCipher.isEmpty() || ((baCipher.size() % 16) != 0) || (baIV.size() < 16) || baKey.isEmpty()) {
        return QByteArray();
    }

    QByteArray baResult(baCipher.size(), Qt::Uninitialized);
    if (!XAESDecoder::decryptAESCBC(baKey, baIV, reinterpret_cast<const quint8 *>(baCipher.constData()), reinterpret_cast<quint8 *>(baResult.data()),
                                    baCipher.size())) {
        return QByteArray();
    }

    if (bStripPadding && !baResult.isEmpty()) {
        const qint32 nPad = static_cast<quint8>(baResult.at(baResult.size() - 1));
        if ((nPad >= 1) && (nPad <= 16) && (nPad <= baResult.size())) {
            baResult.chop(nPad);
        }
    }

    return baResult;
}

QByteArray XPDFCrypt::aesCbcEncryptNoPad(const QByteArray &baKey, const QByteArray &baIV, const QByteArray &baPlain)
{
    if (baPlain.isEmpty() || ((baPlain.size() % 16) != 0) || (baIV.size() < 16) || baKey.isEmpty()) {
        return QByteArray();
    }

    CUSTOM_AES_KEY aesKey;
    if (XAESDecoder::custom_aes_set_encrypt_key(reinterpret_cast<const quint8 *>(baKey.constData()), baKey.size() * 8, &aesKey) != 0) {
        return QByteArray();
    }

    QByteArray baResult(baPlain.size(), Qt::Uninitialized);
    quint8 nPrev[16];
    memcpy(nPrev, baIV.constData(), 16);

    for (qint32 nOffset = 0; nOffset + 16 <= baPlain.size(); nOffset += 16) {
        quint8 nBlock[16];
        for (qint32 i = 0; i < 16; ++i) {
            nBlock[i] = static_cast<quint8>(baPlain.at(nOffset + i)) ^ nPrev[i];
        }
        quint8 nOut[16];
        XAESDecoder::custom_aes_encrypt(nBlock, nOut, &aesKey);
        memcpy(reinterpret_cast<quint8 *>(baResult.data()) + nOffset, nOut, 16);
        memcpy(nPrev, nOut, 16);
    }

    return baResult;
}

// ISO 32000-2 Algorithm 2.B (the R6 password hash).
QByteArray XPDFCrypt::hashR6(const QByteArray &baPassword, const QByteArray &baSalt, const QByteArray &baUserData)
{
    QByteArray baK = QCryptographicHash::hash(baPassword + baSalt + baUserData, QCryptographicHash::Sha256);

    qint32 nRound = 0;
    while (true) {
        // K1 = (password + K + userdata) repeated 64 times.
        QByteArray baBlock = baPassword + baK + baUserData;
        QByteArray baK1;
        baK1.reserve(baBlock.size() * 64);
        for (qint32 i = 0; i < 64; ++i) {
            baK1.append(baBlock);
        }

        // E = AES-128-CBC-encrypt(K1) with key K[0:16], IV K[16:32], no padding.
        const QByteArray baKey = baK.left(16);
        const QByteArray baIV = baK.mid(16, 16);
        const QByteArray baE = aesCbcEncryptNoPad(baKey, baIV, baK1);
        if (baE.isEmpty()) {
            return QByteArray();
        }

        // Next hash chosen by (sum of first 16 bytes of E) mod 3.
        qint32 nSum = 0;
        for (qint32 i = 0; i < 16; ++i) {
            nSum += static_cast<quint8>(baE.at(i));
        }
        const qint32 nMod = nSum % 3;
        if (nMod == 0) {
            baK = QCryptographicHash::hash(baE, QCryptographicHash::Sha256);
        } else if (nMod == 1) {
            baK = QCryptographicHash::hash(baE, QCryptographicHash::Sha384);
        } else {
            baK = QCryptographicHash::hash(baE, QCryptographicHash::Sha512);
        }

        ++nRound;
        if ((nRound >= 64) && (static_cast<quint8>(baE.at(baE.size() - 1)) <= static_cast<quint8>(nRound - 32))) {
            break;
        }
    }

    return baK.left(32);
}

QByteArray XPDFCrypt::computeFileKey(const SECURITY &security, const QByteArray &baPassword, bool *pbPasswordOk)
{
    if (pbPasswordOk) {
        *pbPasswordOk = false;
    }

    if (security.nR >= 5) {
        // AES-256 (R5/R6). Validate: hash(password, U validation salt) == U[0:32]; then file key from UE.
        if ((security.baU.size() < 48) || (security.baUE.size() < 32)) {
            return QByteArray();
        }
        const QByteArray baValidationSalt = security.baU.mid(32, 8);
        const QByteArray baKeySalt = security.baU.mid(40, 8);

        const QByteArray baCheck = hashR6(baPassword, baValidationSalt, QByteArray());
        const bool bOk = (baCheck.left(32) == security.baU.left(32));
        if (pbPasswordOk) {
            *pbPasswordOk = bOk;
        }
        if (!bOk) {
            return QByteArray();  // wrong password: cannot recover the file key
        }

        const QByteArray baIntermediate = hashR6(baPassword, baKeySalt, QByteArray());
        // File key = AES-256-CBC-decrypt(UE) with intermediate key, zero IV, no padding.
        const QByteArray baZeroIV(16, '\0');
        return aesCbcDecrypt(baIntermediate, baZeroIV, security.baUE.left(32), false);
    }

    // R2-R4 (Algorithm 2).
    if (security.baO.size() < 32) {
        return QByteArray();
    }

    QByteArray baInput = padPassword(baPassword);
    baInput.append(security.baO.left(32));

    const quint32 nP = static_cast<quint32>(static_cast<qint32>(security.nP));
    char nPBytes[4];
    nPBytes[0] = static_cast<char>(nP & 0xFF);
    nPBytes[1] = static_cast<char>((nP >> 8) & 0xFF);
    nPBytes[2] = static_cast<char>((nP >> 16) & 0xFF);
    nPBytes[3] = static_cast<char>((nP >> 24) & 0xFF);
    baInput.append(nPBytes, 4);

    baInput.append(security.baID);

    if ((security.nR >= 4) && (!security.bEncryptMetadata)) {
        const char nFF[4] = {static_cast<char>(0xFF), static_cast<char>(0xFF), static_cast<char>(0xFF), static_cast<char>(0xFF)};
        baInput.append(nFF, 4);
    }

    QByteArray baKey = md5(baInput);

    const qint32 nKeyBytes = qBound(5, security.nKeyBytes, 16);
    if (security.nR >= 3) {
        for (qint32 i = 0; i < 50; ++i) {
            baKey = md5(baKey.left(nKeyBytes));
        }
    }
    baKey = baKey.left(nKeyBytes);

    if (pbPasswordOk) {
        *pbPasswordOk = validateUserPassword(security, baKey);
    }

    return baKey;
}

bool XPDFCrypt::validateUserPassword(const SECURITY &security, const QByteArray &baKey)
{
    if (security.baU.size() < 16) {
        return false;
    }

    if (security.nR == 2) {
        // Algorithm 4: U = RC4(key, pad).
        const QByteArray baU = rc4(baKey, QByteArray(reinterpret_cast<const char *>(g_pad), 32));
        return (baU.left(32) == security.baU.left(32));
    }

    // Algorithm 5 (R>=3): U = RC4-chain(MD5(pad + ID)); compare first 16 bytes.
    QByteArray baInput(reinterpret_cast<const char *>(g_pad), 32);
    baInput.append(security.baID);
    QByteArray baU = rc4(baKey, md5(baInput));

    for (qint32 i = 1; i <= 19; ++i) {
        QByteArray baStepKey(baKey.size(), Qt::Uninitialized);
        for (qint32 b = 0; b < baKey.size(); ++b) {
            baStepKey[b] = static_cast<char>(static_cast<quint8>(baKey.at(b)) ^ static_cast<quint8>(i));
        }
        baU = rc4(baStepKey, baU);
    }

    return (baU.left(16) == security.baU.left(16));
}

QByteArray XPDFCrypt::decryptObject(const QByteArray &baData, quint64 nObjNum, quint32 nGeneration, const SECURITY &security, const QByteArray &baFileKey)
{
    if (baFileKey.isEmpty() || baData.isEmpty()) {
        return baData;
    }

    if (security.nV >= 5) {
        // AES-256: the file key is used directly; first 16 bytes are the IV.
        if (baData.size() <= 16) {
            return QByteArray();
        }
        const QByteArray baIV = baData.left(16);
        return aesCbcDecrypt(baFileKey, baIV, baData.mid(16), true);
    }

    // Per-object key (Algorithm 1): MD5(fileKey + objNum[3] + gen[2] + ("sAlT" for AES)).
    QByteArray baInput = baFileKey;
    char nObjBytes[5];
    nObjBytes[0] = static_cast<char>(nObjNum & 0xFF);
    nObjBytes[1] = static_cast<char>((nObjNum >> 8) & 0xFF);
    nObjBytes[2] = static_cast<char>((nObjNum >> 16) & 0xFF);
    nObjBytes[3] = static_cast<char>(nGeneration & 0xFF);
    nObjBytes[4] = static_cast<char>((nGeneration >> 8) & 0xFF);
    baInput.append(nObjBytes, 5);

    if (security.bAES) {
        const char nSalt[4] = {0x73, 0x41, 0x6C, 0x54};  // "sAlT"
        baInput.append(nSalt, 4);
    }

    const qint32 nObjKeyLen = qMin(baFileKey.size() + 5, 16);
    const QByteArray baObjKey = md5(baInput).left(nObjKeyLen);

    if (security.bAES) {
        if (baData.size() <= 16) {
            return QByteArray();
        }
        const QByteArray baIV = baData.left(16);
        return aesCbcDecrypt(baObjKey, baIV, baData.mid(16), true);
    }

    return rc4(baObjKey, baData);
}
