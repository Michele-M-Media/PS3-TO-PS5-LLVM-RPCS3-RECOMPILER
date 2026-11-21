#include "stdafx.h"
#include "main_application.h"
#include "Emu/System.h"
#include "Emu/IdManager.h"
#include "Emu/Audio/audio_utils.h"
#include "Emu/RSX/VK/VKGSRender.h"
#include "Emu/Io/Null/null_camera_handler.h"
#include "Emu/Io/Null/null_music_handler.h"
#include "Emu/Cell/Modules/cellMsgDialog.h"
#include "Emu/Cell/Modules/cellOskDialog.h"
#include "Emu/Cell/Modules/cellSaveData.h"
#include "Emu/Cell/Modules/sceNpTrophy.h"
#include "Emu/Cell/ErrorCodes.h"
#include "Emu/RSX/Overlays/overlay_manager.h"
#include "Emu/RSX/Overlays/overlay_trophy_notification.h"
#include "rpcs3qt/qt_music_handler.h"
#include "rpcs3qt/qt_video_source.h"
#include "rpcs3qt/gs_frame.h"
#include "rpcs3qt/gui_settings.h"
#include "rpcs3qt/msg_dialog_frame.h"
#include "rpcs3qt/osk_dialog_frame.h"
#include "rpcs3qt/save_data_dialog.h"
#include "rpcs3qt/localized_emu.h"
#include "util/video_source.h"
#include "Utilities/File.h"
#include "Loader/PSF.h"
#include "util/yaml.hpp"
#include "recompiler_cache_cleanup.h"
#include "recompiler_system_language.h"
#if __has_include("recompiler_title_update_manifest.h")
#include "recompiler_title_update.h"
#endif
#include "recompiler_trophy_notice.h"
#include <QApplication>
#include <QMetaObject>
#include <QScreen>
#include <QSoundEffect>
#include <QUrl>
#include <QTimer>
#include <ps5platform/heap.h>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>

namespace rpcs3::ps5 { void initialize_qt_platform(); void initialize_core_paths(); }
int recompiler_rpcs3_prepare_vfs(const char*, const char*);
extern "C" void catchReturnFromMain(int);
LOG_CHANNEL(sys_log, "SYS");
extern "C" void recompiler_log_display_mode(const char* line)
{
    sys_log.notice("%s", line);
}

atomic_t<bool> g_headless = true;
std::string g_input_config_override;

[[noreturn]] void report_fatal_error(std::string_view text, bool, bool)
{
    std::fprintf(stderr,"RECOMPILER_CORE_BLOCKED: %.*s\n",static_cast<int>(text.size()),text.data());
    std::fflush(stderr);
    catchReturnFromMain(71);
    // A rejected native close remains an explicit fatal fault.
    __builtin_trap();
}

extern "C" __attribute__((weak))
int sceKernelSendNotificationRequest(int device, recompiler_trophy::request* request, std::size_t bytes, int flags);

class recompiler_trophy_notification final : public TrophyNotificationBase
{
public:
    s32 ShowTrophyNotification(const SceNpTrophyDetails& trophy, const std::vector<uchar>& icon) override
    {
        if (!sceKernelSendNotificationRequest)
            return show_game_overlay(trophy, icon);

        // The guest unlock handler may still hold its trophy mutex.
        const int language = static_cast<int>(g_cfg.sys.language.get());
        Emu.CallFromMainThread([trophy, icon, language]()
        {
            const auto* end = std::find(std::begin(trophy.name), std::end(trophy.name), '\0');
            const std::string_view name(trophy.name, end - trophy.name);
            if (recompiler_trophy::submit(sceKernelSendNotificationRequest, name, language))
            {
                sys_log.notice("RECOMPILER_TROPHY_SYSTEM: notification accepted for trophy %d", static_cast<s32>(trophy.trophyId));
                return;
            }

            sys_log.warning("RECOMPILER_TROPHY_SYSTEM: using game overlay");
            show_game_overlay(trophy, icon);
        });
        return 0;
    }

private:
    static s32 show_game_overlay(const SceNpTrophyDetails& trophy, const std::vector<uchar>& icon)
    {
        auto manager = g_fxo->try_get<rsx::overlays::display_manager>();
        if (!manager)
        {
            sys_log.warning("RECOMPILER_TROPHY: overlay unavailable");
            return static_cast<s32>(CELL_ENOSYS);
        }

        auto popup = std::make_shared<rsx::overlays::trophy_notification>();
        const s32 result = manager->add(popup, false)->show(trophy, icon);
        if (result == 0)
            sys_log.notice("RECOMPILER_TROPHY: notification queued");
        return result;
    }
};

class recompiler_title_application final : public QApplication, public main_application
{
public:
    recompiler_title_application(int& argc, char** argv) : QApplication(argc,argv)
    {
        setApplicationName("PS3 TO PS5 LLVM RPCS3 RECOMPILER");
        setQuitOnLastWindowClosed(false);
    }

    QThread* get_thread() override { return thread(); }
    bool Init() override;

private:
    std::deque<std::unique_ptr<QSoundEffect>> m_sound_effects;
};

bool recompiler_title_application::Init()
{
    std::setlocale(LC_NUMERIC,"C");
    EmuCallbacks callbacks = CreateCallbacks();
    callbacks.call_from_main_thread = [this](std::function<void()> fn, atomic_t<u32>* wake)
    {
        auto call = [fn=std::move(fn),wake]() { fn(); if(wake) { *wake=true; wake->notify_one(); } };
        if(!QMetaObject::invokeMethod(this,std::move(call),Qt::AutoConnection))
            report_fatal_error("Native main-thread dispatch failed",false,false);
    };
    callbacks.try_to_quit = [this](bool force,std::function<void()> done)
    {
        if(!force) return false;
        if(done) done();
        quit();
        return true;
    };
    callbacks.init_gs_render = [](utils::serial* ar)
    {
        if(g_cfg.video.renderer != video_renderer::vulkan)
            report_fatal_error("This title build requires its native Vulkan backend",false,false);
        const char* target = g_cfg.video.frame_limit == frame_limit_type::_120 ? "119880" : "60000";
        if (::setenv("RPCS3_PS5_DISPLAY_TARGET_MILLIHZ", target, 1) != 0)
            sys_log.warning("RECOMPILER_DISPLAY_MODE: unable to apply profile target %s mHz", target);
        g_fxo->init<rsx::thread,named_thread<VKGSRender>>(ar);
    };
    callbacks.get_gs_frame = [this]() -> std::unique_ptr<GSFrameBase>
    {
        if(m_game_window)
            report_fatal_error("Unexpected second game window; lifecycle not verified",false,false);
        auto settings = std::make_shared<gui_settings>();
        QScreen* screen = primaryScreen();
        if (!screen)
            report_fatal_error("Native display unavailable", false, false);
        const QRect geometry = screen->geometry();
        if (geometry.isEmpty())
            report_fatal_error("Invalid native display geometry", false, false);
        std::fprintf(stderr, "RECOMPILER_BOOT_DISPLAY window=%dx%d fullscreen=1\n", geometry.width(), geometry.height());
        auto frame = std::make_unique<gs_frame>(screen, geometry, QIcon(), std::move(settings), true);
        m_game_window=frame.get();
        QObject::connect(m_game_window,&QObject::destroyed,this,[this](){m_game_window=nullptr;});
        return frame;
    };
    callbacks.close_gs_frame = [this]()
    {
        if(m_game_window) { m_game_window->ignore_stop_events(); delete m_game_window; m_game_window=nullptr; }
    };
    callbacks.get_camera_handler = []() -> std::shared_ptr<camera_handler_base>
    {
        if(g_cfg.io.camera != camera_handler::null && g_cfg.io.camera != camera_handler::fake)
            report_fatal_error("Camera device not supported by this title build",false,false);
        return std::make_shared<null_camera_handler>();
    };
    callbacks.get_music_handler = []() -> std::shared_ptr<music_handler_base>
    {
        switch(g_cfg.audio.music.get())
        {
        case music_handler::null:
            return std::make_shared<null_music_handler>();
        case music_handler::qt:
        {
            bool available=false;
            Emu.BlockingCallFromMainThread([&available]() {
                QMediaPlayer probe;
                available=probe.isAvailable();
            });
            if(!available)
                report_fatal_error("Qt/FFmpeg music backend unavailable",false,false);
            auto handler=std::make_shared<qt_music_handler>();
            std::fputs("RECOMPILER_CORE_PROVIDER music=qt backend=available\n",stderr);
            return handler;
        }
        }
        report_fatal_error("Unsupported music handler in title configuration",false,false);
    };
    // These are guest-facing dialogs, not the emulator game list/settings UI.
    callbacks.get_msg_dialog = []() -> std::shared_ptr<MsgDialogBase> {return std::make_shared<msg_dialog_frame>();};
    callbacks.get_osk_dialog = []() -> std::shared_ptr<OskDialogBase> {return std::make_shared<osk_dialog_frame>();};
    callbacks.get_save_dialog = []() -> std::unique_ptr<SaveDialogBase> {return std::make_unique<save_data_dialog>();};
    callbacks.get_trophy_notification_dialog = []() -> std::unique_ptr<TrophyNotificationBase>
    { return std::make_unique<recompiler_trophy_notification>(); };
    callbacks.get_sendmessage_dialog = []() -> std::shared_ptr<SendMessageDialogBase>
    { report_fatal_error("Online message UI is not integrated",false,false); };
    callbacks.get_recvmessage_dialog = []() -> std::shared_ptr<RecvMessageDialogBase>
    { report_fatal_error("Online message UI is not integrated",false,false); };
    callbacks.on_run = [](bool){std::fputs("RECOMPILER_CORE_STATE: running guest\n",stderr);};
    callbacks.on_pause = [](){std::fputs("RECOMPILER_CORE_STATE: paused\n",stderr);};
    callbacks.on_resume = [](){std::fputs("RECOMPILER_CORE_STATE: resumed\n",stderr);};
    callbacks.on_stop = [this](){std::fputs("RECOMPILER_CORE_STATE: stopped\n",stderr);quit();};
    callbacks.on_ready = [](){std::fputs("RECOMPILER_CORE_STATE: ready\n",stderr);};
    callbacks.on_missing_fw = [](){report_fatal_error("Required private PS3 firmware is missing",false,false);};
    callbacks.on_emulation_stop_no_response = [](std::shared_ptr<atomic_t<bool>> stopped,int)
    { if(!stopped || !*stopped) report_fatal_error("Guest shutdown has not completed",false,false); };
    callbacks.on_save_state_progress = [](std::shared_ptr<atomic_t<bool>>,stx::shared_ptr<utils::serial>,stx::atomic_ptr<std::string>*,std::shared_ptr<void>)
    {report_fatal_error("Savestate workflow is not enabled in the title build",false,false);};
    callbacks.enable_disc_eject = [](bool enabled){if(enabled) sys_log.notice("Disc eject notification; fixed disc remains mounted");};
    callbacks.enable_disc_insert = [](bool enabled){if(enabled) sys_log.notice("Disc insert notification; fixed disc remains mounted");};
    callbacks.handle_taskbar_progress = [](s32 type,s32 value)
    {std::fprintf(stderr,"RECOMPILER_CORE_PROGRESS type=%d value=%d\n",type,value);};
    callbacks.get_localized_string = [](localized_string_id id,const char* args){return localized_emu::get_string(id,args);};
    callbacks.get_localized_u32string = [](localized_string_id id,const char* args){return localized_emu::get_u32string(id,args);};
    callbacks.get_localized_setting = [](const cfg::_base*,u32) -> std::string
    {report_fatal_error("Settings frontend requested outside the title contract",false,false);};
    callbacks.play_sound = [this](const std::string& path, std::optional<f32> volume)
    {
        Emu.CallFromMainThread([this, path, volume]()
        {
            // RPCS3's UI cues are optional local WAV files.
            if (!fs::is_file(path))
            {
                sys_log.notice("RECOMPILER_UI_SOUND: optional WAV absent: %s", path);
                return;
            }

            while (m_sound_effects.size() >= 3)
                m_sound_effects.pop_front();

            auto effect = std::make_unique<QSoundEffect>();
            effect->setSource(QUrl::fromLocalFile(QString::fromStdString(path)));
            effect->setVolume(volume ? *volume : audio::get_volume());
            effect->play();
            m_sound_effects.push_back(std::move(effect));
            sys_log.notice("RECOMPILER_UI_SOUND: queued local WAV: %s", path);
        }, nullptr, false);
    };
    callbacks.add_breakpoint = [](u32){report_fatal_error("Debugger frontend is not included",false,false);};
    callbacks.display_sleep_control_supported = [](){return false;};
    callbacks.enable_display_sleep = [](bool){}; // No native sleep-control capability advertised.
    callbacks.check_microphone_permissions = [](){report_fatal_error("Microphone service is not integrated",false,false);};
    callbacks.make_video_source = []() -> std::unique_ptr<video_source>
    {
        auto source = std::make_unique<qt_video_source_wrapper>();
        std::fputs("RECOMPILER_CORE_PROVIDER overlay_media=qt\n", stderr);
        return source;
    };
    // Configuration is a required, verified local per-title file. No online
    // compatibility database, account UI or generic game browser is consulted.
    callbacks.get_database_config = [](const std::string&){return std::string();};
    Emu.SetCallbacks(std::move(callbacks));
    InitializeEmulator("00000001",false,false);
    return true;
}

// libSceSystemService is already a standard import of the title runtime.
extern "C" int sceSystemServiceParamGetInt(int parameter, int* value);

static std::string boot_profile_with_system_language(const std::string& base)
{
    const fs::file source(base);
    if (!source)
        return base;

    auto [profile, error] = yaml_load(source.to_string());
    int ps5_language = -1;
    const int result = sceSystemServiceParamGetInt(recompiler_locale::system_language_parameter, &ps5_language);
    const char* language = recompiler_locale::guest_language(result, ps5_language);
    if (!error.empty() || !recompiler_locale::apply_system_language(profile, language))
    {
        sys_log.warning("RECOMPILER_LANGUAGE: using original configuration (invalid profile)");
        return base;
    }

    const std::string effective = fs::get_cache_dir() + "recompiler-system-language.yml";
    const std::string yaml = YAML::Dump(profile);
    fs::pending_file pending(effective);
    if (!pending.file || pending.file.write(yaml.data(), yaml.size()) != yaml.size() || !pending.commit())
    {
        sys_log.warning("RECOMPILER_LANGUAGE: using original configuration (cannot write effective profile)");
        return base;
    }

    sys_log.notice("RECOMPILER_LANGUAGE: ps5=%d result=%d guest=%s", ps5_language, result, language);
    return effective;
}

static void configure_radv_cache(const char* data)
{
    // Qt's PS5 bootstrap has already created and checked the private cache directory.
    const std::string directory = std::string(data) + "/cache";
    if (::setenv("MESA_SHADER_CACHE_DIR", directory.c_str(), 1) == 0)
        std::fprintf(stderr, "RECOMPILER_RADV_CACHE: %s\n", directory.c_str());
    else
        std::fputs("RECOMPILER_RADV_CACHE: setting cache directory failed\n", stderr);
}

int main()
{
    rpcs3::ps5::initialize_qt_platform();
    rpcs3::ps5::initialize_core_paths();
    const char* data=std::getenv("RPCS3_PS5_DATA_ROOT");
    const char* app=std::getenv("RPCS3_PS5_APP_ROOT");
    if (!data || !app) return 67;
    const std::string videoout_param = std::string(app) + "/sce_sys/param.json";
    if (fs::is_file(videoout_param))
    {
        const int result = ::setenv("PS5_VIDEOOUT_PARAM_JSON", videoout_param.c_str(), 1);
        std::fprintf(stderr, "RECOMPILER_VIDEOOUT_PARAM: title metadata configured=%d\n", result == 0);
    }
    configure_radv_cache(data);
    const std::string game=std::string(app)+"/PS3_GAME/USRDIR/EBOOT.BIN";
    const bool pro_profile = fs::is_file(std::string(app) + "/ps5-pro.enable");
    const std::string config = std::string(app) + (pro_profile ? "/rpcs3-recompiler-pro.yml" : "/rpcs3-recompiler.yml");
    if(!fs::is_file(game) || !fs::is_file(config)) return 64;
    std::fprintf(stderr, "RECOMPILER_GRAPHICS_PROFILE: %s\n", pro_profile ? "pro" : "base");
    fs::file lock(fs::get_cache_dir()+"recompiler-core.lock",fs::rewrite+fs::lock);
    if (!lock) return 72;
    const auto cleanup = recompiler_cache_cleanup::run(app, data, [&]
    {
        const auto fields = psf::load_object(std::string(app) + "/PS3_GAME/PARAM.SFO");
        return std::string(psf::get_string(fields, "TITLE_ID", ""));
    });
    if (cleanup.state == recompiler_cache_cleanup::status::request_unavailable)
        std::fprintf(stderr, "RECOMPILER_CACHE_CLEANUP: optional marker unavailable errno=%d; continuing boot\n", cleanup.error.value());
    if (cleanup.state == recompiler_cache_cleanup::status::complete)
    {
        std::fprintf(stderr, "RECOMPILER_CACHE_CLEANUP: complete removed=%ju\n", cleanup.removed);
        return 0;
    }
    if (cleanup.state == recompiler_cache_cleanup::status::failed)
    {
        std::fprintf(stderr, "RECOMPILER_CACHE_CLEANUP: failed errno=%d\n", cleanup.error.value());
        return 76;
    }
    if (const int error = recompiler_rpcs3_prepare_vfs(data, app)) return error;
    const auto memory_stage=[](const char* stage) {
        struct ps5_heap_stats state{};
        ps5_heap_stats(&state);
        std::fprintf(stderr,"RECOMPILER_CORE_STAGE %s heap_mapped=%zu heap_peak=%zu segments=%u arenas=%u libc_fallbacks=%llu\n",
            stage,state.mapped_bytes,state.peak_bytes,state.segments,state.arenas,state.libc_fallbacks);
    };
    memory_stage("logger_begin");
    auto log=logs::make_file_listener(fs::get_log_dir()+"recompiler-core.log",64*1024*1024);
    memory_stage("logger_ready");
    logs::stored_message first{sys_log.always()};first.text="PS3 TO PS5 LLVM RPCS3 RECOMPILER: PPU/SPU LLVM and native Vulkan";
    logs::set_init({std::move(first)});
    int argc=1;char name[]="PS3 TO PS5 LLVM RPCS3 RECOMPILER";char* argv[]={name,nullptr};
    memory_stage("qt_begin");
    recompiler_title_application title(argc,argv);
    memory_stage("qt_ready");
    if(!title.Init()) return 73;
#if __has_include("recompiler_title_update_manifest.h")
    if (!recompiler_title_update::apply(app, data, sys_log))
        return 75;
#endif
    memory_stage("core_initialized");
    QTimer::singleShot(0, &title, [game,config]()
    {
        Emu.SetForceBoot(true);
        const auto effective=boot_profile_with_system_language(config);
        const auto result=Emu.BootGame(game,"",false,cfg_mode::config_override,effective);
        if(result!=game_boot_result::no_errors)
        {
            std::fprintf(stderr,"RECOMPILER_CORE_BLOCKED: guest boot result=%u\n",static_cast<unsigned>(result));
            QCoreApplication::exit(74);
        }
    });
    const int result=title.exec();
    logs::listener::shutdown_all();
    return result;
}
