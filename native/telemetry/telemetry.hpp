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
    // Vehicle-local SI acceleration: lateral left, longitudinal forward, up.
    std::optional<double> lateralAccelMps2,longitudinalAccelMps2,verticalAccelMps2;
    std::optional<double> speedMps,rpm,throttle,brake,clutch,fuelLiters;
    std::optional<double> steeringRadians; // Positive counterclockwise as seen by the driver.
    std::optional<int> gear,lap,lapsCompleted;
    std::optional<double> lapTimeSeconds,lastLapSeconds,bestLapSeconds;
    std::optional<double> airTempC,trackTempC,rainIntensity,trackWetness;
    std::optional<double> sessionTimeSeconds,timeOfDaySeconds,solarAltitudeRadians;
    std::optional<int> carsNearby,carsVisible,carsNearbyRadiusMeters;
    std::optional<std::string> cloudCover,trackWetnessState,carsNearbyMethod,trackName,sessionId;
    std::optional<std::string> carId,carName;
    Flags flags;
    std::optional<bool> inPits,inCar;
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
struct SteeringSample {
    ReadStatus status{ReadStatus::Disconnected};
    std::uint64_t token{};
    double radians{};
};
// Reads only the active player's steering and freshness witnesses. Static
// iRacing descriptors are resolved once per schema or session-info revision.
class SteeringDecoder {
public:
    SteeringSample read(Game,const Memory&,const Memory*lock=nullptr);
private:
    bool configured_{};
    int update_{},varCount_{},varBase_{},rowLength_{},angleOffset_{-1},onTrackOffset_{-1};
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
