#include "helmet_renderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

namespace og {
namespace {
constexpr float limit = .01745329252f;
XrQuaternionf mul(XrQuaternionf a, XrQuaternionf b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y, a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w, a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
bool unit(XrQuaternionf &q) {
    float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    if (!std::isfinite(n) || n<.5f || n>1.5f) return false;
    n=std::sqrt(n); q={q.x/n,q.y/n,q.z/n,q.w/n}; return true;
}
} // namespace
bool parse_helmet_config(const std::string &json, const std::string &game, HelmetConfig &out) {
    out={};
    if ((game!="iracing" && game!="lmu") || json.size()>2048) return false;
    struct Value { bool quoted{}; std::string text; };
    std::map<std::string,Value> fields;
    size_t p=0;
    auto ws=[&] { while(p<json.size() && (json[p]==' '||json[p]=='\t'||json[p]=='\r'||json[p]=='\n')) ++p; };
    auto quote=[&](std::string &s) {
        if(p>=json.size()||json[p++]!='"')return false;
        while(p<json.size()&&json[p]!='"') {
            unsigned char c=json[p++]; if(c<32||c=='\\'||s.size()>=128)return false; s+=char(c);
        }
        return p<json.size()&&json[p++]=='"';
    };
    ws(); if(p>=json.size()||json[p++]!='{')return false; ws();
    while(p<json.size()&&json[p]!='}') {
        std::string key; if(!quote(key))return false; ws();
        if(p>=json.size()||json[p++]!=':')return false; ws(); Value value;
        if(p<json.size()&&json[p]=='"') { value.quoted=true; if(!quote(value.text))return false; }
        else { auto begin=p; while(p<json.size()&&json[p]!=','&&json[p]!='}'&&json[p]!=' '&&json[p]!='\r'&&json[p]!='\n'&&json[p]!='\t')++p; value.text=json.substr(begin,p-begin); if(value.text.empty())return false; }
        if(!fields.emplace(key,value).second||fields.size()>7)return false;
        ws(); if(p<json.size()&&json[p]==',') { ++p; ws(); if(p>=json.size()||json[p]=='}')return false; }
        else break;
    }
    if(p>=json.size()||json[p++]!='}')return false; ws(); if(p!=json.size())return false;
    for(const auto &[key,value]:fields)
        if(key!="version"&&key!="enabled"&&key!="leftImage"&&key!="rightImage"&&key!="headMotion"&&key!="vehicleMotion"&&key!="generation")return false;
    auto v=fields.find("version"), e=fields.find("enabled");
    if(v==fields.end()||v->second.quoted||v->second.text!="1"||e==fields.end()||e->second.quoted||
       (e->second.text!="true"&&e->second.text!="false"))return false;
    out.enabled=e->second.text=="true";
    if(auto g=fields.find("generation");g!=fields.end()) {
        const auto &s=g->second.text;
        if(!g->second.quoted||s.empty()||s.size()>64||!std::all_of(s.begin(),s.end(),[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-';}))return false;
        out.generation=s;
    }
    for(auto pair:{std::pair<const char*,float*>{"headMotion",&out.headMotion},{"vehicleMotion",&out.vehicleMotion}}) {
        auto f=fields.find(pair.first); if(f==fields.end())continue;
        char *end{}; float n=std::strtof(f->second.text.c_str(),&end);
        if(f->second.quoted||end!=f->second.text.c_str()+f->second.text.size()||!std::isfinite(n)||n<0||n>1)return false;
        *pair.second=n;
    }
    for(auto pair:{std::pair<const char*,std::string*>{"leftImage",&out.leftImage},{"rightImage",&out.rightImage}}) {
        auto f=fields.find(pair.first);
        if(f==fields.end()) { if(out.enabled)return false; else continue; }
        std::string expected="OpenGridXR-helmet-"+game+(std::string(pair.first)=="leftImage"?"-left.png":"-right.png");
        if(!f->second.quoted||f->second.text!=expected)return false;
        *pair.second=f->second.text;
    }
    return true;
}
std::array<float,2> helmet_motion(HelmetMotionState &s, XrQuaternionf q, bool valid,
    double now, float strength, float vehicleStrength, HelmetVehicleSample vehicle) {
    if(!valid||!unit(q)||!std::isfinite(now)||!std::isfinite(strength)||!std::isfinite(vehicleStrength)) { s={}; return {}; }
    double dt=now-s.time;
    if(!s.initialized||dt<=0||dt>.25) { s={q,now,0,0,true}; return {}; }
    auto delta=mul({-s.previous.x,-s.previous.y,-s.previous.z,s.previous.w},q);
    if(delta.w<0) delta={-delta.x,-delta.y,-delta.z,-delta.w};
    float yaw=-2*delta.y/float(dt)*.008f*std::clamp(strength,0.f,1.f);
    float pitch=-2*delta.x/float(dt)*.008f*std::clamp(strength,0.f,1.f);
    if(vehicle.valid&&std::isfinite(vehicle.ageSeconds)&&vehicle.ageSeconds>=0&&vehicle.ageSeconds<=.25f&&
       std::isfinite(vehicle.lateralG)&&std::isfinite(vehicle.longitudinalG)) {
        yaw+=std::clamp(vehicle.lateralG,-3.f,3.f)*.003f*std::clamp(vehicleStrength,0.f,1.f);
        pitch+=std::clamp(vehicle.longitudinalG,-3.f,3.f)*.003f*std::clamp(vehicleStrength,0.f,1.f);
    }
    float magnitude=std::hypot(yaw,pitch); if(magnitude>limit) { yaw*=limit/magnitude; pitch*=limit/magnitude; }
    float t=1-std::exp(-float(dt)/.08f);
    s.yaw+=(yaw-s.yaw)*t; s.pitch+=(pitch-s.pitch)*t; s.previous=q; s.time=now;
    return {s.yaw,s.pitch};
}
bool helmet_quad_geometry(const XrView &eye,float yaw,float pitch,XrPosef &pose,XrExtent2Df &size) {
    auto q=eye.pose.orientation;
    if(!unit(q)||!std::isfinite(yaw)||!std::isfinite(pitch)||std::hypot(yaw,pitch)>limit+.00001f)return false;
    float l=std::tan(eye.fov.angleLeft), r=std::tan(eye.fov.angleRight),
          u=std::tan(eye.fov.angleUp), d=std::tan(eye.fov.angleDown);
    if(!std::isfinite(l)||!std::isfinite(r)||!std::isfinite(u)||!std::isfinite(d)||l>=r||d>=u||
       std::max({std::abs(l),std::abs(r),std::abs(u),std::abs(d)})>10)return false;
    constexpr float distance=2;
    // The desktop aperture fitter reserves 3% per axis for optional motion.
    float sx=std::clamp(std::tan(yaw),-(r-l)*.03f,(r-l)*.03f);
    float sy=std::clamp(std::tan(pitch),-(u-d)*.03f,(u-d)*.03f);
    XrQuaternionf center{(l+r)*distance*.5f+sx*distance,
                         (u+d)*distance*.5f+sy*distance,-distance,0};
    auto offset=mul(mul(q,center),{-q.x,-q.y,-q.z,q.w});
    pose={q,{eye.pose.position.x+offset.x,eye.pose.position.y+offset.y,eye.pose.position.z+offset.z}};
    size={(r-l)*distance,(u-d)*distance};
    return std::isfinite(pose.position.x)&&std::isfinite(pose.position.y)&&std::isfinite(pose.position.z);
}
} // namespace og
