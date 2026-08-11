#pragma once

namespace ppc {
class App;

/// The bug reporter: everything a report would send, laid out to be read before it is sent, plus
/// the one box the user can write in. Also draws the confirmation the send leaves behind, which
/// is `Screen::ReportSent` and a window of its own — the dialog closes first, so there is nothing
/// left to draw it over.
void draw_report_screen(App& app);

} // namespace ppc
