#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../nv_vrs.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
namespace {void log(void* user,const char* message){static_cast<std::vector<std::string>*>(user)->emplace_back(message);}}
int main(){
    lmuqv::NvVrs vrs;auto initial=GetModuleHandleW(L"nvapi64.dll");
    assert(!vrs.ready()&&!vrs.active()&&!vrs.faulted());
    assert(vrs.uploads()==0);
    assert(!vrs.update_focus(nullptr,{}));
    assert(!vrs.update_focus(nullptr,{},1)&&!vrs.update_focus(nullptr,{},2));
    assert(!vrs.configure(nullptr,0,0));assert(!vrs.begin_outer(nullptr)&&!vrs.end_outer(nullptr));vrs.reset();vrs.reset();
    assert(GetModuleHandleW(L"nvapi64.dll")==initial);
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;D3D_FEATURE_LEVEL feature{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&feature,&context)));
    initial=GetModuleHandleW(L"nvapi64.dll");std::vector<std::string> lines;
    assert(!vrs.configure(context,256,256,2,true,lmuqv::NvVrsOwnership::Unknown,log,&lines));
    assert(GetModuleHandleW(L"nvapi64.dll")==initial&&lines.size()==1&&lines[0].find("explicit ExclusiveKnownDisabled")!=std::string::npos);
    assert(!vrs.configure(context,256,256,8,true,lmuqv::NvVrsOwnership::ExclusiveKnownDisabled));
    assert(!vrs.configure(context,0,256,2,true,lmuqv::NvVrsOwnership::ExclusiveKnownDisabled));
    assert(!vrs.configure(context,256,256,2,true,lmuqv::NvVrsOwnership::ExclusiveKnownDisabled,log,&lines));
    assert(!vrs.ready()&&!vrs.active()&&vrs.faulted());
    assert(!vrs.update_focus(context,{}));
    assert(!vrs.update_focus(context,{},1)&&vrs.uploads()==0);
    assert(lines.back().find("no VRS setter called")!=std::string::npos);vrs.reset();vrs.reset();
    assert(!vrs.faulted()&&vrs.uploads()==0);
    ID3D11DeviceContext* deferred=nullptr;assert(SUCCEEDED(device->CreateDeferredContext(0,&deferred)));
    assert(!vrs.configure(deferred,256,256,2,true,lmuqv::NvVrsOwnership::ExclusiveKnownDisabled));
    deferred->Release();context->Release();device->Release();
    std::cout<<"nv_vrs tests passed: default/disabled no NVAPI load, unknown ownership rejected, WARP unsupported, invalid/deferred contexts, repeated reset\n";
}
