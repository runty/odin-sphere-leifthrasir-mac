# Portable startup services

The startup runner now reuses the existing TrophyService, store, metadata and
archive code instead of inventing trophy results. Its six imported functions
validate guest pointers and copy scalar/flag outputs through aligned host
locals. Context/handle creation, registration, unlock bits, duplicate unlocks,
invalid handles and misaligned guest output addresses are exercised by the
runner's `--service-check` mode (38 checks across service families).

This experiment has not configured an asset/profile directory for trophies.
The existing service therefore uses its metadata-unavailable RAM fallback;
`trophy_persistence=false` is explicit in diagnostics. It does not read or request
console/trophy keys. Persistence and metadata import remain separate work before
a playable app is delivered. RAM trophy checks are not game-save verification.

`crypto_commoncrypto.cpp` supplies Apple SHA-1, SHA-256, streaming SHA-256,
HMAC-SHA256 and unpadded AES-128 ECB/CBC for these host services. RSA PKG decryption
returns failure explicitly; the startup backend does not import encrypted PKGs.
The Mac port's existing OpenSSL backend remains unchanged.

```sh
python3 ios/services/test-crypto.py --root "$NEW_EXTERNAL_FIXTURE"
```

The native arm64 comparison passes 263 Apple/OpenSSL checks using ephemeral
random inputs generated in memory. No user keys or raw test key/data values are
read, saved or logged. The tests compare encryption bytes, cross-decryption,
hashes, streaming and HMAC results and reject unsupported/invalid operations.

`ios/game/service_build.py` compiles these sources into cached objects for the
Mac/iPhoneOS/Simulator runners. The startup ledger hashes the service sources
and headers; an iOS export refuses source drift from the established host run.
