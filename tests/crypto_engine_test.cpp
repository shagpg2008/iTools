#include "tools/crypto/CryptoEngine.h"

#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void RoundTrip(crypto_tool::Algorithm algorithm, crypto_tool::Mode mode,
               size_t keySize, size_t ivSize)
{
    crypto_tool::Request request;
    request.algorithm = algorithm;
    request.mode = mode;
    request.encrypt = true;
    request.input = std::string("iTool crypto test \0 binary", 25);
    request.key = crypto_tool::RandomBytes(keySize);
    request.iv = crypto_tool::RandomBytes(ivSize);
    request.aad = mode == crypto_tool::GCM ? "authenticated metadata" : "";
    request.tagSize = 16;
    request.padding = crypto_tool::PKCS7;
    const std::string encrypted = crypto_tool::Transform(request);
    Require(encrypted != request.input, "ciphertext unexpectedly equals plaintext");
    request.encrypt = false;
    request.input = encrypted;
    Require(crypto_tool::Transform(request) == std::string("iTool crypto test \0 binary", 25),
            "symmetric round trip failed");
}

void PaddingRoundTrip(crypto_tool::Padding padding, const std::string& plaintext)
{
    crypto_tool::Request request;
    request.algorithm = crypto_tool::AES; request.mode = crypto_tool::CBC;
    request.encrypt = true; request.input = plaintext;
    request.key = crypto_tool::RandomBytes(16); request.iv = crypto_tool::RandomBytes(16);
    request.tagSize = 0; request.padding = padding;
    const std::string encrypted = crypto_tool::Transform(request);
    request.encrypt = false; request.input = encrypted;
    Require(crypto_tool::Transform(request) == plaintext, "padding round trip failed");
}
}

int main()
{
    try
    {
        RoundTrip(crypto_tool::AES, crypto_tool::GCM, 32, 12);
        RoundTrip(crypto_tool::AES, crypto_tool::CBC, 16, 16);
        RoundTrip(crypto_tool::SM4, crypto_tool::CTR, 16, 16);
        RoundTrip(crypto_tool::DES, crypto_tool::CBC, 8, 8);
        RoundTrip(crypto_tool::TripleDES, crypto_tool::ECB, 24, 0);
        RoundTrip(crypto_tool::Blowfish, crypto_tool::CBC, 16, 8);
        RoundTrip(crypto_tool::Twofish, crypto_tool::CTR, 32, 16);
        RoundTrip(crypto_tool::CAST5, crypto_tool::CBC, 16, 8);
        RoundTrip(crypto_tool::IDEA, crypto_tool::CBC, 16, 8);
        RoundTrip(crypto_tool::Serpent, crypto_tool::CTR, 32, 16);
        RoundTrip(crypto_tool::TEA, crypto_tool::CBC, 16, 8);
        RoundTrip(crypto_tool::XTEA, crypto_tool::CBC, 16, 8);
        RoundTrip(crypto_tool::RC4, crypto_tool::Stream, 16, 0);
        RoundTrip(crypto_tool::RC5, crypto_tool::CBC, 16, 8);
        RoundTrip(crypto_tool::RC6, crypto_tool::CTR, 32, 16);
        RoundTrip(crypto_tool::ChaCha20, crypto_tool::Stream, 32, 12);
        RoundTrip(crypto_tool::Salsa20, crypto_tool::Stream, 32, 8);
        RoundTrip(crypto_tool::Camellia, crypto_tool::CBC, 32, 16);
        RoundTrip(crypto_tool::SEED, crypto_tool::CBC, 16, 16);
        PaddingRoundTrip(crypto_tool::Zeros, "zero padding data");
        PaddingRoundTrip(crypto_tool::ISO7816, "ISO padding data");
        PaddingRoundTrip(crypto_tool::NoPadding, "exactly-16-bytes");

        crypto_tool::Request xxtea;
        xxtea.algorithm = crypto_tool::XXTEA; xxtea.mode = crypto_tool::Raw;
        xxtea.encrypt = true; xxtea.input = "exactly-16-bytes";
        xxtea.key = crypto_tool::RandomBytes(16); xxtea.tagSize = 0; xxtea.padding = crypto_tool::NoPadding;
        const std::string xxteaEncrypted = crypto_tool::Transform(xxtea);
        xxtea.encrypt = false; xxtea.input = xxteaEncrypted;
        Require(crypto_tool::Transform(xxtea) == "exactly-16-bytes", "XXTEA round trip failed");

        const std::pair<std::string, std::string> keys = crypto_tool::GenerateRsaKeyPair(2048);
        crypto_tool::Request rsa;
        rsa.algorithm = crypto_tool::RSA; rsa.mode = crypto_tool::OAEP_SHA256;
        rsa.encrypt = true; rsa.input = "short RSA message"; rsa.key = keys.first;
        rsa.tagSize = 0;
        rsa.padding = crypto_tool::NoPadding;
        const std::string encrypted = crypto_tool::Transform(rsa);
        rsa.encrypt = false; rsa.input = encrypted; rsa.key = keys.second;
        Require(crypto_tool::Transform(rsa) == "short RSA message", "RSA round trip failed");

        std::cout << "crypto engine tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
