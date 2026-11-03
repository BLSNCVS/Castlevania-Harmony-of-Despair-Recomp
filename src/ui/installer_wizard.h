// harmony-recomp - first-run installer wizard
//
// Shown instead of the game until an installation exists. Walks the player
// through pointing at their own console packages and installs them with
// src/install/ on a worker thread.

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rex/ui/imgui_dialog.h>

#include "install/installer.h"

namespace hod::ui {

class InstallerWizard : public rex::ui::ImGuiDialog {
 public:
  struct Options {
    std::filesystem::path game_dir;       // Where the game gets installed.
    std::filesystem::path user_data_dir;  // Runtime user data root (DLC lands here).
    bool installed = false;               // An installation already exists (adding DLC).
  };
  // Called on the UI thread when the player presses Play (true) or Quit (false).
  using FinishFn = std::function<void(bool play)>;

  InstallerWizard(rex::ui::ImGuiDrawer* drawer, Options options, FinishFn on_finish);
  ~InstallerWizard() override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  enum class Page { kWelcome, kSources, kOptions, kInstalling, kDone };
  enum class Footer { kNone, kBack, kNext };

  // Results handed over from file dialogs and worker threads.
  struct Shared {
    std::mutex mutex;
    std::vector<std::filesystem::path> picked;  // From the native file dialogs.
    std::string dialog_error;
  };

  void DrawBackground(ImGuiIO& io);
  void DrawSteps();
  void DrawWelcome();
  void DrawSources();
  void DrawOptions();
  void DrawInstalling();
  void DrawDone();
  Footer FooterButtons(const char* back, const char* next, bool next_enabled = true);

  void OpenFileDialog(bool folders);
  void QueueScan(std::filesystem::path path);
  void PumpScans();
  void StartInstall();

  Options options_;
  FinishFn on_finish_;
  Page page_ = Page::kWelcome;
  std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();

  hod::install::Sources sources_;
  std::vector<bool> dlc_selected_;
  bool install_title_update_ = true;
  char path_input_[1024] = {};
  bool dialog_error_logged_ = false;
  char game_dir_input_[1024] = {};

  // Folder/package scanning runs off the UI thread; one scan at a time.
  std::vector<std::filesystem::path> scan_queue_;
  std::thread scan_thread_;
  std::atomic<bool> scan_done_{false};
  std::atomic<bool> scan_cancel_{false};  // set when the wizard closes mid-scan
  hod::install::Sources scan_result_;
  std::string scanning_path_;

  // Installation.
  std::thread install_thread_;
  hod::install::Progress progress_;
  std::atomic<bool> install_done_{false};
  hod::install::Result install_result_;
};

}  // namespace hod::ui
