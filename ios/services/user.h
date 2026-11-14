#pragma once
#include <cstdint>
// Exact native signatures from the production offline-user implementation.
extern "C" uintptr_t runtime_user_resolve(const char *);
extern "C" int runtime_user_player(int32_t);
extern "C" void osl_aot_user_reset();
namespace Users {
inline int32_t initialize(const void *params) {
    return reinterpret_cast<int32_t (*)(const void *)>(runtime_user_resolve("sceUserServiceInitialize"))(params);
}
inline int32_t terminate() {
    return reinterpret_cast<int32_t (*)()>(runtime_user_resolve("sceUserServiceTerminate"))();
}
inline int32_t initial(int32_t *out) {
    return reinterpret_cast<int32_t (*)(int32_t *)>(runtime_user_resolve("sceUserServiceGetInitialUser"))(out);
}
inline int32_t list(int32_t *out) {
    return reinterpret_cast<int32_t (*)(int32_t *)>(runtime_user_resolve("sceUserServiceGetLoginUserIdList"))(out);
}
inline int32_t name(int32_t id, char *out, uint64_t size) {
    return reinterpret_cast<int32_t (*)(int32_t,char *,uint64_t)>(runtime_user_resolve("sceUserServiceGetUserName"))(id,out,size);
}
}
