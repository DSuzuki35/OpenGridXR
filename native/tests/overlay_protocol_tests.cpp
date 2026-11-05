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
        check(og::parse_overlay_command(R"({"op":"click","slot":0,"button":"left"})", c, error),
              "desktop click operation parses");
        check(!og::parse_overlay_command(R"({"op":"configure","slot":0,"desktopMode":2,"enabled":true})", c, error),
              "internal UI configure requires a UUID token");
        check(!og::parse_overlay_command(R"({"op":"click","slot":0,"button":"left","uiToken":"arbitrary"})", c, error),
              "malformed UI token rejected");
        check(og::parse_overlay_command(R"({"op":"configure","slot":0,"desktopMode":2,"enabled":true,"uiToken":"12345678-abcd-1234-abcd-123456789012"})", c, error),
              "internal UI UUID configure parses");
        check(!og::parse_overlay_command(R"({"op":"click","slot":0,"button":"left","uiToken":"12345678-abcd-1234-abcd-123456789012"})", c, error),
              "internal UI click requires originating focus and session");
        check(!og::parse_overlay_command(R"({"op":"click","slot":0,"button":"left","uiToken":"12345678-abcd-1234-abcd-123456789012","session":"9007199254740993","focusEpoch":"0"})", c, error),
              "internal UI click requires originating configuration, source and recenter");
        check(og::parse_overlay_command(R"({"op":"click","slot":0,"button":"left","uiToken":"12345678-abcd-1234-abcd-123456789012","session":"9007199254740993","focusEpoch":"0","configRevision":"2","sourceRevision":"3","recenter":"4"})", c, error),
              "internal UI click keeps exact decimal session and focus epoch");
        for (auto identity : {"9007199254740993", "\"1.0\"", "\"-1\"", "\"01\"", "\"18446744073709551616\""}) {
            auto json = std::string(R"({"op":"click","slot":0,"button":"left","uiToken":"12345678-abcd-1234-abcd-123456789012","session":)") + identity + R"(,"focusEpoch":"1","configRevision":"2","sourceRevision":"3","recenter":"4"})";
            check(!og::parse_overlay_command(json, c, error), "UI click identity must be exact unsigned decimal string");
        }
        check(og::parse_overlay_command(R"({"op":"resume","slot":0})", c, error),
              "explicit pointer resume parses");
        check(og::parse_overlay_command(R"({"op":"precision","slot":0,"held":true,"pressId":1})", c, error), "precision hold parses");
        check(og::parse_overlay_command(R"({"op":"precision","slot":14,"held":false,"pressId":9007199254740991})", c, error), "release and maximum safe press id parse");
        for (auto invalid : {R"({"op":"precision","slot":0,"held":1,"pressId":1})", R"({"op":"precision","slot":0,"held":"true","pressId":1})", R"({"op":"precision","slot":0,"held":true,"pressId":0})", R"({"op":"precision","slot":0,"held":false,"pressId":1.5})", R"({"op":"precision","slot":0,"held":true,"pressId":9007199254740992})", R"({"op":"precision","slot":15,"held":true,"pressId":1})"})
            check(!og::parse_overlay_command(invalid, c, error), "invalid precision lease rejected");
        og::OverlayChannel a, b;
        check(a.open("test") && b.open("test"), "mapping opens");
        check(a.lock(), "producer lock");
        a.data()->slots[14].revision = 42;
        a.unlock();
        check(b.lock() && b.data()->slots[14].revision == 42, "fixed ABI shared");
        b.unlock();
        check(a.lock(), "ABI compatibility lock");
        a.data()->version = 4;
        a.unlock();
        {
            og::OverlayChannel incompatible;
            check(!incompatible.open("test"),
                  "old layout is rejected rather than interpreted as desktop input");
        }
        check(a.lock(), "restore ABI version");
        a.data()->version = 8;
        a.unlock();
        std::cout << "Overlay protocol and ABI tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
