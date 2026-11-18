#pragma once
#include <windows.h>
#include <string>

namespace og {
// Only a contended initial handshake is retried. Invalid mappings, access
// failures and an existing live owner retain their original failure semantics.
class ChannelOpenRetry {
    std::string game_;
    ULONGLONG next_{};
public:
    bool pending() const { return !game_.empty(); }
    void clear() { game_.clear(); }
    void defer(const std::string& game) { game_=game; next_=GetTickCount64()+250; }
    template<class Open> bool poll(ULONGLONG now, Open open) {
        if (!pending() || now<next_) return false;
        std::string game;
        game.swap(game_);
        return open(game);
    }
};
}
