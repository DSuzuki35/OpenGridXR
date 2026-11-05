#include "../vrs_foveation.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <iostream>
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
 std::cout<<"VRS focus/map/projection tests passed\n";
}
