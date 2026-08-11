#include "report_service.hpp"

#include <memory>
#include <utility>

#include <SDL3/SDL.h>

#include "net/http.hpp"
#include "util/debug_log.hpp"
#include "util/png.hpp"

namespace ppc {

ReportService::~ReportService() { shutdown(); }

void ReportService::init(uint32_t done_event_type) { done_event_ = done_event_type; }

void ReportService::send(report::Report r, Capture shot) {
    if (busy_.exchange(true)) return;
    if (worker_.joinable()) worker_.join(); // reap the previous, already-finished worker

    state_ = ReportState::Sending;
    id_.clear();
    error_.clear();

    const uint32_t ev = done_event_;
    const std::string url = report::relay_url();
    debug::log("[report] sending to %s: %zu byte item, %zu byte parse, %zu byte comment, "
               "%dx%d screenshot",
               url.c_str(), r.item.size(), r.parse.size(), r.comment.size(), shot.w, shot.h);

    worker_ = std::thread([ev, url, r = std::move(r), shot = std::move(shot)]() mutable {
        auto* out = new Result{};
        if (!shot.empty()) {
            const std::vector<uint8_t> png = encode_png(shot.rgba.data(), shot.w, shot.h);
            r.png.assign(reinterpret_cast<const char*>(png.data()), png.size());
        }
        net::Request req;
        req.url = url;
        req.body = report::to_json(r);
        // A report carries a screenshot and can run to megabytes on a slow line, so this is well
        // past the eight seconds every other request here gets — a send that gives up under a
        // user watching it is worse than one that takes a while.
        req.timeout_ms = 30000;
        const net::Response resp = net::get(req);
        out->outcome = report::read_response(resp.status, resp.body, resp.error);
        SDL_Event e{};
        e.type = ev;
        e.user.data1 = out;
        if (!SDL_PushEvent(&e)) delete out; // queue full, or SDL is shutting down
    });
}

void ReportService::on_done(const SDL_Event& e) {
    std::unique_ptr<Result> r(static_cast<Result*>(e.user.data1));
    busy_ = false;
    if (!r) return;
    if (r->outcome.ok) {
        id_ = std::move(r->outcome.id);
        state_ = ReportState::Sent;
        debug::log("[report] accepted as %s", id_.c_str());
    } else {
        error_ = std::move(r->outcome.error);
        state_ = ReportState::Failed;
        debug::log("[report] refused: %s", error_.c_str());
    }
}

void ReportService::reset() {
    if (state_ == ReportState::Sending) return; // the answer is still coming; it has somewhere to land
    state_ = ReportState::Idle;
    id_.clear();
    error_.clear();
}

void ReportService::shutdown() {
    if (worker_.joinable()) worker_.join();
    if (!done_event_) return;

    SDL_Event drop[16];
    int n;
    while ((n = SDL_PeepEvents(drop, 16, SDL_GETEVENT, done_event_, done_event_)) > 0)
        for (int i = 0; i < n; ++i) delete static_cast<Result*>(drop[i].user.data1);

    done_event_ = 0;
}

} // namespace ppc
