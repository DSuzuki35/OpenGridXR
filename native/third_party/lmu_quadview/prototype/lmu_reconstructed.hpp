#pragma once
// Analytically reconstructed snapshots for the examined LMU x64 build only.
// Names are ours, not recovered symbols. Unknown bytes must remain opaque.
// These are NOT owning objects and must never be constructed in game memory.
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace lmu::reconstructed {
inline constexpr char executable_sha256[] =
    "66942337813b301b87d12edfa330f33354a2a4b8cd9b048885cbf77eb6f940cf";
inline constexpr std::uint32_t renderer_pointer_rva = 0x1b12be8;
inline constexpr std::uint32_t current_camera_manager_pointer_rva = 0x1a51380;
inline constexpr std::uint32_t current_view_pointer_rva = 0x1a51370;
inline constexpr std::uint32_t camera_state_rva = 0x1b1be30;
inline constexpr std::uint32_t dynamic_state_loop_rva = 0xcd0bd0;
inline constexpr std::uint32_t camera_update_dispatch_rva = 0xc7fa90;
inline constexpr std::uint32_t scene_render_dispatch_rva = 0xd58a70;
inline constexpr std::uint32_t original_eye_loop_compare_rva = 0xcd17c7;
// The last address is evidence, NOT an approved single-byte patch location.

#pragma pack(push, 1)
struct TransformRecord {
    std::uint32_t metadata;
    std::array<float, 16> matrix;
    std::array<std::byte, 0x28> unknown_tail;
};
struct VrCameraState {
    TransformRecord head;
    std::array<TransformRecord, 2> relative_eye_transform;
    std::array<TransformRecord, 2> engine_projection;
    std::array<TransformRecord, 2> far_projection;
    std::array<std::byte, 8> unknown_2f4;
    std::array<TransformRecord, 2> ui_composite;
    std::array<float, 3> head_translation_copy;
    std::array<std::byte, 0x30> unknown_3e0;
    float width;
    float height;
};
// MSVC vector-shaped snapshots contain addresses, never usable std::vector objects.
struct VectorAddresses {
    std::uint64_t begin, end, capacity_end;
};
struct ViewSnapshot {
    std::uint32_t flags;
    std::array<std::byte, 0x1d4> unknown_004;
    TransformRecord current_transform;
    TransformRecord previous_transform;
    std::uint8_t previous_transform_valid;
    std::array<std::byte, 0xaf> unknown_2b1;
    std::array<float, 16> projection;
    std::array<std::byte, 0x40> unknown_3a0;
    std::uint64_t scene;
    std::array<std::byte, 8> unknown_3e8;
    std::uint64_t render_target;
    std::uint64_t second_target_unknown;
    std::uint32_t target_slice_or_subresource;
    std::array<std::byte, 0x4c> unknown_404;
    std::array<VectorAddresses, 4> callbacks;
    std::uint64_t parent;
    VectorAddresses children;
    VectorAddresses main_passes; // Observed record stride 0x94, not an eye array.
    std::array<std::byte, 0x90> unknown_4e8;
    std::array<std::uint64_t, 2> visibility_state_ids; // Exact subsystem semantics pending.
    std::uint8_t child_processing_flag;
    std::uint8_t late_child_flag;
    std::array<std::byte, 2> unknown_58a;
    std::array<float, 16> far_projection;
    std::int32_t eye_index; // Existing consumers require 0 or 1.
};
#pragma pack(pop)

static_assert(sizeof(float) == 4);
static_assert(sizeof(TransformRecord) == 0x6c);
static_assert(offsetof(TransformRecord, matrix) == 4);
static_assert(sizeof(VrCameraState) == 0x418);
static_assert(offsetof(VrCameraState, relative_eye_transform) == 0x06c);
static_assert(offsetof(VrCameraState, engine_projection) == 0x144);
static_assert(offsetof(VrCameraState, far_projection) == 0x21c);
static_assert(offsetof(VrCameraState, ui_composite) == 0x2fc);
static_assert(offsetof(VrCameraState, head_translation_copy) == 0x3d4);
static_assert(offsetof(VrCameraState, width) == 0x410);
static_assert(offsetof(VrCameraState, height) == 0x414);
static_assert(sizeof(VectorAddresses) == 0x18);
static_assert(sizeof(ViewSnapshot) == 0x5d0);
static_assert(offsetof(ViewSnapshot, current_transform) == 0x1d8);
static_assert(offsetof(ViewSnapshot, previous_transform) == 0x244);
static_assert(offsetof(ViewSnapshot, previous_transform_valid) == 0x2b0);
static_assert(offsetof(ViewSnapshot, projection) == 0x360);
static_assert(offsetof(ViewSnapshot, scene) == 0x3e0);
static_assert(offsetof(ViewSnapshot, render_target) == 0x3f0);
static_assert(offsetof(ViewSnapshot, second_target_unknown) == 0x3f8);
static_assert(offsetof(ViewSnapshot, target_slice_or_subresource) == 0x400);
static_assert(offsetof(ViewSnapshot, callbacks) == 0x450);
static_assert(offsetof(ViewSnapshot, parent) == 0x4b0);
static_assert(offsetof(ViewSnapshot, children) == 0x4b8);
static_assert(offsetof(ViewSnapshot, main_passes) == 0x4d0);
static_assert(offsetof(ViewSnapshot, visibility_state_ids) == 0x578);
static_assert(offsetof(ViewSnapshot, child_processing_flag) == 0x588);
static_assert(offsetof(ViewSnapshot, late_child_flag) == 0x589);
static_assert(offsetof(ViewSnapshot, far_projection) == 0x58c);
static_assert(offsetof(ViewSnapshot, eye_index) == 0x5cc);
static_assert(std::is_trivially_copyable_v<ViewSnapshot>);
// For a future adapter: XR slot and legacy eye index are separate concepts.
enum class XrQuadSlot : std::uint8_t { outer_left, outer_right, inner_left, inner_right };
constexpr std::uint8_t legacy_eye(XrQuadSlot slot) {
    return static_cast<std::uint8_t>(slot) & 1;
}
static_assert(legacy_eye(XrQuadSlot::inner_left) == 0);
static_assert(legacy_eye(XrQuadSlot::inner_right) == 1);
} // namespace lmu::reconstructed
