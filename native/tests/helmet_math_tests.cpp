#include "helmet_renderer.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
void check(bool x,const char *s){if(!x)throw std::runtime_error(s);}
int main(){try{
    og::HelmetConfig c;
    const std::string live=R"({"version":2,"enabled":true,"leftImage":"OpenGridXR-helmet-iracing-left.png","rightImage":"OpenGridXR-helmet-iracing-right.png","visorHotkey":true,"visorPosition":"half","halfImage":"OpenGridXR-helmet-iracing-half.png","openImage":"OpenGridXR-helmet-iracing-open.png"})";
    check(og::parse_helmet_config(live,"iracing",c),"v2 live visor configuration accepted");
    check(c.visorHotkey&&c.visorPosition==og::HelmetVisorPosition::Half&&c.halfImage=="OpenGridXR-helmet-iracing-half.png","v2 visor state retained");
    for(auto bad:{R"({"version":1,"enabled":false,"visorHotkey":true})",R"({"version":2,"enabled":false,"visorHotkey":"true"})",R"({"version":2,"enabled":false,"visorPosition":"ajar"})",R"({"version":2,"enabled":false,"halfImage":"../half.png"})",R"({"version":2,"enabled":false,"openImage":"OpenGridXR-helmet-lmu-open.png"})"})
        check(!og::parse_helmet_config(bad,"iracing",c),"invalid visor contract rejected");
    auto missing=live;missing.erase(missing.find(",\"openImage\""));missing+='}';
    check(!og::parse_helmet_config(missing,"iracing",c),"enabled live visor requires both variant filenames");
    check(og::parse_helmet_config(R"({"version":2,"enabled":false,"visorHotkey":true})","iracing",c),"disabled live manifest needs no images");
    bool held=false;
    const og::HelmetShortcutSample down{true,true,true,true},up{false,false,false,true},background{true,true,true,false};
    check(og::helmet_visor_shortcut(held,true,down),"shortcut rising edge cycles");
    check(!og::helmet_visor_shortcut(held,true,down),"held shortcut does not repeat");
    check(!og::helmet_visor_shortcut(held,true,up)&&!og::helmet_visor_shortcut(held,true,background)&&!og::helmet_visor_shortcut(held,true,down),"focus return with held shortcut cannot cycle");
    og::helmet_visor_shortcut(held,true,up);
    check(!og::helmet_visor_shortcut(held,false,down)&&!og::helmet_visor_shortcut(held,true,down),"inactive press cannot carry into active rendering");
    const std::string good=R"({"version":1,"enabled":true,"leftImage":"OpenGridXR-helmet-iracing-left.png","rightImage":"OpenGridXR-helmet-iracing-right.png","headMotion":0.5,"vehicleMotion":1})";
    check(og::parse_helmet_config(good,"iracing",c)&&c.enabled&&c.headMotion==.5f&&c.stereoOffset==0,"legacy owned config defaults to zero stereo correction");
    check(!c.visorHotkey&&c.visorPosition==og::HelmetVisorPosition::Closed&&c.halfImage.empty(),"legacy config defaults to static closed visor");
    for(auto value:{"0","5","-5","1.25","-2.5e-1"}){
        check(og::parse_helmet_config(std::string(R"({"version":1,"enabled":false,"stereoOffset":)")+value+"}","iracing",c),
              "bounded horizontal stereo correction accepted");
        check(c.stereoOffset==std::stof(value),"stereo correction keeps its signed percentage");
    }
    const auto complete=good.substr(0,good.size()-1)+R"(,"stereoOffset":-1.5,"generation":"test-1"})";
    check(og::parse_helmet_config(complete,"iracing",c)&&c.stereoOffset== -1.5f&&c.generation=="test-1",
          "complete eight-field sidecar accepted");
    for(auto value:{"5.01","-5.01","1e99","NaN","Infinity","\"1\"","+1","01",".5","0x1","-","1.","1e","-01","null"})
        check(!og::parse_helmet_config(std::string(R"({"version":1,"enabled":false,"stereoOffset":)")+value+"}","iracing",c),
              "invalid stereo correction rejected");
    for(auto bad:{R"({"version":1,"enabled":"true"})",R"({"version":1,"enabled":true,"leftImage":"../left.png"})",R"({"version":1,"enabled":false,"headMotion":2})",R"({"version":1,"enabled":false,"headMotion":"0.5"})",R"({"version":1,"enabled":false,"enabled":true})",R"({"version":1,"enabled":false,})",R"({"version":1,"enabled":false}trash)"})
        check(!og::parse_helmet_config(bad,"iracing",c),"invalid config rejected");
    check(!og::parse_helmet_config(good,"lmu",c),"cross-game filename rejected");
    check(og::parse_helmet_config(R"({"version":1,"enabled":false})","lmu",c)&&!c.enabled,"disabled config");
    check(og::parse_helmet_config(R"({"version":1,"enabled":false,"generation":"a41c-1234"})","lmu",c)&&c.generation=="a41c-1234","generation token accepted");
    check(!og::parse_helmet_config(R"({"version":1,"enabled":false,"generation":"../unsafe"})","lmu",c),"invalid generation token rejected");
    og::HelmetMotionState s;
    auto q=XrQuaternionf{0,0,0,1};
    auto m=og::helmet_motion(s,q,true,1,1,1);
    check(m[0]==0&&m[1]==0,"first motion sample stationary");
    for(int n=1;n<100;++n){
        float a=float(n)*.1f;
        m=og::helmet_motion(s,{0,std::sin(a),0,std::cos(a)},true,1+n*.01,1,1,{true,0,100,100});
        check(std::hypot(m[0],m[1])<=.017454f,"combined motion bounded one degree");
    }
    m=og::helmet_motion(s,q,false,3,1,1);check(m[0]==0&&m[1]==0,"invalid tracking resets motion");
    og::helmet_motion(s,q,true,4,0,1);
    m=og::helmet_motion(s,q,true,4.01,0,1,{true,.3f,2,2});check(m[0]==0&&m[1]==0,"stale vehicle sample ignored");
    m=og::helmet_motion(s,q,true,4.02,0,1,{true,0,2,2});check(m[0]>0&&m[1]>0,"fresh vehicle sample moves helmet");
    for(int n=3;n<120;++n)m=og::helmet_motion(s,q,true,4+n*.01,0,1,{});
    check(std::hypot(m[0],m[1])<.00001f,"lost telemetry decays to neutral");
    m=og::helmet_motion(s,q,true,9,1,1,{true,0,2,2});check(m[0]==0&&m[1]==0,"time gap resets motion");
    // Check the user's exact slider strengths before clipping/rounding can hide
    // a broken multiplier. One tenth radian/second stays well below the limit.
    og::HelmetMotionState fullHead,scaledHead,fullVehicle,scaledVehicle;
    for(auto *state:{&fullHead,&scaledHead,&fullVehicle,&scaledVehicle})og::helmet_motion(*state,q,true,1,0,0);
    const XrQuaternionf smallTurn{0,std::sin(.0005f),0,std::cos(.0005f)};
    const auto fullH=og::helmet_motion(fullHead,smallTurn,true,1.01,1,0);
    const auto scaledH=og::helmet_motion(scaledHead,smallTurn,true,1.01,.77f,0);
    check(fullH[0]<0&&std::abs(scaledH[0]/fullH[0]-.77f)<.00001f&&scaledH[1]==0,"77 percent head response scales yaw velocity without cross-axis movement");
    const auto fullV=og::helmet_motion(fullVehicle,q,true,1.01,0,1,{true,0,1,-2});
    const auto scaledV=og::helmet_motion(scaledVehicle,q,true,1.01,0,.75f,{true,0,1,-2});
    check(fullV[0]>0&&fullV[1]<0&&std::abs(scaledV[0]/fullV[0]-.75f)<.00001f&&std::abs(scaledV[1]/fullV[1]-.75f)<.00001f,"75 percent vehicle response scales lateral and longitudinal g independently");
    og::HelmetMotionState signFlip,zero;
    og::helmet_motion(signFlip,q,true,1,1,0);
    m=og::helmet_motion(signFlip,{0,0,0,-1},true,1.01,1,0);
    check(m[0]==0&&m[1]==0,"equivalent opposite quaternion sign cannot create head motion");
    og::helmet_motion(zero,q,true,1,0,0);
    m=og::helmet_motion(zero,smallTurn,true,1.01,0,0,{true,0,3,3});
    check(m[0]==0&&m[1]==0,"zero strengths suppress both valid motion inputs");
    m=og::helmet_motion(scaledVehicle,q,true,1.02,0,.75f,{true,.3f,3,3});
    check(m[0]<scaledV[0]&&m[1]>scaledV[1],"stale telemetry fades existing displacement toward zero");
    m=og::helmet_motion(scaledHead,{0,0,0,0},true,1.02,.77f,.75f,{true,0,3,3});
    check(m[0]==0&&m[1]==0,"invalid head quaternion resets displacement even with fresh vehicle motion");
    XrView eye{XR_TYPE_VIEW};eye.pose={{0,0,0,1},{-.032f,0,0}};eye.fov={-.8f,.7f,.75f,-.65f};
    XrPosef pose;XrExtent2Df size;
    check(og::helmet_quad_geometry(eye,0,0,pose,size),"asymmetric geometry");
    check(std::abs(pose.position.x-(std::tan(-.8f)+std::tan(.7f)-.032f))<.0001f,"eye offset and asymmetric FOV preserved");
    check(std::abs(size.width-2*(std::tan(.7f)-std::tan(-.8f)))<.0001f,"full original FOV coverage");
    XrView left=eye,right=eye;
    left.fov={-.9f,.65f,.75f,-.65f};right.fov={-.6f,.85f,.7f,-.7f};
    left.pose.orientation={0,std::sin(.06f),0,std::cos(.06f)};
    right.pose.orientation={0,-std::sin(.06f),0,std::cos(.06f)};
    right.pose.position.x=.032f;
    XrFovf shared{};
    check(og::helmet_shared_fov(left,right,shared),"canted asymmetric eye envelope");
    check(std::abs(shared.angleLeft-(-.9f-.12f))<.00001f&&
          std::abs(shared.angleRight-(.85f+.12f))<.00001f,"common head-space envelope includes both canted horizontal FOVs");
    for(unsigned i=0;i<2;++i){
        const auto &view=i?right:left;const float angle=i?-.12f:.12f;
        for(float x:{std::tan(view.fov.angleLeft),std::tan(view.fov.angleRight)})
            for(float y:{std::tan(view.fov.angleDown),std::tan(view.fov.angleUp)}){
                const float z=std::sin(angle)*x+std::cos(angle);
                const float tx=(std::cos(angle)*x-std::sin(angle))/z,ty=y/z;
                check(tx>=std::tan(shared.angleLeft)-.00001f&&tx<=std::tan(shared.angleRight)+.00001f&&
                      ty>=std::tan(shared.angleDown)-.00001f&&ty<=std::tan(shared.angleUp)+.00001f,
                      "shared plane covers every canted view corner without exposed scene edges");
            }
    }
    right.pose.orientation={0,1,0,0};
    check(!og::helmet_shared_fov(left,right,shared),"back-facing view cannot produce an invalid plane");
    right=left;right.pose.position.x=NAN;
    check(!og::helmet_shared_fov(left,right,shared),"invalid eye position rejected");
    eye.fov.angleRight=-.9f;check(!og::helmet_quad_geometry(eye,0,0,pose,size),"invalid FOV rejected");
    std::cout<<"Helmet configuration, geometry and bounded head/vehicle motion passed.\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
