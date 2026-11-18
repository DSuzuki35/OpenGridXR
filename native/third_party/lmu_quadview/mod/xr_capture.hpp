#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <cstdint>
#include <cstring>
#include <vector>

namespace lmuqv::capture_detail {
struct Config {
    bool requested=false,valid=false;
    std::filesystem::path directory,trigger;
};
inline Config config(const std::wstring& directory,const std::wstring& trigger) {
    Config value{};value.requested=!directory.empty();
    if(!value.requested)return value;
    value.directory=std::filesystem::path(directory).lexically_normal();value.trigger=std::filesystem::path(trigger).lexically_normal();
    if(!value.directory.is_absolute()||!value.trigger.is_absolute())return value;
    DWORD attributes=GetFileAttributesW(value.directory.c_str());
    value.valid=attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
    return value;
}
inline bool triggered(const Config& value) {
    if(!value.requested||!value.valid)return false;
    std::error_code error;
    return std::filesystem::is_directory(value.directory,error)&&!error&&std::filesystem::is_regular_file(value.trigger,error)&&!error;
}
inline bool write_all(HANDLE file,const void* bytes,DWORD length,DWORD& error) {
    const auto* data=static_cast<const unsigned char*>(bytes);
    while(length){
        DWORD written=0;
        if(!WriteFile(file,data,length,&written,nullptr)){error=GetLastError();return false;}
        if(!written){error=ERROR_WRITE_FAULT;return false;}
        data+=written;length-=written;
    }
    return true;
}
inline bool write_bmp(const Config& value,unsigned slot,uint32_t width,uint32_t height,uint32_t pitch,const unsigned char* data,bool bgra,DWORD& error) {
    static_assert(sizeof(BITMAPFILEHEADER)==14&&sizeof(BITMAPV4HEADER)==108);
    error=ERROR_INVALID_PARAMETER;
    if(!triggered(value)||slot>=4||!data||!width||!height||width>16384||height>16384||uint64_t(pitch)<uint64_t(width)*4)return false;
    static const wchar_t* names[]={L"outer0.bmp",L"outer1.bmp",L"app0.bmp",L"app1.bmp"};
    const auto target=value.directory/names[slot];
    HANDLE file=CreateFileW(target.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){error=GetLastError();return false;}
    BITMAPFILEHEADER fh{};BITMAPV4HEADER vh{};
    fh.bfType=0x4d42;fh.bfOffBits=sizeof(fh)+sizeof(vh);fh.bfSize=fh.bfOffBits+width*height*4;
    vh.bV4Size=sizeof(vh);vh.bV4Width=static_cast<LONG>(width);vh.bV4Height=-static_cast<LONG>(height);
    vh.bV4Planes=1;vh.bV4BitCount=32;vh.bV4V4Compression=BI_BITFIELDS;vh.bV4SizeImage=width*height*4;
    vh.bV4RedMask=0x00ff0000;vh.bV4GreenMask=0x0000ff00;vh.bV4BlueMask=0x000000ff;vh.bV4AlphaMask=0xff000000;
    vh.bV4CSType=0x73524742u; // LCS_sRGB, without MinGW's multicharacter macro warning.
    bool okay=write_all(file,&fh,sizeof(fh),error)&&write_all(file,&vh,sizeof(vh),error);
    std::vector<unsigned char> row(size_t(width)*4);
    for(uint32_t y=0;okay&&y<height;++y){
        const auto* source=data+size_t(y)*pitch;
        if(bgra)std::memcpy(row.data(),source,row.size());
        else for(uint32_t x=0;x<width;++x){
            row[size_t(x)*4]=source[size_t(x)*4+2];row[size_t(x)*4+1]=source[size_t(x)*4+1];
            row[size_t(x)*4+2]=source[size_t(x)*4];row[size_t(x)*4+3]=source[size_t(x)*4+3];
        }
        okay=write_all(file,row.data(),static_cast<DWORD>(row.size()),error);
    }
    if(!CloseHandle(file)&&okay){error=GetLastError();okay=false;}
    if(okay)error=ERROR_SUCCESS;
    return okay;
}
}
