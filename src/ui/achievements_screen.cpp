// harmony-recomp - achievements screen (see achievements_screen.h)

#include "achievements_screen.h"

#include <algorithm>
#include <vector>
#include <string>

#include <imgui.h>

#include <rex/system/achievement_manager.h>

namespace hod::ui {
namespace {

constexpr float kIconSize = 64.0f;
constexpr float kRowHeight = 82.0f;

}  // namespace

AchievementsScreen::AchievementsScreen(rex::ui::ImGuiDrawer* drawer,
                                       rex::ui::ImmediateDrawer* immediate_drawer,
                                       rex::Runtime* runtime,
                                       rex::system::AchievementManager* achievements,
                                       std::function<void()> on_close)
    : ImGuiDialog(drawer),
      achievements_(achievements),
      icons_(immediate_drawer, runtime),
      on_close_(std::move(on_close)) {}

void AchievementsScreen::OnDraw(ImGuiIO& io) {
  if (closed_) return;
  const auto list = achievements_->ListAchievements();
  const int count = int(list.size());

  switch (navigator_.Read()) {
    case Nav::kUp:
    case Nav::kLeft:
      if (count) selected_ = (selected_ + count - 1) % count;
      scroll_to_selected_ = true;
      break;
    case Nav::kDown:
    case Nav::kRight:
      if (count) selected_ = (selected_ + 1) % count;
      scroll_to_selected_ = true;
      break;
    case Nav::kBack:
      close_requested_ = true;
      break;
    default:
      break;
  }
  if (close_requested_) {
    closed_ = true;
    if (on_close_) on_close_();
    return;
  }
  selected_ = std::clamp(selected_, 0, std::max(0, count - 1));

  int unlocked = 0;
  int score = 0;
  int total_score = 0;
  std::vector<bool> is_unlocked(list.size());
  for (size_t i = 0; i < list.size(); ++i) {
    total_score += int(list[i].gamerscore);
    if (achievements_->IsUnlocked(list[i].id)) {
      is_unlocked[i] = true;
      ++unlocked;
      score += int(list[i].gamerscore);
    }
  }

  Panel panel("##achievements", io);
  PanelHeading("Achievements");
  ImGui::TextColored(palette::kText, "%d of %d unlocked", unlocked, count);
  ImGui::SameLine();
  ImGui::TextColored(palette::kGold, "   %d / %d G", score, total_score);
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram, palette::kAccent);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, palette::kAccentSoft);
  ImGui::ProgressBar(count ? float(unlocked) / float(count) : 0.0f, ImVec2(-1.0f, 6.0f), "");
  ImGui::PopStyleColor(2);
  ImGui::Spacing();

  ImGui::BeginChild("list", ImVec2(0, kPanelHeight - 190.0f));
  if (count == 0) {
    ImGui::TextColored(palette::kDim, "The game's achievement list could not be read.");
  }
  for (int i = 0; i < count; ++i) {
    const auto& a = list[i];
    ImGui::PushID(i);
    ImVec2 row = ImGui::GetCursorPos();
    if (ImGui::Selectable("##row", i == selected_, ImGuiSelectableFlags_AllowOverlap,
                          ImVec2(0, kRowHeight))) {
      selected_ = i;
    }
    if (i == selected_ && scroll_to_selected_) {
      ImGui::SetScrollHereY(0.5f);
      scroll_to_selected_ = false;
    }

    ImGui::SetCursorPos(ImVec2(row.x + 10, row.y + (kRowHeight - kIconSize) * 0.5f));
    if (auto* icon = icons_.GetIcon(a)) {
      ImVec4 tint = is_unlocked[i] ? ImVec4(1, 1, 1, 1) : ImVec4(0.35f, 0.35f, 0.35f, 1);
      ImGui::ImageWithBg(reinterpret_cast<ImTextureID>(icon), ImVec2(kIconSize, kIconSize),
                         ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tint);
    } else {
      ImGui::Dummy(ImVec2(kIconSize, kIconSize));
    }

    float text_x = row.x + kIconSize + 28.0f;
    float width = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPos(ImVec2(text_x, row.y + 10));
    ImGui::TextColored(is_unlocked[i] ? palette::kGold : palette::kText, "%s", a.label.c_str());
    std::string points = std::to_string(a.gamerscore) + " G";
    ImGui::SetCursorPos(ImVec2(row.x + width - ImGui::CalcTextSize(points.c_str()).x - 16,
                               row.y + 10));
    ImGui::TextColored(is_unlocked[i] ? palette::kGold : palette::kDim, "%s", points.c_str());

    ImGui::SetCursorPos(ImVec2(text_x, row.y + 40));
    const std::string& description = is_unlocked[i] || a.unachieved_description.empty()
                                         ? a.description
                                         : a.unachieved_description;
    ImGui::PushTextWrapPos(row.x + width - 16);
    ImGui::TextColored(palette::kDim, "%s%s", is_unlocked[i] ? "" : "Locked  \xC2\xB7  ",
                       description.c_str());
    ImGui::PopTextWrapPos();

    // Move below the row and submit an item there, as ImGui requires when
    // the cursor is used to lay out overlapping widgets.
    ImGui::SetCursorPos(ImVec2(row.x, row.y + kRowHeight + 4));
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::PopID();
  }
  ImGui::EndChild();

  PanelFooter("Up/Down  browse     B / Esc  close");
}

}  // namespace hod::ui
