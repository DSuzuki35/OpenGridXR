#pragma once
#include <d3d11.h>
#include <array>
namespace lmuqv {
inline constexpr std::array<unsigned,24> stereo_audit_vtable{12,13,20,21,38,39,40,41,42,14,15,48,47,46,12,15,13,16,17,18,11,58,27,28};
enum class StereoAuditPass:unsigned {outer0,outer1,focus0,focus1,none};
inline StereoAuditPass stereo_audit_eye(bool focus,unsigned eye){return StereoAuditPass((focus?2:0)+eye);}
struct StereoAuditToken {bool entered=false;unsigned slot=0;long long started=0;};
struct StereoNativePrepareToken {bool entered=false;unsigned slot=0;long long started=0;};
StereoNativePrepareToken stereo_audit_native_prepare_begin(const void* mesh);
void stereo_audit_native_prepare_end(const StereoNativePrepareToken&);
bool stereo_audit_configure(ID3D11DeviceContext*,bool,const wchar_t* directory,void(*log)(const char*));
// Arm before the game creates its D3D11 device, to observe input layouts and
// shaders that already exist by xrCreateSession. Only main-executable factory
// calls qualify; runtime/provider helper devices are ignored.
bool stereo_audit_prepare(bool,const wchar_t* directory,void(*log)(const char*));
bool stereo_audit_enabled();
void stereo_audit_frame(bool keyDown);
void stereo_audit_frame_end();
StereoAuditToken stereo_audit_begin(StereoAuditPass);
void stereo_audit_end(StereoAuditToken&);
int stereo_audit_group(int group);
void stereo_audit_stop();
class StereoAuditScope {
    StereoAuditToken token_;
public:
    explicit StereoAuditScope(StereoAuditPass pass):token_(stereo_audit_begin(pass)){}
    ~StereoAuditScope(){end();}
    void end(){stereo_audit_end(token_);}
    StereoAuditScope(const StereoAuditScope&)=delete;
    StereoAuditScope& operator=(const StereoAuditScope&)=delete;
};
class StereoAuditGroup {
    int previous_;
public:
    explicit StereoAuditGroup(int group):previous_(stereo_audit_group(group)){}
    ~StereoAuditGroup(){stereo_audit_group(previous_);}
    StereoAuditGroup(const StereoAuditGroup&)=delete;
    StereoAuditGroup& operator=(const StereoAuditGroup&)=delete;
};
}
