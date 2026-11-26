#include "artura_dials.hpp"
#include <cstdlib>
#include <iostream>
#include <thread>
void check(bool ok,const char* text){if(!ok){std::cerr<<text<<'\n';std::exit(1);}}
int main(){
    using namespace og::artura;
    Positions p;std::array<bool,128> buttons{};
    check(p.valid()==0 && p.angle(0)==0,"start unknown");
    for(unsigned i=0;i<12;++i){buttons={};buttons[30+i]=true;p.sample(buttons);check(p.position[0]==i+1 && p.position[1]==0,"absolute left position");}
    buttons={};p.sample(buttons);check(p.position[0]==12,"retain between pulses");
    buttons[42+10]=true;p.sample(buttons);check(p.position[1]==11 && p.valid()==3,"independent right position");
    buttons[42+5]=true;p.sample(buttons);check(p.position[1]==11,"ignore ambiguous input");
    p.position={3,11};check(std::abs(p.angle(0)-float(-5*pi/6))<1e-6,"left calibration");
    check(std::abs(p.angle(1)-float(pi/2))<1e-6,"right calibration");
    for(unsigned side=0;side<2;++side)for(unsigned position=1;position<=12;++position){p.position[side]=position;check(std::abs(p.angle(side))<=float(pi)+1e-6,"bounded angles");}
    p.reset();check(p.valid()==0,"disconnect clears known positions");
    check(device_name("Ascher Racing Artura ULTIMATE")&&!device_name("Other wheel"),"exact device identity");
#ifdef _WIN32
    const auto name=L"Local\\OpenGridXR.ArturaDials.Test."+std::to_wstring(GetCurrentProcessId());
    const auto lock=name+L".writer";
    Reader reader(name.c_str());reader.update(1000);check(reader.angles[2]==0,"missing writer is neutral");
    Writer writer(name.c_str(),lock.c_str());check(writer.acquire(2000),"open writer");
    p.position={3,11};writer.publish(p,2000);reader.update(2000);
    check(reader.angles[2]==1 && std::abs(reader.angles[0]-p.angle(0))<1e-6 && std::abs(reader.angles[1]-p.angle(1))<1e-6,"IPC receives both calibrated values");
    reader.update(3001);check(reader.angles[2]==0,"stale data is neutral");
    reader.update(1999);check(reader.angles[2]==0,"future data is neutral");
    bool competing=false;
    std::thread other([&]{Writer second(name.c_str(),lock.c_str());competing=second.acquire(2000);});other.join();
    check(!competing,"second input process cannot overwrite owner");
    writer.close();reader.update(GetTickCount64());check(reader.angles[0]==0 && reader.angles[1]==0,"writer close clears positions");
    Writer replacement(name.c_str(),lock.c_str());check(replacement.acquire(4000),"restart reuses mapping");
    reader.update(4000);check(reader.angles[0]==0 && reader.angles[1]==0,"restart waits for movement");
    buttons={};buttons[31]=true;p.reset();p.sample(buttons);replacement.publish(p,4000);reader.update(4000);
    check(std::abs(reader.angles[0]-p.angle(0))<1e-6 && reader.angles[1]==0,"only moved side becomes known after restart");
#endif
    std::cout<<"Artura positions, calibration, restart and IPC checks passed\n";
}
