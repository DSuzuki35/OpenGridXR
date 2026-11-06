#include "lmu_diagnostics.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    const auto directory=std::filesystem::temp_directory_path()/(L"opengrid-diagnostics-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    SetEnvironmentVariableW(L"OPENGRIDXR_STATUS_DIR",directory.c_str());
    SetEnvironmentVariableA("LMU_QV_DIAGNOSTICS","0");
    const auto file=directory/L"OpenGridXR-lmu-diagnostics.json";
    og::lmu::report_diagnostic("starting","Installing engine hooks");
    og::lmu::report_cpu_affinity("skipped: unsupported CPU topology");
    og::lmu::report_diagnostic("unavailable","Engine data reference mismatch");
    auto snapshot=og::lmu::diagnostic_snapshot();
    require(snapshot.state=="unavailable"&&snapshot.reason=="Engine data reference mismatch","failure is explicit");
    require(snapshot.cpuAffinity=="skipped: unsupported CPU topology","CPU outcome is retained");
    require(snapshot.pid==GetCurrentProcessId()&&snapshot.timestampMs>0,"origin and time are recorded");
    std::ifstream input(file);const std::string before((std::istreambuf_iterator<char>(input)),{});input.close();
    require(before.find("Engine data reference mismatch")!=std::string::npos,"failure persists with verbose diagnostics off");
    require(before.find("skipped: unsupported CPU topology")!=std::string::npos,"CPU outcome persists");
    std::vector<std::thread> workers;
    for(unsigned i=0;i<4;++i)workers.emplace_back([]{for(unsigned n=0;n<1000;++n){og::lmu::note_reentry();og::lmu::note_outer_timeout();}});
    for(auto& worker:workers)worker.join();
    snapshot=og::lmu::diagnostic_snapshot();
    require(snapshot.reentryCount==4000&&snapshot.outerTimeoutCount==4000,"concurrent counters do not lose events");
    std::ifstream afterInput(file);const std::string after((std::istreambuf_iterator<char>(afterInput)),{});afterInput.close();
    require(after==before,"frame counters perform no file IO");
    og::lmu::report_diagnostic("unavailable",("bad\n\"\\"+std::string(2000,'x')).c_str());
    snapshot=og::lmu::diagnostic_snapshot();
    require(snapshot.reason.size()<=512&&snapshot.reason.find('\n')==std::string::npos,"reasons are bounded printable text");
    std::ifstream boundedInput(file);const std::string bounded((std::istreambuf_iterator<char>(boundedInput)),{});boundedInput.close();
    require(bounded.size()<8192&&bounded.find("\"reentryCount\":4000")!=std::string::npos,"lifecycle flush persists bounded snapshot and counters");
    require(bounded.find("\\\"")!=std::string::npos&&bounded.find("\\\\")!=std::string::npos,"JSON strings escape quotes and slashes");
    require(!std::filesystem::exists(directory/(L"OpenGridXR-lmu-diagnostics.json."+std::to_wstring(GetCurrentProcessId())+L".tmp")),"atomic temporary file is removed");
    og::lmu::report_diagnostic("ready","Engine hooks verified");
    std::ifstream readyInput(file);const std::string readyFile((std::istreambuf_iterator<char>(readyInput)),{});readyInput.close();
    const auto readyTimestamp=og::lmu::diagnostic_snapshot().timestampMs;
    og::lmu::note_native_outer_refusal("Temporary full-resolution fallback: native outer data unavailable");
    snapshot=og::lmu::diagnostic_snapshot();
    require(snapshot.state=="ready"&&snapshot.reason=="Engine hooks verified","temporary fallback never invalidates ready hooks");
    require(snapshot.warning=="Temporary full-resolution fallback: native outer data unavailable"&&snapshot.nativeOuterRefusalCount==1,"nonfatal fallback reason and count are observable");
    require(snapshot.timestampMs==readyTimestamp,"warning does not alter hook lifecycle timestamp");
    og::lmu::note_native_outer_refusal(("bounded\n"+std::string(2000,'x')).c_str());
    snapshot=og::lmu::diagnostic_snapshot();
    require(snapshot.state=="ready"&&snapshot.warning.size()<=512&&snapshot.warning.find('\n')==std::string::npos&&snapshot.nativeOuterRefusalCount==2,"subsequent fallback stays bounded and ready");
    std::ifstream warningInput(file);const std::string warningFile((std::istreambuf_iterator<char>(warningInput)),{});warningInput.close();
    require(warningFile==readyFile,"native outer refusal performs no file IO");
    const auto warningJson=og::lmu::diagnostic_json(snapshot);
    require(warningJson.find("\"nativeOuterRefusalCount\":2")!=std::string::npos&&warningJson.find("\"warning\":\"bounded ")!=std::string::npos,"live JSON carries bounded warning and counter");
    std::filesystem::remove_all(directory);
    SetEnvironmentVariableW(L"OPENGRIDXR_STATUS_DIR",nullptr);
    std::cout<<"LMU durable diagnostics passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
