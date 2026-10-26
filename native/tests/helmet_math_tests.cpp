#include "helmet_renderer.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
void check(bool x,const char *s){if(!x)throw std::runtime_error(s);}
int main(){try{
    og::HelmetConfig c;
    const std::string good=R"({"version":1,"enabled":true,"leftImage":"OpenGridXR-helmet-iracing-left.png","rightImage":"OpenGridXR-helmet-iracing-right.png","headMotion":0.5,"vehicleMotion":1})";
    check(og::parse_helmet_config(good,"iracing",c)&&c.enabled&&c.headMotion==.5f,"owned config");
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
    XrView eye{XR_TYPE_VIEW};eye.pose={{0,0,0,1},{-.032f,0,0}};eye.fov={-.8f,.7f,.75f,-.65f};
    XrPosef pose;XrExtent2Df size;
    check(og::helmet_quad_geometry(eye,0,0,pose,size),"asymmetric geometry");
    check(std::abs(pose.position.x-(std::tan(-.8f)+std::tan(.7f)-.032f))<.0001f,"eye offset and asymmetric FOV preserved");
    check(std::abs(size.width-2*(std::tan(.7f)-std::tan(-.8f)))<.0001f,"full original FOV coverage");
    eye.fov.angleRight=-.9f;check(!og::helmet_quad_geometry(eye,0,0,pose,size),"invalid FOV rejected");
    std::cout<<"Helmet configuration, geometry and bounded head/vehicle motion passed.\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
