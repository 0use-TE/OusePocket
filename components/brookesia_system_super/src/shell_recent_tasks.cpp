#include "private/shell_impl.hpp"
namespace esp_brookesia::system::super {
void System::on_app_snapshot(const core::AppInfo &app, const std::vector<std::string> &paths)
{
    if (!desktop_backend_ || app.app_id == shell_app_id_) return;
    auto result = desktop_backend_->capture_app_snapshot(app.app_id, paths, system_gui().get_theme() == "dark");
    if (!result) BROOKESIA_LOGW("App snapshot failed: id(%1%), error(%2%)", app.app_id, result.error());
    else BROOKESIA_LOGI("App snapshot saved: id(%1%)", app.app_id);
}
std::expected<void, std::string> ShellApp::show_recent_tasks(bool from_gesture)
{
    if (!context_ || !owner_.desktop_backend_) return std::unexpected("Screenshot desktop unavailable");
    recent_task_ids_.clear();
    std::vector<gui::lvgl::RecentTask> tasks;
    for (auto it = owner_.recent_app_ids_.rbegin(); it != owner_.recent_app_ids_.rend(); ++it) {
        auto app = owner_.get_app(*it);
        if (app && (app->state == core::AppState::Paused || app->state == core::AppState::Running)) {
            recent_task_ids_.push_back(app->app_id);
            tasks.push_back({app->app_id, get_app_display_name(*app), app->manifest.icon_path});
        }
        if (tasks.size() == 3) break;
    }
    const bool chinese = context_->gui().get_language() == "zh_CN";
    auto visible = context_->gui().set_binding_values({
        {"/overlay/recents/title", "text", chinese ? "最近任务" : "Recent apps"},
        {"/overlay/recents", "hidden", "false"}});
    if (!visible) return visible;
    recent_tasks_open_ = true;
    recent_tasks_ignore_release_ = from_gesture;
    ++recent_tasks_generation_;
    auto result = owner_.desktop_backend_->show_recent_tasks(tasks, owner_.recent_focus_app_id_,
        context_->gui().get_theme() == "dark", chinese, recent_tasks_generation_, from_gesture);
    if (!result) close_recent_tasks();
    BROOKESIA_LOGI("Screenshot recent tasks shown: count(%1%)", tasks.size());
    return result;
}
void ShellApp::close_recent_tasks()
{
    recent_tasks_open_ = false;
    ++recent_tasks_generation_;
    if (owner_.desktop_backend_) owner_.desktop_backend_->hide_recent_tasks();
    if (context_) (void)context_->gui().set_binding_value("/overlay/recents", "hidden", "true");
}
void ShellApp::handle_recent_action(const gui::Event &event)
{
    if (!recent_tasks_open_ || event.action != "ouse.recents.native" ||
        event.get_int64("generation") != static_cast<int64_t>(recent_tasks_generation_)) return;
    const auto operation = event.get_string("operation");
    if (operation == "close_all") {
        const auto tasks = recent_task_ids_;
        for (const auto id : tasks) {
            auto result = owner_.stop_app(id);
            if (!result) BROOKESIA_LOGW("Close all failed: %1%", result.error());
        }
        (void)owner_.go_home();
        return;
    }
    const auto value = event.get_int64("app_id");
    if (!value || *value <= 0) return;
    const auto id = static_cast<core::AppId>(*value);
    if (std::find(recent_task_ids_.begin(), recent_task_ids_.end(), id) == recent_task_ids_.end()) return;
    if (operation == "open") {
        close_recent_tasks();
        auto result = owner_.activate_app(id);
        if (!result) BROOKESIA_LOGW("Resume failed: %1%", result.error());
    } else if (operation == "close") {
        auto result = owner_.stop_app(id);
        if (!result) BROOKESIA_LOGW("Close failed: %1%", result.error());
        if (recent_task_ids_.size() <= 1 && result) (void)owner_.go_home();
        else (void)show_recent_tasks();
    }
}
}
