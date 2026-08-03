#include "screens/pricecheck_screen.hpp"

#include <memory>
#include <string>

#include <imgui.h>

#include "app.hpp"

namespace ppc {

void draw_pricecheck_screen(App& app) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Price check", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

    ImGui::PushFont(app.fonts().bold, 0.0f);
    ImGui::TextUnformatted("Copied item (clipboard)");
    ImGui::PopFont();
    ImGui::SameLine(ImGui::GetWindowWidth() - 34);
    if (ImGui::Button("X", ImVec2(24, 0))) app.close_overlay();
    ImGui::Separator();

    // Copy the snapshot once: the updater can swap the bundle in from its own thread
    // between two reads, and the second would dangle.
    const std::shared_ptr<data::GameData> gd = app.game_data();
    if (!gd) {
        const data::DataUpdater::Status st = app.data_status();
        if (st.state == data::DataUpdater::State::Failed)
            ImGui::TextColored(ImVec4(0.90f, 0.55f, 0.25f, 1.0f),
                               "Pricing data unavailable (%s)", st.error.c_str());
        else if (st.bytes_total)
            ImGui::TextDisabled("Pricing data is downloading (%.1f / %.1f MB)\xe2\x80\xa6",
                                st.bytes_done / 1e6, st.bytes_total / 1e6);
        else
            ImGui::TextDisabled("Pricing data is downloading\xe2\x80\xa6");
        ImGui::Separator();
    }

    const std::string& clip = app.clipboard_text();
    if (app.copy_late()) {
        // Still watching — the game's clipboard handover sometimes lands seconds late.
        ImGui::TextDisabled("Waiting for the game to hand over the clipboard\xe2\x80\xa6");
    } else if (app.copying()) {
        ImGui::TextDisabled("Copying item\xe2\x80\xa6");
    } else if (clip.empty()) {
        ImGui::TextDisabled("Clipboard is empty. Hover an item in-game and press the price-check hotkey.");
    } else {
        // Item text in small caps, the way the game renders it. Wrapped: the panel is a
        // narrow full-height dock and long mod lines would otherwise run off the edge.
        ImGui::PushFont(app.fonts().small_caps, 0.0f);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(clip.c_str(), clip.c_str() + clip.size());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }

    ImGui::End();
}

} // namespace ppc
