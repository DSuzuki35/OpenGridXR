#pragma once
#include <charconv>
#include <cstdint>
#include <string_view>
#include <system_error>

namespace lmuqv {
// Render-thread-owned controller. No file IO, graphics calls or borrowed strings.
// On acceptance the caller applies requestedRate() and resets its old queries.
// It must reject/abort unsupported rates before collecting the new measurement.
class BenchmarkControl {
public:
    enum class State { idle, warming, measuring, complete };

    // Strict ASCII decimal command: "phaseId rate warmupFrames". Surrounding
    // ASCII whitespace is allowed. Rejected commands change nothing.
    // A larger nonzero phase ID can replace any phase, including an active one.
    bool beginCommand(std::string_view command) noexcept {
        uint64_t id=0,rate=0,warmup=0;
        if(!number(command,id)||!number(command,rate)||!number(command,warmup))return false;
        trim(command);
        if(!command.empty()||id==0||id<=id_||(rate!=1&&rate!=2&&rate!=4)||warmup>1800)return false;
        id_=id;rate_=static_cast<unsigned>(rate);remaining_=static_cast<unsigned>(warmup);
        state_=State::warming;
        return true;
    }

    // Call once at the outer0 frame boundary, AFTER applying an accepted rate.
    // Warmup N skips exactly N calls, then starts measurement on call N+1.
    // Zero starts on the first call. True is returned exactly once per phase.
    bool onFrame() noexcept {
        if(state_!=State::warming)return false;
        if(remaining_){--remaining_;return false;}
        state_=State::measuring;
        return true;
    }
    unsigned requestedRate() const noexcept {return rate_;}
    uint64_t phaseId() const noexcept {return id_;}
    State state() const noexcept {return state_;}
    unsigned warmupRemaining() const noexcept {return remaining_;}
    // Only closes a measuring phase; cannot bypass warmup. Idempotent.
    void complete() noexcept {if(state_==State::measuring)state_=State::complete;}

private:
    static bool whitespace(char c) noexcept {
        return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\v'||c=='\f';
    }
    static void trim(std::string_view& s) noexcept {
        while(!s.empty()&&whitespace(s.front()))s.remove_prefix(1);
    }
    static bool number(std::string_view& s,uint64_t& value) noexcept {
        trim(s);
        if(s.empty())return false;
        size_t n=0;
        while(n<s.size()&&!whitespace(s[n]))++n;
        const auto result=std::from_chars(s.data(),s.data()+n,value,10);
        if(result.ec!=std::errc{}||result.ptr!=s.data()+n)return false;
        s.remove_prefix(n);
        return true;
    }
    uint64_t id_=0;
    unsigned rate_=1,remaining_=0;
    State state_=State::idle;
};
}
