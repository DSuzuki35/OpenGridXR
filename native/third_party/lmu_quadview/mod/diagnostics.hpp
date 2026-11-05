#pragma once
#include <windows.h>
namespace lmuqv {
// Process-local launch setting. Cached once; no environment reads per frame.
inline bool diagnostics_enabled(){
    static const bool enabled=[]{
        char value[8]{};
        const auto length=GetEnvironmentVariableA("LMU_QV_DIAGNOSTICS",value,sizeof(value));
        return !(length==1&&value[0]=='0');
    }();
    return enabled;
}
}
