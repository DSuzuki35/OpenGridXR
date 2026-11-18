#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace lmuqv::vrs_scope {

// RVAs and whole-instruction prologue prefixes for the captured 2026-10-28 PE.
// These are validation anchors, not a relocator or a runtime hook implementation.
inline constexpr std::uint32_t kMainPassReturn = 0x5e0891;

inline constexpr std::array<std::uint32_t, 31> kWorldSortReturns{{
    0x51da5e, 0x51db5d, 0x51dc5e, 0x51dc97, 0x51dcd0,
    0x51dd09, 0x51dd42, 0x51dd7b, 0x51ddb7, 0x51ddf0,
    0x51de29, 0x51de70, 0x51df1d, 0x51dfa6, 0x51e022,
    0x51e05b, 0x51e0ae, 0x51e16b, 0x51e1bf, 0x51e22e,
    0x51e26b, 0x51e2a4, 0x51e2dd, 0x51e316, 0x51e384,
    0x51e3bf, 0x51e416, 0x51e469, 0x51e4a2, 0x51e62d,
    0x51e66a
}};

struct ScopeInput {
    bool outer_scope;
    bool focus;
    bool nested_excluded;
    std::uintptr_t selected_view;
    std::uintptr_t argument_view;
    std::uint32_t view_flags;
};

constexpr bool base_eligible(const ScopeInput& input) noexcept {
    return input.outer_scope && !input.focus && !input.nested_excluded &&
           input.selected_view != 0 && input.argument_view == input.selected_view &&
           (input.view_flags & 0xc0000000u) == 0;
}

// Marker only: do not enable VRS for all of 51d8a0. It contains SSR/offscreen work.
constexpr bool outer_main_pass_eligible(const ScopeInput& input,
                                        std::uintptr_t caller_rva) noexcept {
    return base_eligible(input) && caller_rva == kMainPassReturn;
}

// main_pass_marker means the currently executing 51d8a0 invocation passed the
// predicate above. A nested/rejected invocation must replace it with false for
// its duration, then restore the previous TLS value (never simply inherit true).
constexpr bool outer_world_sort_eligible(const ScopeInput& input,
                                         bool main_pass_marker,
                                         std::uintptr_t caller_rva) noexcept {
    if (!base_eligible(input) || !main_pass_marker) return false;
    for (auto allowed : kWorldSortReturns)
        if (caller_rva == allowed) return true;
    return false;
}

inline constexpr std::array<std::uint8_t, 16> kMainPassAnchor{{
    0x48, 0x89, 0x5c, 0x24, 0x10, 0x55, 0x56, 0x57,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57
}};
inline constexpr std::array<std::uint8_t, 19> kWorldSortAnchor{{
    0x4c, 0x8b, 0xdc, 0x45, 0x89, 0x4b, 0x20, 0x49,
    0x89, 0x53, 0x10, 0x55, 0x48, 0x81, 0xec, 0xe0,
    0x00, 0x00, 0x00
}};
struct HookAnchor {
    std::uint32_t rva;
    const std::uint8_t* bytes;
    std::size_t size;
};
inline constexpr std::array<HookAnchor, 2> kHookAnchors{{
    {0x51d8a0, kMainPassAnchor.data(), kMainPassAnchor.size()},
    {0x51f040, kWorldSortAnchor.data(), kWorldSortAnchor.size()}
}};

} // namespace lmuqv::vrs_scope
