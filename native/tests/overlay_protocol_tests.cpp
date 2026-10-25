#include "overlay_protocol.hpp"
#include "overlay_transport.hpp"
#include <iostream>
#include <stdexcept>
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
int main() {
    try {
        og::OverlayCommand c;
        std::string error;
        std::uint64_t handle{};
        check(
            og::parse_overlay_command(
                R"({"op":"frame","slot":14,"frameId":42,"handle":"18446744073709551615","width":1024,"height":576,"format":"bgra"})",
                c, error),
            "bounded frame parses");
        check(og::overlay_handle(c.text("handle"), handle) && handle == UINT64_MAX,
              "64-bit handles retain precision");
        for (auto s : {R"({"op":"frame","slot":NaN})", R"({"op":"frame","op":"clear"})",
                       R"({"op":"frame",})", R"({"op":"frame","bad":{}})", R"({"op":"frame"}x)"})
            check(!og::parse_overlay_command(s, c, error), "invalid command rejected");
        check(!og::parse_overlay_command(std::string(4097, 'x'), c, error), "oversize rejected");
        og::OverlayChannel a, b;
        check(a.open("test") && b.open("test"), "mapping opens");
        check(a.lock(), "producer lock");
        a.data()->slots[14].revision = 42;
        a.unlock();
        check(b.lock() && b.data()->slots[14].revision == 42, "fixed ABI shared");
        b.unlock();
        std::cout << "Overlay protocol and ABI tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
