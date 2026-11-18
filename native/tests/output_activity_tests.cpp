#include "output_activity.hpp"
#include <cstdlib>
using namespace og;
struct CaptureProbe {
    static inline unsigned opened{},closed{};
    CaptureProbe(){++opened;}
    ~CaptureProbe(){++closed;}
};
int main(){
    using Mode=OutputActivity;
    for(auto mode:{Mode::Wait,Mode::Clone,Mode::Transparent,Mode::Close}){
        if(chooseOutput(false,mode,true,true,Mode::Transparent,5000,0)!=mode)return 1;
        if(chooseOutput(true,Mode::Close,true,false,mode,5000,2000)!=mode)return 2;
        if(chooseOutput(true,Mode::Close,true,true,mode,5000,0)!=Mode::Scene)return 3;
        if(chooseOutput(true,Mode::Close,false,false,mode,5000,0)!=Mode::Scene)return 4;
    }
    if(chooseOutput(false,Mode::Close,false,false,Mode::Wait,2500,0)!=Mode::Wait)return 5;
    if(chooseOutput(true,Mode::Wait,true,false,Mode::Close,6000,100)!=Mode::Wait)return 6;
    std::optional<CaptureProbe> capture;
    for(auto next:{Mode::Scene,Mode::Wait,Mode::Clone,Mode::Clone,Mode::Scene,Mode::Scene,
                   Mode::Clone,Mode::Transparent,Mode::Clone,Mode::Close}){
        selectMonitorCapture(capture,next);
        if(capture.has_value()!=(next==Mode::Clone))return 7;
    }
    if(CaptureProbe::opened!=3 || CaptureProbe::closed!=3)return 8;
    return 0;
}
