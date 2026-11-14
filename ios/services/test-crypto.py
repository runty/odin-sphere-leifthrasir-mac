#!/usr/bin/env python3
"""Cross-check Apple service crypto without reading or recording any user key material."""
import argparse
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',required=True,type=Path)
p.add_argument('--openssl',type=Path,default=Path('/opt/homebrew/opt/openssl@3'))
a=p.parse_args()
root=a.root.resolve()
if len(root.parts)<3 or root.parts[1]!='Volumes' or not Path('/Volumes',root.parts[2]).is_mount():
    p.error('Use a mounted external fixture directory')
root.mkdir(parents=True,exist_ok=True)
source=Path(__file__).resolve().parent;repo=source.parents[1]
(root/'reference.cpp').write_text('''#include "crypto.h"
#include <cstring>
extern "C" bool reference_cipher(int mode,bool encrypt,const uint8_t *key,uint8_t *bytes,size_t size,uint8_t *iv) {
 osl::crypto::AesKey aes;if(!aes.Init(mode?osl::crypto::AesMode::kCbc:osl::crypto::AesMode::kEcb,key))return false;
 return encrypt?aes.Encrypt(bytes,size,iv):aes.Decrypt(bytes,size,iv);
}
extern "C" bool reference_hash(int type,const uint8_t *bytes,size_t size,uint8_t *output) {
 uint8_t a[20],b[32];bool ok=type==1?osl::crypto::Sha1(bytes,size,a):osl::crypto::Sha256(bytes,size,b);
 if(ok)std::memcpy(output,type==1?a:b,type==1?20:32);return ok;
}
extern "C" bool reference_hmac(const uint8_t *key,size_t k,const uint8_t *bytes,size_t n,uint8_t *output) {
 uint8_t d[32];bool ok=osl::crypto::HmacSha256(key,k,bytes,n,d);if(ok)std::memcpy(output,d,32);return ok;
}
''')
for name,path in [('reference',root/'reference.cpp'),('openssl',repo/'launcher/crypto_openssl.cpp')]:
 subprocess.run(['xcrun','clang++','-arch','arm64','-std=c++17','-O2','-Dosl=reference_osl',
  '-I'+str(repo/'launcher'),'-I'+str(a.openssl/'include'),'-c',str(path),'-o',str(root/(name+'.o'))],check=True)
subprocess.run(['xcrun','clang++','-arch','arm64','-std=c++17','-O2','-I'+str(repo/'launcher'),
 str(source/'crypto_commoncrypto.cpp'),str(source/'crypto-test.cpp'),str(root/'reference.o'),str(root/'openssl.o'),
 '-L'+str(a.openssl/'lib'),'-lcrypto','-o',str(root/'crypto-test')],check=True)
subprocess.run([str(root/'crypto-test')],check=True,timeout=15)
