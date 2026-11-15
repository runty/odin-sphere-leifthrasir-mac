#include "save-dialog.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
static unsigned checks;
static void require(bool value){++checks;if(!value){std::fprintf(stderr,"save dialog check %u failed\n",checks);std::abort();}}
struct Fixture {
    Memory memory;
    std::array<uint8_t,8192> bytes{};
    uint64_t base=0x10000,param=base+1,user=base+201,text=base+301,out=base+4601,dir=base+4801,save=base+5001;
    MobileSaveDialog dialog;
    SaveMessage seen;
    unsigned begun=0,ended=0,polled=0;
    uint32_t initial=0;
    int selected=-1;bool can_begin=true;
    SaveMessagePresenter presenter{this,
        [](void *p,const SaveMessage &message,uint32_t buttons){auto &f=*static_cast<Fixture *>(p);++f.begun;f.seen=message;f.initial=buttons;return f.can_begin;},
        [](void *p,uint32_t){auto &f=*static_cast<Fixture *>(p);++f.polled;return f.selected;},
        [](void *p){++static_cast<Fixture *>(p)->ended;}};
    template<class T> void put(uint64_t where,T value){std::memcpy(bytes.data()+where-base,&value,sizeof(value));}
    template<class T> T get(uint64_t where){T v;std::memcpy(&v,bytes.data()+where-base,sizeof(v));return v;}
    Fixture() {
        mapRegion(memory,base,bytes.data(),bytes.size());
        put<uint64_t>(param,48);put<uint32_t>(param+48,152);put<uint32_t>(param+52,2);put<uint32_t>(param+56,1);
        put<uint64_t>(param+80,user);put<uint64_t>(param+112,0xfedcba9876543210);
        put<uint64_t>(user+8,text);std::memcpy(bytes.data()+text-base,"Create system data?",20);
        std::memset(bytes.data()+out-base,0xa5,72);put<uint64_t>(out+16,dir);put<uint64_t>(out+24,save);
        std::memset(bytes.data()+dir-base,0xcc,32);std::memset(bytes.data()+save-base,0xdd,1328);
    }
    uint32_t open(){return dialog.open(memory,param,presenter,0x4000);}
};
int main() {
    using D=MobileSaveDialog;
    for(uint32_t kind=0;kind<4;++kind)for(int selection:{-2,0,1,2}) {
        Fixture f;require(f.dialog.status()==D::None && f.dialog.terminate()==D::NotInitialized);
        require(f.open()==D::NotInitialized && !f.begun);
        require(f.dialog.initialize()==0 && f.dialog.initialize()==D::AlreadyInitialized);
        require(f.dialog.getResult(f.memory,0)==D::NotFinished && f.dialog.close()==D::NotRunning);
        f.put<uint32_t>(f.user,kind);require(f.open()==0 && f.dialog.status()==D::Running);
        require(f.seen.text=="Create system data?" && f.seen.button_type==kind && f.initial==0x4000);
        require(f.open()==D::InvalidState && f.begun==1 && !f.ended);
        require(f.dialog.poll(0)==D::Running && !f.ended);
        f.selected=selection;const bool completes=kind!=2 && (selection==-2 || selection==0 || (selection==1 && (kind==1 || kind==3)));
        require(f.dialog.poll(0)==(completes ? D::Finished : D::Running));
        if(!completes){require(f.dialog.close()==0);selection=-2;}
        require(f.ended==1 && f.dialog.status()==D::Finished);
        require(f.dialog.getResult(f.memory,0)==D::ArgumentNull);
        require(f.dialog.getResult(f.memory,f.out)==0 && f.memory.fault.empty());
        require(f.get<uint32_t>(f.out)==2 && f.get<uint32_t>(f.out+4)==uint32_t(selection==-2 || (kind==3 && selection==1)));
        require(f.get<uint32_t>(f.out+8)==uint32_t(selection==-2 || (kind==3 && selection==1) ? 0 : selection==1 ? 2 : 1));
        require(f.get<uint64_t>(f.out+16)==f.dir && f.get<uint64_t>(f.out+24)==f.save && f.get<uint64_t>(f.out+32)==0xfedcba9876543210);
        require(f.get<uint32_t>(f.out+12)==0xa5a5a5a5 && f.bytes[f.out+71-f.base]==0xa5);
        require(f.bytes[f.dir-f.base]==0 && f.bytes[f.dir+31-f.base]==0 && f.bytes[f.save-f.base]==0xdd && f.bytes[f.save+1327-f.base]==0xdd);
        require(f.dialog.getResult(f.memory,f.out)==0 && f.ended==1);
        f.selected=-1;require(f.open()==0 && f.begun==2);
        require(f.dialog.terminate()==0 && f.ended==2 && f.dialog.status()==D::None);
    }
    // Every malformed input leaves the session initialized and UI unowned.
    for(unsigned which=0;which<14;++which) {
        Fixture f;require(f.dialog.initialize()==0);
        switch(which) {
            case 0:f.put<uint64_t>(f.param,47);break;
            case 1:f.put<uint32_t>(f.param+48,151);break;
            case 2:f.put<uint32_t>(f.param+56,0);break;
            case 3:f.put<uint32_t>(f.param+56,4);break;
            case 4:f.put<uint64_t>(f.param+80,0);break;
            case 5:f.put<uint64_t>(f.user+8,0);break;
            case 6:f.put<uint32_t>(f.user,4);break;
            case 7:f.put<uint32_t>(f.user+4,2);break;
            case 8:f.put<uint64_t>(f.user+8,UINT64_MAX);break;
            case 9:std::memset(f.bytes.data()+f.text-f.base,'X',4096);break;
            case 10:f.put<uint64_t>(f.param+120,f.base+7001);f.put<uint32_t>(f.base+7001,2);break;
            case 11:f.put<uint64_t>(f.param+72,f.base+7001);f.put<uint32_t>(f.base+7025,1);break;
            case 12:f.put<uint64_t>(f.param+72,f.base+7001);f.put<uint64_t>(f.base+7033,1);break;
            case 13:f.put<uint32_t>(f.param+52,1);break;
        }
        require(f.open()!=0 && f.dialog.status()==D::Initialized && !f.begun && !f.ended);
    }
    for(unsigned which=0;which<6;++which) {
        Fixture f;require(f.dialog.initialize()==0 && f.open()==0);f.selected=0;require(f.dialog.poll(0)==D::Finished);
        if(which==0)f.put<uint64_t>(f.out+16,0x99999999);
        if(which==1)f.put<uint64_t>(f.out+24,0x99999999);
        if(which==2)f.put<uint64_t>(f.out+16,f.out+71);
        if(which==3)f.put<uint64_t>(f.out+24,f.out+71);
        if(which==4)f.put<uint64_t>(f.out+24,f.dir+31);
        if(which==5){unmapRegion(f.memory,f.base);mapRegion(f.memory,f.base,f.bytes.data(),f.bytes.size(),false);}
        auto before=f.bytes;require(f.dialog.getResult(f.memory,f.out)!=0 && f.bytes==before);
    }
    {Fixture f;require(f.dialog.initialize()==0);require(f.dialog.open(f.memory,0,f.presenter,0)==D::ArgumentNull);
     require(f.dialog.open(f.memory,f.param,{},0)==D::NotSupported && !f.begun);
     f.can_begin=false;require(f.open()==D::NotSupported && !f.ended);require(f.dialog.terminate()==0 && !f.ended);}
    {Fixture f;require(f.dialog.initialize()==0);f.put<uint64_t>(f.param+120,f.base+7001);f.put<uint32_t>(f.base+7001,1);
     require(f.open()==0 && !f.seen.enable_back);f.selected=-2;require(f.dialog.poll(0)==D::Running);require(f.dialog.close()==0);}
    {Fixture f;require(f.dialog.initialize()==0);std::memset(f.bytes.data()+f.text-f.base,'X',4095);f.bytes[f.text+4095-f.base]=0;
     require(f.open()==0 && f.seen.text.size()==4095);require(f.dialog.terminate()==0);}
    {Fixture f;require(f.dialog.initialize()==0);f.bytes[f.text-f.base]=0;require(f.open()==0 && f.seen.text.empty());}
    std::printf("{\"checks\":%u,\"failed\":0,\"real_ui_verified\":false}\n",checks);
}
