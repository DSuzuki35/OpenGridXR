#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../data_abi.hpp"
#include "../code_anchors.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {
using namespace lmuqv;
std::vector<std::uint8_t> image;
void require(bool ok,const char* message){if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
bool readableFixture(const void* pointer,std::size_t size){
    const auto address=reinterpret_cast<std::uintptr_t>(pointer),start=reinterpret_cast<std::uintptr_t>(image.data());
    return address>=start&&address-start<=image.size()&&size<=image.size()-(address-start);
}
auto validate(){return data_abi::validate(reinterpret_cast<std::uintptr_t>(image.data()),readableFixture);}
IMAGE_NT_HEADERS64* nt(){return reinterpret_cast<IMAGE_NT_HEADERS64*>(image.data()+0x80);}
void fixture(){
    image.assign(0x3d41000,0);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image.data());dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x80;
    auto* pe=nt();pe->Signature=IMAGE_NT_SIGNATURE;pe->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
    pe->FileHeader.NumberOfSections=3;pe->FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);
    pe->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;pe->OptionalHeader.SizeOfImage=static_cast<DWORD>(image.size());pe->OptionalHeader.NumberOfRvaAndSizes=16;
    auto* s=IMAGE_FIRST_SECTION(pe);
    s[0].VirtualAddress=0x1000;s[0].Misc.VirtualSize=0x10e3000;s[0].Characteristics=IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_READ;
    s[1].VirtualAddress=0x199c000;s[1].Misc.VirtualSize=0x2200000;s[1].Characteristics=IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_WRITE;
    s[2].VirtualAddress=0x3bc0000;s[2].Misc.VirtualSize=0x10000;s[2].Characteristics=IMAGE_SCN_MEM_READ;
    std::fill(image.begin()+0x1000,image.begin()+0x10e4000,0x90);
    std::vector<std::uint32_t> functions;
    for(const auto& anchor:data_abi::instructions)functions.push_back(anchor.function);
    std::sort(functions.begin(),functions.end());functions.erase(std::unique(functions.begin(),functions.end()),functions.end());
    auto* entries=reinterpret_cast<RUNTIME_FUNCTION*>(image.data()+0x3bc0000);
    for(std::size_t i=0;i<functions.size();++i){
        entries[i].BeginAddress=functions[i];entries[i].EndAddress=functions[i]+1;
        for(const auto& a:data_abi::instructions)if(a.function==functions[i])entries[i].EndAddress=(std::max)(entries[i].EndAddress,DWORD(a.rva+16));
    }
    pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION]={0x3bc0000,static_cast<DWORD>(functions.size()*sizeof(RUNTIME_FUNCTION))};
    for(const auto& anchor:data_abi::instructions)std::memcpy(image.data()+anchor.rva,anchor.bytes.data(),anchor.size);
    // Existing short function guards remain valid while instructions elsewhere move data.
    std::memcpy(image.data()+0xd58a70,render_anchor.data(),render_anchor.size());
    std::memcpy(image.data()+0x557870,derived_anchor.data(),derived_anchor.size());
}
void originalProloguesUnchanged(){
    require(!std::memcmp(image.data()+0xd58a70,render_anchor.data(),render_anchor.size()),"render prologue fixture changed");
    require(!std::memcmp(image.data()+0x557870,derived_anchor.data(),derived_anchor.size()),"derived prologue fixture changed");
}
void rejectMutation(std::uint32_t byte,const char* message){
    image[byte]^=0x08;
    originalProloguesUnchanged();
    require(!validate().supported,message);
    image[byte]^=0x08;
    require(validate().supported,"restored data ABI rejected");
}
// Explicit manual mode reads an existing non-runnable captured PE only. It never
// executes mapped bytes, launches a process, attaches to a process or uses D3D/XR.
void verifyAnalysisImage(const char* path){
    std::ifstream file(path,std::ios::binary);require(bool(file),"analysis image unavailable");
    std::vector<char> disk((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    require(disk.size()>sizeof(IMAGE_DOS_HEADER),"analysis image too short");
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(disk.data());
    require(dos->e_magic==IMAGE_DOS_SIGNATURE&&dos->e_lfanew>0&&std::size_t(dos->e_lfanew)+sizeof(IMAGE_NT_HEADERS64)<disk.size(),"invalid analysis image DOS header");
    const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(disk.data()+dos->e_lfanew);
    require(pe->Signature==IMAGE_NT_SIGNATURE&&pe->OptionalHeader.SizeOfImage<0x10000000&&pe->OptionalHeader.SizeOfHeaders<=disk.size(),"invalid analysis image PE header");
    image.assign(pe->OptionalHeader.SizeOfImage,0);std::memcpy(image.data(),disk.data(),pe->OptionalHeader.SizeOfHeaders);
    const auto* sections=IMAGE_FIRST_SECTION(pe);
    require(reinterpret_cast<const char*>(sections+pe->FileHeader.NumberOfSections)<=disk.data()+disk.size(),"analysis section table outside file");
    for(unsigned i=0;i<pe->FileHeader.NumberOfSections;++i){const auto& s=sections[i];
        require(s.VirtualAddress<image.size()&&s.SizeOfRawData<=image.size()-s.VirtualAddress&&s.PointerToRawData<disk.size()&&s.SizeOfRawData<=disk.size()-s.PointerToRawData,"analysis section outside image");
        std::memcpy(image.data()+s.VirtualAddress,disk.data()+s.PointerToRawData,s.SizeOfRawData);
    }
    const auto result=validate();
    if(!result.supported)std::cerr<<result.reason<<" anchor="<<result.anchor<<" rva="<<std::hex<<result.rva<<'\n';
    require(result.supported,"captured game data ABI refused");
}
}
int main(int argc,char** argv){
    if(argc==3&&!std::strcmp(argv[1],"--analysis-image")){verifyAnalysisImage(argv[2]);std::cout<<"Captured LMU data ABI validated without executing game code\n";return 0;}
    fixture();require(validate().supported,"known data ABI fixture rejected");
    // Regression: updating only a RIP displacement used to pass all prologue guards.
    rejectMutation(0xcd1763+3,"changed current-view global accepted despite unchanged prologues");
    rejectMutation(0x5df84b+3,"changed renderer global accepted");
    rejectMutation(0x5bd6dd+2,"changed eye-dimensions global accepted");
    rejectMutation(0x5be024+4,"changed render-scale global accepted");
    rejectMutation(0xc7f713+3,"changed scene global accepted");
    rejectMutation(0x5bd4e1+3,"changed swapchain-identity global accepted");
    rejectMutation(0x5ad946+2,"changed renderer mode field accepted despite unchanged prologues");
    rejectMutation(0x5ac939+1,"changed renderer allocation size accepted");
    rejectMutation(0x5bce83+3,"changed renderer resource slot accepted");
    rejectMutation(0x5e06f8+2,"changed previous-transform validity field accepted");
    rejectMutation(0x5bdb16+4,"changed indexed eye UAV slot accepted");
    rejectMutation(0x618112+4,"changed UI FOV field accepted");
    rejectMutation(0x5df84b,"changed reference opcode accepted");
    // Every recorded consumer participates in refusal, including optional ABI.
    for(const auto& anchor:data_abi::instructions)rejectMutation(anchor.rva,"unvalidated recorded ABI consumer");
    // Complete bytes at the expected RVA are insufficient when a preceding
    // instruction consumes them as an immediate after an update.
    image[0xcd1762]=0xb8;
    originalProloguesUnchanged();
    require(!validate().supported,"ABI bytes embedded inside another instruction accepted");
    image[0xcd1762]=0x90;
    require(validate().supported,"restored instruction boundary rejected");
    auto* section=IMAGE_FIRST_SECTION(nt());section[0].Characteristics&=~IMAGE_SCN_MEM_EXECUTE;
    require(!validate().supported,"data ABI anchor in non-executable section accepted");section[0].Characteristics|=IMAGE_SCN_MEM_EXECUTE;
    section[1].Characteristics|=IMAGE_SCN_MEM_EXECUTE;
    require(!validate().supported,"global in executable section accepted");section[1].Characteristics&=~IMAGE_SCN_MEM_EXECUTE;
    nt()->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size=0xffffffff;
    require(!validate().supported,"out-of-image unwind table accepted");
    std::cout<<"Data ABI refusal tests passed: globals, renderer/view/UI fields, opcodes and PE bounds\n";
}

