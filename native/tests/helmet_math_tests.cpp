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
    check(c.ambientLight==0&&c.wheelAmbientLight==0,"legacy manifest keeps static lighting");
    for(auto value:{"0","0.25","100","3e1"}) {
        check(og::parse_helmet_config(std::string(R"({"version":2,"enabled":false,"ambientLight":)")+value+"}","iracing",c)
              && c.ambientLight==std::stof(value),"bounded scene-light percentage retained");
    }
    for(auto value:{"-1","100.1","NaN","1e99","01","\"1\"","0x2"}) {
        check(!og::parse_helmet_config(std::string(R"({"version":2,"enabled":false,"ambientLight":)")+value+"}","iracing",c),
              "malformed or unbounded scene light rejected");
        check(!og::parse_helmet_config(std::string(R"({"version":2,"enabled":false,"wheelAmbientLight":)")+value+"}","iracing",c),
              "malformed or unbounded wheel scene light rejected");
    }
    check(og::parse_helmet_config(R"({"version":2,"enabled":false,"wheelAmbientLight":72.5})","iracing",c)
          && !c.enabled&&c.wheelAmbientLight==72.5f&&c.ambientLight==0,"wheel can request a probe without helmet artwork");
    check(og::parse_helmet_config(live.substr(0,live.size()-1)+
        R"(,"headMotion":0,"vehicleMotion":0,"stereoOffset":0,"generation":"all","ambientLight":100,"wheelAmbientLight":10})","iracing",c)
          &&c.ambientLight==100&&c.wheelAmbientLight==10,"full manifest accepts independent helmet and wheel strengths");
    bool held=false;
    const og::HelmetShortcutSample down{true,true,true,true},up{false,false,false,true},background{true,true,true,false};
    check(og::helmet_visor_shortcut(held,true,down),"shortcut rising edge cycles");
    check(!og::helmet_visor_shortcut(held,true,down),"held shortcut does not repeat");
    check(!og::helmet_visor_shortcut(held,true,up)&&!og::helmet_visor_shortcut(held,true,background)&&!og::helmet_visor_shortcut(held,true,down),"focus return with held shortcut cannot cycle");
    og::helmet_visor_shortcut(held,true,up);
    check(!og::helmet_visor_shortcut(held,false,down)&&!og::helmet_visor_shortcut(held,true,down),"inactive press cannot carry into active rendering");
    const std::string good=R"({"version":1,"enabled":true,"leftImage":"OpenGridXR-helmet-iracing-left.png","rightImage":"OpenGridXR-helmet-iracing-right.png","headMotion":0.5,"vehicleMotion":1})";
    check(og::parse_helmet_config(good,"iracing",c)&&c.enabled&&c.stereoOffset==0,"legacy owned config defaults to zero stereo correction");
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
    XrView eye{XR_TYPE_VIEW};eye.pose={{0,0,0,1},{-.032f,0,0}};eye.fov={-.8f,.7f,.75f,-.65f};
    XrPosef pose;XrExtent2Df size;
    check(og::helmet_quad_geometry(eye,pose,size),"asymmetric geometry");
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
    eye.fov.angleRight=-.9f;check(!og::helmet_quad_geometry(eye,pose,size),"invalid FOV rejected");
    std::cout<<"Helmet configuration and static geometry passed.\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
