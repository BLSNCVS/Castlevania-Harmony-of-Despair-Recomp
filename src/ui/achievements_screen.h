// harmony-recomp - achievements screen
//
// Lists the game's achievements with their icons, in the options menu's
// style. It opens from the game's own Achievements menu entry (the game calls
// XamShowAchievementsUI, which on a console opens the guide) and from the
// options menu. Unlocks are tracked by the SDK and saved in the user data
// folder (achievements/<title id>.toml).

#pragma once

#include <functional>

#include <rex/ui/imgui_dialog.h>
#include <rex/ui/overlay/achievement_icon_cache.h>

#include "menu_common.h"

namespace rex {
class Runtime;
}  // namespace rex
namespace rex::system {
class AchievementManager;
}  // namespace rex::system
namespace rex::ui {
class ImmediateDrawer;
}  // namespace rex::ui

namespace hod::ui {

class AchievementsScreen : public rex::ui::ImGuiDialog {
 public:
  AchievementsScreen(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer,
                     rex::Runtime* runtime, rex::system::AchievementManager* achievements,
                     std::function<void()> on_close);

  void RequestClose() { close_requested_ = true; }

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  rex::system::AchievementManager* achievements_;
  rex::ui::AchievementIconCache icons_;
  std::function<void()> on_close_;
  MenuNavigator navigator_;
  int selected_ = 0;
  bool scroll_to_selected_ = true;
  bool close_requested_ = false;
  bool closed_ = false;
};

}  // namespace hod::ui
