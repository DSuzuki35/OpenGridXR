#include "../upload_reuse_toggle.hpp"
#include <cassert>
#include <iostream>

int main(){
    using Toggle=lmuqv::UploadReuseToggle;
    Toggle t;t.configure(true);
    assert(t.enabled());
    assert(t.poll(true,false)==Toggle::Event::disabled&&!t.enabled());
    for(int i=0;i<100;++i)assert(t.poll(true,false)==Toggle::Event::none&&!t.enabled());
    t.poll(false,false);
    assert(t.poll(true,true)==Toggle::Event::blocked&&!t.enabled());
    // A rejected press is consumed, not deferred until capture completion.
    assert(t.poll(true,false)==Toggle::Event::none&&!t.enabled());
    t.poll(false,false);
    assert(t.poll(true,false)==Toggle::Event::enabled&&t.enabled());
    t.poll(false,false);
    assert(t.poll(true,true)==Toggle::Event::blocked&&t.enabled());
    t.configure(false);
    assert(!t.enabled()&&t.poll(true,false)==Toggle::Event::none);
    t.configure(true);
    assert(t.enabled()&&t.poll(true,false)==Toggle::Event::disabled);
    std::cout<<"Upload reuse toggle: one transition per press, capture lockout, no deferred press and capability reset passed\n";
}
