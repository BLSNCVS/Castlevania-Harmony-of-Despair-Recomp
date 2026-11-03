// harmony-recomp - first-run installer wizard (see installer_wizard.h)

#include "installer_wizard.h"

#include <algorithm>

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_video.h>
#include <fmt/format.h>
#include <imgui.h>

#include <rex/filesystem.h>
#include <rex/logging.h>

#include "fonts.h"
#include "menu_common.h"

namespace fs = std::filesystem;
namespace install = hod::install;

namespace hod::ui {
namespace {

using palette::kAccent;
using palette::kAccentHover;
using palette::kDim;
using palette::kGold;
using palette::kGood;
using palette::kPanel;
using palette::kWarn;

constexpr float kWizardWidth = 820.0f;
constexpr float kWizardHeight = 560.0f;

std::string HumanBytes(uint64_t bytes) {
  if (bytes >= (1ull << 30)) return fmt::format("{:.1f} GB", double(bytes) / double(1ull << 30));
  if (bytes >= (1ull << 20)) return fmt::format("{:.0f} MB", double(bytes) / double(1ull << 20));
  return fmt::format("{} KB", bytes >> 10);
}

void CenteredText(ImFont* font, const ImVec4& color, const char* text) {
  ScopedFont scoped(font);
  float width = ImGui::CalcTextSize(text).x;
  ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) * 0.5f);
  ImGui::TextColored(color, "%s", text);
}

void Heading(const char* text, const ImVec4& color = kGold) {
  ScopedFont scoped(GetFonts().heading);
  ImGui::TextColored(color, "%s", text);
  ImGui::Spacing();
}

void CopyToBuffer(char* buffer, size_t size, const std::string& text) {
  size_t n = std::min(size - 1, text.size());
  std::copy_n(text.data(), n, buffer);
  buffer[n] = '\0';
}

}  // namespace

InstallerWizard::InstallerWizard(rex::ui::ImGuiDrawer* drawer, Options options, FinishFn on_finish)
    : ImGuiDialog(drawer), options_(std::move(options)), on_finish_(std::move(on_finish)) {
  CopyToBuffer(game_dir_input_, sizeof(game_dir_input_), rex::path_to_utf8(options_.game_dir));
  if (options_.installed) page_ = Page::kSources;
}

InstallerWizard::~InstallerWizard() {
  progress_.Cancel();
  scan_cancel_ = true;
  if (install_thread_.joinable()) install_thread_.join();
  if (scan_thread_.joinable()) scan_thread_.join();
}

// --- Background work ---------------------------------------------------------

void InstallerWizard::OpenFileDialog(bool folders) {
  // SDL may call back on another thread, and after this dialog is gone; the
  // callback only touches the shared state it keeps alive itself.
  auto* request = new std::shared_ptr<Shared>(shared_);
  auto callback = [](void* userdata, const char* const* files, int) {
    std::unique_ptr<std::shared_ptr<Shared>> owned(static_cast<std::shared_ptr<Shared>*>(userdata));
    auto& shared = **owned;
    std::lock_guard lock(shared.mutex);
    if (!files) {
      shared.dialog_error = SDL_GetError();
      return;
    }
    for (; *files; ++files) shared.picked.emplace_back(rex::to_path(*files));
  };
  int window_count = 0;
  SDL_Window** windows = SDL_GetWindows(&window_count);
  SDL_Window* parent = (windows && window_count > 0) ? windows[0] : nullptr;
  SDL_free(windows);
  if (folders) {
    SDL_ShowOpenFolderDialog(callback, request, parent, nullptr, true);
  } else {
    SDL_ShowOpenFileDialog(callback, request, parent, nullptr, 0, nullptr, true);
  }
}

void InstallerWizard::QueueScan(fs::path path) {
  if (!path.empty()) scan_queue_.push_back(std::move(path));
}

void InstallerWizard::PumpScans() {
  {
    std::lock_guard lock(shared_->mutex);
    for (auto& picked : shared_->picked) scan_queue_.push_back(std::move(picked));
    shared_->picked.clear();
  }
  if (scan_thread_.joinable() && scan_done_) {
    scan_thread_.join();
    sources_ = std::move(scan_result_);
    scanning_path_.clear();
    // Keep earlier choices; newly found DLC starts selected.
    dlc_selected_.resize(sources_.dlc.size(), true);
  }
  if (!scan_thread_.joinable() && !scan_queue_.empty()) {
    auto path = std::move(scan_queue_.front());
    scan_queue_.erase(scan_queue_.begin());
    scanning_path_ = rex::path_to_utf8(path);
    scan_done_ = false;
    scan_thread_ = std::thread([this, path, sources = sources_]() mutable {
      install::AddSource(sources, path, &scan_cancel_);
      scan_result_ = std::move(sources);
      scan_done_ = true;
    });
  }
}

void InstallerWizard::StartInstall() {
  install::Request request;
  request.sources = sources_;
  request.sources.dlc.clear();
  for (size_t i = 0; i < sources_.dlc.size(); ++i) {
    if (dlc_selected_[i]) request.sources.dlc.push_back(sources_.dlc[i]);
  }
  request.game_dir = rex::to_path(game_dir_input_);
  request.user_data_dir = options_.user_data_dir;
  request.install_title_update = install_title_update_;
  // Adding DLC to an existing install: don't reinstall the game.
  if (options_.installed && !sources_.has_game()) {
    request.sources.game_dir = request.game_dir;
  }
  install_done_ = false;
  page_ = Page::kInstalling;
  install_thread_ = std::thread([this, request = std::move(request)]() {
    install_result_ = install::Install(request, &progress_);
    install_done_ = true;
  });
}

// --- Drawing -----------------------------------------------------------------

void InstallerWizard::DrawBackground(ImGuiIO& io) {
  auto* bg = ImGui::GetBackgroundDrawList();
  bg->AddRectFilledMultiColor(ImVec2(0, 0), io.DisplaySize, palette::kBackgroundTop, palette::kBackgroundTop,
                              palette::kBackgroundBottom, palette::kBackgroundBottom);
  // A faint vertical band of light, like a window in a dark hall.
  float cx = io.DisplaySize.x * 0.5f;
  bg->AddRectFilledMultiColor(ImVec2(cx - 260, 0), ImVec2(cx, io.DisplaySize.y),
                              IM_COL32(120, 40, 30, 0), IM_COL32(120, 40, 30, 28),
                              IM_COL32(120, 40, 30, 8), IM_COL32(120, 40, 30, 0));
  bg->AddRectFilledMultiColor(ImVec2(cx, 0), ImVec2(cx + 260, io.DisplaySize.y),
                              IM_COL32(120, 40, 30, 28), IM_COL32(120, 40, 30, 0),
                              IM_COL32(120, 40, 30, 0), IM_COL32(120, 40, 30, 8));
}

void InstallerWizard::DrawSteps() {
  static const char* kSteps[] = {"Welcome", "Game files", "Options", "Install", "Done"};
  ScopedFont scoped(GetFonts().caption);
  int current = int(page_);
  for (int i = 0; i < 5; ++i) {
    if (i) {
      ImGui::SameLine();
      ImGui::TextColored(kDim, "  >  ");
      ImGui::SameLine();
    }
    ImGui::TextColored(i == current ? kGold : kDim, "%d  %s", i + 1, kSteps[i]);
  }
  ImGui::Separator();
  ImGui::Spacing();
}

InstallerWizard::Footer InstallerWizard::FooterButtons(const char* back, const char* next,
                                                      bool next_enabled) {
  float button_width = 150.0f;
  float y = kWizardHeight - ImGui::GetFrameHeightWithSpacing() - ImGui::GetStyle().WindowPadding.y;
  ImGui::SetCursorPosY(y);
  Footer pressed = Footer::kNone;
  if (back) {
    if (ImGui::Button(back, ImVec2(button_width, 0))) pressed = Footer::kBack;
    ImGui::SameLine();
  }
  if (next) {
    ImGui::SetCursorPosX(kWizardWidth - button_width - ImGui::GetStyle().WindowPadding.x);
    ImGui::BeginDisabled(!next_enabled);
    if (ImGui::Button(next, ImVec2(button_width, 0))) pressed = Footer::kNext;
    ImGui::EndDisabled();
  }
  return pressed;
}

void InstallerWizard::DrawWelcome() {
  ImGui::Dummy(ImVec2(0, 12));
  CenteredText(GetFonts().title, kGold, "Harmony Recomp");
  CenteredText(GetFonts().body, kDim, "Castlevania: Harmony of Despair, recompiled for PC");
  ImGui::Dummy(ImVec2(0, 24));
  ImGui::TextWrapped(
      "Before playing, the game has to be installed from your own copy of the Xbox 360 "
      "version. Nothing from the game is included with this program.");
  ImGui::Spacing();
  ImGui::TextWrapped("You will need the files you copied off your console:");
  ImGui::BulletText("the game package (from Content\\0000000000000000\\58410A7A\\000D0000)");
  ImGui::BulletText("optionally, the title update and any DLC you own");
  ImGui::Spacing();
  ImGui::TextWrapped(
      "You can select the package files directly, or just pick a folder that contains them "
      "in any layout. The installer recognizes each file by its contents, not its name.");
  switch (FooterButtons("Quit", "Next")) {
    case Footer::kBack:
      on_finish_(false);
      break;
    case Footer::kNext:
      page_ = Page::kSources;
      break;
    case Footer::kNone:
      break;
  }
}

void InstallerWizard::DrawSources() {
  Heading("Where are your game files?");
  bool busy = scan_thread_.joinable() || !scan_queue_.empty();
  if (ImGui::Button("Add files...")) OpenFileDialog(false);
  ImGui::SameLine();
  if (ImGui::Button("Add folder...")) OpenFileDialog(true);
  ImGui::SameLine();
  ImGui::BeginDisabled(busy);
  if (ImGui::Button("Clear")) {
    sources_ = {};
    dlc_selected_.clear();
  }
  ImGui::EndDisabled();
  ImGui::SetNextItemWidth(kWizardWidth - 140.0f);
  bool entered = ImGui::InputTextWithHint("##path", "...or type a file or folder path",
                                          path_input_, sizeof(path_input_),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  if (ImGui::Button("Add", ImVec2(80, 0)) || entered) {
    QueueScan(rex::to_path(path_input_));
    path_input_[0] = '\0';
  }
  {
    std::lock_guard lock(shared_->mutex);
    if (!shared_->dialog_error.empty()) {
      if (!dialog_error_logged_) {
        REXLOG_WARN("Native file dialog unavailable: {}", shared_->dialog_error);
        dialog_error_logged_ = true;
      }
      ImGui::TextColored(kWarn, "No file browser on this system. Type the path above instead.");
    }
  }
  ImGui::Spacing();
  ImGui::Separator();

  ImGui::BeginChild("found", ImVec2(0, kWizardHeight - 290.0f));
  if (sources_.game) {
    ImGui::TextColored(kGood, "Game");
    ImGui::SameLine(140);
    ImGui::TextUnformatted(sources_.game->display_name.c_str());
  } else if (sources_.game_dir) {
    ImGui::TextColored(kGood, "Game");
    ImGui::SameLine(140);
    ImGui::Text("extracted folder %s", rex::path_to_utf8(*sources_.game_dir).c_str());
  } else if (options_.installed) {
    ImGui::TextColored(kGood, "Game");
    ImGui::SameLine(140);
    ImGui::TextUnformatted("already installed");
  } else {
    ImGui::TextColored(kWarn, "Game");
    ImGui::SameLine(140);
    ImGui::TextUnformatted("not found yet");
  }
  for (const auto& tu : sources_.title_updates) {
    ImGui::TextColored(kGood, "Title update");
    ImGui::SameLine(140);
    ImGui::TextUnformatted(tu.display_name.c_str());
  }
  for (const auto& dlc : sources_.dlc) {
    ImGui::TextColored(kGood, "DLC");
    ImGui::SameLine(140);
    ImGui::TextUnformatted(dlc.display_name.c_str());
  }
  if (!sources_.ignored.empty()) {
    ImGui::TextColored(kDim, "%zu other package(s) ignored (different game or content type)",
                       sources_.ignored.size());
  }
  for (const auto& warning : sources_.warnings) {
    ImGui::TextColored(kWarn, "%s", warning.c_str());
  }
  if (busy) {
    ImGui::TextColored(kDim, "Scanning %s ...", scanning_path_.c_str());
  }
  ImGui::EndChild();

  bool ready = !busy && (sources_.has_game() || (options_.installed && !sources_.dlc.empty()));
  switch (FooterButtons(options_.installed ? "Quit" : "Back", "Next", ready)) {
    case Footer::kBack:
      if (options_.installed) {
        on_finish_(true);  // Nothing to add; carry on with the existing install.
      } else {
        page_ = Page::kWelcome;
      }
      break;
    case Footer::kNext:
      page_ = Page::kOptions;
      break;
    case Footer::kNone:
      break;
  }
}

void InstallerWizard::DrawOptions() {
  Heading("Installation options");
  if (sources_.has_game()) {
    ImGui::TextUnformatted("Install the game to:");
    ImGui::SetNextItemWidth(kWizardWidth - 40.0f);
    ImGui::InputText("##gamedir", game_dir_input_, sizeof(game_dir_input_));
    ImGui::Spacing();
  }
  if (const auto* tu = sources_.best_title_update()) {
    ImGui::Checkbox("Apply the title update", &install_title_update_);
    ImGui::SameLine();
    ImGui::TextColored(kDim, "(%s)", tu->display_name.c_str());
  }
  if (!sources_.dlc.empty()) {
    ImGui::Spacing();
    ImGui::TextUnformatted("Downloadable content:");
    ImGui::BeginChild("dlc", ImVec2(0, kWizardHeight - 330.0f), ImGuiChildFlags_Borders);
    for (size_t i = 0; i < sources_.dlc.size(); ++i) {
      bool selected = dlc_selected_[i];
      if (ImGui::Checkbox(fmt::format("{}##dlc{}", sources_.dlc[i].display_name, i).c_str(),
                          &selected)) {
        dlc_selected_[i] = selected;
      }
    }
    ImGui::EndChild();
  }
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Text, kDim);
  ImGui::TextWrapped("DLC and saves are kept in %s", rex::path_to_utf8(options_.user_data_dir).c_str());
  ImGui::PopStyleColor();
  switch (FooterButtons("Back", "Install", game_dir_input_[0] != '\0')) {
    case Footer::kBack:
      page_ = Page::kSources;
      break;
    case Footer::kNext:
      StartInstall();
      break;
    case Footer::kNone:
      break;
  }
}

void InstallerWizard::DrawInstalling() {
  Heading("Installing");
  ImGui::TextUnformatted(progress_.step().c_str());
  ImGui::ProgressBar(progress_.fraction(), ImVec2(-1, 28),
                     fmt::format("{} / {}", HumanBytes(progress_.done()),
                                 HumanBytes(progress_.total()))
                         .c_str());
  if (install_done_) {
    install_thread_.join();
    page_ = Page::kDone;
  }
  if (FooterButtons(nullptr, "Cancel", !progress_.cancelled()) == Footer::kNext) {
    progress_.Cancel();
  }
}

void InstallerWizard::DrawDone() {
  const auto& result = install_result_;
  if (!result.ok) {
    Heading("Installation did not finish", kWarn);
    ImGui::TextWrapped("%s", result.error.c_str());
    if (FooterButtons(nullptr, "Back", true) == Footer::kNext) {
      progress_.Clear();
      page_ = Page::kSources;
    }
    return;
  }
  Heading("Ready to play", kGood);
  ImGui::TextWrapped("Game installed to %s", game_dir_input_);
  if (result.title_update) ImGui::Text("Title update: %s", result.title_update->c_str());
  if (!result.dlc.empty()) {
    ImGui::Text("DLC installed:");
    for (const auto& name : result.dlc) ImGui::BulletText("%s", name.c_str());
  }
  ImGui::Spacing();
  ImGui::TextColored(kDim, "default.xex %s", result.xex_sha256.c_str());
  if (!result.xex_known) {
    ImGui::TextColored(kWarn, "This dump hasn't been verified with this build yet.");
  }
  switch (FooterButtons("Quit", "Play")) {
    case Footer::kBack:
      on_finish_(false);
      break;
    case Footer::kNext:
      on_finish_(true);
      break;
    case Footer::kNone:
      break;
  }
}

void InstallerWizard::OnDraw(ImGuiIO& io) {
  PumpScans();
  DrawBackground(io);

  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                          ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(kWizardWidth, kWizardHeight), ImGuiCond_Always);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, kPanel);
  ImGui::PushStyleColor(ImGuiCol_Border, kAccent);
  ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kAccentHover);
  ImGui::PushStyleColor(ImGuiCol_CheckMark, kGold);
  ImGui::PushStyleColor(ImGuiCol_Separator, kAccent);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.10f, 0.11f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.24f, 0.13f, 0.15f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.03f, 0.04f, 0.6f));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.05f, 0.03f, 0.04f, 0.6f));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, kAccent);
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, kAccentHover);
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, kGold);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 22));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::Begin("##installer", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
  {
    ScopedFont body(GetFonts().body);  // Popped before End().
    DrawSteps();
    switch (page_) {
      case Page::kWelcome:
        DrawWelcome();
        break;
      case Page::kSources:
        DrawSources();
        break;
      case Page::kOptions:
        DrawOptions();
        break;
      case Page::kInstalling:
        DrawInstalling();
        break;
      case Page::kDone:
        DrawDone();
        break;
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor(14);
}

}  // namespace hod::ui
