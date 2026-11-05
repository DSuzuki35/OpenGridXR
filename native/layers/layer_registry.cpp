#include "layer_registry.h"
#include <cwctype>
#include <limits>
namespace opengrid::layers {
bool supported_process(const std::wstring& executable) {
    std::wstring name = executable;
    for (auto& c : name) c = static_cast<wchar_t>(std::towlower(c));
    return name == L"iracingsim64dx11.exe" || name == L"iracingsim64dx11_test.exe" ||
        name == L"le mans ultimate.exe" || name == L"lemansultimate.exe" || name == L"opengridxr-output.exe";
}
static bool uint32_argument(const std::wstring& text, uint32_t& out) {
    if (text.empty() || text.size() > 10) return false;
    uint64_t value = 0;
    for (wchar_t c : text) {
        if (c < L'0' || c > L'9') return false;
        value = value * 10 + c - L'0';
        if (value > std::numeric_limits<uint32_t>::max()) return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}
bool valid_manifest(const std::wstring& path) {
    if (path.size() < 8 || path.size() > 8192) return false;
    if (!((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) ||
        path[1] != L':' || (path[2] != L'\\' && path[2] != L'/')) return false;
    size_t component = 3;
    for (size_t i = 3; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == L'\\' || path[i] == L'/') {
            if (i == component || path[i - 1] == L' ' || path[i - 1] == L'.') return false;
            component = i + 1;
        } else if (path[i] < 32 || (path[i] >= 127 && path[i] <= 159) || std::wstring(L"\":*?<>|").find(path[i]) != std::wstring::npos) {
            return false;
        }
    }
    std::wstring suffix = path.substr(path.size() - 5);
    for (auto& c : suffix) c = static_cast<wchar_t>(std::towlower(c));
    return suffix == L".json";
}
static bool valid_target(const Target& target) {
    return (target.scope == Scope::user || target.scope == Scope::machine) &&
        (target.view == View::x64 || target.view == View::x86) &&
        (target.kind == Kind::implicit || target.kind == Kind::explicit_layer) && valid_manifest(target.manifest);
}
uint32_t parse_command(const std::vector<std::wstring>& args, Command& command) {
    Command parsed;
    if (args.size() == 1 && args[0] == L"--inventory") { command = parsed; return 0; }
    if (args.size() != 7 || (args[0] != L"--set" && args[0] != L"--elevate-set")) return invalid_argument;
    parsed.operation = args[0] == L"--set" ? Operation::set : Operation::elevate_set;
    if (args[1] != L"HKCU" && args[1] != L"HKLM") return invalid_argument;
    parsed.target.scope = args[1] == L"HKCU" ? Scope::user : Scope::machine;
    if (parsed.operation == Operation::elevate_set && parsed.target.scope != Scope::machine) return invalid_argument;
    if (args[2] != L"64" && args[2] != L"32") return invalid_argument;
    parsed.target.view = args[2] == L"64" ? View::x64 : View::x86;
    if (args[3] != L"implicit" && args[3] != L"explicit") return invalid_argument;
    parsed.target.kind = args[3] == L"implicit" ? Kind::implicit : Kind::explicit_layer;
    parsed.target.manifest = args[4];
    if (!valid_target(parsed.target) || !uint32_argument(args[5], parsed.expected) || !uint32_argument(args[6], parsed.desired)) return invalid_argument;
    command = parsed;
    return 0;
}
uint32_t set_existing(const Command& command, Registry& registry) {
    if (command.operation != Operation::set || !valid_target(command.target)) return invalid_argument;
    uint32_t error = registry.ensure_idle();
    if (error) return error;
    error = registry.begin(command.target);
    if (error) return error;
    Value value;
    if ((error = registry.read(value))) return error;
    if (value.type != 4 || value.size != 4) return invalid_data;
    if (value.data != command.expected) return stale_value;
    if ((error = registry.write(command.desired))) return error;
    if ((error = registry.ensure_idle())) return error;
    if ((error = registry.commit())) return error;
    if ((error = registry.read_committed(value))) return error;
    return value.type == 4 && value.size == 4 && value.data == command.desired ? 0 : readback_failed;
}
std::wstring registry_key(Kind kind) {
    if (kind == Kind::implicit) return L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit";
    if (kind == Kind::explicit_layer) return L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Explicit";
    return {};
}
std::wstring elevated_parameters(const Command& command) {
    if (command.operation != Operation::elevate_set || command.target.scope != Scope::machine || !valid_target(command.target)) return {};
    return std::wstring(L"--set HKLM ") + (command.target.view == View::x64 ? L"64 " : L"32 ") +
        (command.target.kind == Kind::implicit ? L"implicit \"" : L"explicit \"") + command.target.manifest +
        L"\" " + std::to_wstring(command.expected) + L" " + std::to_wstring(command.desired);
}
std::string json_string(const std::string& utf8) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : utf8) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
                else out += static_cast<char>(c);
        }
    }
    return out + '"';
}
}
