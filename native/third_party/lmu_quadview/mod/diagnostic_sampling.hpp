#pragma once
#include <atomic>
#include <cstdint>

namespace lmuqv::diagnostic_logging {
// The XR lifecycle owns this policy under stateMutex. Draw hooks only read the
// published frame gate, so no draw needs to query the monotonic clock.
class FrameSampling {
    std::uint64_t frame_=0,nextSampleMs_=0;
    bool hasFrame_=false,scheduled_=false,sample_=false;
public:
    static constexpr std::uint64_t initial_frames=10,interval_ms=10000;
    template<class Clock>bool sample(std::uint64_t frame,bool enabled,Clock&& clock){
        if(!enabled){hasFrame_=scheduled_=sample_=false;return false;}
        if(hasFrame_&&frame==frame_)return sample_;
        if(hasFrame_&&frame<frame_)scheduled_=false;
        frame_=frame;hasFrame_=true;sample_=false;
        // Preserve each site's initial diagnostics separately. Start the
        // periodic cadence only after those initial frames have elapsed.
        if(frame<initial_frames)return false;
        const auto now=clock();
        if(!scheduled_){nextSampleMs_=now+interval_ms;scheduled_=true;}
        else if(now>=nextSampleMs_){sample_=true;nextSampleMs_=now+interval_ms;}
        return sample_;
    }
};
inline std::atomic<bool> sampledFrame{false};
inline void publish_frame(bool sample){
    if(sampledFrame.load(std::memory_order_relaxed)!=sample)
        sampledFrame.store(sample,std::memory_order_release);
}
inline bool periodic_frame(){return sampledFrame.load(std::memory_order_acquire);}
}
