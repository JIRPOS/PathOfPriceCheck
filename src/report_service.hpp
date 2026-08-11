#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "capture.hpp"
#include "report/report.hpp"

union SDL_Event;

namespace ppc {

enum class ReportState { Idle, Sending, Sent, Failed };

/// Sends one bug report to the relay, off the UI thread.
///
/// Shaped like `LeagueService` and for the same reasons: every member is touched on the main
/// thread only, and the worker owns nothing but its own stack and the heap payload it hands back
/// through the SDL event queue.
///
/// **Deliberately not through `trade::request`.** The relay is our own host with our own rules;
/// the shared rate limiter exists for GGG's policy and nothing else belongs behind it. What keeps
/// this endpoint from being hammered is the relay's own per-IP cap and the fact that a report is
/// a thing a person types.
class ReportService {
public:
    struct Result {
        report::Outcome outcome;
    };

    ~ReportService();

    void init(uint32_t done_event_type); ///< after SDL_RegisterEvents
    void shutdown();                     ///< join the worker, drain unconsumed events; before SDL_Quit

    /// Send it, attaching `shot` when it is not empty.
    ///
    /// The screenshot arrives as raw pixels rather than as a PNG: encoding it, base64ing it and
    /// serialising the body are together most of a second on a 4K panel, and all three belong on
    /// the worker rather than in the frame that drew the button. Ignored while one is already in
    /// flight — the dialog disables its own button, and this is the guard behind that.
    void send(report::Report r, Capture shot);

    void on_done(const SDL_Event& e);

    ReportState state() const { return state_; }
    /// The relay's id for the last accepted report, which is what the user is shown and what
    /// names the forum post it became.
    const std::string& id() const { return id_; }
    const std::string& error() const { return error_; }
    /// Back to Idle, so the dialog can be reopened without last time's answer still on it.
    void reset();

private:
    uint32_t done_event_ = 0;
    std::thread worker_;
    std::atomic<bool> busy_{false};
    ReportState state_ = ReportState::Idle;
    std::string id_;
    std::string error_;
};

} // namespace ppc
