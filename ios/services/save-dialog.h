#pragma once
#include "../cpu/runtime.h"
#include "../../src/runtime_savedata_types.h"
#include <array>
#include <cstring>
#include <string>

// Host-only values: no guest pointer crosses the presentation boundary. The
// presenter copies strings during begin; it owns its UI until end/selection.
struct SaveMessage {
    uint32_t display_type=0,button_type=0,message_type=0;
    bool enable_back=true;
    std::string text;
};
struct SaveMessagePresenter {
    void *context=nullptr;
    bool (*begin)(void *,const SaveMessage &,uint32_t initial_buttons)=nullptr;
    // -1 pending, -2 cancelled, otherwise the zero-based button index.
    int (*poll)(void *,uint32_t buttons)=nullptr;
    void (*end)(void *)=nullptr;
    bool available()const{return begin && poll && end;}
};

// The caller serializes this state with the other guest services. Only mode 2
// is implemented here. Other modes keep an explicit service gap in the runner.
// ABI offsets are cross-checked against the actual desktop runtime and pinned
// shadPS4 4e22b3ba29d0133ef97477e5740df0c85c61c2be dialog headers.
class MobileSaveDialog {
public:
    enum : uint32_t { None=0,Initialized=1,Running=2,Finished=3,
        NotInitialized=0x80b80003u,AlreadyInitialized=0x80b80004u,
        NotFinished=0x80b80005u,InvalidState=0x80b80006u,
        InvalidParameter=0x80b8000au,NotRunning=0x80b8000bu,
        ArgumentNull=0x80b8000du,NotSupported=0x80b8000fu };
private:
    uint32_t status_=None,result_=0,button_=0;
    uint64_t user_data_=0;
    SaveMessage message_;
    SaveMessagePresenter presenter_;
    bool owns_ui_=false;
    uint64_t opens_=0,completions_=0;
    void end() {if(owns_ui_){presenter_.end(presenter_.context);owns_ui_=false;}}
    template<class T> static T field(const uint8_t *p,size_t offset) {
        T value{};std::memcpy(&value,p+offset,sizeof(value));return value;
    }
public:
    MobileSaveDialog()=default;
    MobileSaveDialog(const MobileSaveDialog &)=delete;
    MobileSaveDialog &operator=(const MobileSaveDialog &)=delete;
    ~MobileSaveDialog(){end();}
    uint32_t status()const{return status_;}
    uint64_t opens()const{return opens_;}
    uint64_t completions()const{return completions_;}
    uint32_t button()const{return button_;}
    uint32_t result()const{return result_;}
    const SaveMessage &message()const{return message_;}
    uint32_t initialize() {
        if(status_!=None)return AlreadyInitialized;
        status_=Initialized;return 0;
    }
    uint32_t terminate() {
        if(status_==None)return NotInitialized;
        end();status_=None;return 0;
    }
    uint32_t close() {
        if(status_!=Running)return NotRunning;
        end();status_=Finished;result_=1;button_=0;++completions_;return 0;
    }
    uint32_t open(Memory &memory,uint64_t address,const SaveMessagePresenter &presenter,uint32_t buttons) {
        if(status_==None)return NotInitialized;
        if(status_==Running)return InvalidState;
        if(!address)return ArgumentNull;
        auto p=guestPointer(memory,address,152);if(!p)return InvalidParameter;
        if(field<uint64_t>(p,0)!=48 || field<uint32_t>(p,48)!=152)return InvalidParameter;
        if(field<uint32_t>(p,52)!=2)return NotSupported;
        SaveMessage next;
        next.display_type=field<uint32_t>(p,56);
        if(next.display_type<1 || next.display_type>3)return InvalidParameter;
        const auto user_data=field<uint64_t>(p,112),user_message=field<uint64_t>(p,80),options=field<uint64_t>(p,120);
        // Lists/item metadata need their own adapter. Never silently discard
        // them and return an empty filename as a successful selection.
        const auto items=field<uint64_t>(p,72);
        if(items) {
            auto i=guestPointer(memory,items,96);if(!i)return InvalidParameter;
            if(field<uint32_t>(i,24) || field<uint64_t>(i,32))return NotSupported;
        }
        if(options) {
            auto o=guestPointer(memory,options,36);if(!o)return InvalidParameter;
            auto back=field<uint32_t>(o,0);if(back>1)return InvalidParameter;
            next.enable_back=back==0;
        }
        if(!user_message)return ArgumentNull;
        auto u=guestPointer(memory,user_message,48);if(!u)return InvalidParameter;
        next.button_type=field<uint32_t>(u,0);next.message_type=field<uint32_t>(u,4);
        if(next.button_type>3 || next.message_type>1)return InvalidParameter;
        const auto text=field<uint64_t>(u,8);
        if(!text)return ArgumentNull;
        bool terminated=false;
        for(size_t n=0;n<4096;++n) {
            if(text>UINT64_MAX-n)return InvalidParameter;
            auto c=guestPointer(memory,text+n,1);if(!c)return InvalidParameter;
            if(!*c){terminated=true;break;}
            next.text.push_back(static_cast<char>(*c));
        }
        if(!terminated)return InvalidParameter;
        if(!presenter.available())return NotSupported;
        if(!presenter.begin(presenter.context,next,buttons))return NotSupported;
        message_=std::move(next);presenter_=presenter;owns_ui_=true;
        user_data_=user_data;result_=button_=0;status_=Running;++opens_;return 0;
    }
    uint32_t poll(uint32_t buttons) {
        if(status_!=Running)return status_;
        const int selection=presenter_.poll(presenter_.context,buttons);
        if(message_.button_type==2)return status_; // NONE waits for Close.
        if(selection==-1)return status_;
        if(selection==-2) {
            if(!message_.enable_back)return status_;
            result_=1;button_=0;
        } else if(selection==0) {result_=0;button_=1;}
        else if(selection==1 && message_.button_type==1) {result_=0;button_=2;}
        else if(selection==1 && message_.button_type==3) {result_=1;button_=0;}
        else return status_; // An invalid presenter result cannot complete.
        end();status_=Finished;++completions_;return status_;
    }
    uint32_t getResult(Memory &memory,uint64_t address)const {
        if(status_!=Finished)return NotFinished;
        if(!address)return ArgumentNull;
        auto out=guestPointer(memory,address,72,true);if(!out)return InvalidParameter;
        // Validate every destination before any write. Preserve pointer fields,
        // padding and caller-owned save parameters (this mode selected no item).
        const auto dirname=field<uint64_t>(out,16),param=field<uint64_t>(out,24);
        auto overlaps=[](uint64_t a,size_t n,uint64_t b,size_t k){return a<=b ? b-a<n : a-b<k;};
        if((dirname && overlaps(address,72,dirname,32)) ||
           (param && overlaps(address,72,param,sizeof(OslSaveParam))) ||
           (dirname && param && overlaps(dirname,32,param,sizeof(OslSaveParam))))return InvalidParameter;
        auto dir=dirname ? guestPointer(memory,dirname,32,true) : nullptr;
        if(dirname && !dir)return InvalidParameter;
        if(param && !guestPointer(memory,param,sizeof(OslSaveParam),true))return InvalidParameter;
        const uint32_t mode=2;
        std::memcpy(out,&mode,4);std::memcpy(out+4,&result_,4);std::memcpy(out+8,&button_,4);
        std::memcpy(out+32,&user_data_,8);
        if(dir)std::memset(dir,0,32);
        return 0;
    }
};
