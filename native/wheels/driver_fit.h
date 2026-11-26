#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ovrlay {
// Metres relative to the authored seat; positive distance moves toward the driver.
struct DriverFit {
    float seatHeight{},seatDistance{},shoulderWidth{1},torsoLean{},legLength{1},kneeHeight{};
};
struct DriverMount {float pivotX{},pivotY{},pivotZ{},tiltDegrees{};};
namespace driver_fit {
struct Vec {
    float x{},y{},z{};
    Vec operator+(Vec b)const{return {x+b.x,y+b.y,z+b.z};}
    Vec operator-(Vec b)const{return {x-b.x,y-b.y,z-b.z};}
    Vec operator*(float s)const{return {x*s,y*s,z*s};}
};
inline float dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec cross(Vec a,Vec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float length(Vec a){return std::sqrt(dot(a,a));}
inline Vec unit(Vec a,Vec fallback={1,0,0}){float d=length(a);return d>1e-7f?a*(1/d):fallback;}
struct Matrix {
    float v[12]{1,0,0,0,0,1,0,0,0,0,1,0};
    Vec vector(Vec p)const{return {v[0]*p.x+v[1]*p.y+v[2]*p.z,v[4]*p.x+v[5]*p.y+v[6]*p.z,v[8]*p.x+v[9]*p.y+v[10]*p.z};}
    Vec point(Vec p)const{return vector(p)+Vec{v[3],v[7],v[11]};}
    void translate(Vec t){v[3]+=t.x;v[7]+=t.y;v[11]+=t.z;}
};
inline Matrix multiply(const Matrix& a,const Matrix& b) {
    Matrix r;
    for(unsigned i=0;i<3;i++)for(unsigned j=0;j<4;j++) {
        r.v[4*i+j]=(j==3?a.v[4*i+3]:0);
        for(unsigned k=0;k<3;k++)r.v[4*i+j]+=a.v[4*i+k]*b.v[4*k+j];
    }
    return r;
}
inline Matrix inverseRigid(const Matrix& m) {
    Matrix r{{m.v[0],m.v[4],m.v[8],0,m.v[1],m.v[5],m.v[9],0,m.v[2],m.v[6],m.v[10],0}};
    r.translate(r.vector({-m.v[3],-m.v[7],-m.v[11]}));return r;
}
inline Matrix steering(const DriverMount& settings,float radians) {
    const float tilt=settings.tiltDegrees*.01745329252f,y=-std::sin(tilt),z=std::cos(tilt),c=std::cos(radians),s=std::sin(radians),t=1-c;
    Matrix r{{c,-z*s,y*s,0,z*s,t*y*y+c,t*y*z,0,-y*s,t*y*z,t*z*z+c,0}};
    const Vec pivot{settings.pivotX,settings.pivotY,settings.pivotZ};r.translate(pivot-r.point(pivot));return r;
}
inline Matrix gripRotation(float radians) {const float c=std::cos(radians),s=std::sin(radians);return {{c,s,0,0,-s,c,0,0,0,0,1,0}};}
inline Matrix rotation(Vec from,Vec to) {
    const Vec a=unit(from),b=unit(to);const float c=std::clamp(dot(a,b),-1.f,1.f);
    Vec axis=cross(a,b);const float s=length(axis);
    if(s<1e-6f && c>0)return {};
    axis=s<1e-6f?unit(cross(a,std::abs(a.x)<.8f?Vec{1,0,0}:Vec{0,1,0})):axis*(1/s);
    const float x=axis.x,y=axis.y,z=axis.z,t=1-c;
    return {{t*x*x+c,t*x*y-s*z,t*x*z+s*y,0,t*x*y+s*z,t*y*y+c,t*y*z-s*x,0,t*x*z-s*y,t*y*z+s*x,t*z*z+c,0}};
}
inline Matrix segment(Vec a,Vec b,Vec targetA,Vec targetB,float scale=1) {
    Matrix r=rotation(b-a,targetB-targetA);
    for(unsigned i=0;i<12;i++)r.v[i]*=scale;
    r.translate(targetA-r.point(a));return r;
}
inline Vec elbow(Vec shoulder,Vec wrist,float upper,float lower,Vec pole) {
    const Vec axis=unit(wrist-shoulder);const float d=std::clamp(length(wrist-shoulder),std::abs(upper-lower)+1e-5f,upper+lower-1e-5f);
    const float along=(upper*upper-lower*lower+d*d)/(2*d);
    Vec bend=pole-axis*dot(pole,axis);
    if(length(bend)<1e-6f)bend=cross(axis,std::abs(axis.x)<.8f?Vec{1,0,0}:Vec{0,1,0});
    return shoulder+axis*along+unit(bend)*std::sqrt(std::max(0.f,upper*upper-along*along));
}
inline float finiteClamp(float v,float lo,float hi,float fallback){return std::isfinite(v)?std::clamp(v,lo,hi):fallback;}
inline DriverFit sanitized(DriverFit f) {
    f.seatHeight=finiteClamp(f.seatHeight,-.3f,.3f,0);f.seatDistance=finiteClamp(f.seatDistance,-.3f,.4f,0);
    f.shoulderWidth=finiteClamp(f.shoulderWidth,.75f,1.25f,1);f.torsoLean=finiteClamp(f.torsoLean,-20,30,0);
    f.legLength=finiteClamp(f.legLength,.7f,1.15f,1);f.kneeHeight=finiteClamp(f.kneeHeight,-.15f,.15f,0);return f;
}
// v5 roles: body, left/right glove, upper arm, forearm, cuff, thigh, shin, foot.
constexpr unsigned roleCount=15;
struct Metadata {float width{},pelvis[3]{},upper[2]{},lower[2]{};};
// The seventh point is the cuff-axis endpoint, used to bound wrist flexion.
struct Frame {Vec points[2][7];};
static_assert(sizeof(Metadata)==32 && sizeof(Frame)==168);
inline Frame sample(const Frame (&frames)[241],float index) {
    const unsigned a=unsigned(index),b=std::min(a+1,240u);Frame sampled;
    for(unsigned side=0;side<2;side++)for(unsigned point=0;point<7;point++) {
        const Vec p=frames[a].points[side][point];sampled.points[side][point]=p+(frames[b].points[side][point]-p)*(index-float(a));
    }
    return sampled;
}
struct Solution {Matrix transforms[roleCount];Vec shoulders[2],elbows[2],wrists[2];bool limited{};};
inline Solution solveReach(const Frame& frame,const Metadata& meta,const DriverFit& input,const Matrix& mount,const Matrix& grip,float wheelWidth,Vec fixedCorrection={},bool project=true) {
    const DriverFit fit=sanitized(input);Solution out;
    const Vec pelvis{meta.pelvis[0]*meta.width,meta.pelvis[1]*meta.width,meta.pelvis[2]*meta.width};
    const Vec seat{0,fit.seatHeight,fit.seatDistance};
    const float radians=fit.torsoLean*.01745329252f,c=std::cos(radians),s=std::sin(radians);
    Matrix lean{{fit.shoulderWidth,0,0,0,0,c,-s,0,0,s,c,0}};
    lean.translate(pelvis+seat-lean.point(pelvis));
    Matrix body=multiply(mount,lean),hands=grip;
    const float handScale=wheelWidth/meta.width;
    for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++)hands.v[4*i+j]*=handScale;
    float upper[2],lower[2];Vec correction=fixedCorrection;
    for(unsigned side=0;side<2;side++) {
        const auto* p=frame.points[side];
        upper[side]=meta.upper[side];lower[side]=meta.lower[side];
        out.shoulders[side]=body.point(p[0]*meta.width);out.wrists[side]=hands.point(p[2]*meta.width);
    }
    // Project the complete seated body into the two arm reach spheres. The
    // hands and each arm's bone lengths stay fixed even at extreme requests.
    for(unsigned iteration=0;project && iteration<48;iteration++) {
        bool moved=false;
        for(unsigned side=0;side<2;side++) {
            const Vec delta=out.shoulders[side]+correction-out.wrists[side];const float d=length(delta);
            const float reach=upper[side]+lower[side]-.0001f;
            const float minimum=std::abs(upper[side]-lower[side])+.0001f;
            if(d>reach || d<minimum) {correction=correction+unit(delta)*(std::clamp(d,minimum,reach)-d);moved=true;}
        }
        if(!moved)break;
    }
    out.limited=length(correction)>.001f;body.translate(correction);out.transforms[0]=body;
    for(unsigned side=0;side<2;side++) {
        const auto* p=frame.points[side];const Vec sh=p[0]*meta.width,el=p[1]*meta.width,wr=p[2]*meta.width;
        const Vec targetShoulder=body.point(sh),targetWrist=out.wrists[side];
        Vec targetElbow=elbow(targetShoulder,targetWrist,upper[side],lower[side],body.vector(el-sh));
        // Choose the nearest point on the two-bone elbow circle that respects
        // the glove's cuff axis. This changes bend direction, never bone length.
        const Vec cuff=unit(grip.vector(p[6]-p[2]));
        constexpr float wristDot=.52994f; // 58 degrees, reserving interpolation margin
        if(dot(cuff,unit(targetWrist-targetElbow))<wristDot) {
            const Vec axis=unit(targetWrist-targetShoulder);
            const float along=dot(targetElbow-targetShoulder,axis);
            const Vec center=targetShoulder+axis*along,bend=targetElbow-center;
            const float radius=length(bend);const Vec projected=cuff-axis*dot(cuff,axis);
            const float projection=length(projected);
            if(radius>1e-6f && projection>1e-6f) {
                const Vec direction=projected*(1/projection);
                const float threshold=std::clamp((dot(targetWrist-center,cuff)-lower[side]*wristDot)/(radius*projection),-1.f,1.f);
                const Vec perpendicular=unit(bend-direction*dot(bend,direction),unit(cross(axis,direction)));
                targetElbow=center+(direction*threshold+perpendicular*std::sqrt(std::max(0.f,1-threshold*threshold)))*radius;
            }
        }
        out.shoulders[side]=targetShoulder;out.elbows[side]=targetElbow;
        out.transforms[1+side]=hands;
        // Undo the sub-millimetre chord shortening from palette interpolation.
        out.transforms[3+side]=segment(sh,el,targetShoulder,targetElbow,upper[side]/length(el-sh));
        out.transforms[5+side]=segment(el,wr,targetElbow,targetWrist,lower[side]/length(wr-el));
        out.transforms[7+side]=grip;out.transforms[7+side].translate(targetWrist-grip.point(wr));
        const Vec hip=p[3]*meta.width,knee=p[4]*meta.width,ankle=p[5]*meta.width;
        const Vec kneeVector=knee-hip;
        const Vec raised=unit(kneeVector*fit.legLength+Vec{0,fit.kneeHeight,0})*(length(kneeVector)*fit.legLength);
        const Vec newHip=mount.point(hip+seat)+correction,newKnee=newHip+mount.vector(raised);
        const Vec newAnkle=newKnee+mount.vector((ankle-knee)*fit.legLength);
        out.transforms[9+side]=segment(hip,knee,newHip,newKnee,fit.legLength);
        out.transforms[11+side]=segment(knee,ankle,newKnee,newAnkle,fit.legLength);
        Matrix foot=mount;
        for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++)foot.v[4*i+j]*=fit.legLength;
        foot.translate(newAnkle-foot.point(ankle));out.transforms[13+side]=foot;
    }
    return out;
}
inline bool wristsComfortable(const Solution& solution,const Frame& frame,const Matrix& grip,float minimumDot=.5f) {
    // A 60-degree limit includes authored palm relief and supported shaft tilt
    // variation, while preventing the near-right-angle bends of extreme fits.
    for(unsigned side=0;side<2;side++) {
        const Vec cuff=unit(grip.vector(frame.points[side][6]-frame.points[side][2]));
        if(dot(cuff,unit(solution.wrists[side]-solution.elbows[side]))<minimumDot)return false;
    }
    return true;
}
struct Plan {DriverFit fit;Vec correction;bool limited{},valid{true};};
inline Plan prepare(const Frame (&frames)[241],const Metadata& meta,const DriverFit& input,const DriverMount& settings,float width) {
    const DriverFit desired=sanitized(input);
    Matrix grips[241];
    for(unsigned f=0;f<241;f++) {
        const float radians=(float(f)*3-360)*.01745329252f;
        grips[f]=multiply(steering(settings,radians),gripRotation(radians));
    }
    Vec seed{};
    auto evaluate=[&](DriverFit fit,Plan& plan) {
        plan={fit,seed,false};
        const auto initial=solveReach(frames[120],meta,fit,Matrix{},grips[120],width,{},false);
        // One constant root correction satisfies the entire steering sweep,
        // so turning the wheel can never move the seat or the legs.
        for(unsigned iteration=0;iteration<64;iteration++) {
            float movement=0;
            for(unsigned f=0;f<241;f++)for(unsigned side=0;side<2;side++) {
                const Vec delta=initial.shoulders[side]+plan.correction-grips[f].point(frames[f].points[side][2]*width);
                const float d=length(delta),reach=meta.upper[side]+meta.lower[side]-.001f;
                if(d>reach){const Vec change=unit(delta)*(reach-d);plan.correction=plan.correction+change;movement+=length(change);}
            }
            if(movement<1e-6f)break;
        }
        plan.limited=length(plan.correction)>.001f;
        for(unsigned f=0;f<961;f++) {
            const auto frame=sample(frames,float(f)*.25f);
            const float radians=(float(f)*.75f-360)*.01745329252f;
            const auto grip=multiply(steering(settings,radians),gripRotation(radians));
            const auto solution=solveReach(frame,meta,fit,Matrix{},grip,width,plan.correction,false);
            // Reserve two degrees for interpolated poses between samples.
            if(!wristsComfortable(solution,frame,grip,.529919264f))return false;
            for(unsigned side=0;side<2;side++) {
                const float reach=length(solution.wrists[side]-solution.shoulders[side]);
                if(reach>meta.upper[side]+meta.lower[side]-.0001f || reach<std::abs(meta.upper[side]-meta.lower[side])+.0001f)return false;
            }
        }
        return true;
    };
    Plan accepted;
    if(evaluate(desired,accepted))return accepted;
    DriverFit neutral;neutral.legLength=desired.legLength;neutral.kneeHeight=desired.kneeHeight;
    if(!evaluate(neutral,accepted)) {
        // A tilted, displaced steering axis can make the nearest reach-only
        // correction unsuitable for the cuff orientation. Search a bounded
        // seated offset once per calibration, never during steering updates.
        bool found=false;
        for(int ring=1;ring<=6 && !found;ring++)for(int y=-ring;y<=ring && !found;y++)for(int z=-ring;z<=ring && !found;z++) {
            if(std::max(std::abs(y),std::abs(z))!=ring)continue;
            seed={0,float(y)*.025f,float(z)*.025f};found=evaluate(neutral,accepted);
        }
        if(!found){accepted.limited=true;accepted.valid=false;return accepted;}
    }
    float lo=0,hi=1;
    for(unsigned i=0;i<12;i++) {
        const float t=(lo+hi)*.5f;DriverFit candidate=desired;Plan tested;
        candidate.seatHeight*=t;candidate.seatDistance*=t;
        candidate.shoulderWidth=1+(desired.shoulderWidth-1)*t;candidate.torsoLean*=t;
        if(evaluate(candidate,tested)){accepted=tested;lo=t;}else hi=t;
    }
    accepted.limited=true;return accepted;
}
inline Solution solve(const Frame& frame,const Metadata& meta,const Plan& plan,const Matrix& mount,const Matrix& grip,float width) {
    const auto worldGrip=multiply(inverseRigid(mount),grip);
    auto result=solveReach(frame,meta,plan.fit,Matrix{},worldGrip,width,plan.correction,false);
    // Calibration uses the stationary wheel frame. Only this final rotation
    // changes with steering; all torso and leg transforms remain stationary.
    for(auto& transform:result.transforms)transform=multiply(mount,transform);
    for(unsigned side=0;side<2;side++) {
        result.shoulders[side]=mount.point(result.shoulders[side]);result.elbows[side]=mount.point(result.elbows[side]);result.wrists[side]=mount.point(result.wrists[side]);
    }
    result.limited=plan.limited;return result;
}
}
}
