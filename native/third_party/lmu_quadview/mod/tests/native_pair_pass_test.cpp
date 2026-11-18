#include "../native_pair_pass.hpp"
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace lmuqv;
static void require(bool okay,const char* why){if(!okay){std::cerr<<why<<'\n';std::exit(1);}}
static NativePairPreflight plan(){
    NativePairPreflight p;
    p.frame=5;p.view=7;p.eye=1;p.bankGeneration=3;
    p.arrayWidth=140;p.arrayHeight=100;
    p.rects={NativePairRect{80,70},NativePairRect{140,100}};
    p.colorSeeds={11,12};p.depthSeeds={13,14};p.constantBanks={15,16};
    p.inputsFrozen=true;p.visibilityConservative=true;p.consumerOrderProven=true;
    p.preparationEffectsProven=true;p.depthTransferProven=true;
    NativePairPacket packet;packet.mesh=10;packet.material=20;packet.shader=30;
    packet.geometryEpoch=40;packet.resourceEpoch=50;packet.eligible=true;
    p.packets[0].push_back(packet);p.packets[1].push_back(packet);
    return p;
}
int main(){
    NativePairPassCoordinator c;
    auto p=plan();
    require(c.prepare(p)==NativePairDecision::ready,"valid preflight refused");
    const auto first=c.token();
    require(!c.focus_suppression_allowed(first,0,p.packets[1][0]),"suppression before focus export");
    require(c.cancel()==NativePairDecision::native_fallback,"precommit cancel refused");
    require(c.phase()==NativePairPhase::idle,"cancel retained resources");
    p.packets[1].push_back(p.packets[1][0]);p.packets[1].back().eligible=false;
    require(c.prepare(p)==NativePairDecision::native_fallback,"late unsupported packet accepted");
    p=plan();p.packets[1][0].resourceEpoch++;
    require(c.prepare(p)==NativePairDecision::native_fallback,"changed input epoch accepted");
    p=plan();p.depthTransferProven=false;
    require(c.prepare(p)==NativePairDecision::native_fallback,"unproven depth transfer accepted");
    p=plan();p.depthSeeds[1]=0;
    require(c.prepare(p)==NativePairDecision::native_fallback,"missing focus seed accepted");
    p=plan();p.constantBanks[1]=0;
    require(c.prepare(p)==NativePairDecision::native_fallback,"missing focus constants accepted");
    p=plan();require(c.prepare(p)==NativePairDecision::ready,"second preflight refused");
    auto t=c.token();
    require(t.generation!=first.generation,"cancelled token reused");
    require(c.begin_outer_commit(first,p.bankGeneration)==NativePairDecision::native_fallback,"stale token accepted");
    require(c.phase()==NativePairPhase::prepared,"stale token destroyed prepared work");
    require(c.begin_outer_commit(t,p.bankGeneration+1)==NativePairDecision::native_fallback,"changed bank committed");
    require(c.phase()==NativePairPhase::idle,"changed bank did not invalidate preflight");
    require(c.prepare(p)==NativePairDecision::ready,"third preflight refused");t=c.token();
    bool otherThreadAccepted=false;
    std::thread other([&]{otherThreadAccepted=c.begin_outer_commit(t,p.bankGeneration)==NativePairDecision::ready;});other.join();
    require(!otherThreadAccepted&&c.phase()==NativePairPhase::prepared,"wrong thread changed state");
    require(c.begin_outer_commit(t,p.bankGeneration)==NativePairDecision::ready,"commit authorization failed");
    require(c.cancel()==NativePairDecision::drain_required,"postcommit offered native fallback");
    require(c.prepare(p)==NativePairDecision::drain_required,"nested pair discarded pending focus");
    require(c.finish_frame()==NativePairDecision::drain_required,"skipped focus released pending result");
    require(c.retire(t)==NativePairDecision::ready,"explicit drain failed");
    require(c.phase()==NativePairPhase::idle,"drain retained active pair");
    require(c.prepare(p)==NativePairDecision::ready,"final preflight refused");t=c.token();
    require(c.begin_outer_commit(t,p.bankGeneration)==NativePairDecision::ready,"final outer commit failed");
    require(c.outer_published(t)==NativePairDecision::ready,"outer publish failed");
    require(c.begin_focus_commit(t,p.frame+1,p.view,p.eye,p.bankGeneration)==NativePairDecision::drain_required,"different frame consumed result");
    require(!c.focus_suppression_allowed(t,0,p.packets[1][0]),"bad focus boundary enabled suppression");
    require(c.retire(t)==NativePairDecision::ready,"second drain failed");
    require(c.prepare(p)==NativePairDecision::ready,"happy path preflight refused");t=c.token();
    require(c.begin_outer_commit(t,p.bankGeneration)==NativePairDecision::ready,"happy path outer commit");
    require(c.outer_published(t)==NativePairDecision::ready,"happy path outer publish");
    require(c.begin_focus_commit(t,p.frame,p.view,p.eye,p.bankGeneration)==NativePairDecision::ready,"focus commit authorization");
    require(!c.focus_suppression_allowed(t,0,p.packets[1][0]),"partial focus export enabled suppression");
    require(c.focus_published(t)==NativePairDecision::ready,"focus publish failed");
    require(c.focus_suppression_allowed(t,0,p.packets[1][0]),"complete focus result did not permit bounded packet");
    auto changed=p.packets[1][0];changed.resourceEpoch++;
    require(!c.focus_suppression_allowed(t,0,changed),"changed current resource epoch permitted");
    require(!c.focus_suppression_allowed(t,1,p.packets[1][0]),"out of range packet permitted");
    require(!c.focus_suppression_allowed(first,0,p.packets[1][0]),"stale token permitted suppression");
    require(c.retire(t)==NativePairDecision::ready,"happy path retirement");
    require(!c.focus_suppression_allowed(t,0,p.packets[1][0]),"retired token permitted suppression");
    require(c.finish_frame()==NativePairDecision::ready,"idle frame end refused");
    // Every unsupported preflight remains a decision only: no target writer is
    // reachable through this interface. The production scheduler must supply
    // the actual independently owned array and depth transfer implementation.
    std::cout<<"native_pair_pass_test passed\n";
}
