#include "layer_registry.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
using namespace opengrid::layers;
static void require(bool condition, const char* name) {
    if (!condition) throw std::runtime_error(name);
}
struct Fixture : Registry {
    Value value{4, 4, 0};
    Value pending = value;
    uint32_t begin_error = 0, read_error = 0, write_error = 0, commit_error = 0, verify_error = 0;
    int writes = 0, begins = 0;
    bool corrupt_readback = false;
    int idle_calls = 0;
    int busy_on_check = 0;
    uint32_t ensure_idle() override { return ++idle_calls == busy_on_check ? 170 : 0; }
    uint32_t begin(const Target& target) override {
        ++begins;
        require(target.manifest == L"C:\\Layers\\vendor.json", "exact target forwarded");
        return begin_error;
    }
    uint32_t read(Value& out) override { out = value; return read_error; }
    uint32_t write(uint32_t desired) override { ++writes; pending = {4, 4, desired}; return write_error; }
    uint32_t commit() override { if (!commit_error) value = pending; return commit_error; }
    uint32_t read_committed(Value& out) override {
        out = value;
        if (corrupt_readback) out.data ^= 1;
        return verify_error;
    }
};
static std::vector<std::wstring> args() {
    return {L"--set", L"HKCU", L"64", L"implicit", L"C:\\Layers\\vendor.json", L"0", L"1"};
}
int main() {
    try {
        Command command;
        require(supported_process(L"Le Mans Ultimate.exe"), "LMU blocks mutation");
        require(supported_process(L"IRACINGSIM64DX11.EXE"), "case insensitive iRacing process guard");
        require(supported_process(L"LeMansUltimate.exe") && supported_process(L"iRacingSim64DX11_test.exe") && supported_process(L"OpenGridXR-Output.exe"), "all runtime processes guarded");
        require(!supported_process(L"NotLe Mans Ultimate.exe") && !supported_process(L"explorer.exe"), "unrelated process does not block");
        require(parse_command(args(), command) == 0, "valid fixed-purpose set accepted");
        require(command.expected == 0 && command.desired == 1, "bounded numbers retained");
        require(parse_command({L"--inventory"}, command) == 0, "inventory accepted");
        require(parse_command({L"--inventory", L"C:\\out.json"}, command) == 87, "inventory output path rejected");
        auto candidate = args();
        for (const auto& bad : {L"HKCR", L"HKCU\\Software", L"hkcu"}) {
            candidate = args(); candidate[1] = bad;
            require(parse_command(candidate, command) == 87, "arbitrary hive rejected");
        }
        for (const auto& bad : {L"--run", L"--delete", L"--create"}) {
            candidate = args(); candidate[0] = bad;
            require(parse_command(candidate, command) == 87, "arbitrary operation rejected");
        }
        candidate = args(); candidate[2] = L"128";
        require(parse_command(candidate, command) == 87, "unknown registry view rejected");
        candidate = args(); candidate[3] = L"Implicit\\Other";
        require(parse_command(candidate, command) == 87, "arbitrary subkey rejected");
        for (const auto& bad : {L"-1", L"4294967296", L"1e0", L"0x1", L" 1", L"1 ", L"", L"+1"}) {
            for (size_t field : {size_t(5), size_t(6)}) {
                candidate = args(); candidate[field] = bad;
                require(parse_command(candidate, command) == 87, "malformed uint32 rejected");
            }
        }
        for (const auto& bad : {L"relative.json", L"C:relative.json", L"\\\\server\\share\\x.json",
                 L"\\\\?\\C:\\x.json", L"C:\\x.json:stream", L"C:\\x.txt", L"C:\\x\".json",
                 L"C:\\x\n.json", L"C:\\x\u0085.json", L"C:\\..\\x.json", L"C:\\a*\\x.json"}) {
            candidate = args(); candidate[4] = bad;
            require(parse_command(candidate, command) == 87, "unsafe manifest name rejected");
        }
        candidate = args(); candidate[4] = L"C:\\" + std::wstring(9000, L'a') + L".json";
        require(parse_command(candidate, command) == 87, "oversized manifest rejected");
        candidate = args(); candidate.push_back(L"extra");
        require(parse_command(candidate, command) == 87, "extra argument rejected");
        candidate = args(); candidate[0] = L"--elevate-set";
        require(parse_command(candidate, command) == 87, "user hive never elevated");
        candidate[1] = L"HKLM"; candidate[2] = L"32"; candidate[3] = L"explicit";
        candidate[4] = L"C:\\Program Files\\Layer.JSON"; candidate[6] = L"4294967295";
        require(parse_command(candidate, command) == 0, "machine elevation and exact restore accepted");
        require(elevated_parameters(command) == L"--set HKLM 32 explicit \"C:\\Program Files\\Layer.JSON\" 0 4294967295", "elevated command is fixed and quoted");
        require(registry_key(Kind::implicit) == L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit", "implicit registry boundary");
        require(registry_key(Kind::explicit_layer) == L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Explicit", "explicit registry boundary");
        require(json_string("a\"\\\n\t\x01") == "\"a\\\"\\\\\\n\\t\\u0001\"", "inventory JSON escapes registry names");
        parse_command(args(), command);
        { Fixture fixture; fixture.busy_on_check = 1; require(set_existing(command, fixture) == 170 && fixture.begins == 0, "running game prevents mutation"); }
        { Fixture fixture; fixture.busy_on_check = 2; require(set_existing(command, fixture) == 170 && fixture.value.data == 0, "game starting before commit rolls back mutation"); }
        { Fixture fixture; require(set_existing(command, fixture) == 0 && fixture.value.data == 1, "existing DWORD changed and verified"); }
        { Fixture fixture; fixture.read_error = 2; require(set_existing(command, fixture) == 2 && fixture.writes == 0, "missing value not created"); }
        { Fixture fixture; fixture.value.type = 1; require(set_existing(command, fixture) == 13 && fixture.writes == 0, "non DWORD not overwritten"); }
        { Fixture fixture; fixture.value.size = 8; require(set_existing(command, fixture) == 13 && fixture.writes == 0, "malformed DWORD not overwritten"); }
        { Fixture fixture; fixture.value.data = 3; require(set_existing(command, fixture) == 1306 && fixture.writes == 0, "stale value not overwritten"); }
        { Fixture fixture; fixture.begin_error = 5; require(set_existing(command, fixture) == 5 && fixture.writes == 0, "open failure propagated"); }
        { Fixture fixture; fixture.write_error = 5; require(set_existing(command, fixture) == 5 && fixture.value.data == 0, "failed write not committed"); }
        { Fixture fixture; fixture.commit_error = 6800; require(set_existing(command, fixture) == 6800 && fixture.value.data == 0, "transaction conflict not committed"); }
        { Fixture fixture; fixture.corrupt_readback = true; require(set_existing(command, fixture) == 1117, "readback mismatch never reports success"); }
        { Fixture fixture; fixture.verify_error = 5; require(set_existing(command, fixture) == 5, "readback access failure propagated"); }
        { Fixture fixture; command.desired = 0xffffffff; require(set_existing(command, fixture) == 0 && fixture.value.data == 0xffffffff, "original nonzero DWORD restored exactly"); }
        { Fixture fixture; command.target.manifest = L"relative.json"; require(set_existing(command, fixture) == 87 && fixture.begins == 0, "mutation independently validates target"); }
        std::cout << "Layer registry boundary tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
