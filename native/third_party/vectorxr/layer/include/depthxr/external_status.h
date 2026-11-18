#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace depthxr {

// The public status file for other programs (%LOCALAPPDATA%\VectorXR\api\
// status.json; docs/status-api.md): its own document, not the runtime relay's
// status, so the relay can change freely. Version 1 only ever gains fields; a
// breaking change would write status-v2.json beside it for a while.
constexpr const char* kExternalStatusSchema = "opengridxr.enhancements.status";
constexpr std::uint32_t kExternalStatusVersion = 1;

struct ExternalStatusDocument {
    std::uint32_t process_id{0};
    std::string application;
    std::string session_id;
    std::uint64_t updated_at_unix_milliseconds{0};
};

// OPENGRIDXR_ENHANCEMENTS_EXTERNAL_STATUS_PATH overrides the path (development and tests).
std::filesystem::path ResolveExternalStatusPath();

std::string FormatExternalStatus(const ExternalStatusDocument& document);
// Replaces the file in one step: a reader sees the old document or the new one, never part of one.
bool WriteExternalStatus(const std::filesystem::path& path, const ExternalStatusDocument& document,
                         std::string* error = nullptr);
// At session end: removes the file only while it still names `process_id`,
// so a game shutting down never removes the next game's file. The check and
// the removal hold the file open against a writer's replace meanwhile.
enum class ExternalStatusRemoval : std::uint8_t {
    kRemoved,
    kNotOwned, // no file, or another process's (or not a status document)
    kBusy,     // held open by a reader without delete sharing beyond the retries: try again later
};
ExternalStatusRemoval RemoveExternalStatusIfOwned(const std::filesystem::path& path, std::uint32_t process_id);

} // namespace depthxr
