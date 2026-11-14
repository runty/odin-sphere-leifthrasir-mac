// Apple hashes/AES for portable host services. No external crypto dylib is needed.
#include "crypto.h"
#include <CommonCrypto/CommonCrypto.h>
#include <cstring>
#include <algorithm>
#include <limits>

namespace osl::crypto {
bool Sha1(const uint8_t *data, size_t size, uint8_t (&digest)[20]) {
    return size <= std::numeric_limits<CC_LONG>::max() && CC_SHA1(data, CC_LONG(size), digest);
}
bool Sha256(const uint8_t *data, size_t size, uint8_t (&digest)[32]) {
    Sha256Stream stream;
    return stream.Update(data, size) && stream.Finish(digest);
}
bool HmacSha256(const uint8_t *key, size_t key_size, const uint8_t *data, size_t size,
                uint8_t (&digest)[32]) {
    if ((!key && key_size) || (!data && size)) return false;
    CCHmac(kCCHmacAlgSHA256, key, key_size, data, size, digest);
    return true;
}
struct Sha256Stream::Impl { CC_SHA256_CTX context; bool ready = false; };
Sha256Stream::Sha256Stream() : impl_(new Impl) { impl_->ready = CC_SHA256_Init(&impl_->context); }
Sha256Stream::~Sha256Stream() = default;
bool Sha256Stream::Update(const void *data, size_t size) {
    if (!impl_->ready || (!data && size)) return false;
    const auto *bytes = static_cast<const uint8_t *>(data);
    while (size) {
        const CC_LONG chunk = CC_LONG(std::min<size_t>(size, std::numeric_limits<CC_LONG>::max()));
        if (!CC_SHA256_Update(&impl_->context, bytes, chunk)) { impl_->ready = false; return false; }
        bytes += chunk; size -= chunk;
    }
    return true;
}
bool Sha256Stream::Finish(uint8_t (&digest)[32]) {
    if (!impl_->ready) return false;
    impl_->ready = false;
    return CC_SHA256_Final(digest, &impl_->context);
}
struct AesKey::Impl {
    AesMode mode = AesMode::kEcb;
    uint8_t key[16]{};
    bool ready = false;
    bool Run(bool encrypt, uint8_t *data, size_t size, const uint8_t *iv) const {
        if (!ready || size % 16 || size > 0x7ffffff0u || (!data && size) ||
            (mode == AesMode::kCbc && !iv)) return false;
        if (!size) return true;
        size_t written = 0;
        return CCCrypt(encrypt ? kCCEncrypt : kCCDecrypt, kCCAlgorithmAES,
                       mode == AesMode::kEcb ? kCCOptionECBMode : 0,
                       key, sizeof(key), iv, data, size, data, size, &written) == kCCSuccess && written == size;
    }
};
AesKey::AesKey() : impl_(new Impl) {}
AesKey::~AesKey() { for (volatile uint8_t &byte : impl_->key) byte = 0; }
bool AesKey::Init(AesMode mode, const uint8_t *key) {
    impl_->ready = false;
    if (!key || (mode != AesMode::kEcb && mode != AesMode::kCbc)) return false;
    impl_->mode = mode; std::memcpy(impl_->key, key, 16); impl_->ready = true; return true;
}
bool AesKey::Encrypt(uint8_t *data, size_t size, uint8_t *iv) { return impl_->Run(true, data, size, iv); }
bool AesKey::Decrypt(uint8_t *data, size_t size, uint8_t *iv) { return impl_->Run(false, data, size, iv); }
bool RsaDecryptPkcs1(const uint8_t *, const uint8_t *, const uint8_t *, const uint8_t *,
                     std::vector<uint8_t> &) {
    // The startup service backend does not import encrypted PKGs. Never claim
    // that this unsupported operation produced plaintext.
    return false;
}
} // namespace osl::crypto
