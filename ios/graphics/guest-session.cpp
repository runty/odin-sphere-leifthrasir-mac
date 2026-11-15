#include "../services/guest_audio.h"
// UIKit-facing ownership boundary; all guest pointers use the shared adapter.
#include "guest-session.h"
#include "guest-gpu.h"
#include "image-layout.h"
#include <CommonCrypto/CommonDigest.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <pthread.h>
#include <signal.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern "C" uint64_t osl_aot_memory_allocated();
extern "C" uint64_t runtime_eventflag_live_count();
struct OslGuestSession {
    explicit OslGuestSession(uint32_t budget):budget_ms(budget){}
    const uint32_t budget_ms;
    NativeGpu gpu;
    std::atomic<bool> stop{false},running{false},started{false};
    std::atomic<unsigned> calls{0};
    std::atomic<bool> test_gate{false},test_gate_reached{false};
    bool faults=false;
    std::string report;
};
namespace {
OslGuestSession *fault_owner=nullptr; // Main-thread ownership, never read by the handler.
struct sigaction previous_segv{},previous_bus{};
void fault(int number,siginfo_t *info,void *context) {
    if(info && reinterpret_cast<uintptr_t>(info->si_addr)<(uint64_t(1)<<40) && oslgpu_handle_fault(context,info->si_addr))return;
    const auto &previous=number==SIGBUS ? previous_bus : previous_segv;
    if(previous.sa_handler==SIG_IGN)return;
    if(previous.sa_handler!=SIG_DFL) {
        if(previous.sa_flags&SA_SIGINFO)previous.sa_sigaction(number,info,context);
        else previous.sa_handler(number);
        return;
    }
    // Preserve the platform's normal fatal-signal handling; never turn an
    // unhandled host fault into a guest checkpoint or successful diagnostic.
    sigaction(number,&previous,nullptr);raise(number);
}
bool restore(int number,const struct sigaction &previous) {
    struct sigaction current{};
    if(sigaction(number,nullptr,&current))return false;
    if(!(current.sa_flags&SA_SIGINFO) || current.sa_sigaction!=fault)return false;
    return sigaction(number,&previous,nullptr)==0;
}
bool read(const std::string &path,size_t size,const char *hash,std::vector<uint8_t> &bytes) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file || file.tellg()!=static_cast<std::streamoff>(size))return false;
    bytes.resize(size);file.seekg(0);
    if(!file.read(reinterpret_cast<char *>(bytes.data()),size))return false;
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256(bytes.data(),static_cast<CC_LONG>(size),digest);
    char hex[65];for(unsigned i=0;i<32;++i)std::snprintf(hex+i*2,3,"%02x",digest[i]);
    return !std::strcmp(hex,hash);
}
}
extern "C" int osl_guest_session_sdk(const char *path,uint32_t *sdk) {
    if(!path || !sdk)return 0;
    try {
        std::vector<uint8_t> image,pointers;
        if(!read(path,image_size,image_sha256_hex,image) || procparam>image.size() || image.size()-procparam<24)return 0;
        const std::string name(path);const auto slash=name.find_last_of('/');
        const auto table=(slash==std::string::npos ? std::string{} : name.substr(0,slash+1))+"image-pointers.bin";
        if(!read(table,image_pointer_count*8,pointer_sha256_hex,pointers))return 0;
        uint64_t value=0;std::memcpy(&value,image.data()+procparam+16,8);
        if(!value || value>UINT32_MAX)return 0;
        *sdk=static_cast<uint32_t>(value);return 1;
    } catch(...) {return 0;}
}
extern "C" OslGuestSession *osl_guest_session_create() {
    return osl_guest_session_create_with_budget(2000);
}
extern "C" OslGuestSession *osl_guest_session_create_with_budget(uint32_t milliseconds) {
    if(milliseconds<100 || milliseconds>600000)return nullptr;
    try{return new OslGuestSession(milliseconds);}catch(...){return nullptr;}
}
extern "C" int osl_guest_session_faults_begin(OslGuestSession *session) {
    if(!pthread_main_np() || !session || fault_owner || session->faults)return 0;
    if(sigaction(SIGSEGV,nullptr,&previous_segv) || sigaction(SIGBUS,nullptr,&previous_bus))return 0;
    struct sigaction action{};action.sa_sigaction=fault;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);
    if(sigaction(SIGSEGV,&action,nullptr))return 0;
    if(sigaction(SIGBUS,&action,nullptr)){sigaction(SIGSEGV,&previous_segv,nullptr);return 0;}
    fault_owner=session;session->faults=true;return 1;
}
extern "C" int osl_guest_session_faults_end(OslGuestSession *session) {
    if(!pthread_main_np() || !session || fault_owner!=session || session->running)return 0;
    const bool segv=restore(SIGSEGV,previous_segv),bus=restore(SIGBUS,previous_bus);
    if(!segv || !bus)return 0; // Retain ownership and reject reuse after failed restoration.
    fault_owner=nullptr;session->faults=false;return 1;
}
extern "C" const char *osl_guest_session_run(OslGuestSession *session,const char *path) {
    static constexpr const char *invalid="{\"error\":\"Invalid or reused guest session\",\"game_frames\":false}";
    if(!session || !path || session->started.exchange(true))return invalid;
    session->running=true;
    try {
        GuestAudio audio;
        MobileGpuServices services{session,
            [](void *opaque,const char *name,const char *nid,const uint64_t args[6],uint64_t stack,Memory *memory,uint64_t *result,GuestWaitScope wait){
                auto &s=*static_cast<OslGuestSession *>(opaque);
                const bool handled=NativeGpu::call(&s.gpu,name,nid,args,stack,memory,result,wait);
                if(handled && ++s.calls==7 && s.test_gate) {
                    s.test_gate_reached=true;
                    std::fputs("OSL_GUEST_TEST_GATE_REACHED\n",stderr);std::fflush(stderr);
                    const auto end=std::min(memory->deadline,std::chrono::steady_clock::now()+std::chrono::seconds(1));
                    while(!s.stop && std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                return handled;
            },[](void *opaque){NativeGpu::finish(&static_cast<OslGuestSession *>(opaque)->gpu);},&session->stop};
        services.execution_budget_ms=session->budget_ms;
        services.audio_context=&audio;services.audio_call=GuestAudio::call;
        const std::string guest=osl_mobile_game_report_with_gpu(path,&services);
        audio.finish();
        const auto &gpu=session->gpu;
        const bool unmapped=gpu.observed_mapping && !oslgpu_guest_range_mapped(gpu.observed_mapping,1);
        const bool released=osl_aot_memory_allocated()==0 && runtime_eventflag_live_count()==0;
        const bool clean=gpu.closed==gpu.opened && gpu.queues_created==gpu.queues_deleted && released &&
            (!gpu.observed_mapping || (gpu.mapped_before_reset && unmapped));
        std::ostringstream out;out<<std::boolalpha<<"{\"execution_budget_ms\":"<<session->budget_ms<<",\"guest\":"<<guest<<",\"cleanup\":{\"passed\":"<<clean
            <<",\"opened\":"<<gpu.opened<<",\"closed\":"<<gpu.closed<<",\"queues_created\":"<<gpu.queues_created
            <<",\"queues_deleted\":"<<gpu.queues_deleted<<",\"events_added\":"<<gpu.events_added
            <<",\"attributes_set\":"<<gpu.attributes_set<<",\"buffers_registered\":"<<gpu.buffers_registered
            <<",\"gnm_init_calls\":"<<gpu.gnm_init_calls<<",\"gnm_init_words\":"<<gpu.gnm_init_words
            <<",\"gnm_shader_calls\":"<<gpu.gnm_shader_calls
            <<",\"gnm_submission_calls\":"<<gpu.submissions.calls<<",\"gnm_batches\":"<<gpu.submissions.batches
            <<",\"gnm_buffers\":"<<gpu.submissions.buffers<<",\"gnm_bytes\":"<<gpu.submissions.bytes<<",\"gnm_submit_done\":"<<gpu.submissions.submit_done
            <<",\"flip_wait_packets\":"<<gpu.presentation.flip_wait_packets<<",\"vblank_waits\":"<<gpu.presentation.vblank_waits
            <<",\"presentation_interruptions\":"<<gpu.presentation.interruptions<<",\"gnm_performance_queries\":"<<gpu.presentation.performance_queries
            <<",\"mapped_before_reset\":"<<gpu.mapped_before_reset<<",\"unmapped_after_reset\":"<<unmapped
            <<",\"guest_resources_released\":"<<released<<"},\"presented_frames\":"<<oslgpu_presented_frames()
            <<",\"test_gate_enabled\":"<<session->test_gate.load()<<",\"test_gate_reached\":"<<session->test_gate_reached.load()<<",\"game_frames\":false}";
        session->report=out.str();
    } catch(...) {session->report="{\"error\":\"Native guest frontend failure\",\"game_frames\":false}";}
    session->running=false;return session->report.c_str();
}
extern "C" void osl_guest_session_request_stop(OslGuestSession *session) {if(session)session->stop=true;}
extern "C" unsigned osl_guest_session_gpu_calls(OslGuestSession *session) {return session ? session->calls.load() : 0;}
extern "C" int osl_guest_session_test_gate_after_buffers(OslGuestSession *session) {
    if(!session || session->started)return 0;
    session->test_gate=true;return 1;
}
extern "C" int osl_guest_session_destroy(OslGuestSession *session) {
    if(!session || session->running || session->faults)return 0;
    delete session;return 1;
}
