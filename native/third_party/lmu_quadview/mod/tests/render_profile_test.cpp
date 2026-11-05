#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../render_profile.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <thread>

namespace {
struct Clock {
    inline static uint64_t ticks=0,reads=0,frequencies=0;inline static DWORD id=1;
    static uint64_t now(){++reads;return ticks;}
    static uint64_t frequency(){++frequencies;return 1000;}
    static DWORD thread(){return id;}
};
struct Gpu {
    using Context=void*;
    inline static unsigned starts=0,polls=0,finishes=0,snapshots=0;
    void reset(){}void configure(bool,unsigned,void*,void*){}
    bool start(Context,unsigned){++starts;return true;}void stop(Context){}
    void poll(Context){++polls;}void finish(){++finishes;}
    bool faulted()const{return false;}
    lmuqv::GpuTimingStats stats(unsigned)const{return {};}
    lmuqv::GpuTimingStats snapshot(unsigned)const{++snapshots;return {};}
};
std::vector<std::string> lines;
unsigned flushes=0;
void flush(){++flushes;}
void log(const char* s){lines.emplace_back(s);}
bool contains(const char* s){for(auto& line:lines)if(line.find(s)!=std::string::npos)return true;return false;}
}
int main(int argc,char** argv){
    if(argc>1&&!std::strcmp(argv[1],"--diagnostics-off")){
        SetEnvironmentVariableA("LMU_QV_DIAGNOSTICS","0");
        lmuqv::BasicRenderProfile<Clock,Gpu,32> quiet;
        quiet.configure(true,log,120,flush);
        for(unsigned i=0;i<1000;++i){quiet.xr_frame_begin();quiet.frame(nullptr,true);
            auto token=quiet.begin(lmuqv::ProfileSlot::outer0);quiet.end(token);
            assert(!quiet.gpu_start(nullptr,0));quiet.xr_frame_end(nullptr);}
        quiet.shutdown();
        assert(!quiet.enabled()&&Clock::reads==0&&Clock::frequencies==0&&Gpu::starts==0&&Gpu::polls==0&&lines.empty());
        std::cout<<"Diagnostics off: overrides profile-on, no CPU clock reads, GPU queries or profile messages\n";return 0;
    }
    using namespace lmuqv;
    BasicRenderProfile<Clock,Gpu,3> p;
    p.configure(false,log);p.frame(nullptr,true);
    auto ignored=p.begin(ProfileSlot::outer0);Clock::ticks+=10;p.end(ignored);
    assert(!p.enabled()&&!p.collecting()&&lines.empty()&&Gpu::polls==0);
    p.configure(true,log,30);p.frame(nullptr,false);assert(!p.collecting());
    p.frame(nullptr,true);assert(p.collecting());
    assert(p.gpu_start(nullptr,0)&&Gpu::starts==1);p.gpu_stop(nullptr);
    auto outer=p.begin(ProfileSlot::outer0);Clock::ticks+=2;
    auto inner=p.begin(ProfileSlot::native_prepare0);Clock::ticks+=3;p.end(inner);
    Clock::ticks+=5;p.end(outer);p.end(outer); // double end cannot count twice
    for(unsigned i=0;i<2;++i){auto sort=p.begin(ProfileSlot::sort_outer0);Clock::ticks+=2;p.end(sort);}
    auto old=p.begin(ProfileSlot::ui);
    Clock::id=2;assert(!p.collecting());p.frame(nullptr,false);
    auto foreign=p.begin(ProfileSlot::outer1);p.end(foreign);
    assert(!p.gpu_start(nullptr,1));Clock::id=1;
    Clock::ticks+=1;p.frame(nullptr,true); // held hotkey does not stop
    assert(p.collecting()&&p.samples(ProfileSlot::outer0)==1);
    assert(!p.gpu_start(nullptr,0)); // sparse queries: frame 2
    for(unsigned i=0;i<4;++i){
        auto token=p.begin(ProfileSlot::outer0);Clock::ticks+=20;p.end(token);
        p.frame(nullptr,false);
    }
    p.frame(nullptr,true);assert(!p.collecting()&&Gpu::finishes==1);
    assert(contains("slot=outer0 samples=3 calls=5 dropped=2"));
    assert(contains("slot=native_prepare0 samples=1 calls=1 dropped=0"));
    assert(contains("slot=sort_outer0 samples=1 calls=2 dropped=0"));
    assert(contains("mean_ms=4.000000"));
    assert(!contains("cpu slot=outer1"));
    p.frame(nullptr,false);p.frame(nullptr,true);assert(p.collecting());
    p.end(old); // a token from another capture cannot record
    Clock::ticks+=30000;p.frame(nullptr,false);
    assert(!p.collecting()&&contains("capture=2 reason=duration")&&!contains("cpu slot=ui"));
    p.shutdown();assert(!p.enabled());
    // Long captures retain frames beyond the old 4096-frame buffer and save on
    // another teardown thread without making GPU context calls there.
    lines.clear();auto longRun=std::make_unique<BasicRenderProfile<Clock,Gpu,8192>>();
    longRun->configure(true,log,120,flush);longRun->frame(nullptr,true);
    Clock::ticks+=31000;longRun->frame(nullptr,false);assert(longRun->collecting());
    assert(contains("render_profile checkpoint"));
    for(unsigned i=0;i<6000;++i){auto t=longRun->begin(ProfileSlot::focus_scene0);Clock::ticks+=2;longRun->end(t);Clock::ticks+=6;longRun->frame(nullptr,false);}
    assert(longRun->samples(ProfileSlot::focus_scene0)==6000);
    const auto finished=Gpu::finishes;Clock::id=2;longRun->shutdown();Clock::id=1;
    assert(!longRun->enabled()&&Gpu::finishes==finished&&Gpu::snapshots==4&&flushes>0);
    assert(contains("reason=session_end")&&contains("partial saved on teardown thread"));
    assert(contains("slot=focus_scene0 samples=6000 calls=6000 dropped=0"));
    lines.clear();p.configure(true,log,300,flush);p.frame(nullptr,true);Clock::ticks+=120000;p.frame(nullptr,false);
    assert(p.collecting());Clock::ticks+=180000;p.frame(nullptr,false);assert(!p.collecting()&&contains("reason=duration"));p.shutdown();
    // Actual cross-thread shutdown while the owner is recording scopes.
    lines.clear();BasicRenderProfile<NativeProfileClock,Gpu,32> concurrent;
    concurrent.configure(true,log,120,flush);concurrent.frame(nullptr,true);
    auto initial=concurrent.begin(ProfileSlot::outer0);concurrent.end(initial);
    std::atomic<bool> go{false};std::thread closer([&]{while(!go.load())std::this_thread::yield();concurrent.shutdown();});
    go.store(true);
    for(unsigned i=0;i<1000;++i){auto t=concurrent.begin(ProfileSlot::outer0);concurrent.end(t);}
    closer.join();assert(!concurrent.enabled()&&contains("partial saved on teardown thread")&&contains("cpu slot=outer0"));
    // Hundreds of menu XR frames must remain separate samples and must not
    // pollute scene UI/submission or the cadence across a menu/race transition.
    lines.clear();auto menus=std::make_unique<BasicRenderProfile<Clock,Gpu,1024>>();
    menus->configure(true,log,30,flush);menus->frame(nullptr,true);
    auto sceneSubmit=menus->begin(ProfileSlot::provider_submit);Clock::ticks+=3;menus->end(sceneSubmit);menus->xr_frame_end(nullptr);
    for(unsigned i=0;i<750;++i){
        menus->xr_frame_begin();auto ui=menus->begin(ProfileSlot::ui);Clock::ticks+=2;menus->end(ui);
        auto submit=menus->begin(ProfileSlot::provider_submit);Clock::ticks+=3;menus->end(submit);menus->xr_frame_end(nullptr);
    }
    assert(menus->samples(ProfileSlot::provider_submit)==1&&menus->samples(ProfileSlot::non_scene_provider_submit)==750);
    assert(menus->samples(ProfileSlot::non_scene_ui)==750);
    Clock::ticks+=1000;menus->xr_frame_begin();menus->frame(nullptr,false);menus->xr_frame_end(nullptr);
    assert(menus->samples(ProfileSlot::frame_interval)==0); // no menu gap in scene cadence
    menus->xr_frame_begin();auto staleFrame=menus->begin(ProfileSlot::ui);
    menus->xr_frame_begin();menus->end(staleFrame); // abandoned frame token cannot leak
    assert(menus->samples(ProfileSlot::non_scene_ui)==750);
    Clock::ticks+=30000;menus->xr_frame_end(nullptr);assert(!menus->collecting());
    assert(contains("slot=non_scene_provider_submit samples=750 calls=750 dropped=0")&&contains("max_ms=3.000000"));
    assert(contains("reason=duration"));menus->shutdown();
    std::cout<<"Render profiling tests passed: opt-in/hotkey/timeout, 30/120/300 seconds, >4096 frames, checkpoints, same/cross-thread shutdown, flush, inclusive scopes, bounds, sparse GPU, stale tokens\n";
}
