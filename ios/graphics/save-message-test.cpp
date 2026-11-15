#include "../services/clock.h"
#include "oslgpu.h"
#include "osl_overlay.h"
#include "imgui.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
extern "C" uint64_t runtime_process_time_counter(){return MobileClock::processCounter();}
extern "C" uint64_t runtime_process_time_us(){return MobileClock::processTime();}
extern "C" uint64_t runtime_tsc_frequency(){return MobileClock::frequency;}
extern "C" uint64_t runtime_tsc_counter(){return MobileClock::tsc();}
extern "C" void runtime_log_flush(){std::fflush(nullptr);}
extern "C" [[noreturn]] void runtime_exit_clean(){std::abort();}
static unsigned checks;
static void require(bool ok){++checks;if(!ok){std::fprintf(stderr,"message presenter check %u failed\n",checks);std::abort();}}
int main(int argc,char **argv) {
    if(argc!=2 || !std::filesystem::is_directory(argv[1]))return 2;
    const char *buttons[]={"Yes","No"};
    require(!oslgpu_message_begin("Save","Create system data?",buttons,2,0,1,0));
    const std::string profile=argv[1],settings=profile+"/osl.ini";
    OslGpuConfig config{"Odin Save Message Adapter Check","ODIN_MESSAGE_TEST",profile.c_str(),settings.c_str(),0x03008001,0,640,360};
    for(unsigned cycle=0;cycle<2;++cycle) {
        oslgpu_register_kernel();require(oslgpu_init(&config)==0);
        require(!oslgpu_message_begin("Save","invalid",nullptr,1,0,1,0));
        require(!oslgpu_message_begin("Save","invalid",buttons,3,0,1,0));
        require(oslgpu_message_begin("Save","Create system data?",buttons,2,0,1,0x4000));
        require(oslgpu_ui_captures_input()==1);
        require(oslgpu_message_poll(0x4000)==-1); // Opening Cross waits for release.
        require(oslgpu_message_poll(0)==-1);
        require(oslgpu_message_poll(0x4000)==0);
        require(oslgpu_ui_captures_input()==0);
        oslgpu_message_end();require(oslgpu_message_poll(0)==-1);
        require(oslgpu_message_begin("Save","Choose deliberately",buttons,2,0,0,0));
        require(oslgpu_message_poll(0x2000)==-1); // Back disabled, explicit No remains available.
        require(oslgpu_message_poll(0)==-1);
        require(oslgpu_message_poll(0x40)==-1 && oslgpu_message_poll(0)==-1);
        require(oslgpu_message_poll(0x4000)==1);
        oslgpu_message_end();
        require(oslgpu_message_begin("Save","No automatic completion",nullptr,0,0,1,0));
        for(uint32_t b:{0x4000u,0u,0x2000u,0u,0x10u})require(oslgpu_message_poll(b)==-1);
        require(oslgpu_ui_captures_input()==1);oslgpu_message_end();
        require(oslgpu_ui_captures_input()==0);
        require(oslgpu_message_begin("Save","Cancel",buttons,2,1,1,0));
        require(oslgpu_message_poll(0x2000)==-2);oslgpu_message_end();
        require(oslgpu_message_begin("Save","Pointer input",buttons,2,0,1,0));
        auto &io=ImGui::GetIO();io.DisplaySize=ImVec2(640,360);io.DeltaTime=1.0f/60;
        auto finger=[&](SDL_EventType type,SDL_FingerID id,float x,float y){SDL_Event e{};e.type=type;e.tfinger.fingerID=id;e.tfinger.x=x;e.tfinger.y=y;return OslOverlay::HandleEvent(e);};
        auto frame=[&]{ImGui::NewFrame();ImGui::EndFrame();};
        require(finger(SDL_EVENT_FINGER_DOWN,11,0.25f,0.5f));
        for(unsigned n=0;n<4;++n)frame(); // ImGui trickles queued transitions over frames.
        require(io.MouseDown[0] && io.MousePos.x==160 && io.MousePos.y==180);
        require(finger(SDL_EVENT_FINGER_DOWN,12,0.9f,0.9f));frame();
        require(io.MouseDown[0] && io.MousePos.x==160 && io.MousePos.y==180);
        require(finger(SDL_EVENT_FINGER_CANCELED,11,0.25f,0.5f));
        for(unsigned n=0;n<4;++n)frame();
        require(!io.MouseDown[0] && io.MousePos.x<0 && io.MousePos.y<0);
        oslgpu_message_end();
        require(oslgpu_shutdown_main_thread()==0);
        require(!oslgpu_ui_captures_input());
    }
    std::printf("{\"checks\":%u,\"failed\":0,\"native_renderer_lifecycle\":true,\"physical_touch_verified\":false,\"dialog_pixels_verified\":false}\n",checks);
}
