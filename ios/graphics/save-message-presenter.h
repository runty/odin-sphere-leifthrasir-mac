#pragma once
#include "../services/save-dialog.h"
#include "oslgpu.h"

inline SaveMessagePresenter nativeSaveMessagePresenter() {
    return {nullptr,
        [](void *,const SaveMessage &message,uint32_t initial) {
            const char *title=message.display_type==1 ? "Save game" : message.display_type==2 ? "Load game" : "Delete save";
            const char *labels[2]={message.button_type==1 ? "Yes" : "OK",message.button_type==1 ? "No" : "Cancel"};
            const int count=message.button_type==2 ? 0 : message.button_type==1 || message.button_type==3 ? 2 : 1;
            return oslgpu_message_begin(title,message.text.c_str(),labels,count,message.message_type==1,message.enable_back,initial)!=0;
        },
        [](void *,uint32_t buttons){return oslgpu_message_poll(buttons);},
        [](void *){oslgpu_message_end();}};
}
