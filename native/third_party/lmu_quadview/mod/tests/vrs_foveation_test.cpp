#include "../vrs_foveation.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <iostream>
#include <random>
namespace {
// Reference: the original per-tile implementation before 0.5.2.
std::vector<unsigned char> reference_map(unsigned width,unsigned height,const lmuqv::VrsFocus& f){
    if(!width||!height||width>16384||height>16384)return {};
    const unsigned cols=(width+15)/16,rows=(height+15)/16;
    std::vector<unsigned char> map(size_t(cols)*rows,0);
    if(!std::isfinite(f.left)||!std::isfinite(f.top)||!std::isfinite(f.right)||!std::isfinite(f.bottom)||
       f.left<0||f.top<0||f.right>1||f.bottom>1||f.left>=f.right||f.top>=f.bottom)return map;
    for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<cols;++x){
        const bool outside=(x+1)*16<=f.left*width||x*16>=f.right*width||(y+1)*16<=f.top*height||y*16>=f.bottom*height;
        map[size_t(y)*cols+x]=outside?1:0;
    }
    return map;
}
void map_cache_tests(){
    using namespace lmuqv;using R=VrsEyeMapCache::Result;
    // Equivalence with the original implementation (random + exact tile edges + invalid input).
    std::mt19937 random(1234);std::uniform_real_distribution<float> unit(0,1);
    const unsigned sizes[][2]{{16,16},{17,33},{1000,1000},{2064,2208},{1,1},{4096,3072}};
    for(auto& size:sizes)for(int i=0;i<400;++i){
        float a=unit(random),b=unit(random),c=unit(random),d=unit(random);
        if(i%5==0){a=std::round(a*size[0]/16)*16/size[0];b=std::round(b*size[1]/16)*16/size[1];}
        VrsFocus f{std::min(a,c),std::min(b,d),std::max(a,c),std::max(b,d)};
        if(i%37==0)f.left=NAN;
        std::vector<unsigned char> fast;vrs_map_into(size[0],size[1],f,fast);
        assert(fast==reference_map(size[0],size[1],f));
    }
    VrsEyeMapCache cache;
    assert(cache.update(0,{})==R::Invalid); // not configured
    cache.reset(1600,1600);
    assert(cache.update(2,{})==R::Invalid);
    const VrsFocus center{.305f,.305f,.705f,.705f},moved{.325f,.305f,.725f,.705f},jitter{.3051f,.305f,.7051f,.705f}; // mid-tile values (1600 px, 16 px tiles)
    assert(cache.update(0,center)==R::Upload);
    assert(cache.update(0,center)==R::Unchanged);          // steady focus: no work, no upload
    assert(cache.update(0,jitter)==R::Unchanged);          // sub-tile motion: same tiles, no upload
    assert(cache.update(1,center)==R::Upload);             // eyes are independent
    assert(cache.update(0,center)==R::Unchanged&&cache.update(1,center)==R::Unchanged); // alternating eyes: no uploads
    assert(cache.update(0,moved)==R::Upload);              // real movement uploads
    assert(cache.map(0)==reference_map(1600,1600,moved)&&cache.map(1)==reference_map(1600,1600,center));
    cache.invalidate(1);assert(cache.update(1,center)==R::Upload); // failed upload is retried
    cache.reset(800,800);assert(cache.update(0,center)==R::Upload&&cache.map(0).size()==50*50);

    // A failed moving-eye upload must be retried even when its next focus has
    // identical tiles. It must not evict the other eye's successfully uploaded map.
    cache.reset(1600,1600);
    assert(cache.update(0,center)==R::Upload&&cache.update(1,moved)==R::Upload);
    const auto rightMap=cache.map(1);
    assert(cache.update(0,moved)==R::Upload);
    cache.invalidate(0);
    const VrsFocus movedJitter{.3251f,.305f,.7251f,.705f};
    assert(cache.update(0,movedJitter)==R::Upload);
    assert(cache.update(0,movedJitter)==R::Unchanged);
    assert(cache.update(1,moved)==R::Unchanged&&cache.map(1)==rightMap);
    cache.invalidate(0);cache.invalidate(0);
    assert(cache.update(0,center)==R::Upload); // a return to old tiles still needs an upload
    assert(cache.map(0)==reference_map(1600,1600,center));

    // Reconfiguration can keep dimensions but replace both GPU resources.
    cache.reset(1600,1600);
    assert(cache.map(0).empty()&&cache.map(1).empty());
    assert(cache.update(0,center)==R::Upload&&cache.update(1,moved)==R::Upload);
    for(unsigned frame=0;frame<1000;++frame){
        assert(cache.update(0,center)==R::Unchanged);
        assert(cache.update(1,moved)==R::Unchanged);
    }
    cache.reset(0,1600);assert(cache.update(0,center)==R::Invalid);
    cache.reset(1600,16385);assert(cache.update(1,moved)==R::Invalid);
    cache.reset(16,16);
    assert(cache.update(0,center)==R::Upload&&cache.map(0)==std::vector<unsigned char>{0});
    assert(cache.update(1,moved)==R::Upload&&cache.map(1)==std::vector<unsigned char>{0});

    // Invalid rectangles fail sharp, including after a valid coarse map.
    cache.reset(64,64);assert(cache.update(0,{})==R::Upload);
    const VrsFocus reversed{.75f,.25f,.25f,.75f};
    assert(cache.update(0,reversed)==R::Upload);
    assert(cache.map(0)==std::vector<unsigned char>(16,0));
    assert(cache.update(0,reversed)==R::Unchanged);
    for(const auto& bad: {VrsFocus{-1,0,1,1},VrsFocus{0,0,2,1},VrsFocus{0,NAN,1,1},
                         VrsFocus{0,0,1,std::numeric_limits<float>::infinity()}}){
        assert(cache.update(0,bad)==R::Unchanged);
        assert(cache.map(0)==std::vector<unsigned char>(16,0));
    }
}
}
int main(){
 using namespace lmuqv;
 VrsFocus f=vrs_focus(.5f,.5f,.5f,.5f,0,0);
 auto map=vrs_map(64,64,f);
 assert(map.size()==16&&map[0]==1&&map[5]==0&&map[10]==0&&map[15]==1);
 // A tile touching the focus boundary remains sharp; a partial edge tile exists.
 assert(vrs_map(65,33,f).size()==15);
 auto shifted=vrs_focus(1,0,.5f,.5f,.5f,.5f);
 assert(shifted.left==.5f&&shifted.right==1&&shifted.top==0&&shifted.bottom==.5f);
 auto invalid=vrs_focus(NAN,.5f,.5f,.5f,0,0);
 assert(invalid.left==.25f&&invalid.right==.75f);
 XrView eye{};eye.pose.orientation.w=1;eye.fov={-.785398163f,.785398163f,.785398163f,-.785398163f};
 XrPosef gaze{};gaze.orientation.w=1;float x=0,y=0;
 assert(vrs_gaze_center(eye,gaze,x,y)&&std::abs(x-.5f)<1e-5&&std::abs(y-.5f)<1e-5);
 gaze.orientation.y=std::sin(-.2f);gaze.orientation.w=std::cos(-.2f);
 assert(vrs_gaze_center(eye,gaze,x,y)&&x>.5f);
 eye.fov.angleLeft=-.4f;gaze.orientation={0,0,0,1};
 assert(vrs_gaze_center(eye,gaze,x,y)&&x<.5f);
 gaze.orientation={0,1,0,0};assert(!vrs_gaze_center(eye,gaze,x,y));
 gaze.orientation={0,0,0,0};assert(!vrs_gaze_center(eye,gaze,x,y));
 eye.fov.angleRight=NAN;gaze.orientation.w=1;assert(!vrs_gaze_center(eye,gaze,x,y));
 float cropped[16]{};cropped[0]=2;cropped[5]=4;cropped[8]=.2f;cropped[9]=-.1f;cropped[11]=1;
 XrFovf actual{};assert(vrs_projection_fov(cropped,actual));
 assert(std::abs(std::tan(actual.angleLeft)-(-.6f))<1e-5f);
 assert(std::abs(std::tan(actual.angleRight)-.4f)<1e-5f);
 eye.fov=actual;gaze.orientation={0,std::sin(-.1f),0,std::cos(-.1f)};
 assert(vrs_gaze_center(eye,gaze,x,y));
 assert(std::abs(x-(std::tan(.2f)+.6f))<1e-5f);
 cropped[11]=0;assert(!vrs_projection_fov(cropped,actual));
 assert(vrs_map(0,64,f).empty()&&vrs_map(16385,64,f).empty());
 map_cache_tests();
 std::cout<<"VRS focus/map/projection tests passed (incl. 0.5.2 map equivalence and per-eye cache)\n";
}
