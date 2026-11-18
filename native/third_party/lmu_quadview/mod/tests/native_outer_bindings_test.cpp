#include "../native_outer_bindings.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
int main(){
    using lmuqv::NativeOuterBindings;
    void* slot=reinterpret_cast<void*>(std::uintptr_t(1));
    void* replacement=reinterpret_cast<void*>(std::uintptr_t(2));
    std::uint32_t width=3380,height=3432;
    {
        NativeOuterBindings bank;
        assert(bank.add(&slot,replacement)&&bank.add(&width,std::uint32_t(756))&&bank.add(&height,std::uint32_t(768)));
        assert(slot!=replacement&&width==3380&&height==3432);
        assert(bank.apply());
        assert(slot==replacement&&width==756&&height==768);
        assert(!bank.apply());
        assert(!bank.add(&width,std::uint32_t(10)));
    }
    assert(slot!=replacement&&width==3380&&height==3432);
    {
        NativeOuterBindings bank;
        assert(bank.add(&width,std::uint32_t(756)));
        assert(!bank.add(&width,std::uint32_t(768)));
        assert(!bank.apply()); // A rejected entry invalidates the entire transaction.
        assert(width==3380);
    }
    {
        NativeOuterBindings bank;
        assert(!bank.add(static_cast<std::uint32_t*>(nullptr),std::uint32_t(1)));
        assert(!bank.apply());
    }
    {
        NativeOuterBindings bank;
        assert(bank.add(&width,std::uint32_t(756))&&bank.apply());
        bank.restore();bank.restore();
        assert(width==3380&&!bank.active());
    }
    std::cout<<"native outer binding transactions passed\n";
}
