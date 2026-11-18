#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

namespace lmuqv {
// Scheduling contract only. No production caller currently establishes all of
// these proofs. This class never submits, changes a native target or suppresses
// a native call. The renderer remains responsible for actual resource ownership,
// immutable prepared inputs, transfers and native preparation side effects.
enum class NativePairDecision {ready,native_fallback,drain_required,rejected};
enum class NativePairPhase {idle,prepared,outer_committing,outer_published,focus_committing,focus_published};
struct NativePairRect {unsigned width=0,height=0;};
struct NativePairPacket {
    std::uint64_t mesh=0,material=0,shader=0,geometryEpoch=0,resourceEpoch=0;
    std::uint32_t prepareFlags=0;
    bool eligible=false;
    bool same_shared_inputs(const NativePairPacket& b)const {
        return mesh==b.mesh&&material==b.material&&shader==b.shader&&
            geometryEpoch==b.geometryEpoch&&resourceEpoch==b.resourceEpoch&&prepareFlags==b.prepareFlags;
    }
};
struct NativePairPreflight {
    std::uint64_t frame=0,view=0,bankGeneration=0;
    unsigned eye=0,arrayWidth=0,arrayHeight=0;
    std::array<NativePairRect,2> rects{};
    // Nonzero generations identify independently owned, current-frame seeds and
    // constant banks, not COM identities or borrowed ViewSnapshot addresses.
    std::array<std::uint64_t,2> colorSeeds{},depthSeeds{},constantBanks{};
    std::array<std::vector<NativePairPacket>,2> packets;
    bool inputsFrozen=false,visibilityConservative=false,consumerOrderProven=false;
    bool preparationEffectsProven=false,depthTransferProven=false;
};
struct NativePairToken {
    std::uint64_t generation=0,frame=0,view=0,bankGeneration=0;
    unsigned eye=0;
};
class NativePairPassCoordinator {
    NativePairPhase phase_=NativePairPhase::idle;
    std::thread::id owner_{};
    NativePairToken token_{};
    std::uint64_t generation_=0;
    std::vector<NativePairPacket> packets_;
    bool owner()const{return owner_==std::this_thread::get_id();}
    bool matches(const NativePairToken& t)const {
        return t.generation&&t.generation==token_.generation&&t.frame==token_.frame&&
            t.view==token_.view&&t.bankGeneration==token_.bankGeneration&&t.eye==token_.eye;
    }
    bool committed()const{return phase_!=NativePairPhase::idle&&phase_!=NativePairPhase::prepared;}
    void clear(){phase_=NativePairPhase::idle;token_={};packets_.clear();}
    NativePairDecision bad_boundary(){
        if(committed())return NativePairDecision::drain_required;
        clear();return NativePairDecision::native_fallback;
    }
public:
    static constexpr std::size_t maximum_packets=4096;
    NativePairPhase phase()const{return phase_;}
    NativePairToken token()const{return token_;}
    NativePairDecision prepare(const NativePairPreflight& p){
        if(phase_!=NativePairPhase::idle){
            if(!owner())return NativePairDecision::rejected;
            return bad_boundary();
        }
        // Once associated, this coordinator is never transferable to another
        // render thread, even between pairs. Use a new coordinator after reset.
        if(owner_!=std::thread::id{}&&!owner())return NativePairDecision::rejected;
        if(!p.frame||!p.view||!p.bankGeneration||p.eye>1||!p.arrayWidth||!p.arrayHeight||
           !p.inputsFrozen||!p.visibilityConservative||!p.consumerOrderProven||
           !p.preparationEffectsProven||!p.depthTransferProven)return NativePairDecision::native_fallback;
        for(unsigned v=0;v<2;++v){
            if(!p.rects[v].width||!p.rects[v].height||p.rects[v].width>p.arrayWidth||
               p.rects[v].height>p.arrayHeight||!p.colorSeeds[v]||!p.depthSeeds[v]||!p.constantBanks[v])
                return NativePairDecision::native_fallback;
        }
        const auto count=p.packets[0].size();
        if(!count||count>maximum_packets||count!=p.packets[1].size())return NativePairDecision::native_fallback;
        for(std::size_t i=0;i<count;++i){
            const auto& a=p.packets[0][i];const auto& b=p.packets[1][i];
            if(!a.eligible||!b.eligible||!a.mesh||!a.material||!a.shader||
               !a.geometryEpoch||!a.resourceEpoch||!a.same_shared_inputs(b))return NativePairDecision::native_fallback;
        }
        // Own packet values, not the caller's vector storage or native pointers.
        packets_=p.packets[1];
        owner_=std::this_thread::get_id();
        if(++generation_==0)++generation_;
        token_={generation_,p.frame,p.view,p.bankGeneration,p.eye};
        phase_=NativePairPhase::prepared;return NativePairDecision::ready;
    }
    // Authorize BEFORE the first native-visible write. Once authorized a write
    // might have happened: failure must drain/complete owned work, never pretend
    // the ordinary native path is an untouched fallback.
    NativePairDecision begin_outer_commit(const NativePairToken& t,std::uint64_t currentBank){
        if(!owner())return NativePairDecision::rejected;
        if(!matches(t))return committed()?NativePairDecision::drain_required:NativePairDecision::native_fallback;
        if(phase_!=NativePairPhase::prepared)return committed()?NativePairDecision::drain_required:NativePairDecision::rejected;
        if(currentBank!=token_.bankGeneration)return bad_boundary();
        phase_=NativePairPhase::outer_committing;return NativePairDecision::ready;
    }
    NativePairDecision outer_published(const NativePairToken& t){
        if(!owner()||!matches(t)||phase_!=NativePairPhase::outer_committing)return NativePairDecision::rejected;
        phase_=NativePairPhase::outer_published;return NativePairDecision::ready;
    }
    NativePairDecision begin_focus_commit(const NativePairToken& t,std::uint64_t frame,std::uint64_t view,
                                         unsigned eye,std::uint64_t bank){
        if(!owner()||!matches(t))return NativePairDecision::rejected;
        if(phase_!=NativePairPhase::outer_published||frame!=token_.frame||view!=token_.view||
           eye!=token_.eye||bank!=token_.bankGeneration)return bad_boundary();
        phase_=NativePairPhase::focus_committing;return NativePairDecision::ready;
    }
    NativePairDecision focus_published(const NativePairToken& t){
        if(!owner()||!matches(t)||phase_!=NativePairPhase::focus_committing)return NativePairDecision::rejected;
        phase_=NativePairPhase::focus_published;return NativePairDecision::ready;
    }
    bool focus_suppression_allowed(const NativePairToken& t,std::size_t index,const NativePairPacket& current)const {
        return owner()&&matches(t)&&phase_==NativePairPhase::focus_published&&index<packets_.size()&&
            current.eligible&&packets_[index].same_shared_inputs(current);
    }
    NativePairDecision cancel(){
        if(phase_==NativePairPhase::idle)return NativePairDecision::ready;
        if(!owner())return NativePairDecision::rejected;
        return bad_boundary();
    }
    NativePairDecision finish_frame(){
        if(phase_==NativePairPhase::idle)return NativePairDecision::ready;
        if(!owner())return NativePairDecision::rejected;
        return bad_boundary();
    }
    // Caller certifies both outputs no longer have pending consumers (including
    // an explicit failure drain). This method does not release graphics objects.
    NativePairDecision retire(const NativePairToken& t){
        if(!owner()||!matches(t))return NativePairDecision::rejected;
        clear();return NativePairDecision::ready;
    }
};
}
