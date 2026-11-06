// White-box benchmark integration tests. No game process or graphics device.
#include "../engine_bridge.cpp"
#include <fstream>
#include <cassert>
#include <iostream>

namespace lmuqv {
std::vector<std::string> benchmarkTestLogs;
bool xr_runtime_sizes(unsigned eye,uint32_t& width,uint32_t& height,uint32_t& focusWidth,uint32_t& focusHeight){
    if(eye>1)return false;
    width=eye?20:24;height=eye?28:16;focusWidth=focusHeight=32;return true;
}
bool xr_vrs_active(){return false;}
bool xr_vrs_focus(unsigned,VrsFocus&){return false;}
bool xr_vrs_render_focus(unsigned,const float*,VrsFocus&){return false;}
void xr_vrs_scene_complete(unsigned){}
bool xr_quad_active(){return false;}
bool xr_frame_views(XrView (&)[4]){return false;}
bool xr_frame_full_outer_fov(XrFovf (&)[2]){return false;}
bool xr_copy_outer(unsigned,ID3D11Texture2D*){return false;}
void xr_scene_focus_complete(unsigned){}
ID3D11RenderTargetView* xr_outer_rtv(unsigned){return nullptr;}
OuterTarget xr_outer_target(unsigned){return {};}
void log_line(const char* line){benchmarkTestLogs.emplace_back(line);}
void flush_log(){}
}
int main(){
    using namespace lmuqv;
    assert(benchmarkRequestedRate()==1);
    std::string content;
    assert(!readBenchmarkCommand(content));
    wchar_t temporaryDirectory[MAX_PATH]{},temporaryFile[MAX_PATH]{};
    assert(GetTempPathW(MAX_PATH,temporaryDirectory));
    assert(GetTempFileNameW(temporaryDirectory,L"qvb",0,temporaryFile));
    benchmarkPath=temporaryFile;
    auto write=[&](const std::string& bytes){
        std::ofstream file(std::filesystem::path(benchmarkPath),std::ios::binary|std::ios::trunc);
        file.write(bytes.data(),std::streamsize(bytes.size()));file.close();assert(file.good());
    };
    write("");assert(!readBenchmarkCommand(content));
    write(std::string(257,' '));assert(!readBenchmarkCommand(content));
    write("1 4 1"+std::string(251,' '));assert(readBenchmarkCommand(content)&&content.size()==256);
    benchmarkFrame();assert(benchmarkControl.state()==BenchmarkControl::State::warming);
    assert(benchmarkRequestedRate()==4&&benchmarkControl.warmupRemaining()==0);
    assert(!gpuTiming.enabled()&&!pipelineStats.enabled());
    benchmarkFrame();assert(benchmarkControl.state()==BenchmarkControl::State::measuring);
    assert(gpuTiming.enabled()&&pipelineStats.enabled());
    const auto logCount=benchmarkTestLogs.size();
    benchmarkFrame();assert(benchmarkTestLogs.size()==logCount);
    write("2 4 NaN");benchmarkFrame();assert(benchmarkTestLogs.size()==logCount);
    assert(benchmarkControl.phaseId()==1);
    write("2 1 3");benchmarkFrame();
    assert(benchmarkControl.phaseId()==2&&benchmarkRequestedRate()==1&&benchmarkControl.warmupRemaining()==2);
    assert(!gpuTiming.enabled()&&!pipelineStats.enabled());
    assert(benchmarkSnapshot.phase==2&&benchmarkSnapshot.requestedRate==1);
    benchmarkLog(&benchmarkSnapshot,"sample");
    assert(benchmarkTestLogs.back()=="bench phase=2 requested_rate=1 sample");
    nativeOuterOptIn=true;
    nativeOuterFull[0]=nativeOuterFull[1]=60;
    assert(nativeBenchmarkComplete());
    nativeOuterFull[1]=59;assert(!nativeBenchmarkComplete());
    nativeOuterFull[1]=60;nativeOuterCompleted[0]=1;assert(!nativeBenchmarkComplete());
    write("3 2 0");benchmarkFrame();
    assert(nativeOuterFull[0]==0 && nativeOuterFull[1]==0 && nativeOuterCompleted[0]==0);
    nativeOuterCompleted[0]=nativeOuterCompleted[1]=60;assert(nativeBenchmarkComplete());
    nativeOuterFailed=true;assert(!nativeBenchmarkComplete());nativeOuterFailed=false;
    nativeOuterFull[1]=1;assert(!nativeBenchmarkComplete());
    write("4 4 0");benchmarkFrame();
    assert(benchmarkControl.state()==BenchmarkControl::State::complete);
    assert(!gpuTiming.enabled()&&!pipelineStats.enabled());
    assert(benchmarkTestLogs.back().find("bench_failed: native resolution")!=std::string::npos);
    const auto ownedPath=benchmarkPath;
    engine_stop();assert(benchmarkPath.empty()&&benchmarkControl.state()==BenchmarkControl::State::idle);
    assert(benchmarkRequestedRate()==1&&!gpuTiming.enabled()&&!pipelineStats.enabled());
    assert(DeleteFileW(ownedPath.c_str()));
    std::cout<<"Benchmark engine tests passed: bounded command reads, phase replacement, exclusive profilers, stable log snapshot, stop cleanup\n";
}
