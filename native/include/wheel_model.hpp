#pragma once
#include <algorithm>
#include <array>
#include <string_view>
namespace og {
inline constexpr std::array<std::string_view,6> wheel_names{"hyper-p1","hyper-sl","gxl","gt-max","fpe2","ascher-artura"};
inline unsigned wheel_kind(std::string_view id) {
    auto it=std::find(wheel_names.begin(),wheel_names.end(),id);
    return it==wheel_names.end()?0:unsigned(it-wheel_names.begin()+1);
}
inline bool wheel_model_id(std::string_view id) {
    return wheel_kind(id) || (id.size()==64 && id.find_first_not_of("0123456789abcdef")==id.npos);
}
inline unsigned wheel_pixel_count(unsigned device) {
    constexpr std::array<unsigned,7> counts{0,88,86,62,76,66,63};
    return device<counts.size()?counts[device]:0;
}
}
