#include "settings.hpp"
#include <windows.h>
namespace {
unsigned settingsReads{};
bool hideFileIdentity{};
BOOL WINAPI countedSettingsRead(HANDLE file, LPVOID data, DWORD size, LPDWORD read, LPOVERLAPPED overlapped) {
    ++settingsReads;
    return ReadFile(file, data, size, read, overlapped);
}
BOOL WINAPI settingsFileInfo(HANDLE file, FILE_INFO_BY_HANDLE_CLASS type, LPVOID data, DWORD size) {
    if (hideFileIdentity && type == FileIdInfo) return FALSE;
    return GetFileInformationByHandleEx(file, type, data, size);
}
}
#define ReadFile countedSettingsRead
#define GetFileInformationByHandleEx settingsFileInfo
#include "output_framing.hpp"
#undef GetFileInformationByHandleEx
#undef ReadFile
#include "output_framing_preview.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <windows.h>
namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
struct Fixture {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        (L"OpenGridXR-framing-test-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    ~Fixture() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    void save(const std::string& content, const FILETIME* writeTime = nullptr) {
        auto temp = path; temp += L".tmp";
        { std::ofstream file(temp); file << content; }
        if (writeTime) {
            HANDLE file = CreateFileW(temp.c_str(), FILE_WRITE_ATTRIBUTES, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            require(file != INVALID_HANDLE_VALUE, "open replacement to preserve timestamp");
            const auto stamped = SetFileTime(file, nullptr, nullptr, writeTime);
            CloseHandle(file);
            require(stamped, "stamp replacement");
        }
        require(MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0,
                "atomic fixture save");
    }
};
std::string framing(unsigned reserve, int x = 0, int y = 0) {
    return "[lmu]\nReserveLeft=" + std::to_string(reserve) + "\nReserveRight=" +
        std::to_string(reserve) + "\nReserveTop=" + std::to_string(reserve) +
        "\nReserveBottom=" + std::to_string(reserve) + "\nOffsetX=" + std::to_string(x) +
        "\nOffsetY=" + std::to_string(y) + "\n";
}
void settingsReadTest() {
    Fixture file;
    og::Config config;
    og::OutputFramingReload reload(file.path.wstring(), "lmu");
    FILETIME sameTime{123000000, 31000000};
    file.save(framing(11), &sameTime);
    settingsReads = 0;
    require(reload.poll(config, 0), "load initial framing");
    for (uint64_t now = 500; now <= 60000; now += 500) reload.poll(config, now);
    require(settingsReads == 1, "unchanged framing was read and parsed again");
    file.save(framing(12), &sameTime);
    require(reload.poll(config, 60500) && config.reserve.left == 12 && settingsReads == 2,
            "same-size, same-time atomic replacement was missed");
    config.reserve.left = 1;
    require(reload.poll(config, 60501, true) && config.reserve.left == 12 && settingsReads == 3,
            "forced preview reset must reread an unchanged file immediately");

    hideFileIdentity = true;
    og::OutputFramingReload fallback(file.path.wstring(), "lmu");
    fallback.poll(config, 0);
    settingsReads = 0;
    file.save(framing(13), &sameTime);
    for (uint64_t now = 500; now <= 5000; now += 500) fallback.poll(config, now);
    require(config.reserve.left == 13 && settingsReads == 1,
            "identity-less filesystem must periodically reread a same-metadata replacement");
    file.save(framing(31), &sameTime);
    require(!fallback.poll(config, 5001, true) && config.reserve.left == 13,
            "forced reload of an invalid replacement changed the last good framing");
    file.save(framing(14), &sameTime);
    require(fallback.poll(config, 5501) && config.reserve.left == 14,
            "failed reload prevented recovery on the next poll");
    hideFileIdentity = false;
}
}
int main() {
    try {
        settingsReadTest();
        HANDLE reader{}, writer{};
        require(CreatePipe(&reader, &writer, nullptr, 4096) != 0, "isolated preview pipe");
        og::OutputFramingPreview preview(reader);
        const auto send = [&](const std::string& line) {
            DWORD written{};
            require(WriteFile(writer, line.data(), DWORD(line.size()), &written, nullptr) &&
                    written == line.size(), "preview command written");
        };
        og::Config savedPreview, livePreview;
        savedPreview.reserve = {4, 4, 4, 4}; savedPreview.offsetX = 2;
        livePreview.eye = 1; livePreview.crop = {1, 2, 3, 4};
        send("F 13 13 13 13 31 6\n");
        preview.poll(1000); preview.apply(savedPreview, livePreview);
        require(livePreview.reserve.left == 13 && livePreview.offsetX == 31 && livePreview.offsetY == 6,
                "unsaved stdin framing applies immediately without file changes");
        require(livePreview.eye == 1 && livePreview.crop.left == 1,
                "preview cannot alter capture geometry");
        send("F 30 30 30 30 -100 100\nF 5 6 7 8 9 10\n");
        preview.poll(1100); preview.apply(savedPreview, livePreview);
        require(livePreview.reserve.left == 5 && livePreview.reserve.right == 6 && livePreview.offsetY == 10,
                "latest complete preview command wins");
        send("F 13 13 13 13 "); preview.poll(1200); preview.apply(savedPreview, livePreview);
        require(livePreview.reserve.left == 5, "partial command cannot mutate preview");
        send("NaN 0\nF 31 0 0 0 0 0\nF 0 0 0 0 0 0 trailing\n" + std::string(300, 'x') + "\n");
        preview.poll(1300); preview.apply(savedPreview, livePreview);
        require(livePreview.reserve.left == 5, "invalid, nonfinite and oversized commands are ignored");
        savedPreview.reserve = {17, 17, 17, 17};
        send("R\n"); require(preview.poll(1400), "reset requests fresh saved framing");
        preview.apply(savedPreview, livePreview);
        require(livePreview.reserve.left == 17 && livePreview.offsetX == 2, "discard restores latest saved framing");
        send("F 13 13 13 13 31 6\n"); preview.poll(1500);
        require(preview.poll(3500), "preview lease expiry requests saved framing");
        preview.apply(savedPreview, livePreview);
        require(livePreview.reserve.left == 17, "renderer failure expires transient preview");
        send("F 13 13 13 13 31 6\n"); preview.poll(3600);
        CloseHandle(writer);
        require(preview.poll(3601), "pipe disconnect requests saved framing");
        preview.apply(savedPreview, livePreview); CloseHandle(reader);
        require(livePreview.reserve.left == 17, "parent disconnect restores saved framing");
        Fixture file;
        file.save(framing(0));
        og::Config config;
        config.streaming = true; config.crop = {3, 4, 5, 6}; config.renderScale = 120;
        config.extraLeft = 8; config.extraRight = 9; config.eye = 1;
        config.width = 2560; config.height = 1440; config.fps = 90;
        config.mirror = true; config.smoothing = 79; config.pitchLock = 3.9;
        og::OutputFramingReload reload(file.path.wstring(), "lmu");
        reload.poll(config, 1000);
        file.save(framing(13, 31, 6) + "Mode=driving\nCropLeft=30\nRenderScale=70\n"
            "StreamEye=0\nOutputWidth=640\nOutputHeight=360\nOutputFps=30\n"
            "Mirror=0\nSmoothing=0\nPitchLock=100\nExtraLeft=0\nExtraRight=0\n");
        require(!reload.poll(config, 1499) && config.reserve.left == 0,
                "file reads are bounded to twice per second");
        require(reload.poll(config, 1500) && config.reserve.left == 13 &&
                config.reserve.right == 13 && config.reserve.top == 13 &&
                config.reserve.bottom == 13 && config.offsetX == 31 && config.offsetY == 6,
                "saved reserve and framing apply to running output without restart");
        require(config.streaming && config.crop.left == 3 && config.crop.right == 4 &&
                config.crop.top == 5 && config.crop.bottom == 6 && config.renderScale == 120 &&
                config.extraLeft == 8 && config.extraRight == 9 && config.eye == 1 &&
                config.width == 2560 && config.height == 1440 && config.fps == 90 &&
                config.mirror && config.smoothing == 79 && config.pitchLock == 3.9,
                "live framing preserves startup capture geometry and other settings");
        uint64_t now = 2000;
        auto nonFinite = framing(30);
        nonFinite.replace(nonFinite.find("OffsetX=0"), 9, "OffsetX=NaN");
        for (auto bad : {std::string{}, std::string("[lmu]\nReserveLeft=30\n"),
                         nonFinite, framing(30) + "OffsetX=0\n", framing(31),
                         framing(30, 101), framing(30).substr(0, framing(30).size() - 1),
                         std::string("[iracing]\n") + framing(30).substr(6)}) {
            file.save(bad);
            require(!reload.poll(config, now) && config.reserve.left == 13 && config.offsetX == 31,
                    "empty, partial, invalid and wrong-game saves preserve last good framing");
            now += 500;
        }
        std::filesystem::remove(file.path);
        require(!reload.poll(config, now) && config.reserve.left == 13,
                "missing file preserves last good framing");
        file.save(framing(30, -100, 100));
        require(reload.poll(config, now + 500) && config.reserve.left == 30 &&
                config.offsetX == -100 && config.offsetY == 100,
                "valid atomic save recovers after missing or rejected file");
        std::cout << "Saved output framing reload passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n'; return 1;
    }
}
