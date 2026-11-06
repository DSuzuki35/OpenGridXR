#pragma once

namespace lmuqv {
// Owned by the render thread at the left-eye boundary, serialized with upload
// teardown by nativeUploadMutex. No hot-patching or render-resource ownership.
class UploadReuseToggle {
public:
    enum class Event {none,enabled,disabled,blocked};
    void configure(bool available){available_=enabled_=available;keyWasDown_=false;}
    bool available()const{return available_;}
    bool enabled()const{return enabled_;}
    Event poll(bool keyDown,bool capturing){
        const bool pressed=keyDown&&!keyWasDown_;keyWasDown_=keyDown;
        if(!available_||!pressed)return Event::none;
        if(capturing)return Event::blocked;
        enabled_=!enabled_;
        return enabled_?Event::enabled:Event::disabled;
    }
private:
    bool available_=false,enabled_=false,keyWasDown_=false;
};
}
