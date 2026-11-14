// Compare Apple crypto against the existing OpenSSL backend using ephemeral test inputs.
#include "crypto.h"
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>
extern "C" bool reference_cipher(int, bool, const uint8_t *, uint8_t *, size_t, uint8_t *);
extern "C" bool reference_hash(int, const uint8_t *, size_t, uint8_t *);
extern "C" bool reference_hmac(const uint8_t *, size_t, const uint8_t *, size_t, uint8_t *);
int main() {
    unsigned checks = 0;
    for (int round = 0; round < 16; ++round) {
        std::array<uint8_t,16> key{},iv{};
        arc4random_buf(key.data(),key.size()); arc4random_buf(iv.data(),iv.size());
        for (int mode = 0; mode < 2; ++mode) for (size_t size : {size_t(16),size_t(32),size_t(256),size_t(4096)}) {
            std::vector<uint8_t> plain(size);arc4random_buf(plain.data(),plain.size());
            auto apple=plain,reference=plain;
            osl::crypto::AesKey aes;
            assert(aes.Init(mode?osl::crypto::AesMode::kCbc:osl::crypto::AesMode::kEcb,key.data()));
            auto aiv=iv,riv=iv;
            assert(aes.Encrypt(apple.data(),apple.size(),aiv.data()));
            assert(reference_cipher(mode,true,key.data(),reference.data(),reference.size(),riv.data()));
            assert(apple==reference);++checks;
            aiv=iv;assert(aes.Decrypt(reference.data(),reference.size(),aiv.data()));
            riv=iv;assert(reference_cipher(mode,false,key.data(),apple.data(),apple.size(),riv.data()));
            assert(reference==plain && apple==plain);++checks;
        }
    }
    std::vector<uint8_t> data(8192);arc4random_buf(data.data(),data.size());
    uint8_t sha1[20],sha256[32],expected[32];
    assert(osl::crypto::Sha1(data.data(),data.size(),sha1) && reference_hash(1,data.data(),data.size(),expected));
    assert(!memcmp(sha1,expected,20));++checks;
    assert(osl::crypto::Sha256(data.data(),data.size(),sha256) && reference_hash(256,data.data(),data.size(),expected));
    assert(!memcmp(sha256,expected,32));++checks;
    osl::crypto::Sha256Stream stream;
    assert(stream.Update(data.data(),7) && stream.Update(data.data()+7,data.size()-7) && stream.Finish(sha256));
    assert(!memcmp(sha256,expected,32));++checks;
    std::array<uint8_t,32> key{};arc4random_buf(key.data(),key.size());
    assert(osl::crypto::HmacSha256(key.data(),key.size(),data.data(),data.size(),sha256));
    assert(reference_hmac(key.data(),key.size(),data.data(),data.size(),expected));
    assert(!memcmp(sha256,expected,32));++checks;
    osl::crypto::AesKey invalid;
    assert(!invalid.Init(osl::crypto::AesMode::kEcb,nullptr));++checks;
    assert(invalid.Init(osl::crypto::AesMode::kCbc,key.data()));
    assert(!invalid.Encrypt(data.data(),17,key.data()) && !invalid.Encrypt(data.data(),16,nullptr));++checks;
    std::vector<uint8_t> output;
    assert(!osl::crypto::RsaDecryptPkcs1(nullptr,nullptr,nullptr,nullptr,output));++checks;
    printf("Apple/OpenSSL AES/hash/HMAC cross-checks: %u passed; no test key/data values recorded\n",checks);
}
