#include "screens/pricecheck_screen.hpp"

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

    ImGui::TextUnformatted("Copied item (clipboard)");
    ImGui::SameLine(ImGui::GetWindowWidth() - 34);
    if (ImGui::Button("X", ImVec2(24, 0))) app.close_overlay();
    ImGui::Separator();

    const std::string& clip = app.clipboard_text();
    if (clip.empty())
        ImGui::TextDisabled("Clipboard is empty. Hover an item in-game and press the price-check hotkey.");
    else
        ImGui::TextUnformatted(clip.c_str(), clip.c_str() + clip.size());

    ImGui::End();
}

} // namespace ppc
