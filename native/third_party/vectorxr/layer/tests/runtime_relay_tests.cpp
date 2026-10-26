#include "depthxr/runtime_relay.h"
#include "depthxr/atomic_file.h"
#include "depthxr/turbo_metrics.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace {

void Expect(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void Touch(const std::filesystem::path& path, const std::string& content = "{}") {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << content;
}

// Last written more than a day ago.
void TouchOld(const std::filesystem::path& path, const std::string& content = "{}") {
    Touch(path, content);
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() - std::chrono::hours(25));
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("vectorxr-relay-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        using Recorder=depthxr::RuntimeBenchmarkRecorder;const auto start=Recorder::Clock::time_point{}+std::chrono::seconds(1);
        Recorder recorder;recorder.Reset(start,100000);
        recorder.Application(start,true);recorder.Application(start+std::chrono::milliseconds(10),true);
        Expect(recorder.Snapshot().frames.empty()&&recorder.Snapshot().counters.application==2,"No benchmark demand retains no frame history");
        recorder.Demand(start+std::chrono::seconds(5));
        for(int i=11;i<1111;++i)recorder.Application(start+std::chrono::milliseconds(i),true);
        auto snapshot=recorder.Snapshot();
        Expect(snapshot.frames.size()==1024&&snapshot.frames.back().frame_ms==1&&snapshot.frames.back().timestamp_ms==101110,"Frame ring is bounded and records actual intervals and aligned Unix timestamps");
        Expect(snapshot.frames.front().sequence<snapshot.frames.back().sequence,"Frame sequence allows deduplication across snapshots");
        recorder.Submitted(false,true);recorder.Submitted(true,false);recorder.Submitted(true,true);
        Expect(recorder.Snapshot(false).frames.empty()&&recorder.Snapshot(false).counters.submitted==1,"Counters exclude failed and empty runtime submissions without copying history");
        recorder.Application(start+std::chrono::seconds(6),true);
        Expect(recorder.Snapshot().frames.empty()&&recorder.Snapshot().counters.application>snapshot.counters.application,"Lease expiration drops history but retains monotonic session counts");
        recorder.Demand(start+std::chrono::seconds(10));recorder.Application(start+std::chrono::seconds(7),false);recorder.Application(start+std::chrono::seconds(8),true);
        Expect(recorder.Snapshot().frames.empty(),"Non-rendering pause does not create a fake long application frame");
        const auto request=depthxr::RuntimeBenchmarkRequestPath(root,"123-1000-1");
        const std::string valid=R"({"protocolVersion":1,"sessionId":"123-1000-1","requestedAtMs":1000,"expiresAtMs":5000})";
        Touch(request,valid);Expect(depthxr::ReadRuntimeBenchmarkLease(request,"123-1000-1",2000)==3000,"Fresh session-scoped benchmark lease");
        Expect(!depthxr::ReadRuntimeBenchmarkLease(request,"other",2000)&&!depthxr::ReadRuntimeBenchmarkLease(request,"123-1000-1",999)&&!depthxr::ReadRuntimeBenchmarkLease(request,"123-1000-1",5000),"Other-session, future and expired leases rejected");
        for(const auto& invalid:{R"({"protocolVersion":1,"sessionId":"123-1000-1","requestedAtMs":1000,"expiresAtMs":9000})",R"({"protocolVersion":1,"sessionId":"123-1000-1","requestedAtMs":1000,"expiresAtMs":5000,"expiresAtMs":4000})"}){
            Touch(request,invalid);Expect(!depthxr::ReadRuntimeBenchmarkLease(request,"123-1000-1",2000),"Oversized leases and duplicate fields rejected");}
        snapshot.session_id="123-1000-1";snapshot.timestamp_ms=101110;snapshot.state="active";snapshot.mode="interpolate";snapshot.multiplier=2;snapshot.counters.generated=3;
        const auto output=depthxr::RuntimeBenchmarkPath(root,snapshot.session_id);Expect(depthxr::WriteRuntimeBenchmark(output,snapshot),"Benchmark feed written atomically");
        std::ifstream feed(output);std::string json((std::istreambuf_iterator<char>(feed)),{});
        feed.close();
        Expect(json.find("\"cpuMs\":null,\"gpuMs\":null")!=std::string::npos&&json.find("\"displayed\":null")!=std::string::npos&&json.find("\"generated\":3")!=std::string::npos,"Unknown hardware timings stay null and generated is an actual count");
        recorder.Reset(start,200000);Expect(recorder.Snapshot().counters.application==0&&recorder.Snapshot().frames.empty(),"New session resets history and counters");
        std::filesystem::remove(output);std::filesystem::remove(request);
    }

    {
        const auto control = root / "control-test.json";
        depthxr::RuntimeControlDocument document;
        const std::string prefix = "{\"protocolVersion\":1,\"targetSessionId\":\"1234-1000-1\",\"revision\":7,\"expiresAtUnixMilliseconds\":2000,";
        for (const auto* action : {"framegen.toggle", "osd.toggle", "osd.cycle"}) {
            Touch(control, prefix + "\"drivingAction\":\"" + action + "\"}");
            Expect(depthxr::ReadRuntimeControl(control, &document), "A supported driving action should parse without a diagnostic command");
            Expect(document.driving_action == action && !document.quadviews_diagnostic_visualization,
                   "Parsing must keep exactly the requested action and clear previous commands");
        }
        std::uint64_t applied = 0;
        int operations = 0;
        const auto apply = [&](const depthxr::RuntimeControlDocument& command) {
            if (command.driving_action != "osd.cycle") return false;
            ++operations; return true;
        };
        Expect(!depthxr::ApplyRuntimeControl(document, "other-session", 1000, &applied, apply) && operations == 0,
               "A command for another session must not execute");
        Expect(!depthxr::ApplyRuntimeControl(document, "1234-1000-1", 2001, &applied, apply) && operations == 0,
               "An expired command must not execute");
        Expect(depthxr::ApplyRuntimeControl(document, "1234-1000-1", 1000, &applied, apply) && operations == 1 && applied == 7,
               "A fresh command must execute and advance its acknowledgement");
        Expect(!depthxr::ApplyRuntimeControl(document, "1234-1000-1", 1001, &applied, apply) && operations == 1,
               "Polling an already applied command must not toggle it again");
        document.revision = 8;
        document.driving_action = "framegen.toggle";
        Expect(!depthxr::ApplyRuntimeControl(document, "1234-1000-1", 1001, &applied, apply) && applied == 7,
               "An unavailable operation must not be acknowledged");
        Touch(control, prefix + "\"quadviewsDiagnosticVisualization\":false}");
        Expect(depthxr::ReadRuntimeControl(control, &document) && document.quadviews_diagnostic_visualization == false,
               "Existing diagnostic commands must remain compatible");
        Touch(control, prefix + "\"desired\":{\"quadviewsDiagnosticVisualization\":true}}");
        Expect(depthxr::ReadRuntimeControl(control, &document) && document.quadviews_diagnostic_visualization == true,
               "The original nested diagnostic command must remain compatible");
        for (const auto& invalid : {
            prefix + "\"drivingAction\":\"settings.write\"}",
            prefix + "\"drivingAction\":\"osd.toggle\",\"drivingAction\":\"osd.cycle\"}",
            prefix + "\"drivingAction\":\"osd.toggle\",\"quadviewsDiagnosticVisualization\":true}",
            prefix + "\"drivingAction\":\"osd.toggle\"} trailing",
            prefix + "\"drivingAction\":\"osd.toggle\",\"extra\":true}",
            prefix + "\"drivingAction\":\"osd.toggle\",}",
            std::string("{\"protocolVersion\":1.5,\"targetSessionId\":\"1234-1000-1\",\"revision\":7,\"expiresAtUnixMilliseconds\":2000,\"drivingAction\":\"osd.toggle\"}"),
            std::string(4097, ' '),
        }) {
            Touch(control, invalid);
            Expect(!depthxr::ReadRuntimeControl(control, &document), "Malformed, ambiguous or oversized control must fail closed");
        }
        depthxr::RuntimeStatusDocument status;
        status.session_id = "1234-1000-1";
        status.framegen_toggle_available = true;
        const auto status_path = root / "status-test.json";
        Expect(depthxr::WriteRuntimeStatus(status_path, status), "Driving capabilities should serialize");
        std::ifstream status_stream(status_path);
        const std::string status_text((std::istreambuf_iterator<char>(status_stream)), std::istreambuf_iterator<char>());
        Expect(status_text.find("\"drivingControls\": true") != std::string::npos &&
               status_text.find("\"framegenToggle\": true") != std::string::npos,
               "Status must advertise supported driving controls to desktop clients");
    }

    // Sessions of process 1234 ended without teardown (one status file is
    // zero-filled, as after a hard shutdown); process 5678 is still running.
    // The last of 1234's sessions ended within the day, a crash whose last
    // state Export Debug must still find.
    TouchOld(root / "status" / "1234-1791949116543-1.json", std::string(64, '\0'));
    TouchOld(root / "status" / "1234-1791949116543-1.json.tmp.5.6");
    TouchOld(root / "control" / "1234-1791949116543-1.json");
    TouchOld(root / "status" / "1234-1791949999999-2.json");
    Touch(root / "status" / "1234-1791950100000-3.json");
    Touch(root / "control" / "1234-1791950100000-3.json");
    TouchOld(root / "status" / "5678-1791950000000-1.json");
    Touch(root / "control" / "5678-1791950000000-1.json");
    TouchOld(root / "status" / "notes.txt");
    TouchOld(root / "status" / "app-1791949116543-1.json");
    TouchOld(root / "status" / "99999999999-1-1.json"); // Not a process id.

    std::set<std::uint64_t> asked;
    const std::size_t removed = depthxr::PruneRuntimeRelayFiles(root, [&](std::uint32_t pid, std::uint64_t started) {
        asked.insert(started);
        return pid == 5678;
    });
    Expect(removed == 4, "Every day-old file of an ended session should be removed");
    Expect(!std::filesystem::exists(root / "status" / "1234-1791949116543-1.json"), "A zero-filled status file should be removed");
    Expect(!std::filesystem::exists(root / "status" / "1234-1791949116543-1.json.tmp.5.6"), "A leftover temporary should be removed");
    Expect(!std::filesystem::exists(root / "control" / "1234-1791949116543-1.json"), "An ended session's control file should be removed");
    Expect(std::filesystem::exists(root / "status" / "1234-1791950100000-3.json") &&
               std::filesystem::exists(root / "control" / "1234-1791950100000-3.json"),
           "A session that ended within the day keeps its files for Export Debug");
    Expect(std::filesystem::exists(root / "status" / "5678-1791950000000-1.json") &&
               std::filesystem::exists(root / "control" / "5678-1791950000000-1.json"),
           "A running session's files must stay");
    Expect(std::filesystem::exists(root / "status" / "notes.txt") &&
               std::filesystem::exists(root / "status" / "app-1791949116543-1.json") &&
               std::filesystem::exists(root / "status" / "99999999999-1-1.json"),
           "Files that are not session files must stay");
    Expect(asked.count(1791949116543) == 1 && asked.count(1791949999999) == 1,
           "The session start time should reach the liveness check");
    Expect(depthxr::PruneRuntimeRelayFiles(root / "missing") == 0, "A missing relay folder is not an error");

#if defined(_WIN32)
    const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    Expect(depthxr::RuntimeRelaySessionAlive(GetCurrentProcessId(), now), "This process is alive");
    Expect(!depthxr::RuntimeRelaySessionAlive(GetCurrentProcessId(), 1000),
           "A process created after the session started reuses the session's process id");

    wchar_t command[] = L"cmd.exe /c exit 0";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        WaitForSingleObject(process.hProcess, 10000);
        Expect(!depthxr::RuntimeRelaySessionAlive(process.dwProcessId, now),
               "An exited process (handle still open) is not alive");
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
#endif

#if defined(_WIN32)
    // Antivirus or indexing holding a file open without delete sharing makes
    // a replace fail for a moment; the shared replace waits it out.
    const auto busy = root / "busy.json";
    Touch(busy, "old");
    for (const bool hold_source : {false, true}) {
        const auto temporary = depthxr::UniqueTemporaryPath(busy);
        Touch(temporary, "new");
        HANDLE held = CreateFileW((hold_source ? temporary : busy).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Expect(held != INVALID_HANDLE_VALUE, "Could not hold the file open");
        std::thread release([held] {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            CloseHandle(held);
        });
        std::uint32_t error_code = 0;
        const bool replaced = depthxr::ReplaceFileAtomically(temporary, busy, &error_code);
        release.join();
        Expect(replaced, "A briefly held file should be replaced after a retry");
        std::ifstream stream(busy);
        std::string content;
        stream >> content;
        Expect(content == "new", "The replaced file should hold the new content");
        Touch(busy, "old");
    }

    // Turbo metrics went through delete-then-rename with no retry: a scan of
    // the file failed the flush, and a scan of its temporary lost the history.
    const auto metrics = root / "turbo-metrics.json";
    depthxr::TurboMetricsSession first;
    first.session_id = "StarCitizen.exe-1";
    first.app_name = "StarCitizen.exe";
    first.started_unix_seconds = 1;
    Expect(depthxr::RecordTurboMetricsSession(metrics, first, nullptr), "The first metrics session should be written");
    HANDLE scanner = CreateFileW(metrics.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    Expect(scanner != INVALID_HANDLE_VALUE, "Could not open the metrics file as a scanner would");
    std::thread scan([scanner] {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        CloseHandle(scanner);
    });
    auto second = first;
    second.session_id = "StarCitizen.exe-2";
    second.started_unix_seconds = 2;
    const bool recorded = depthxr::RecordTurboMetricsSession(metrics, second, nullptr);
    scan.join();
    const auto sessions = depthxr::ReadTurboMetricsSessions(metrics);
    Expect(recorded && sessions.size() == 2, "A briefly scanned turbo-metrics.json should still take the new session");
#endif

    // Star Citizen and MSFS never send the final metrics flush (live=false):
    // the app showed their flights in progress forever.
    {
        const auto history = root / "metrics-history.json";
        const auto make = [](const char* id, const char* app, std::uint32_t pid, std::int64_t started) {
            depthxr::TurboMetricsSession session;
            session.session_id = id;
            session.app_name = app;
            session.process_id = pid;
            session.live = true;
            session.started_unix_seconds = started;
            return session;
        };
        std::set<std::uint32_t> dead{1234};
        std::uint64_t asked_started_ms = 0;
        const auto alive = [&](std::uint32_t pid, std::uint64_t started_ms) {
            if (pid == 1234) asked_started_ms = started_ms;
            return dead.count(pid) == 0;
        };
        Expect(depthxr::RecordTurboMetricsSession(history, make("sc-exited", "StarCitizen.exe", 1234, 100), nullptr, alive) &&
                   depthxr::RecordTurboMetricsSession(history, make("dcs-running", "DCS.exe", 5678, 110), nullptr, alive) &&
                   depthxr::RecordTurboMetricsSession(history, make("msfs-legacy", "FlightSimulator2024.exe", 0, 120), nullptr, alive) &&
                   depthxr::RecordTurboMetricsSession(history, make("sc-legacy", "StarCitizen.exe", 0, 90), nullptr, alive),
               "Metrics sessions should be written");
        Expect(depthxr::RecordTurboMetricsSession(history, make("sc-next", "StarCitizen.exe", 4321, 200), nullptr, alive),
               "The next Star Citizen session should be written");
        std::map<std::string, depthxr::TurboMetricsSession> by_id;
        for (const auto& session : depthxr::ReadTurboMetricsSessions(history)) by_id[session.session_id] = session;
        Expect(by_id.size() == 5 && by_id["sc-exited"].process_id == 1234, "Sessions should keep their process id");
        Expect(!by_id["sc-exited"].live, "A flight whose process exited should be marked ended");
        Expect(asked_started_ms == 100999, "The liveness check should get the session's start in milliseconds");
        Expect(by_id["dcs-running"].live && by_id["sc-next"].live, "Running flights should stay live");
        Expect(!by_id["sc-legacy"].live, "An older live flight of the same game with no process id should be ended");
        Expect(by_id["msfs-legacy"].live, "Another game's flight with no process id should be left alone");
    }

    {
        // Rescan inputs: one request file for every running game, each taking
        // a request once and only while it is fresh.
        const auto rescan = depthxr::InputRescanRequestPath(root);
        Expect(rescan == root / "input-rescan.json", "The rescan request should sit at the relay root");
        std::uint64_t requested_at = 0;
        Expect(!depthxr::ReadInputRescanRequest(rescan, &requested_at), "No request file should read as no request");
        Touch(rescan, "{ \"protocolVersion\": 1, \"requestedAtUnixMilliseconds\": 1700000000123 }");
        Expect(depthxr::ReadInputRescanRequest(rescan, &requested_at) && requested_at == 1700000000123,
               "The app's request should be read with its time");
        Touch(rescan, "{ \"protocolVersion\": 2, \"requestedAtUnixMilliseconds\": 1700000000123 }");
        Expect(!depthxr::ReadInputRescanRequest(rescan, &requested_at),
               "A request of another protocol version should be ignored");
        Touch(rescan, "{ \"protocolVersion\": 1 }");
        Expect(!depthxr::ReadInputRescanRequest(rescan, &requested_at), "A request without a time should be ignored");

        const std::uint64_t at = 1700000000000;
        Expect(depthxr::InputRescanRequestDue(at, 0, at + 2000), "A new request a moment old should be taken");
        Expect(!depthxr::InputRescanRequestDue(at, at, at + 2000), "A request already taken should not be taken again");
        Expect(depthxr::InputRescanRequestDue(at + 5000, at, at + 6000), "A later press should be taken again");
        Expect(!depthxr::InputRescanRequestDue(at, 0, at + depthxr::kInputRescanFreshMilliseconds + 1),
               "A stale request (a game started after the press) should not be replayed");
    }

    std::filesystem::remove_all(root);
    std::cout << "runtime_relay_tests passed\n";
    return 0;
}
