#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace lmuqv {
// Render-thread-only transaction. Caller validates that every slot remains
// mapped and exclusively owned for the scope. It does not own pointed-to objects.
class NativeOuterBindings {
    struct Entry {void* slot{};std::size_t size{};std::array<std::byte,8> before{},after{};};
    std::array<Entry,48> entries_{};
    std::size_t count_=0;
    bool valid_=true,active_=false;
public:
    NativeOuterBindings()=default;
    NativeOuterBindings(const NativeOuterBindings&)=delete;
    NativeOuterBindings& operator=(const NativeOuterBindings&)=delete;
    ~NativeOuterBindings(){restore();}
    template<class T> bool add(T* slot,const T& value){
        static_assert(std::is_trivially_copyable_v<T> && sizeof(T)<=8);
        if(active_)return false;
        if(!valid_||!slot||count_==entries_.size()){valid_=false;return false;}
        const auto begin=reinterpret_cast<std::uintptr_t>(slot),end=begin+sizeof(T);
        if(end<begin){valid_=false;return false;}
        for(std::size_t i=0;i<count_;++i){
            const auto other=reinterpret_cast<std::uintptr_t>(entries_[i].slot);
            if(begin<other+entries_[i].size&&other<end){valid_=false;return false;}
        }
        auto& entry=entries_[count_++];entry.slot=slot;entry.size=sizeof(T);
        std::memcpy(entry.after.data(),&value,sizeof(T));return true;
    }
    bool apply(){
        if(!valid_||active_||!count_)return false;
        for(std::size_t i=0;i<count_;++i)std::memcpy(entries_[i].before.data(),entries_[i].slot,entries_[i].size);
        for(std::size_t i=0;i<count_;++i)std::memcpy(entries_[i].slot,entries_[i].after.data(),entries_[i].size);
        active_=true;return true;
    }
    void restore(){
        if(!active_)return;
        for(std::size_t i=count_;i>0;--i)std::memcpy(entries_[i-1].slot,entries_[i-1].before.data(),entries_[i-1].size);
        active_=false;
    }
    bool active()const{return active_;}
};
}
