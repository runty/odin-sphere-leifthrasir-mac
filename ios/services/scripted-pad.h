// SPDX-License-Identifier: GPL-2.0-or-later
// Bounded startup-test input. Grammar and stick mapping follow OpenCrown's
// frame-indexed scripts; numeric parsing and file snapshots are checked here.
#pragma once
#include "pad.h"
#include <CommonCrypto/CommonDigest.h>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

class InputScriptError final : public std::runtime_error {
public:
    explicit InputScriptError(const std::string &message) : std::runtime_error(message) {}
};

class ScriptedPad {
public:
    static constexpr size_t byte_limit=1024*1024;
    static constexpr uint32_t left=0x10000000, right=0x20000000,
        up=0x40000000, down=0x80000000, sticks=left|right|up|down;
    struct Event { uint64_t frame; uint32_t buttons; };
private:
    std::vector<Event> events_;
    std::function<uint64_t()> frames_;
    // MobilePad serializes sampling; reports are read after its guest CPUs join.
    uint64_t baseline_=0, last_frame_=0, samples_=0, active_samples_=0;
    std::string digest_;

    static uint32_t buttons(std::string value) {
        constexpr uint32_t known=BTN_L3|BTN_R3|BTN_OPTIONS|BTN_UP|BTN_RIGHT|BTN_DOWN|BTN_LEFT|
            BTN_L2|BTN_R2|BTN_L1|BTN_R1|BTN_TRIANGLE|BTN_CIRCLE|BTN_CROSS|BTN_SQUARE|BTN_TOUCHPAD|sticks;
        if (!value.empty() && value[0]>='0' && value[0]<='9') {
            const bool hex=value.size()>2 && value[0]=='0' && (value[1]=='x' || value[1]=='X');
            const char *first=value.data()+(hex?2:0), *end=value.data()+value.size();
            uint32_t result=0;auto p=std::from_chars(first,end,result,hex?16:10);
            if(p.ec!=std::errc{} || p.ptr!=end || (result & ~known))throw InputScriptError("Invalid button mask");
            return result;
        }
        struct Named { const char *name;uint32_t bit; };
        static constexpr Named names[]={
            {"L3",BTN_L3},{"R3",BTN_R3},{"OPTIONS",BTN_OPTIONS},{"UP",BTN_UP},
            {"RIGHT",BTN_RIGHT},{"DOWN",BTN_DOWN},{"LEFT",BTN_LEFT},{"L2",BTN_L2},
            {"R2",BTN_R2},{"L1",BTN_L1},{"R1",BTN_R1},{"TRIANGLE",BTN_TRIANGLE},
            {"CIRCLE",BTN_CIRCLE},{"CROSS",BTN_CROSS},{"SQUARE",BTN_SQUARE},{"TOUCHPAD",BTN_TOUCHPAD},
            {"LS_LEFT",left},{"LS_RIGHT",right},{"LS_UP",up},{"LS_DOWN",down}};
        for(char &c:value)c=char(std::toupper(static_cast<unsigned char>(c)));
        uint32_t result=0;size_t begin=0;
        for(;;) {
            size_t end=value.find_first_of("+|",begin);
            const std::string name=value.substr(begin,end==std::string::npos?end:end-begin);
            auto found=std::find_if(std::begin(names),std::end(names),[&](auto n){return name==n.name;});
            if(found==std::end(names))throw InputScriptError("Invalid button name");
            result|=found->bit;
            if(end==std::string::npos)return result;
            begin=end+1;
        }
    }
public:
    static std::vector<Event> parse(const std::string &text) {
        if(text.size()>byte_limit)throw InputScriptError("Input script exceeds 1 MiB");
        std::istringstream stream(text);std::string line;std::vector<Event> events;size_t number=0;
        while(std::getline(stream,line)) {
            ++number;if(auto comment=line.find('#');comment!=std::string::npos)line.resize(comment);
            std::istringstream tokens(line);std::string frame,keys,extra;if(!(tokens>>frame))continue;
            try {
                if(!(tokens>>keys) || tokens>>extra)throw InputScriptError("Expected frame and buttons");
                uint64_t value=0;auto p=std::from_chars(frame.data(),frame.data()+frame.size(),value);
                if(p.ec!=std::errc{} || p.ptr!=frame.data()+frame.size())throw InputScriptError("Invalid frame");
                events.push_back({value,buttons(keys)});
            } catch(const InputScriptError &) {
                throw InputScriptError("Invalid input script at line "+std::to_string(number));
            }
        }
        std::stable_sort(events.begin(),events.end(),[](auto a,auto b){return a.frame<b.frame;});
        return events;
    }
    ScriptedPad(std::string bytes,std::function<uint64_t()> frames) : events_(parse(bytes)),frames_(std::move(frames)) {
        if(!frames_)throw InputScriptError("Input script requires a renderer frame clock");
        baseline_=frames_();
        unsigned char hash[CC_SHA256_DIGEST_LENGTH];CC_SHA256(bytes.data(),CC_LONG(bytes.size()),hash);
        for(auto b:hash){digest_+="0123456789abcdef"[b>>4];digest_+="0123456789abcdef"[b&15];}
    }
    static std::shared_ptr<ScriptedPad> load(const char *path,std::function<uint64_t()> frames) {
        if(!path)return {};
        if(!frames)throw InputScriptError("Input script requires a renderer frame clock");
        struct File {int fd;~File(){if(fd>=0)::close(fd);}} file{::open(path,O_RDONLY|O_CLOEXEC|O_NONBLOCK|O_NOFOLLOW)};
        struct stat before{},after{};
        if(file.fd<0 || ::fstat(file.fd,&before) || !S_ISREG(before.st_mode))
            throw InputScriptError("Input script must be a readable regular file");
        if(before.st_size<0 || uint64_t(before.st_size)>byte_limit)throw InputScriptError("Input script exceeds 1 MiB");
        std::string bytes(size_t(before.st_size),'\0');size_t at=0;
        while(at<bytes.size()) {
            const ssize_t count=::read(file.fd,bytes.data()+at,bytes.size()-at);
            if(count<0 && errno==EINTR)continue;
            if(count<=0)throw InputScriptError("Input script changed or could not be read");
            at+=size_t(count);
        }
        if(::fstat(file.fd,&after) || before.st_size!=after.st_size ||
           before.st_mtimespec.tv_sec!=after.st_mtimespec.tv_sec || before.st_mtimespec.tv_nsec!=after.st_mtimespec.tv_nsec ||
           before.st_ctimespec.tv_sec!=after.st_ctimespec.tv_sec || before.st_ctimespec.tv_nsec!=after.st_ctimespec.tv_nsec)
            throw InputScriptError("Input script changed while reading");
        return std::make_shared<ScriptedPad>(std::move(bytes),std::move(frames));
    }
    PadSample sample(unsigned slot) {
        if(slot)return {};
        const uint64_t now=frames_();
        if(now<baseline_ || now-baseline_<last_frame_)throw InputScriptError("Input script frame clock moved backwards");
        const uint64_t frame=now-baseline_;last_frame_=frame;++samples_;
        auto event=std::upper_bound(events_.begin(),events_.end(),frame,[](uint64_t value,const Event &e){return value<e.frame;});
        const uint32_t bits=event==events_.begin()?0:std::prev(event)->buttons;
        PadSample result;result.connected=true;result.identity=UINT64_MAX;result.buttons=bits & ~sticks;
        if(bits&left)result.left_x=-1;if(bits&right)result.left_x=1;
        if(bits&up)result.left_y=-1;if(bits&down)result.left_y=1;
        if(bits&BTN_L2)result.l2=1;if(bits&BTN_R2)result.r2=1;
        if(bits)++active_samples_;
        return result;
    }
    const std::string &digest() const{return digest_;}
    size_t events() const{return events_.size();}
    uint64_t samples() const{return samples_;}
    uint64_t activeSamples() const{return active_samples_;}
    uint64_t lastFrame() const{return last_frame_;}
};
