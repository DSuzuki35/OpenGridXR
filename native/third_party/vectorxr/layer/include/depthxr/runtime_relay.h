#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <deque>
#include <mutex>

namespace depthxr {

constexpr std::uint32_t kRuntimeRelayProtocolVersion = 1;

struct RuntimeControlDocument {
    std::uint32_t protocol_version{0};
    std::string target_session_id;
    std::uint64_t revision{0};
    std::uint64_t expires_at_unix_milliseconds{0};
    std::optional<bool> quadviews_diagnostic_visualization;
    std::optional<std::string> driving_action;
};

struct QuadViewDimensions {
    std::uint32_t width{0}, height{0}, allocated_width{0}, allocated_height{0};
    bool operator==(const QuadViewDimensions&) const = default;
};

struct RuntimeFrameCounters {
    std::uint64_t application{}, submitted{};
};
struct RuntimeBenchmarkFrame {
    std::uint64_t sequence{}, timestamp_ms{};
    double frame_ms{}; // application xrEndFrame arrival interval, never GPU time
};
struct RuntimeBenchmarkData {
    std::string session_id;
    std::uint64_t timestamp_ms{};
    RuntimeFrameCounters counters;
    std::vector<RuntimeBenchmarkFrame> frames;
};
// Bounded, demand-driven history; rendering threads never touch files.
class RuntimeBenchmarkRecorder {
  public:
    using Clock=std::chrono::steady_clock;
    static constexpr std::size_t capacity=1024;
    void Reset(Clock::time_point, std::uint64_t unix_ms);
    void Demand(Clock::time_point expires);
    void Application(Clock::time_point, bool rendered);
    void Submitted(bool success, bool rendered);
    RuntimeBenchmarkData Snapshot(bool include_frames=true) const;
  private:
    mutable std::mutex mutex_;
    Clock::time_point epoch_{}, expires_{};
    std::optional<Clock::time_point> previous_;
    std::uint64_t unix_epoch_{}, sequence_{};
    RuntimeFrameCounters counters_;
    std::deque<RuntimeBenchmarkFrame> frames_;
};
std::filesystem::path RuntimeBenchmarkPath(const std::filesystem::path&, const std::string&);
std::filesystem::path RuntimeBenchmarkRequestPath(const std::filesystem::path&, const std::string&);
// Returns a remaining lease in milliseconds, or zero for invalid/stale requests.
std::uint64_t ReadRuntimeBenchmarkLease(const std::filesystem::path&, std::string_view session, std::uint64_t now);
bool WriteRuntimeBenchmark(const std::filesystem::path&, const RuntimeBenchmarkData&, std::string* error=nullptr);

struct RuntimeStatusDocument {
    RuntimeFrameCounters counters;
    bool visor_cycle_available{false};
    bool osd_available{false}, osd_visible{false}, osd_compact{false};
    std::string osd_message;
    std::string turbo_state;
    std::string turbo_reason;
    std::vector<QuadViewDimensions> quadviews_dimensions;
    std::uint64_t quadviews_dimensions_at{0};
    std::string session_id;
    std::uint32_t process_id{0};
    std::string application;
    std::uint64_t updated_at_unix_milliseconds{0};
    std::uint64_t acknowledged_revision{0};
    bool quadviews_diagnostic_visualization_available{false};
    bool quadviews_diagnostic_visualization_enabled{false};
};

std::filesystem::path ResolveRuntimeRelayRoot();
std::filesystem::path RuntimeControlPath(const std::filesystem::path& root, const std::string& session_id);
std::filesystem::path RuntimeStatusPath(const std::filesystem::path& root, const std::string& session_id);
std::uint64_t RuntimeRelayUnixMilliseconds();

// The VectorXR app's Rescan inputs, for every running game at once
// (<root>/input-rescan.json): each looks again for bound input devices that
// were not connected. A request is taken once, and only while fresh, so a
// game started later never replays an old one.
constexpr std::uint64_t kInputRescanFreshMilliseconds = 10'000;
std::filesystem::path InputRescanRequestPath(const std::filesystem::path& root);
// The request's time (Unix ms); false when there is none or it is unreadable.
bool ReadInputRescanRequest(const std::filesystem::path& path, std::uint64_t* requested_at_unix_milliseconds);
// Whether a request at `requested_at` is new since `last_taken` and fresh at
// `now` (all Unix ms).
bool InputRescanRequestDue(std::uint64_t requested_at, std::uint64_t last_taken, std::uint64_t now);

// Session ids begin "<process id>-<session start, Unix ms>-". Unknown or
// access-denied processes count as alive; a process created after the session
// started is a reused process id, not the session's.
bool RuntimeRelaySessionAlive(std::uint32_t process_id, std::uint64_t started_unix_milliseconds);
using RuntimeRelaySessionAliveCheck = std::function<bool(std::uint32_t, std::uint64_t)>;
// Removes the status and control files (and leftover temporary files) of
// sessions whose process is gone, once they were last written a day ago:
// until then Export Debug includes them. Games that exit without OpenXR
// teardown leave them behind; a hard shutdown can leave them zero-filled.
// Files whose name is not a session id are left alone. Returns how many were
// removed.
std::size_t PruneRuntimeRelayFiles(const std::filesystem::path& root,
                                   const RuntimeRelaySessionAliveCheck& alive = RuntimeRelaySessionAlive);

bool ReadRuntimeControl(const std::filesystem::path& path, RuntimeControlDocument* document, std::string* error = nullptr);
// Consume only a fresh command for this session, and only if its operation succeeds.
bool ApplyRuntimeControl(const RuntimeControlDocument&, std::string_view session_id, std::uint64_t now,
                         std::uint64_t* last_applied_revision,
                         const std::function<bool(const RuntimeControlDocument&)>& apply);
bool WriteRuntimeStatus(const std::filesystem::path& path, const RuntimeStatusDocument& document, std::string* error = nullptr);

} // namespace depthxr

