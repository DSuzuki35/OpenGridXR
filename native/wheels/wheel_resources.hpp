#pragma once
#include <array>
#include <chrono>
#include <future>
#include <memory>
#include <string>

namespace ovrlay {
// One immutable asset per path requested by live slots. A loader is allowed to
// finish after its original slot closes; other slots waiting on that path share it.
template<class Asset> class WheelMeshCache {
    struct Entry { std::wstring path; std::shared_ptr<Asset> asset; bool failed{}; };
    std::array<Entry,15> slots{};
    std::wstring loading;
    std::future<std::shared_ptr<Asset>> pending;
public:
    bool idle() const {
        return !pending.valid() || pending.wait_for(std::chrono::seconds(0))==std::future_status::ready;
    }
    bool failed(unsigned slot) const {return slots.at(slot).failed;}
    bool hasAssets() const {
        for(const auto& entry:slots)if(entry.asset)return true;
        return false;
    }
    void forget(unsigned slot) {slots.at(slot)={};}
    void collect() {
        if(pending.valid() && idle()) {
            std::shared_ptr<Asset> asset;
            try {asset=pending.get();} catch(...) {}
            for(auto& item:slots) if(item.path==loading) {
                item.asset=asset;
                item.failed=!asset;
            }
        }
    }
    template<class Load> Asset* select(unsigned slot,const std::wstring& path,Load&& load) {
        auto& entry=slots.at(slot);
        if(entry.path!=path) entry={path,nullptr,false};
        collect();
        if(!entry.asset && !entry.failed) {
            for(const auto& item:slots) if(item.path==path && item.asset) {
                entry.asset=item.asset;
                break;
            }
            if(!entry.asset && !pending.valid()) {
                loading=path;
                try {pending=load();}
                catch(...) {entry.failed=true;}
            }
        }
        return entry.asset.get();
    }
};

// Poll during presentation. Only session reset/destruction may join an in-flight
// worker. Both GPU workers retain their device and execute inside the layer DLL.
template<class Renderer> struct WheelResources {
    std::unique_ptr<Renderer> renderer;
    std::future<std::unique_ptr<Renderer>> pipeline;
    bool retiring{};
    void release(bool join=false) {
        if(!join && ((renderer && !renderer->idle()) ||
           (pipeline.valid() && pipeline.wait_for(std::chrono::seconds(0))!=std::future_status::ready))) {
            retiring=true;
            return;
        }
        renderer.reset();
        pipeline={};
        retiring=false;
    }
};
}
