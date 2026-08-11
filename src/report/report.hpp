#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "item/derive.hpp"
#include "item/item.hpp"
#include "item/plan.hpp"

/// A bug report about a price check, and the one endpoint it is sent to.
///
/// **The whole layer is text.** What leaves the machine is the clipboard capture the user was
/// looking at, what this tool made of it, whatever they wrote, four short version strings and —
/// only if they tick the box — a picture of our own window. There is no identifier of any kind
/// and nothing is kept locally, which is why the dialog can honestly show the payload whole
/// before it is sent: what is on screen there *is* the request body.
///
/// Pure, and part of `ppc_core`: the request itself is `ReportService`'s.
namespace ppc::report {

/// The relay's own cap, mirrored here so the box can refuse the 2001st character rather than
/// letting the send come back 400. See `worker/README.md`.
inline constexpr size_t kCommentMax = 2000;

/// Where a report goes: a Cloudflare Worker that holds the Discord webhook as a secret, so the
/// only thing shipped in the binary is a public, rate-limited URL. `$PPC_REPORT_URL` overrides it
/// for anyone running their own relay, and for testing this without posting into the real channel.
std::string relay_url();

/// The four short strings that say which build produced the report. Nothing here identifies a
/// machine or a person; each is capped at 64 characters by the relay.
struct Meta {
    std::string version; ///< the app's own
    std::string os;
    std::string league;
    std::string bundle; ///< the data bundle's version, which is half of any mispricing
};

struct Report {
    std::string item;    ///< the clipboard capture, verbatim and unedited
    std::string parse;   ///< what this tool made of it — `describe`
    std::string comment; ///< what the user wrote, or empty
    Meta meta;
    std::string png; ///< the screenshot's bytes, empty unless the user attached one
};

/// The request body, exactly as it goes on the wire. The screenshot is base64 here and nowhere
/// else — `Report::png` is the bytes, because the dialog previews the picture and not the
/// encoding of it.
std::string to_json(const Report& r);

/// What this tool made of the item, as the text a maintainer needs in front of them to tell a
/// parse bug from a pricing one: what was read off the clipboard, what it resolved to in the
/// bundle, which modifier matched which stat record — **and which matched none** — and what the
/// search would have asked for.
///
/// Written for a human reading it in a Discord post, so it is plain lines rather than JSON: the
/// question it has to answer at a glance is "which line went wrong", and a nested structure
/// answers that worse.
std::string describe(const item::Item& it, const item::Derived& d, const item::SearchPlan& plan);

/// The relay's answer to a report. `id` is what the user is shown and what names the forum post,
/// so it is the one thing worth repeating back.
struct Outcome {
    bool ok = false;
    std::string id;
    std::string error; ///< why not, in words fit to put in front of a user
};

/// Read a response. `status` is 0 for a request that never completed, and `transport` is then
/// curl's own message. Anything the relay refuses comes back with its reason in the body.
Outcome read_response(long status, const std::string& body, const std::string& transport);

} // namespace ppc::report
