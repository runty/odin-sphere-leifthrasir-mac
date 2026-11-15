#include "scripted-pad.h"
#include "user.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

static unsigned checks;
static void require(bool ok) {
    ++checks;if(!ok)throw std::runtime_error("script pad check "+std::to_string(checks));
}
template<class F> static void rejects(F function,const char *message) {
    try {function();}catch(const InputScriptError &e){require(std::string(e.what()).find(message)!=std::string::npos);return;}
    require(false);
}
static void write(const std::filesystem::path &path,const std::string &bytes) {
    std::ofstream f(path,std::ios::binary);f.write(bytes.data(),bytes.size());if(!f)throw std::runtime_error("write fixture");
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    std::filesystem::path root=argv[1];std::filesystem::create_directories(root);
    uint64_t clock=100;
    const std::string bytes="# comment\n2 cross+ls_up|l2\n0 0\n4 0\n4 SQUARE\n6 0x2000\n8 0 # release\n";
    ScriptedPad script(bytes,[&]{return clock;});
    require(script.events()==6 && script.digest().size()==64);
    auto p=script.sample(0);require(p.connected && p.identity==UINT64_MAX && !p.buttons);
    require(!script.sample(1).connected && script.samples()==1);
    clock=102;p=script.sample(0);require(p.buttons==(BTN_CROSS|BTN_L2) && p.left_y==-1 && p.l2==1);
    require(!(p.buttons&ScriptedPad::sticks) && script.activeSamples()==1);
    clock=104;p=script.sample(0);require(p.buttons==BTN_SQUARE && p.left_y==0 && p.l2==0);
    clock=106;require(script.sample(0).buttons==BTN_CIRCLE);
    clock=108;require(!script.sample(0).buttons && script.lastFrame()==8);
    clock=107;rejects([&]{script.sample(0);},"backwards");
    for(const auto &bad:{"-1 CROSS","+1 CROSS","18446744073709551616 CROSS","1x CROSS","0 4294967296",
        "0 1","0 0x1","0 -1","0 CROSS+","0 CROSS||CIRCLE","0 unknown","0 CROSS extra","0"})
        rejects([&]{ScriptedPad::parse(std::string("# first\n")+bad);},"line 2");
    require(ScriptedPad::parse("18446744073709551615 CROSS").front().frame==UINT64_MAX);
    require(ScriptedPad::parse("0 0X4000").front().buttons==BTN_CROSS);
    require(ScriptedPad::parse("\t0 cross\r\n").front().buttons==BTN_CROSS);
    require(ScriptedPad::parse("\n# empty\n").empty());
    struct Named{const char *name;uint32_t bit;};
    const Named names[]={{"l3",BTN_L3},{"r3",BTN_R3},{"options",BTN_OPTIONS},{"up",BTN_UP},
        {"right",BTN_RIGHT},{"down",BTN_DOWN},{"left",BTN_LEFT},{"l2",BTN_L2},{"r2",BTN_R2},
        {"l1",BTN_L1},{"r1",BTN_R1},{"triangle",BTN_TRIANGLE},{"circle",BTN_CIRCLE},
        {"cross",BTN_CROSS},{"square",BTN_SQUARE},{"touchpad",BTN_TOUCHPAD},
        {"ls_left",ScriptedPad::left},{"ls_right",ScriptedPad::right},{"ls_up",ScriptedPad::up},{"ls_down",ScriptedPad::down}};
    for(auto n:names) {
        ScriptedPad one(std::string("0 ")+n.name,[]{return 0;});auto state=one.sample(0);
        require(state.connected && state.buttons==(n.bit&~ScriptedPad::sticks));
        require(state.left_x==((n.bit&ScriptedPad::left)?-1:(n.bit&ScriptedPad::right)?1:0));
        require(state.left_y==((n.bit&ScriptedPad::up)?-1:(n.bit&ScriptedPad::down)?1:0));
        require(state.l2==((n.bit&BTN_L2)?1:0) && state.r2==((n.bit&BTN_R2)?1:0));
    }
    // The actual MobilePad conversion and primary/fallback precedence.
    osl_aot_user_reset();clock=100;
    auto backup=std::make_shared<ScriptedPad>(bytes,[&]{return clock;});PadSample primary;
    MobilePad pad([&](unsigned){return primary;},[&](unsigned slot){return backup->sample(slot);});
    require(pad.initialize()==0 && pad.open(1,0,0)==1);PadData data{};
    clock=102;require(pad.read(1,&data)==0 && data.connected_count==1 && data.connected);
    require(data.buttons==(BTN_CROSS|BTN_L2) && data.left_y==0 && data.left_x==128 && data.l2==255);
    const auto samples=backup->samples();primary.connected=true;primary.identity=7;primary.buttons=BTN_CIRCLE;
    require(pad.read(1,&data)==0 && data.connected_count==2 && data.buttons==BTN_CIRCLE);
    require(backup->samples()==samples && data.left_y==128 && data.l2==0);
    primary.connected=false;clock=104;require(pad.read(1,&data)==0 && data.connected_count==3 && data.buttons==BTN_SQUARE);
    clock=108;require(pad.read(1,&data)==0 && !data.buttons && data.connected_count==3);
    require(pad.fallbackReads()==3 && pad.fallbackActiveReads()==2);
    auto reads=pad.readStats();
    require(reads.total==4 && reads.primary==1 && reads.fallback==3 && reads.disconnected==0);
    ControllerInfo info{};require(pad.info(1,&info)==0 && info.connected);
    require(pad.fallbackReads()==3 && pad.fallbackActiveReads()==2); // Info does not deliver buttons.
    require(pad.readStats().total==4);
    auto normal=ScriptedPad::selectPrimary(nullptr,true);
    require(!normal.source && normal.physical_enabled && std::string(normal.name)=="fallback");
    require(!ScriptedPad::selectPrimary("fallback",false).source);
    rejects([&]{ScriptedPad::selectPrimary("script-only",false);},"requires OSL_INPUT_SCRIPT");
    rejects([&]{ScriptedPad::selectPrimary("",true);},"OSL_INPUT_MODE");
    rejects([&]{ScriptedPad::selectPrimary("SCRIPT-ONLY",true);},"OSL_INPUT_MODE");
    auto exclusive=ScriptedPad::selectPrimary("script-only",true);
    require(exclusive.source && !exclusive.physical_enabled && std::string(exclusive.name)=="script-only");
    require(!exclusive.source(0).connected && !exclusive.source(3).connected);
    clock=100;
    auto exclusive_script=std::make_shared<ScriptedPad>("0 CROSS\n2 0\n",[&]{return clock;});
    MobilePad exclusive_pad(exclusive.source,[&](unsigned slot){return exclusive_script->sample(slot);});
    require(exclusive_pad.initialize()==0 && exclusive_pad.open(1,0,0)==1);
    require(exclusive_pad.read(1,&data)==0 && data.connected && data.buttons==BTN_CROSS);
    clock=102;require(exclusive_pad.read(1,&data)==0 && !data.buttons);
    reads=exclusive_pad.readStats();
    require(reads.total==2 && reads.primary==0 && reads.fallback==2 && reads.disconnected==0);
    require(exclusive_pad.fallbackActiveReads()==1);
    require(exclusive_pad.info(1,&info)==0 && exclusive_pad.readStats().total==2);
    rejects([&]{ScriptedPad empty("",{});},"frame clock");
    require(!ScriptedPad::load(nullptr,{}));
    rejects([&]{ScriptedPad::load("missing",{});},"frame clock");
    auto path=root/"input.txt";write(path,bytes);clock=100;
    auto loaded=ScriptedPad::load(path.c_str(),[&]{return clock;});require(loaded->digest()==script.digest());
    write(path,"0 CIRCLE");clock=102;require(loaded->sample(0).buttons==(BTN_CROSS|BTN_L2));
    write(path,"");auto empty=ScriptedPad::load(path.c_str(),[]{return 0;});require(empty->events()==0 && !empty->sample(0).buttons);
    rejects([&]{ScriptedPad::load((root/"missing").c_str(),[]{return 0;});},"regular file");
    rejects([&]{ScriptedPad::load(root.c_str(),[]{return 0;});},"regular file");
    auto link=root/"input-link";std::filesystem::remove(link);std::filesystem::create_symlink(path,link);
    rejects([&]{ScriptedPad::load(link.c_str(),[]{return 0;});},"regular file");
    auto fifo=root/"input-fifo";std::filesystem::remove(fifo);require(::mkfifo(fifo.c_str(),0600)==0);
    rejects([&]{ScriptedPad::load(fifo.c_str(),[]{return 0;});},"regular file");
    write(path,std::string(ScriptedPad::byte_limit,'#'));require(ScriptedPad::load(path.c_str(),[]{return 0;})->events()==0);
    write(path,std::string(ScriptedPad::byte_limit+1,'#'));
    rejects([&]{ScriptedPad::load(path.c_str(),[]{return 0;});},"1 MiB");
    rejects([&]{ScriptedPad::parse(std::string(ScriptedPad::byte_limit+1,'#'));},"1 MiB");
    std::cout<<"{\"checks\":"<<checks<<",\"failed\":0,\"script_sha256\":\""<<script.digest()<<"\"}\n";
}
