/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <sal/config.h>

#include <com/sun/star/uno/RuntimeException.hpp>
#include <comphelper/hash.hxx>
#include <rtl/ustring.hxx>
#include <rtl/alloc.h>
#include <osl/endian.h>
#include <config_oox.h>
#include <sstream>
#include <iomanip>

#if USE_TLS_NSS
#include <nss.h>
#include <nspr.h>
#include <sechash.h>
#elif USE_TLS_OPENSSL
#include <openssl/evp.h>
#include <openssl/sha.h>
#endif // USE_TLS_OPENSSL

namespace comphelper {

std::string hashToString(const std::vector<unsigned char>& rHash)
{
    std::stringstream aStringStream;
    for (auto& i: rHash)
    {
        aStringStream << std::setw(2) << std::setfill('0') << std::hex << int(i);
    }

    return aStringStream.str();
}

struct HashImpl
{

#if USE_TLS_NSS
    HASHContext* mpContext;

    HASH_HashType getNSSType() const
    {
        switch (meType)
        {
            case HashType::MD5:
                return HASH_AlgMD5;
            case HashType::SHA1:
                return HASH_AlgSHA1;
            case HashType::SHA256:
                return HASH_AlgSHA256;
            case HashType::SHA384:
                return HASH_AlgSHA384;
            case HashType::SHA512:
                return HASH_AlgSHA512;
        }

        return HASH_AlgNULL;
    }
#elif USE_TLS_OPENSSL
    EVP_MD_CTX* mpContext;

    const EVP_MD* getOpenSSLType() const
    {
        switch (meType)
        {
            case HashType::MD5:
                return EVP_md5();
            case HashType::SHA1:
                return EVP_sha1();
            case HashType::SHA256:
                return EVP_sha256();
            case HashType::SHA384:
                return EVP_sha384();
            case HashType::SHA512:
                return EVP_sha512();
        }

        return nullptr;
    }
#endif

    HashType const meType;

    HashImpl(HashType eType):
        meType(eType)
    {

#if USE_TLS_NSS
        if (!NSS_IsInitialized())
        {
            auto const e = NSS_NoDB_Init(nullptr);
            if (e != SECSuccess)
            {
                PRErrorCode error = PR_GetError();
                const char* errorText = PR_ErrorToName(error);
                throw css::uno::RuntimeException("NSS_NoDB_Init failed with " + OUString(errorText, strlen(errorText), RTL_TEXTENCODING_UTF8) + " (" + OUString::number(static_cast<int>(error)) + ")");
            }
        }
        mpContext = HASH_Create(getNSSType());
#elif USE_TLS_OPENSSL
        mpContext = EVP_MD_CTX_create();
#endif

        initialize();
    }

    void initialize()
    {
#if USE_TLS_NSS
        HASH_Begin(mpContext);
#elif USE_TLS_OPENSSL
        EVP_DigestInit_ex(mpContext, getOpenSSLType(), nullptr);
#endif
    }

    ~HashImpl()
    {
#if USE_TLS_NSS
        HASH_Destroy(mpContext);
#elif USE_TLS_OPENSSL
        EVP_MD_CTX_destroy(mpContext);
#endif
    }
};

Hash::Hash(HashType eType):
    mpImpl(new HashImpl(eType))
{
}

Hash::~Hash()
{
}

void Hash::update(const void* pInput, size_t length)
{
#if USE_TLS_NSS
    HASH_Update(mpImpl->mpContext, static_cast<const unsigned char*>(pInput), length);
#elif USE_TLS_OPENSSL
    EVP_DigestUpdate(mpImpl->mpContext, pInput, length);
#else
    (void)pInput;
    (void)length;
#endif
}

void Hash::initialize()
{
    mpImpl->initialize();
}

std::vector<unsigned char> Hash::finalize()
{
    std::vector<unsigned char> hash(getLength(), 0);
    unsigned int digestWrittenLength;
#if USE_TLS_NSS
    HASH_End(mpImpl->mpContext, hash.data(), &digestWrittenLength, getLength());
#elif USE_TLS_OPENSSL
    EVP_DigestFinal_ex(mpImpl->mpContext, hash.data(), &digestWrittenLength);
#else
    (void)digestWrittenLength;
#endif

    return hash;
}

size_t Hash::getLength() const
{
    switch (mpImpl->meType)
    {
        case HashType::MD5:
            return MD5_HASH_LENGTH;
        case HashType::SHA1:
            return SHA1_HASH_LENGTH;
        case HashType::SHA256:
            return SHA256_HASH_LENGTH;
        case HashType::SHA384:
            return SHA384_HASH_LENGTH;
        case HashType::SHA512:
            return SHA512_HASH_LENGTH;
    }

    return 0;
}

std::vector<unsigned char> Hash::calculateHash(const void* pInput, size_t length, HashType eType)
{
    Hash aHash(eType);
    aHash.update(pInput, length);
    return aHash.finalize();
}

std::vector<unsigned char> Hash::calculateHash(
        const void* pInput, size_t nLength,
        const void* pSalt, size_t nSaltLen,
        sal_uInt32 nSpinCount,
        IterCount eIterCount,
        HashType eType)
{
    if (!pSalt)
        nSaltLen = 0;

    if (!nSaltLen && !nSpinCount)
        return calculateHash( pInput, nLength, eType);

    Hash aHash(eType);
    if (nSaltLen)
    {
        std::vector<unsigned char> initialData( nSaltLen + nLength);
        std::copy_n(static_cast<const unsigned char*>(pSalt), nSaltLen, initialData.begin());
        std::copy_n(static_cast<const unsigned char*>(pInput), nLength, initialData.begin() + nSaltLen);
        aHash.update( initialData.data(), initialData.size());
        rtl_secureZeroMemory( initialData.data(), initialData.size());
    }
    else
    {
        aHash.update( pInput, nLength);
    }
    std::vector<unsigned char> hash( aHash.finalize());

    if (nSpinCount)
    {
        // https://msdn.microsoft.com/en-us/library/dd920692
        // says the iteration is concatenated after the hash.
        // https://msdn.microsoft.com/en-us/library/dd924776 and
        // https://msdn.microsoft.com/en-us/library/dd925430
        // say the iteration is prepended to the hash.
        const size_t nAddIter = (eIterCount == IterCount::NONE ? 0 : 4);
        const size_t nIterPos = (eIterCount == IterCount::APPEND ? hash.size() : 0);
        const size_t nHashPos = (eIterCount == IterCount::PREPEND ? nAddIter : 0);
        std::vector<unsigned char> data( hash.size() + nAddIter, 0);
        for (sal_uInt32 i = 0; i < nSpinCount; ++i)
        {
            std::copy( hash.begin(), hash.end(), data.begin() + nHashPos);
            if (nAddIter)
            {
#ifdef OSL_BIGENDIAN
                sal_uInt32 be = OSL_SWAPDWORD(i);
                memcpy( data.data() + nIterPos, &be, nAddIter);
#else
                memcpy( data.data() + nIterPos, &i, nAddIter);
#endif
            }
            /* TODO: isn't there something better than
             * creating/finalizing/destroying on each iteration? */
            Hash aReHash(eType);
            aReHash.update( data.data(), data.size());
            hash = aReHash.finalize();
        }
    }

    return hash;
}

std::vector<unsigned char> Hash::calculateHash(
        std::u16string_view rPassword,
        const std::vector<unsigned char>& rSaltValue,
        sal_uInt32 nSpinCount,
        IterCount eIterCount,
        HashType eType)
{
    const void* pPassBytes = rPassword.data();
    const size_t nPassBytesLen = rPassword.length() * 2;
#ifdef OSL_BIGENDIAN
    // Swap UTF16-BE to UTF16-LE
    std::vector<char16_t> vPass;
    if (nPassBytesLen)
    {
        vPass.insert(vPass.begin(), rPassword.begin(), rPassword.end());
        for (char16_t& ch : vPass)
            ch = OSL_SWAPWORD(ch);
        pPassBytes = vPass.data();
    }
#endif
    return calculateHash( pPassBytes, nPassBytesLen, rSaltValue.data(), rSaltValue.size(), nSpinCount,
            eIterCount, eType);
}

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
