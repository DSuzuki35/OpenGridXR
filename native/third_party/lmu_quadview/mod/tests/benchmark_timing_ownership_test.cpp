// GPU timing ownership remains required after removing automatic FOV scaling.
#define main submission_regression_main
#include "frame_submission_test.cpp"
#undef main
int main(){
    SetEnvironmentVariableA("LMU_QV_DIAGNOSTICS","1");
    lmuqv::renderProfile.configure(true,lmuqv::log_line);
    assert(!frameGpuSamplingAllowed());
    lmuqv::renderProfile.configure(false,lmuqv::log_line);
    assert(frameGpuSamplingAllowed());
    s.engineGpuTimingArmed=true;assert(!frameGpuSamplingAllowed());
    s.engineGpuTimingArmed=false;assert(frameGpuSamplingAllowed());
    std::cout<<"Benchmark GPU timing respects profiler and legacy timing ownership\n";
}
