#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace opengrid::layers {
enum class Scope { user, machine };
enum class View { x64, x86 };
enum class Kind { implicit, explicit_layer };
enum class Operation { inventory, set, elevate_set };
struct Target {
    Scope scope = Scope::user;
    View view = View::x64;
    Kind kind = Kind::implicit;
    std::wstring manifest;
};
struct Command {
    Operation operation = Operation::inventory;
    Target target;
    uint32_t expected = 0;
    uint32_t desired = 0;
};
constexpr uint32_t invalid_argument = 87;
constexpr uint32_t invalid_data = 13;
constexpr uint32_t stale_value = 1306;
constexpr uint32_t readback_failed = 1117;
struct Value { uint32_t type = 0; uint32_t size = 0; uint32_t data = 0; };
// begin/read/write/commit operate on one transaction. read_committed is independent.
struct Registry {
    virtual ~Registry() = default;
    virtual uint32_t ensure_idle() = 0;
    virtual uint32_t begin(const Target&) = 0;
    virtual uint32_t read(Value&) = 0;
    virtual uint32_t write(uint32_t) = 0;
    virtual uint32_t commit() = 0;
    virtual uint32_t read_committed(Value&) = 0;
};
uint32_t parse_command(const std::vector<std::wstring>& args, Command& command);
bool valid_manifest(const std::wstring& path);
uint32_t set_existing(const Command& command, Registry& registry);
std::wstring registry_key(Kind kind);
std::wstring elevated_parameters(const Command& command);
std::string json_string(const std::string& utf8);
bool supported_process(const std::wstring& executable);
}
