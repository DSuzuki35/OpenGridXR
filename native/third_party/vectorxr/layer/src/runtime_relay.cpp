#include "depthxr/runtime_relay.h"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <array>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
#include <string_view>
#include <thread>

#include "depthxr/atomic_file.h"
#include "depthxr/config_path.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace depthxr {
namespace {

// An ended session's files stay this long: Export Debug includes them, the
// last state of a session that crashed. The app removes any relay file
// after three days.
constexpr std::chrono::hours kEndedSessionFileAge{24};

std::optional<std::size_t> ValueStart(std::string_view content, std::string_view key) {
    const std::string token = "\"" + std::string(key) + "\"";
    std::size_t position = content.find(token);
    if (position == std::string_view::npos) return std::nullopt;
    position = content.find(':', position + token.size());
    if (position == std::string_view::npos) return std::nullopt;
    ++position;
    while (position < content.size() && (content[position] == ' ' || content[position] == '\t' || content[position] == '\r' || content[position] == '\n')) ++position;
    return position;
}

std::optional<std::uint64_t> ReadUnsigned(std::string_view content, std::string_view key) {
    const auto start = ValueStart(content, key);
    if (!start || *start >= content.size() || content[*start] < '0' || content[*start] > '9') return std::nullopt;
    std::uint64_t value = 0;
    std::size_t position = *start;
    while (position < content.size() && content[position] >= '0' && content[position] <= '9') {
        const std::uint64_t digit = static_cast<std::uint64_t>(content[position] - '0');
        if (value > (UINT64_MAX - digit) / 10) return std::nullopt;
        value = value * 10 + digit;
        ++position;
    }
    return value;
}

std::string EscapeJson(std::string_view value) {
    std::string escaped;
    for (const char character : value) {
        switch (character) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(character); break;
        }
    }
    return escaped;
}

bool WriteAtomically(const std::filesystem::path& path, const std::string& content, std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) { if (error) *error = "Unable to create runtime relay directory: " + ec.message(); return false; }
    const std::filesystem::path temporary_path = UniqueTemporaryPath(path);
    {
        std::ofstream stream(temporary_path, std::ios::trunc);
        if (!stream) { if (error) *error = "Unable to write runtime relay temporary file"; return false; }
        stream << content;
    }
    std::uint32_t move_error = 0;
    if (!ReplaceFileAtomically(temporary_path, path, &move_error)) {
        if (error) *error = "Unable to replace runtime relay file: Win32 error " + std::to_string(move_error);
        return false;
    }
    return true;
}

// "<pid>-<unix ms>-..." from a status or control file name (or its temporary).
bool ParseSessionFileName(const std::filesystem::path& path, std::uint32_t* process_id, std::uint64_t* started) {
    const std::wstring name = path.filename().wstring();
    std::size_t position = 0;
    const auto read_number = [&](std::uint64_t limit, std::uint64_t* value) {
        const std::size_t start = position;
        *value = 0;
        while (position < name.size() && name[position] >= L'0' && name[position] <= L'9') {
            const std::uint64_t digit = static_cast<std::uint64_t>(name[position] - L'0');
            if (*value > (limit - digit) / 10) return false;
            *value = *value * 10 + digit;
            ++position;
        }
        return position > start && position < name.size() && name[position++] == L'-';
    };
    std::uint64_t pid = 0;
    if (!read_number(UINT32_MAX, &pid) || !read_number(UINT64_MAX, started)) return false;
    *process_id = static_cast<std::uint32_t>(pid);
    return name.find(L".json") != std::wstring::npos;
}

} // namespace

bool RuntimeRelaySessionAlive(std::uint32_t process_id, std::uint64_t started_unix_milliseconds) {
#if defined(_WIN32)
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (!process) return GetLastError() != ERROR_INVALID_PARAMETER;
    DWORD exit_code = 0;
    const bool running = GetExitCodeProcess(process, &exit_code) == FALSE || exit_code == STILL_ACTIVE;
    FILETIME created{}, exited{}, kernel{}, user{};
    bool same = true;
    if (GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        // FILETIME counts 100 ns since 1601; 11644473600000 ms separate the epochs.
        const std::uint64_t ticks = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        const std::uint64_t created_unix_milliseconds = ticks / 10000 - 11644473600000ull;
        same = created_unix_milliseconds <= started_unix_milliseconds;
    }
    CloseHandle(process);
    return running && same;
#else
    (void)process_id;
    (void)started_unix_milliseconds;
    return true;
#endif
}

std::size_t PruneRuntimeRelayFiles(const std::filesystem::path& root, const RuntimeRelaySessionAliveCheck& alive) {
    std::size_t removed = 0;
    for (const char* folder : {"status", "control"}) {
        std::error_code ec;
        const std::filesystem::path directory = root / folder;
        if (!std::filesystem::is_directory(directory, ec)) continue;
        for (auto iterator = std::filesystem::directory_iterator(directory, ec);
             !ec && iterator != std::filesystem::directory_iterator(); iterator.increment(ec)) {
            std::uint32_t process_id = 0;
            std::uint64_t started = 0;
            if (!ParseSessionFileName(iterator->path(), &process_id, &started)) continue;
            std::error_code time_error;
            const auto written = iterator->last_write_time(time_error);
            if (time_error || std::filesystem::file_time_type::clock::now() - written < kEndedSessionFileAge ||
                alive(process_id, started)) {
                continue;
            }
            std::error_code remove_error;
            if (std::filesystem::remove(iterator->path(), remove_error)) ++removed;
        }
    }
    return removed;
}

std::filesystem::path ResolveRuntimeRelayRoot() {
    if (std::filesystem::path env_path = EnvironmentPath(L"OPENGRIDXR_ENHANCEMENTS_RUNTIME_RELAY_PATH"); !env_path.empty()) return env_path;
    return EngineModuleDirectory() / "enhancements-data" / "runtime";
}

std::filesystem::path RuntimeControlPath(const std::filesystem::path& root, const std::string& session_id) { return root / "control" / (session_id + ".json"); }
std::filesystem::path RuntimeStatusPath(const std::filesystem::path& root, const std::string& session_id) { return root / "status" / (session_id + ".json"); }
std::filesystem::path RuntimeBenchmarkPath(const std::filesystem::path& root,const std::string& id){return root/"status"/(id+"_benchmark.json");}
std::filesystem::path RuntimeBenchmarkRequestPath(const std::filesystem::path& root,const std::string& id){return root/"control"/(id+"_benchmark-request.json");}

void RuntimeBenchmarkRecorder::Reset(Clock::time_point now,std::uint64_t unix_ms){
    std::scoped_lock lock(mutex_);epoch_=now;unix_epoch_=unix_ms;expires_={};previous_.reset();sequence_=0;counters_={};frames_.clear();
}
void RuntimeBenchmarkRecorder::Demand(Clock::time_point expires){std::scoped_lock lock(mutex_);expires_=expires;}
void RuntimeBenchmarkRecorder::Application(Clock::time_point now,bool rendered){
    std::scoped_lock lock(mutex_);
    if(!rendered){previous_.reset();return;}
    ++counters_.application;++sequence_;
    if(now<=expires_&&previous_&&now>*previous_){
        const double ms=std::chrono::duration<double,std::milli>(now-*previous_).count();
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(now-epoch_).count();
        if(elapsed>=0&&std::isfinite(ms)){
            frames_.push_back({sequence_,unix_epoch_+static_cast<std::uint64_t>(elapsed),ms});
            if(frames_.size()>capacity)frames_.pop_front();
        }
    } else if(now>expires_)frames_.clear();
    previous_=now;
}
void RuntimeBenchmarkRecorder::Submitted(bool success,bool rendered){if(success&&rendered){std::scoped_lock lock(mutex_);++counters_.submitted;}}
RuntimeBenchmarkData RuntimeBenchmarkRecorder::Snapshot(bool include_frames)const{
    std::scoped_lock lock(mutex_);RuntimeBenchmarkData data;data.counters=counters_;if(include_frames)data.frames.assign(frames_.begin(),frames_.end());return data;
}
std::uint64_t ReadRuntimeBenchmarkLease(const std::filesystem::path& path,std::string_view session,std::uint64_t now){
    std::ifstream file(path,std::ios::binary);std::array<char,1025> bytes{};file.read(bytes.data(),bytes.size());
    const auto count=file.gcount();if(count<=0||count>1024||file.bad())return 0;
    const std::string_view content(bytes.data(),static_cast<std::size_t>(count));
    // Only a flat object of these four fields is accepted, with unique keys.
    std::size_t p=0;auto ws=[&]{while(p<content.size()&&std::isspace(static_cast<unsigned char>(content[p])))++p;};
    auto take=[&](char c){ws();if(p>=content.size()||content[p]!=c)return false;++p;return true;};
    auto str=[&]()->std::optional<std::string>{if(!take('"'))return {};std::string s;while(p<content.size()&&content[p]!='"'){char c=content[p++];if(c<' '||c=='\\'||s.size()>128)return {};s+=c;}if(p>=content.size())return {};++p;return s;};
    std::set<std::string> keys;std::string id;std::uint64_t version=0,requested=0,expires=0;
    if(!take('{'))return 0;
    for(;;){auto key=str();if(!key||!keys.insert(*key).second||!take(':'))return 0;
        if(*key=="sessionId"){auto value=str();if(!value)return 0;id=*value;}
        else{
            ws();auto start=p;while(p<content.size()&&content[p]>='0'&&content[p]<='9')++p;
            std::uint64_t value{};if(start==p||(p-start>1&&content[start]=='0')||std::from_chars(content.data()+start,content.data()+p,value).ec!=std::errc{})return 0;
            if(*key=="protocolVersion")version=value;else if(*key=="requestedAtMs")requested=value;else if(*key=="expiresAtMs")expires=value;else return 0;
        }
        if(take('}'))break;if(!take(','))return 0;
    }
    ws();if(p!=content.size()||keys.size()!=4||version!=1||id!=session||requested>now||expires<=now||expires<requested||expires-requested>5000)return 0;
    return expires-now;
}
bool WriteRuntimeBenchmark(const std::filesystem::path& path,const RuntimeBenchmarkData& data,std::string* error){
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(4);
    out<<"{\"sessionId\":\""<<EscapeJson(data.session_id)<<"\",\"timestampMs\":"<<data.timestamp_ms<<",\"frames\":[";
    const auto first=data.frames.size()>RuntimeBenchmarkRecorder::capacity?data.frames.size()-RuntimeBenchmarkRecorder::capacity:0;
    bool comma=false;for(std::size_t i=first;i<data.frames.size();++i){const auto& f=data.frames[i];if(!std::isfinite(f.frame_ms)||f.frame_ms<=0)continue;
        if(comma)out<<',';comma=true;out<<"{\"sequence\":"<<f.sequence<<",\"timestampMs\":"<<f.timestamp_ms<<",\"frameMs\":"<<f.frame_ms<<",\"cpuMs\":null,\"gpuMs\":null}";}
    out<<"],\"counters\":{\"application\":"<<data.counters.application<<",\"generated\":"<<data.counters.generated<<",\"repeated\":"<<data.counters.repeated<<",\"submitted\":"<<data.counters.submitted<<",\"displayed\":null},\"framegen\":{\"state\":\""<<EscapeJson(data.state)<<"\",\"mode\":\""<<EscapeJson(data.mode)<<"\",\"multiplier\":"<<data.multiplier<<",\"refreshHz\":";
    if(std::isfinite(data.refresh_hz)&&data.refresh_hz>0)out<<data.refresh_hz;else out<<"null";
    out<<"}}\n";return WriteAtomically(path,out.str(),error);
}

std::filesystem::path InputRescanRequestPath(const std::filesystem::path& root) { return root / "input-rescan.json"; }

bool ReadInputRescanRequest(const std::filesystem::path& path, std::uint64_t* requested_at_unix_milliseconds) {
    std::ifstream stream(path);
    if (!stream) return false;
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    const std::string content = buffer.str();
    const auto protocol_version = ReadUnsigned(content, "protocolVersion");
    const auto requested_at = ReadUnsigned(content, "requestedAtUnixMilliseconds");
    if (!protocol_version || *protocol_version != kRuntimeRelayProtocolVersion || !requested_at || *requested_at == 0) {
        return false;
    }
    *requested_at_unix_milliseconds = *requested_at;
    return true;
}

bool InputRescanRequestDue(std::uint64_t requested_at, std::uint64_t last_taken, std::uint64_t now) {
    return requested_at > last_taken && requested_at + kInputRescanFreshMilliseconds >= now;
}

std::uint64_t RuntimeRelayUnixMilliseconds() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

bool ReadRuntimeControl(const std::filesystem::path& path, RuntimeControlDocument* document, std::string* error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream || !document) return false;
    // This protocol is a small object with ASCII identifiers. Read at
    // most 4097 bytes and reject unknown keys, duplicates and trailing input.
    std::array<char, 4097> bytes{};
    stream.read(bytes.data(), bytes.size());
    const auto count = static_cast<std::size_t>(stream.gcount());
    const auto invalid = [&] { if (error) *error = "Invalid runtime control document"; return false; };
    if (count > 4096 || stream.bad()) return invalid();
    const std::string_view content(bytes.data(), count);
    std::size_t position = 0;
    const auto whitespace = [&] {
        while (position < content.size() && (content[position] == ' ' || content[position] == '\t' ||
               content[position] == '\r' || content[position] == '\n')) ++position;
    };
    const auto take = [&](char c) {
        whitespace();
        if (position >= content.size() || content[position] != c) return false;
        ++position; return true;
    };
    const auto string = [&]() -> std::optional<std::string> {
        if (!take('"')) return std::nullopt;
        const auto start = position;
        while (position < content.size() && content[position] != '"') {
            const auto c = static_cast<unsigned char>(content[position]);
            if (c < 0x20 || c >= 0x7f || c == '\\' || position - start >= 128) return std::nullopt;
            ++position;
        }
        if (position >= content.size()) return std::nullopt;
        return std::string(content.substr(start, position++ - start));
    };
    const auto number = [&]() -> std::optional<std::uint64_t> {
        whitespace();
        const auto start = position;
        while (position < content.size() && content[position] >= '0' && content[position] <= '9') ++position;
        if (start == position || (position - start > 1 && content[start] == '0')) return std::nullopt;
        std::uint64_t value{};
        const auto parsed = std::from_chars(content.data() + start, content.data() + position, value);
        if (parsed.ec != std::errc{}) return std::nullopt;
        return value;
    };
    const auto boolean = [&]() -> std::optional<bool> {
        whitespace();
        if (content.substr(position, 4) == "true") { position += 4; return true; }
        if (content.substr(position, 5) == "false") { position += 5; return false; }
        return std::nullopt;
    };
    RuntimeControlDocument parsed;
    std::set<std::string> keys;
    if (!take('{')) return invalid();
    for (;;) {
        const auto key = string();
        if (!key || !keys.insert(*key).second || !take(':')) return invalid();
        if (*key == "targetSessionId" || *key == "drivingAction") {
            const auto value = string();
            if (!value || value->empty()) return invalid();
            if (*key == "targetSessionId") parsed.target_session_id = *value;
            else if (*value == "framegen.toggle" || *value == "osd.toggle" || *value == "osd.cycle") parsed.driving_action = *value;
            else return invalid();
        } else if (*key == "quadviewsDiagnosticVisualization") {
            parsed.quadviews_diagnostic_visualization = boolean();
            if (!parsed.quadviews_diagnostic_visualization) return invalid();
        } else if (*key == "desired") {
            // The original diagnostic protocol nests its only property here.
            if (!take('{') || string() != "quadviewsDiagnosticVisualization" || !take(':')) return invalid();
            parsed.quadviews_diagnostic_visualization = boolean();
            if (!parsed.quadviews_diagnostic_visualization || !take('}')) return invalid();
        } else if (*key == "protocolVersion" || *key == "revision" || *key == "expiresAtUnixMilliseconds") {
            const auto value = number();
            if (!value) return invalid();
            if (*key == "protocolVersion") {
                if (*value != kRuntimeRelayProtocolVersion) return invalid();
                parsed.protocol_version = kRuntimeRelayProtocolVersion;
            } else if (*key == "revision") parsed.revision = *value;
            else parsed.expires_at_unix_milliseconds = *value;
        } else return invalid();
        if (take('}')) break;
        if (!take(',')) return invalid();
    }
    whitespace();
    if (position != content.size() || keys.size() != 5 || parsed.protocol_version != kRuntimeRelayProtocolVersion ||
        parsed.target_session_id.empty() || !parsed.revision || !parsed.expires_at_unix_milliseconds ||
        parsed.driving_action.has_value() == parsed.quadviews_diagnostic_visualization.has_value()) return invalid();
    *document = std::move(parsed);
    return true;
}

bool ApplyRuntimeControl(const RuntimeControlDocument& control, std::string_view session_id, std::uint64_t now,
                         std::uint64_t* last_applied_revision,
                         const std::function<bool(const RuntimeControlDocument&)>& apply) {
    if (!last_applied_revision || control.protocol_version != kRuntimeRelayProtocolVersion ||
        session_id.empty() || control.target_session_id != session_id ||
        control.expires_at_unix_milliseconds < now || control.revision <= *last_applied_revision) return false;
    if (!apply(control)) return false;
    *last_applied_revision = control.revision;
    return true;
}

bool WriteRuntimeStatus(const std::filesystem::path& path, const RuntimeStatusDocument& document, std::string* error) {
    std::ostringstream stream;
    // A game may set a global locale with a decimal comma or digit grouping.
    stream.imbue(std::locale::classic());
    stream << "{\n"
           << "  \"protocolVersion\": " << kRuntimeRelayProtocolVersion << ",\n"
           << "  \"sessionId\": \"" << EscapeJson(document.session_id) << "\",\n"
           << "  \"processId\": " << document.process_id << ",\n"
           << "  \"application\": \"" << EscapeJson(document.application) << "\",\n"
           << "  \"updatedAtUnixMilliseconds\": " << document.updated_at_unix_milliseconds << ",\n"
           << "  \"acknowledgedRevision\": " << document.acknowledged_revision << ",\n"
           << "  \"counters\": {\"application\":" << document.counters.application << ",\"generated\":" << document.counters.generated
           << ",\"repeated\":" << document.counters.repeated << ",\"submitted\":" << document.counters.submitted << ",\"displayed\":null},\n"
           << "  \"capabilities\": { \"benchmark\": true, \"drivingControls\": true, \"framegenToggle\": " << (document.framegen_toggle_available ? "true" : "false")
           << ", \"quadviewsDiagnosticVisualization\": " << (document.quadviews_diagnostic_visualization_available ? "true" : "false") << ", \"osd\": " << (document.osd_available ? "true" : "false") << " },\n"
           << "  \"state\": { \"quadviewsDiagnosticVisualization\": " << (document.quadviews_diagnostic_visualization_enabled ? "true" : "false")
           << ", \"osdVisible\": " << (document.osd_visible ? "true" : "false")
           << ", \"osdCompact\": " << (document.osd_compact ? "true" : "false")
           << ", \"osdMessage\": \"" << EscapeJson(document.osd_message) << "\""
           << ", \"turboState\": \"" << EscapeJson(document.turbo_state)
           << "\", \"turboReason\": \"" << EscapeJson(document.turbo_reason)
           << "\", \"framegenState\": \"" << EscapeJson(document.framegen_state)
           << "\", \"framegenReason\": \"" << EscapeJson(document.framegen_reason)
           << "\", \"framegenDepth\": \"" << EscapeJson(document.framegen_depth) << "\" },\n";
    if (const auto& rates = document.framegen_rates) {
        stream << std::fixed << std::setprecision(1) << "  \"framegen\": { \"mode\": \"" << EscapeJson(rates->mode)
               << "\", \"frameRate\": \"" << EscapeJson(rates->frame_rate)
               << "\", \"generating\": " << (rates->generating ? "true" : "false")
               << ", \"multiplier\": " << rates->multiplier << ", \"refreshHz\": " << rates->refresh_hz
               << ", \"holdFps\": " << rates->hold_fps << ", \"appFps\": " << rates->app_fps
               << ", \"outputFps\": " << rates->output_fps << ", \"distinctFps\": " << rates->distinct_fps
               << ", \"generatedFps\": " << rates->generated_fps
               << " },\n";
    }
    stream << "  \"quadviewsDimensionsAt\": " << document.quadviews_dimensions_at << ",\n"
           << "  \"quadviewsDimensions\": [";
    for (std::size_t i = 0; i < document.quadviews_dimensions.size(); ++i) {
        const auto& view = document.quadviews_dimensions[i];
        if (i) stream << ",";
        stream << "{\"width\":" << view.width << ",\"height\":" << view.height
               << ",\"allocatedWidth\":" << view.allocated_width << ",\"allocatedHeight\":" << view.allocated_height << "}";
    }
    stream << "]\n}\n";
    return WriteAtomically(path, stream.str(), error);
}

} // namespace depthxr

