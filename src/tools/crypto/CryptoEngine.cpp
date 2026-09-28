#include "tools/crypto/CryptoEngine.h"

#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1
#include <aes.h>
#include <arc4.h>
#include <blowfish.h>
#include <camellia.h>
#include <cast.h>
#include <des.h>
#include <filters.h>
#include <gcm.h>
#include <idea.h>
#include <modes.h>
#include <oaep.h>
#include <osrng.h>
#include <pkcspad.h>
#include <queue.h>
#include <rc5.h>
#include <rc6.h>
#include <rsa.h>
#include <salsa.h>
#include <seed.h>
#include <serpent.h>
#include <sha.h>
#include <sm4.h>
#include <tea.h>
#include <twofish.h>
#include <chacha.h>

namespace crypto_tool
{
namespace
{
template<class Cipher>
std::string BlockTransform(const Request& r)
{
    std::string output;
    if (r.mode == GCM)
    {
        if (r.encrypt)
        {
            typename CryptoPP::GCM<Cipher>::Encryption cipher;
            cipher.SetKeyWithIV(reinterpret_cast<const CryptoPP::byte*>(r.key.data()), r.key.size(),
                                reinterpret_cast<const CryptoPP::byte*>(r.iv.data()), r.iv.size());
            CryptoPP::AuthenticatedEncryptionFilter filter(cipher, new CryptoPP::StringSink(output),
                                                            false, r.tagSize);
            if (!r.aad.empty())
                filter.ChannelPut(CryptoPP::AAD_CHANNEL,
                    reinterpret_cast<const CryptoPP::byte*>(r.aad.data()), r.aad.size());
            filter.ChannelMessageEnd(CryptoPP::AAD_CHANNEL);
            if (!r.input.empty())
                filter.ChannelPut(CryptoPP::DEFAULT_CHANNEL,
                    reinterpret_cast<const CryptoPP::byte*>(r.input.data()), r.input.size());
            filter.ChannelMessageEnd(CryptoPP::DEFAULT_CHANNEL);
        }
        else
        {
            typename CryptoPP::GCM<Cipher>::Decryption cipher;
            cipher.SetKeyWithIV(reinterpret_cast<const CryptoPP::byte*>(r.key.data()), r.key.size(),
                                reinterpret_cast<const CryptoPP::byte*>(r.iv.data()), r.iv.size());
            CryptoPP::AuthenticatedDecryptionFilter filter(cipher, new CryptoPP::StringSink(output),
                CryptoPP::AuthenticatedDecryptionFilter::THROW_EXCEPTION, r.tagSize);
            if (!r.aad.empty())
                filter.ChannelPut(CryptoPP::AAD_CHANNEL,
                    reinterpret_cast<const CryptoPP::byte*>(r.aad.data()), r.aad.size());
            filter.ChannelMessageEnd(CryptoPP::AAD_CHANNEL);
            if (!r.input.empty())
                filter.ChannelPut(CryptoPP::DEFAULT_CHANNEL,
                    reinterpret_cast<const CryptoPP::byte*>(r.input.data()), r.input.size());
            filter.ChannelMessageEnd(CryptoPP::DEFAULT_CHANNEL);
        }
        return output;
    }

    CryptoPP::BlockPaddingSchemeDef::BlockPaddingScheme padding =
        CryptoPP::BlockPaddingSchemeDef::NO_PADDING;
    if (r.mode == CBC || r.mode == ECB)
    {
        if (r.padding == PKCS7) padding = CryptoPP::BlockPaddingSchemeDef::PKCS_PADDING;
        else if (r.padding == Zeros) padding = CryptoPP::BlockPaddingSchemeDef::ZEROS_PADDING;
        else if (r.padding == ISO7816) padding = CryptoPP::BlockPaddingSchemeDef::ONE_AND_ZEROS_PADDING;
    }
    const CryptoPP::byte* key = reinterpret_cast<const CryptoPP::byte*>(r.key.data());
    const CryptoPP::byte* iv = reinterpret_cast<const CryptoPP::byte*>(r.iv.data());
    const CryptoPP::byte* input = reinterpret_cast<const CryptoPP::byte*>(r.input.data());

#define ITOOL_RUN_MODE(MODE_TYPE) \
    if (r.encrypt) { \
        typename CryptoPP::MODE_TYPE<Cipher>::Encryption c; \
        if (r.mode == ECB) c.SetKey(key, r.key.size()); else c.SetKeyWithIV(key, r.key.size(), iv, r.iv.size()); \
        CryptoPP::StringSource source(input, r.input.size(), true, \
            new CryptoPP::StreamTransformationFilter(c, new CryptoPP::StringSink(output), padding)); \
    } else { \
        typename CryptoPP::MODE_TYPE<Cipher>::Decryption c; \
        if (r.mode == ECB) c.SetKey(key, r.key.size()); else c.SetKeyWithIV(key, r.key.size(), iv, r.iv.size()); \
        CryptoPP::StringSource source(input, r.input.size(), true, \
            new CryptoPP::StreamTransformationFilter(c, new CryptoPP::StringSink(output), padding)); \
    }

    if (r.mode == CBC) { ITOOL_RUN_MODE(CBC_Mode); }
    else if (r.mode == CTR) { ITOOL_RUN_MODE(CTR_Mode); }
    else if (r.mode == ECB) { ITOOL_RUN_MODE(ECB_Mode); }
    else throw CryptoPP::InvalidArgument("Unsupported block cipher mode");
#undef ITOOL_RUN_MODE
    // Crypto++ deliberately leaves zero padding in decrypted output because
    // it is ambiguous. This tool follows the convention used by most protocol
    // utilities and removes trailing zero bytes when Zero Padding is selected.
    if (!r.encrypt && r.padding == Zeros)
        while (!output.empty() && output[output.size() - 1] == '\0') output.resize(output.size() - 1);
    return output;
}

template<class Padding>
std::string RsaOaep(const Request& r)
{
    CryptoPP::ByteQueue queue;
    queue.Put(reinterpret_cast<const CryptoPP::byte*>(r.key.data()), r.key.size());
    CryptoPP::AutoSeededRandomPool random;
    std::string output;
    if (r.encrypt)
    {
        CryptoPP::RSA::PublicKey key;
        key.BERDecode(queue);
        typename CryptoPP::RSAES<Padding>::Encryptor encryptor(key);
        CryptoPP::StringSource source(r.input, true,
            new CryptoPP::PK_EncryptorFilter(random, encryptor, new CryptoPP::StringSink(output)));
    }
    else
    {
        CryptoPP::RSA::PrivateKey key;
        key.BERDecode(queue);
        typename CryptoPP::RSAES<Padding>::Decryptor decryptor(key);
        CryptoPP::StringSource source(r.input, true,
            new CryptoPP::PK_DecryptorFilter(random, decryptor, new CryptoPP::StringSink(output)));
    }
    return output;
}

template<class Cipher>
std::string StreamTransform(const Request& r)
{
    typename Cipher::Encryption cipher;
    cipher.SetKeyWithIV(reinterpret_cast<const CryptoPP::byte*>(r.key.data()), r.key.size(),
                        reinterpret_cast<const CryptoPP::byte*>(r.iv.data()), r.iv.size());
    std::string output(r.input.size(), '\0');
    if (!r.input.empty()) cipher.ProcessData(reinterpret_cast<CryptoPP::byte*>(&output[0]),
        reinterpret_cast<const CryptoPP::byte*>(r.input.data()), r.input.size());
    return output;
}

std::string Arc4Transform(const Request& r)
{
    CryptoPP::Weak::ARC4::Encryption cipher;
    cipher.SetKey(reinterpret_cast<const CryptoPP::byte*>(r.key.data()), r.key.size());
    std::string output(r.input.size(), '\0');
    if (!r.input.empty()) cipher.ProcessData(reinterpret_cast<CryptoPP::byte*>(&output[0]),
        reinterpret_cast<const CryptoPP::byte*>(r.input.data()), r.input.size());
    return output;
}

std::string XxteaTransform(const Request& r)
{
    if (r.input.size() < 8 || r.input.size() % 4 != 0)
        throw CryptoPP::InvalidArgument("XXTEA input length must be at least 8 bytes and a multiple of 4");
    std::string output(r.input.size(), '\0');
    CryptoPP::AlgorithmParameters parameters = CryptoPP::MakeParameters("BlockSize", static_cast<int>(r.input.size()));
    if (r.encrypt)
    {
        CryptoPP::BTEA::Encryption cipher;
        cipher.SetKey(reinterpret_cast<const CryptoPP::byte*>(r.key.data()), r.key.size(), parameters);
        cipher.ProcessBlock(reinterpret_cast<const CryptoPP::byte*>(r.input.data()),
                            reinterpret_cast<CryptoPP::byte*>(&output[0]));
    }
    else
    {
        CryptoPP::BTEA::Decryption cipher;
        cipher.SetKey(reinterpret_cast<const CryptoPP::byte*>(r.key.data()), r.key.size(), parameters);
        cipher.ProcessBlock(reinterpret_cast<const CryptoPP::byte*>(r.input.data()),
                            reinterpret_cast<CryptoPP::byte*>(&output[0]));
    }
    return output;
}
}

std::string Transform(const Request& r)
{
    if (r.algorithm == RSA)
    {
        if (r.mode == OAEP_SHA256) return RsaOaep<CryptoPP::OAEP<CryptoPP::SHA256> >(r);
        if (r.mode == OAEP_SHA1) return RsaOaep<CryptoPP::OAEP<CryptoPP::SHA1> >(r);
        if (r.mode == PKCS1_V15) return RsaOaep<CryptoPP::PKCS1v15>(r);
        throw CryptoPP::InvalidArgument("Unsupported RSA padding");
    }
    if (r.algorithm == AES) return BlockTransform<CryptoPP::AES>(r);
    if (r.algorithm == SM4) return BlockTransform<CryptoPP::SM4>(r);
    if (r.algorithm == DES) return BlockTransform<CryptoPP::DES>(r);
    if (r.algorithm == TripleDES) return BlockTransform<CryptoPP::DES_EDE3>(r);
    if (r.algorithm == Blowfish) return BlockTransform<CryptoPP::Blowfish>(r);
    if (r.algorithm == Twofish) return BlockTransform<CryptoPP::Twofish>(r);
    if (r.algorithm == CAST5) return BlockTransform<CryptoPP::CAST128>(r);
    if (r.algorithm == IDEA) return BlockTransform<CryptoPP::IDEA>(r);
    if (r.algorithm == Serpent) return BlockTransform<CryptoPP::Serpent>(r);
    if (r.algorithm == TEA) return BlockTransform<CryptoPP::TEA>(r);
    if (r.algorithm == XTEA) return BlockTransform<CryptoPP::XTEA>(r);
    if (r.algorithm == XXTEA) return XxteaTransform(r);
    if (r.algorithm == RC4) return Arc4Transform(r);
    if (r.algorithm == RC5) return BlockTransform<CryptoPP::RC5>(r);
    if (r.algorithm == RC6) return BlockTransform<CryptoPP::RC6>(r);
    if (r.algorithm == ChaCha20) return StreamTransform<CryptoPP::ChaChaTLS>(r);
    if (r.algorithm == Salsa20) return StreamTransform<CryptoPP::Salsa20>(r);
    if (r.algorithm == Camellia) return BlockTransform<CryptoPP::Camellia>(r);
    if (r.algorithm == SEED) return BlockTransform<CryptoPP::SEED>(r);
    throw CryptoPP::InvalidArgument("Unsupported algorithm");
}

std::string RandomBytes(size_t count)
{
    std::string bytes(count, '\0');
    CryptoPP::AutoSeededRandomPool random;
    if (count) random.GenerateBlock(reinterpret_cast<CryptoPP::byte*>(&bytes[0]), count);
    return bytes;
}

size_t RequiredKeySize(Algorithm algorithm, size_t selectedAesSize)
{
    if (algorithm == AES || algorithm == Blowfish || algorithm == Twofish || algorithm == Serpent ||
        algorithm == RC5 || algorithm == RC6 || algorithm == Camellia || algorithm == ChaCha20 ||
        algorithm == Salsa20) return selectedAesSize;
    if (algorithm == SM4) return CryptoPP::SM4::DEFAULT_KEYLENGTH;
    if (algorithm == DES) return CryptoPP::DES::DEFAULT_KEYLENGTH;
    if (algorithm == TripleDES) return CryptoPP::DES_EDE3::DEFAULT_KEYLENGTH;
    if (algorithm == CAST5) return CryptoPP::CAST128::DEFAULT_KEYLENGTH;
    if (algorithm == IDEA) return CryptoPP::IDEA::DEFAULT_KEYLENGTH;
    if (algorithm == TEA || algorithm == XTEA || algorithm == XXTEA) return 16;
    if (algorithm == RC4) return selectedAesSize;
    if (algorithm == SEED) return CryptoPP::SEED::DEFAULT_KEYLENGTH;
    return 0;
}

size_t RequiredIvSize(Algorithm algorithm, Mode mode)
{
    if (algorithm == RSA || algorithm == RC4 || algorithm == XXTEA || mode == ECB || mode == Raw) return 0;
    if (algorithm == ChaCha20) return 12;
    if (algorithm == Salsa20) return 8;
    if (mode == GCM) return 12;
    return RequiredBlockSize(algorithm);
}

size_t RequiredBlockSize(Algorithm algorithm)
{
    if (algorithm == DES || algorithm == TripleDES || algorithm == Blowfish || algorithm == CAST5 ||
        algorithm == IDEA || algorithm == TEA || algorithm == XTEA || algorithm == RC5) return 8;
    if (algorithm == AES || algorithm == SM4 || algorithm == Twofish || algorithm == Serpent ||
        algorithm == RC6 || algorithm == Camellia || algorithm == SEED) return 16;
    return 0;
}

std::pair<std::string, std::string> GenerateRsaKeyPair(unsigned int bits)
{
    CryptoPP::AutoSeededRandomPool random;
    CryptoPP::InvertibleRSAFunction parameters;
    parameters.GenerateRandomWithKeySize(random, bits);
    CryptoPP::RSA::PrivateKey privateKey(parameters);
    CryptoPP::RSA::PublicKey publicKey(parameters);
    std::string publicDer, privateDer;
    publicKey.DEREncode(CryptoPP::StringSink(publicDer).Ref());
    privateKey.DEREncode(CryptoPP::StringSink(privateDer).Ref());
    return std::make_pair(publicDer, privateDer);
}
}
