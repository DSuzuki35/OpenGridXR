#pragma once
#include <d3d11.h>
#include <memory>
#include "vrs_foveation.hpp"
namespace lmuqv {
enum class NvVrsOwnership { Unknown, ExclusiveKnownDisabled };
using NvVrsLog=void(*)(void*,const char*);
class NvVrs {
    struct Impl;std::unique_ptr<Impl> impl_;
    bool resetFault_=false;
public:
    NvVrs();~NvVrs();
    NvVrs(const NvVrs&)=delete;NvVrs& operator=(const NvVrs&)=delete;
    bool configure(ID3D11DeviceContext*,unsigned,unsigned,unsigned rate=2,bool enable=false,
                   NvVrsOwnership ownership=NvVrsOwnership::Unknown,NvVrsLog callback=nullptr,void* user=nullptr);
    bool begin_outer(ID3D11DeviceContext*);
    // Update once per eye, outside active world-sort scopes. Coordinates are
    // normalized to this helper's configured full eye target. 0.5.2: each eye has
    // its own shading-rate map; the next begin_outer uses the last updated eye.
    bool update_focus(ID3D11DeviceContext*,const VrsFocus&,unsigned eye);
    bool update_focus(ID3D11DeviceContext* context,const VrsFocus& focus){return update_focus(context,focus,0);}
    // Diagnostics: shading-rate map uploads since configure.
    unsigned long long uploads()const;
    bool end_outer(ID3D11DeviceContext*);
    void reset();
    bool ready()const;bool active()const;bool faulted()const;
};
}
