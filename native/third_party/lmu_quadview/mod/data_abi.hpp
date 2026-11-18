#pragma once
#include "data_abi_anchors.hpp"
#include "vendor/minhook-1.3.4/src/hde/hde64.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>

namespace lmuqv::data_abi {
struct Result {bool supported;const char* reason;const char* anchor;std::uint32_t rva;};
using Readable=bool(*)(const void*,std::size_t);

// Startup-only validation. Never dereference a native object or initialize a hook
// until this succeeds. A readable pointer alone cannot establish its native ABI.
inline Result validate(std::uintptr_t base,Readable readable){
    const auto failure=[](const char* reason,const Instruction* a=nullptr)->Result{
        return {false,reason,a?a->name:"image",a?a->rva:0};
    };
    if(!base||!readable||!readable(reinterpret_cast<const void*>(base),sizeof(IMAGE_DOS_HEADER)))return failure("image_layout");
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0||dos->e_lfanew>0x100000||
       base>(std::numeric_limits<std::uintptr_t>::max)()-std::uintptr_t(dos->e_lfanew)-sizeof(IMAGE_NT_HEADERS64))return failure("image_layout");
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+std::uintptr_t(dos->e_lfanew));
    if(!readable(nt,sizeof(*nt))||nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||
       nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||nt->FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER64))return failure("image_layout");
    const std::size_t size=nt->OptionalHeader.SizeOfImage;
    const auto inImage=[&](std::size_t rva,std::size_t bytes){return rva<size&&bytes&&bytes<=size-rva;};
    if(!inImage(std::size_t(dos->e_lfanew),sizeof(*nt))||base>(std::numeric_limits<std::uintptr_t>::max)()-size)return failure("image_layout");
    const unsigned sectionCount=nt->FileHeader.NumberOfSections;
    const auto* sections=IMAGE_FIRST_SECTION(nt);
    const auto sectionsRva=reinterpret_cast<std::uintptr_t>(sections)-base;
    if(!sectionCount||sectionCount>96||!inImage(sectionsRva,sectionCount*sizeof(*sections))||!readable(sections,sectionCount*sizeof(*sections)))return failure("image_layout");
    for(unsigned i=0;i<sectionCount;++i){const auto& s=sections[i];
        if(!inImage(s.VirtualAddress,(std::max)(s.Misc.VirtualSize,s.SizeOfRawData)))return failure("image_layout");
    }
    const auto inSection=[&](std::uint32_t rva,std::size_t bytes,bool code){
        if(!inImage(rva,bytes))return false;
        for(unsigned i=0;i<sectionCount;++i){const auto& s=sections[i];const auto length=(std::max)(s.Misc.VirtualSize,s.SizeOfRawData);
            if(rva<s.VirtualAddress||rva-s.VirtualAddress>=length||bytes>length-(rva-s.VirtualAddress))continue;
            const auto required=IMAGE_SCN_MEM_READ|(code?IMAGE_SCN_MEM_EXECUTE:IMAGE_SCN_MEM_WRITE);
            return (s.Characteristics&required)==required&&(code||!(s.Characteristics&IMAGE_SCN_MEM_EXECUTE));
        }
        return false;
    };
    if(nt->OptionalHeader.NumberOfRvaAndSizes<=IMAGE_DIRECTORY_ENTRY_EXCEPTION)return failure("unwind_layout");
    const auto& directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if(!directory.VirtualAddress||!directory.Size||directory.Size%sizeof(RUNTIME_FUNCTION)||!inImage(directory.VirtualAddress,directory.Size))return failure("unwind_layout");
    const auto* functions=reinterpret_cast<const RUNTIME_FUNCTION*>(base+directory.VirtualAddress);
    const auto count=directory.Size/sizeof(*functions);
    if(!readable(functions,directory.Size))return failure("unwind_layout");
    // Require ordered .pdata before binary search; malformed entries never confer
    // function identity. Large ranges are bounded by SizeOfImage before decoding.
    for(std::size_t i=0;i<count;++i){const auto& f=functions[i];
        if(f.BeginAddress>=f.EndAddress||f.EndAddress>size||(i&&functions[i-1].EndAddress>f.BeginAddress))return failure("unwind_layout");
    }
    std::uint32_t previousFunction=0,decoded=0;
    for(const auto& anchor:instructions){
        if(!inSection(anchor.rva,anchor.size,true)||!readable(reinterpret_cast<const void*>(base+anchor.rva),anchor.size))return failure("instruction_unavailable",&anchor);
        std::size_t low=0,high=count;
        while(low<high){const auto mid=low+(high-low)/2;if(functions[mid].BeginAddress<=anchor.rva)low=mid+1;else high=mid;}
        if(!low)return failure("instruction_boundary",&anchor);
        const auto& f=functions[low-1];
        if(f.BeginAddress!=anchor.function||anchor.rva+anchor.size>f.EndAddress)return failure("instruction_boundary",&anchor);
        // Decode from the known function start, reusing progress for ascending
        // anchors in a function. Signatures inside immediates are not code anchors.
        if(previousFunction!=anchor.function){decoded=anchor.function;previousFunction=anchor.function;}
        while(decoded<anchor.rva){
            const auto bytes=(std::min)(std::size_t(15),std::size_t(f.EndAddress-decoded));
            if(!inSection(decoded,bytes,true)||!readable(reinterpret_cast<const void*>(base+decoded),bytes))return failure("instruction_boundary",&anchor);
            std::uint8_t code[16]{};std::memcpy(code,reinterpret_cast<const void*>(base+decoded),bytes);
            hde64s h{};hde64_disasm(code,&h);
            if(!h.len||(h.flags&F_ERROR)||h.len>bytes||h.len>anchor.rva-decoded)return failure("instruction_boundary",&anchor);
            decoded+=h.len;
        }
        const auto* code=reinterpret_cast<const void*>(base+anchor.rva);
        if(std::memcmp(code,anchor.bytes.data(),anchor.size))return failure(anchor.operand==Operand::rip?"global_reference_changed":"field_instruction_changed",&anchor);
        std::uint8_t instruction[16]{};std::memcpy(instruction,code,anchor.size);
        hde64s h{};hde64_disasm(instruction,&h);
        if(!h.len||(h.flags&F_ERROR)||h.len!=anchor.size)return failure("instruction_decode",&anchor);
        if(anchor.operand==Operand::rip){
            if(!(h.flags&F_DISP32)||h.modrm_mod!=0||h.modrm_rm!=5||h.p_67||
               std::int64_t(anchor.rva)+h.len+std::int32_t(h.disp.disp32)!=anchor.value||
               !inSection(anchor.value,sizeof(void*),false)||!readable(reinterpret_cast<const void*>(base+anchor.value),sizeof(void*)))return failure("global_reference_changed",&anchor);
        }else if(anchor.operand==Operand::field){
            const auto displacement=(h.flags&F_DISP32)?h.disp.disp32:((h.flags&F_DISP8)?std::uint32_t(h.disp.disp8):0u);
            if(!(h.flags&F_MODRM)||h.modrm_mod==3||(!(h.flags&(F_DISP32|F_DISP8)))||displacement!=anchor.value)return failure("field_instruction_changed",&anchor);
        }else if(!(h.flags&F_IMM32)||h.imm.imm32!=anchor.value)return failure("field_instruction_changed",&anchor);
        decoded=anchor.rva+anchor.size;
    }
    return {true,"supported","",0};
}
} // namespace lmuqv::data_abi
