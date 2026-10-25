#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace og::telemetry {
enum class Game { IRacing, Lmu };
enum class ReadStatus { Valid, Disconnected, Busy, Invalid, Unsupported };
struct Flags {
    std::optional<bool> yellow,blue,green,white,checkered,red,black;
};
struct Data {
    std::optional<double> speedMps,rpm,throttle,brake,clutch,fuelLiters;
    std::optional<int> gear,lap,lapsCompleted;
    std::optional<double> lapTimeSeconds,lastLapSeconds,bestLapSeconds;
    Flags flags;
    std::optional<bool> inPits;
};
struct Sample {
    ReadStatus status{ReadStatus::Disconnected};
    std::uint64_t token{};
    Data data;
    const char *reason{"mapping-not-found"};
};
class Memory {
public:
    virtual ~Memory()=default;
    virtual std::size_t size() const=0;
    virtual bool copy(std::size_t offset, void *destination, std::size_t count) const=0;
};
class IRacingDecoder {
public:
    IRacingDecoder(); ~IRacingDecoder();
    Sample read(const Memory &);
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
class LmuDecoder {
public:
    LmuDecoder(); ~LmuDecoder();
    Sample read(const Memory &, const Memory *lock=nullptr);
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
struct Record {
    Game game{Game::IRacing};
    const char *status{"disconnected"};
    bool connected{},stale{},testData{};
    std::uint64_t sequence{},timestampMs{};
    std::optional<std::uint64_t> ageMs;
    Data data;
    const char *reason{"mapping-not-found"};
};
class Tracker {
public:
    Record observe(Game, const Sample &, std::uint64_t monotonicMs, std::uint64_t timestampMs,
                   bool testData=false);
    void reset();
private:
    bool seeded_{},advanced_{};
    std::uint64_t token_{},lastChange_{},sequence_{};
    Data lastData_;
};
std::string json_line(const Record &);
const char *game_name(Game);
} // namespace og::telemetry
