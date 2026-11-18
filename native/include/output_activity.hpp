#pragma once
#include <cstdint>
#include <string_view>
#include <optional>

namespace og {
enum class OutputActivity { Scene, Wait, Transparent, Close, Clone };
template<class Capture>
void selectMonitorCapture(std::optional<Capture>& capture, OutputActivity mode) {
    if(mode!=OutputActivity::Clone)capture.reset();
    else if(!capture)capture.emplace();
}
inline std::optional<OutputActivity> outputActivity(std::wstring_view name) {
    if (name==L"wait") return OutputActivity::Wait;
    if (name==L"transparent") return OutputActivity::Transparent;
    if (name==L"close") return OutputActivity::Close;
    if (name==L"clone") return OutputActivity::Clone;
    return {};
}
inline OutputActivity chooseOutput(bool live, OutputActivity idle, bool cockpitOnly, bool driving,
                                   OutputActivity outside, std::uint64_t sinceLaunchMs, std::uint64_t outsideMs) {
    if (!live) return idle==OutputActivity::Close && sinceLaunchMs<3000 ? OutputActivity::Wait : idle;
    if(!cockpitOnly || driving)return OutputActivity::Scene;
    return outside==OutputActivity::Close && outsideMs<1000 ? OutputActivity::Wait : outside;
}
}
