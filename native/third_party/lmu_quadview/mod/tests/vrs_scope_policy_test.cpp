#include "../vrs_scope_policy.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::uint32_t read_u32(std::ifstream& file, std::uint64_t offset) {
    std::array<unsigned char, 4> bytes{};
    file.seekg(static_cast<std::streamoff>(offset));
    file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    require(static_cast<bool>(file), "PE read failed");
    return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
           (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}

std::uint64_t file_offset(std::ifstream& file, std::uint32_t rva) {
    const auto pe = read_u32(file, 0x3c);
    require(read_u32(file, pe) == 0x00004550, "Invalid PE signature");
    const auto coff = pe + 4;
    const auto section_count = read_u32(file, coff) >> 16;
    const auto optional_size = read_u32(file, coff + 16) & 0xffff;
    const auto section_table = coff + 20 + optional_size;
    for (std::uint32_t index = 0; index < section_count; ++index) {
        const auto section = section_table + index * 40;
        const auto virtual_address = read_u32(file, section + 12);
        const auto raw_size = read_u32(file, section + 16);
        if (rva >= virtual_address && rva - virtual_address < raw_size)
            return read_u32(file, section + 20) + (rva - virtual_address);
    }
    throw std::runtime_error("RVA has no raw PE section");
}

void verify_call(std::ifstream& file, std::uint32_t return_rva,
                 std::uint32_t target_rva) {
    const auto offset = file_offset(file, return_rva - 5);
    file.seekg(static_cast<std::streamoff>(offset));
    char opcode{};
    file.read(&opcode, 1);
    require(static_cast<unsigned char>(opcode) == 0xe8, "Caller is not E8 rel32");
    const auto displacement = static_cast<std::int32_t>(read_u32(file, offset + 1));
    require(std::int64_t(return_rva) + displacement == target_rva,
            "Caller target mismatch");
}
}

int main(int argc, char** argv) {
    try {
        using namespace lmuqv::vrs_scope;
        ScopeInput input{true, false, false, 0x123400, 0x123400, 0};
        require(outer_main_pass_eligible(input, kMainPassReturn), "Main rejected");
        require(!outer_main_pass_eligible(input, 0x5df3c2), "Child main accepted");
        for (auto caller : kWorldSortReturns) {
            require(outer_world_sort_eligible(input, true, caller), "World rejected");
            require(!outer_world_sort_eligible(input, false, caller), "No marker accepted");
            require(!outer_world_sort_eligible(input, true, caller + 1), "Near caller accepted");
        }
        for (auto caller : {0x5893ddu, 0x589422u, 0x5df3c2u, 0x5e0948u, 0u})
            require(!outer_world_sort_eligible(input, true, caller), "Excluded caller accepted");
        auto rejects_all = [&](const ScopeInput& blocked) {
            require(!outer_main_pass_eligible(blocked, kMainPassReturn), "Bad main accepted");
            for (auto caller : kWorldSortReturns)
                require(!outer_world_sort_eligible(blocked, true, caller), "Bad world accepted");
        };
        auto changed = input;
        changed.outer_scope = false; rejects_all(changed);
        changed = input; changed.focus = true; rejects_all(changed);
        changed = input; changed.nested_excluded = true; rejects_all(changed);
        changed = input; changed.selected_view = 0; rejects_all(changed);
        changed = input; changed.argument_view = 0; rejects_all(changed);
        changed = input; changed.argument_view += 0x100; rejects_all(changed);
        for (auto flags : {0x40000000u, 0x80000000u, 0xc0000000u}) {
            changed = input; changed.view_flags = flags; rejects_all(changed);
        }
        if (argc < 2 || std::string(argv[1]) == "--policy-only") {
            std::cout << "VRS scope policy: PASS; optional private PE anchor check skipped\n";
            return 0;
        }
        std::ifstream pe(argv[1], std::ios::binary);
        require(static_cast<bool>(pe), "Cannot open saved PE");
        for (const auto& anchor : kHookAnchors) {
            const auto offset = file_offset(pe, anchor.rva);
            pe.seekg(static_cast<std::streamoff>(offset));
            for (std::size_t index = 0; index < anchor.size; ++index) {
                char byte{};
                pe.read(&byte, 1);
                require(static_cast<bool>(pe) && static_cast<unsigned char>(byte) == anchor.bytes[index],
                        "Hook anchor differs from captured PE");
            }
        }
        verify_call(pe, kMainPassReturn, 0x51d8a0);
        verify_call(pe, 0x5df3c2, 0x51d8a0);
        for (auto caller : kWorldSortReturns) verify_call(pe, caller, 0x51f040);
        verify_call(pe, 0x5893dd, 0x51f040);
        verify_call(pe, 0x589422, 0x51f040);
        std::cout << "VRS scope policy and captured PE anchors: PASS (31 verified scene-sort callers; other coverage unqualified)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
