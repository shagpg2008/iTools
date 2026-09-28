#pragma once

#include <string>
#include <utility>

namespace crypto_tool
{
enum Algorithm {
    AES, SM4, DES, TripleDES, Blowfish, Twofish, CAST5, IDEA, Serpent,
    TEA, XTEA, XXTEA, RC4, RC5, RC6, ChaCha20, Salsa20, Camellia, SEED, RSA
};
enum Mode { GCM, CBC, CTR, ECB, Stream, Raw, OAEP_SHA256, OAEP_SHA1, PKCS1_V15 };
enum Padding { PKCS7, Zeros, ISO7816, NoPadding };

struct Request
{
    Algorithm algorithm;
    Mode mode;
    bool encrypt;
    std::string input;
    std::string key;
    std::string iv;
    std::string aad;
    unsigned int tagSize;
    Padding padding;
};

std::string Transform(const Request& request);
std::string RandomBytes(size_t count);
size_t RequiredKeySize(Algorithm algorithm, size_t selectedAesSize = 32);
size_t RequiredIvSize(Algorithm algorithm, Mode mode);
size_t RequiredBlockSize(Algorithm algorithm);

// Keys are returned as DER bytes: public key first, private key second.
std::pair<std::string, std::string> GenerateRsaKeyPair(unsigned int bits);
}
