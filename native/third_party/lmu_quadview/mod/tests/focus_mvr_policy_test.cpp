#include "../focus_mvr_policy.hpp"
#include <cassert>
using namespace lmuqv;
int main(){
    FocusRetry retry;
    auto now=FocusRetry::Clock::time_point{};
    for (unsigned seconds : {3u,6u,12u,20u,20u}) {
        retry.failed(now);
        now+=std::chrono::seconds(seconds);
        assert(!retry.ready(now-std::chrono::milliseconds(1)));
        assert(retry.ready(now));
    }
    for(unsigned i=0;i<599;++i)retry.reused();
    assert(retry.waitSeconds==20);
    retry.reused();
    retry.failed(now);
    assert(retry.waitSeconds==3);
    FocusAtlas layout{};
    assert(make_focus_atlas(800,700,1200,900,layout));
    assert(layout.width>=2000 && layout.height>=900);
    assert(std::abs(layout.width*layout.scale-800)<1 && std::abs(layout.height*layout.scale-700)<1);
    // LMU rounds the float product UP. A one-pixel overshoot spills into the
    // focus region or leaves the outer postprocessing viewport misaligned.
    for (unsigned w : {317u, 731u, 892u, 1280u, 1727u}) {
        for (unsigned h : {481u, 799u, 991u, 1173u}) {
            assert(make_focus_atlas(w,h,2031,1907,layout));
            assert(layout.width >= w+2031 && layout.height >= std::max(h,1907u));
            assert(std::ceil(layout.width*layout.scale)==w);
            assert(std::ceil(layout.height*layout.scale)==h);
        }
    }
    assert(!make_focus_atlas(9000,2000,9000,1000,layout));
    assert(!make_focus_atlas(0,2000,1200,900,layout));
    Matrix outer{},focus{};
    assert(make_engine_projection({-.9f,.8f,.7f,-.8f},.1f,1000,1,1,outer));
    assert(make_engine_projection({-.2f,.45f,.29f,-.2f},.1f,1000,1,1,focus));
    std::array<float,4> r{};
    assert(focus_clip_remap(outer,focus,r));
    for(float x : {-1.f,.2f,1.f}) {
        assert(std::abs((x*outer[0]+outer[8])*r[0]+r[1]-(x*focus[0]+focus[8]))<.00001f);
        assert(std::abs((x*outer[5]+outer[9])*r[2]+r[3]-(x*focus[5]+focus[9]))<.00001f);
    }
    assert(!focus_clip_remap(focus,outer,r));
    outer[11]=0;assert(!focus_clip_remap(outer,focus,r));
}
