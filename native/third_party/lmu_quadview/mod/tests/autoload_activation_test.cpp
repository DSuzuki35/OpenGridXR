#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../autoload_activation.hpp"
#include <cassert>
#include <iostream>
#include <filesystem>
int main(){
    wchar_t temp[MAX_PATH]{},file[MAX_PATH]{};assert(GetTempPathW(MAX_PATH,temp));assert(GetTempFileNameW(temp,L"lqa",0,file));
    auto write=[&](const std::wstring& value){
        HANDLE h=CreateFileW(file,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);assert(h!=INVALID_HANDLE_VALUE);
        auto text=std::wstring(L"\ufeff")+value;DWORD written=0;assert(WriteFile(h,text.data(),DWORD(text.size()*sizeof(wchar_t)),&written,nullptr));CloseHandle(h);
    };
    const std::wstring root=L"C:\\User Data\\Gr\u00fc\u00dfe\\LMU QuadView";
    write(L"[AutoStart]\r\nPackageRoot="+root+L"\r\n");
    assert(lmuqv::autoload::activation_file(file)==root); // No registry access needed.
    write(L"[AutoStart]\r\nPackageRoot=relative\\package\r\n");
    bool refused=false;try{lmuqv::autoload::activation_file(file);}catch(const std::exception&){refused=true;}assert(refused);
    write(L"[AutoStart]\r\n");refused=false;try{lmuqv::autoload::activation_file(file);}catch(const std::exception&){refused=true;}assert(refused);
    assert(DeleteFileW(file));assert(lmuqv::autoload::activation_file(file).empty());
    std::cout<<"Autoload file activation: Unicode, missing registry independence, invalid/missing path rejection passed\n";
}
