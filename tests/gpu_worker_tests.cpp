// Exercise the actual renderer handoffs on native arm64 and x86-64.
#include "video_core/renderer_vulkan/vk_bind_helper.h"
#include "video_core/renderer_vulkan/vk_draw_pipe.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
// The test enables the same default copy-pool optimizations as the runtime.
extern "C" { uint64_t runtime_disabled_optimizations=0; }

static void require(bool result) { if(!result) std::abort(); }
struct Work { unsigned expected=0, completed=0; };
int main() {
    const auto begin=Common::ReadHostCounter();
    Common::SpinPause();
    require(Common::ReadHostCounter()>=begin);
    {
        Vulkan::BindHelper helper(false);
        require(!helper.Available());
    }
    {
        Work work;
        Vulkan::BindHelper helper(true);
        require(helper.Available());
        for(unsigned i=0;i<20000;++i) {
            work.expected=i;
            helper.Fork([](void *context) {
                auto &work=*static_cast<Work *>(context);
                require(Vulkan::BindHelper::OnHelper());
                require(work.expected==work.completed);
                ++work.completed;
            },&work);
            helper.Join();
            require(work.completed==i+1 && !helper.Active());
            if(i%2000==0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        // Destructor must wake an idle helper and join before context dies.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    {
        Work work;
        Vulkan::DrawPipe pipe([](void *context,const u8 *packet,u32 size) {
            require(Vulkan::DrawPipe::OnStageB());
            auto &work=*static_cast<Work *>(context);
            unsigned sequence; std::memcpy(&sequence,packet,4);
            require(sequence==work.completed && size==128+(sequence%17)*64);
            for(u32 j=4;j<size;++j) require(packet[j]==u8(sequence+j));
            ++work.completed;
        },&work);
        // More than six times the ring capacity, with varied padding and wraps.
        for(unsigned i=0;i<180000;++i) {
            const u32 size=128+(i%17)*64;
            auto *packet=pipe.Begin(size);
            std::memcpy(packet,&i,4);
            for(u32 j=4;j<size;++j) packet[j]=u8(i+j);
            pipe.Commit(size);
            if(i%30000==0) {
                pipe.Drain(); require(work.completed==i+1);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        pipe.Drain();
        require(work.completed==180000 && pipe.Idle() && pipe.Reached(pipe.Head()));
    }
    puts("GPU worker tests: 20000 helper handoffs and 180000 variable-size ring packets passed");
}
