#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gst/gst.h>
#include <glib/gstdio.h>

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/videodev2.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef enum {
    CAPTURE_FORMAT_YUYV,
    CAPTURE_FORMAT_NV12,
    CAPTURE_FORMAT_P010,
    CAPTURE_FORMAT_MJPEG,
} CaptureFormat;

typedef enum {
    HDR_MODE_AUTO,
    HDR_MODE_SDR,
    HDR_MODE_HDR10_PQ,
    HDR_MODE_HLG,
} HdrMode;

typedef enum {
    SESSION_MODE_SMOOTH_SDR,
    SESSION_MODE_HDR60_1080,
    SESSION_MODE_HDR60_1440,
    SESSION_MODE_HDR60_4K,
} SessionMode;

static const char *SESSION_MODE_NAMES[] = {
    "Smooth SDR • 1440p60",
    "HDR60 • Native 1080p",
    "HDR60 • Native 1440p",
    "HDR60 • Native 4K",
};

#define SESSION_MODE_COUNT ((int)(sizeof(SESSION_MODE_NAMES) / sizeof(SESSION_MODE_NAMES[0])))

static const char *HDR_MODE_NAMES[] = {
    "Auto",
    "SDR / Rec.709",
    "HDR10 / PQ",
    "HLG",
};

#define HDR_MODE_COUNT ((int)(sizeof(HDR_MODE_NAMES) / sizeof(HDR_MODE_NAMES[0])))

typedef enum {
    RECORD_PRESET_LOSSLESS,
    RECORD_PRESET_HIGH,
    RECORD_PRESET_BALANCED,
    RECORD_PRESET_COMPACT,
} RecordPreset;

typedef enum {
    RECORD_CODEC_AUTO,
    RECORD_CODEC_FFV1,
    RECORD_CODEC_H264,
} RecordCodec;

typedef enum {
    RECORD_CONTAINER_MKV,
    RECORD_CONTAINER_MP4,
} RecordContainer;

typedef enum {
    STREAM_SERVICE_YOUTUBE,
    STREAM_SERVICE_TWITCH,
    STREAM_SERVICE_CUSTOM,
} StreamService;

static const char *STREAM_SERVICE_NAMES[] = {
    "YouTube — Coming Soon",
    "Twitch — Coming Soon",
    "Custom RTMP / RTMPS — Coming Soon",
};

static const int STREAM_BITRATE_VALUES[] = {6000, 9000, 14000, 24000};
static const char *STREAM_BITRATE_NAMES[] = {
    "6 Mbps", "9 Mbps", "14 Mbps", "24 Mbps"
};

#define STREAM_SERVICE_COUNT 3
#define STREAM_BITRATE_COUNT 4

static const char *RECORD_PRESET_NAMES[] = {
    "Lossless",
    "High Quality",
    "Balanced",
    "Small File",
};

static const char *RECORD_CODEC_NAMES[] = {
    "Automatic",
    "Lossless FFV1",
    "H.264 (CPU)",
};

static const char *RECORD_CONTAINER_NAMES[] = {
    "Matroska (.mkv)",
    "MP4 (.mp4)",
};

#define RECORD_PRESET_COUNT ((int)(sizeof(RECORD_PRESET_NAMES) / sizeof(RECORD_PRESET_NAMES[0])))
#define RECORD_CODEC_COUNT ((int)(sizeof(RECORD_CODEC_NAMES) / sizeof(RECORD_CODEC_NAMES[0])))
#define RECORD_CONTAINER_COUNT ((int)(sizeof(RECORD_CONTAINER_NAMES) / sizeof(RECORD_CONTAINER_NAMES[0])))
#define LCS_EXIT_RECOVER_GC575 75
#define LCS_EXIT_SESSION_SDR 80
#define LCS_EXIT_SESSION_HDR1080 81
#define LCS_EXIT_SESSION_HDR1440 82
#define LCS_EXIT_SESSION_HDR4K 83


typedef struct {
    const char *name;
    CaptureFormat format;
} FormatOption;

typedef struct {
    const char *name;
    int width;
    int height;
} ResolutionOption;

typedef struct {
    CaptureFormat format;
    int width;
    int height;
    int fps;
    gboolean processed;
} SupportedMode;

static const FormatOption FORMATS[] = {
    {"YUYV", CAPTURE_FORMAT_YUYV},
    {"NV12", CAPTURE_FORMAT_NV12},
    {"P010", CAPTURE_FORMAT_P010},
    {"MJPEG", CAPTURE_FORMAT_MJPEG},
};

static const ResolutionOption RESOLUTIONS[] = {
    {"1280 × 720", 1280, 720},
    {"1920 × 1080", 1920, 1080},
    {"2560 × 1440", 2560, 1440},
    {"3840 × 2160 (4K)", 3840, 2160},
};

/* Selectable output modes. A TRUE flag means Linux Capture Studio must capture a
 * lower/native transport format and convert it to a P010 working stream.
 * The Linux UVC descriptor itself exposes native P010 only through 1440p30. */
static const SupportedMode SUPPORTED_MODES[] = {
    /* Common uncompressed UVC modes used by AVerMedia and Elgato devices. */
    {CAPTURE_FORMAT_YUYV, 1280, 720, 30, FALSE},
    {CAPTURE_FORMAT_YUYV, 1280, 720, 60, FALSE},
    {CAPTURE_FORMAT_YUYV, 1920, 1080, 30, FALSE},
    {CAPTURE_FORMAT_YUYV, 1920, 1080, 60, FALSE},
    {CAPTURE_FORMAT_YUYV, 3840, 2160, 30, FALSE},

    {CAPTURE_FORMAT_NV12, 1280, 720, 30, FALSE},
    {CAPTURE_FORMAT_NV12, 1280, 720, 60, FALSE},
    {CAPTURE_FORMAT_NV12, 1920, 1080, 30, FALSE},
    {CAPTURE_FORMAT_NV12, 1920, 1080, 50, FALSE},
    {CAPTURE_FORMAT_NV12, 1920, 1080, 60, FALSE},
    {CAPTURE_FORMAT_NV12, 1920, 1080, 120, FALSE},
    {CAPTURE_FORMAT_NV12, 2560, 1440, 30, FALSE},
    {CAPTURE_FORMAT_NV12, 2560, 1440, 50, FALSE},
    {CAPTURE_FORMAT_NV12, 2560, 1440, 60, FALSE},
    {CAPTURE_FORMAT_NV12, 2560, 1440, 120, FALSE},
    {CAPTURE_FORMAT_NV12, 3840, 2160, 30, FALSE},
    {CAPTURE_FORMAT_NV12, 3840, 2160, 60, FALSE},

    {CAPTURE_FORMAT_P010, 1280, 720, 30, FALSE},
    {CAPTURE_FORMAT_P010, 1280, 720, 60, FALSE},
    {CAPTURE_FORMAT_P010, 1920, 1080, 30, FALSE},
    {CAPTURE_FORMAT_P010, 1920, 1080, 50, FALSE},
    {CAPTURE_FORMAT_P010, 1920, 1080, 60, FALSE},
    {CAPTURE_FORMAT_P010, 2560, 1440, 30, FALSE},

    /* Optional 10-bit working/output modes. They appear only when a real
     * NV12 or MJPEG source mode is exposed by the selected device. */
    {CAPTURE_FORMAT_P010, 2560, 1440, 60, TRUE},
    {CAPTURE_FORMAT_P010, 3840, 2160, 30, TRUE},
    {CAPTURE_FORMAT_P010, 3840, 2160, 60, TRUE},

    {CAPTURE_FORMAT_MJPEG, 1280, 720, 30, FALSE},
    {CAPTURE_FORMAT_MJPEG, 1280, 720, 60, FALSE},
    {CAPTURE_FORMAT_MJPEG, 1920, 1080, 30, FALSE},
    {CAPTURE_FORMAT_MJPEG, 1920, 1080, 60, FALSE},
    {CAPTURE_FORMAT_MJPEG, 1920, 1080, 120, FALSE},
    {CAPTURE_FORMAT_MJPEG, 1920, 1080, 144, FALSE},
    {CAPTURE_FORMAT_MJPEG, 1920, 1080, 240, FALSE},
    {CAPTURE_FORMAT_MJPEG, 2560, 1440, 30, FALSE},
    {CAPTURE_FORMAT_MJPEG, 2560, 1440, 60, FALSE},
    {CAPTURE_FORMAT_MJPEG, 2560, 1440, 120, FALSE},
    {CAPTURE_FORMAT_MJPEG, 2560, 1440, 144, FALSE},
    {CAPTURE_FORMAT_MJPEG, 2560, 1440, 240, FALSE},
    {CAPTURE_FORMAT_MJPEG, 3840, 2160, 30, FALSE},
    {CAPTURE_FORMAT_MJPEG, 3840, 2160, 60, FALSE},
    {CAPTURE_FORMAT_MJPEG, 3840, 2160, 120, FALSE},
    {CAPTURE_FORMAT_MJPEG, 3840, 2160, 144, FALSE},
};
#define FORMAT_COUNT ((int)(sizeof(FORMATS) / sizeof(FORMATS[0])))
#define RESOLUTION_COUNT ((int)(sizeof(RESOLUTIONS) / sizeof(RESOLUTIONS[0])))
#define SUPPORTED_MODE_COUNT ((int)(sizeof(SUPPORTED_MODES) / sizeof(SUPPORTED_MODES[0])))
#define MAX_FPS_OPTIONS 16

typedef struct {
    GtkApplication *gtk_app;
    GtkWidget *window;
    GtkWidget *picture;
    GtkWidget *apply_blackout;
    GtkWidget *apply_blackout_label;
    GtkWidget *status_label;
    GtkWidget *record_button;
    GtkWidget *apply_button;
    GtkWidget *pending_label;
    GtkWidget *format_dropdown;
    GtkWidget *resolution_dropdown;
    GtkWidget *fps_dropdown;
    GtkWidget *hdr_dropdown;
    GtkWidget *hdr_toggle_button;
    GtkWidget *hdr_status_label;
    GtkWidget *hdr_tonemap_switch;
    GtkWidget *hdr_calibration_status_label;
    GtkWidget *hdr_calibration_window;
    GtkWidget *hdr_calibration_drawing;
    GtkWidget *hdr_calibration_step_label;
    GtkWidget *hdr_calibration_title_label;
    GtkWidget *hdr_calibration_instruction_label;
    GtkWidget *hdr_calibration_value_label;
    GtkWidget *hdr_calibration_scale;
    GtkWidget *hdr_calibration_back_button;
    GtkWidget *hdr_calibration_next_button;
    GtkWidget *output_entry;
    GtkWidget *audio_entry;
    GtkWidget *audio_output_dropdown;
    GtkWidget *audio_refresh_button;
    GtkWidget *settings_button;
    GtkWidget *connection_label;
    GtkWidget *session_label;
    GtkWidget *session_dropdown;
    GtkWidget *studio_check_label;
    GtkStringList *audio_output_model;
    GPtrArray *audio_output_ids;
    gboolean audio_output_update;
    gboolean audio_capture_disabled;
    gboolean hdr_preview_gl_failed;
    gboolean native_p010_session;
    int session_mode;
    gboolean settings_pending;
    gboolean apply_in_progress;
    gboolean apply_rolling_back;
    gboolean rollback_finish_scheduled;
    gboolean rollback_safe_fallback_attempted;
    gint64 apply_validation_started_us;
    guint apply_validation_frame_count;
    int pending_session_mode;
    int pending_format_index;
    int pending_resolution_index;
    int pending_fps;
    int pending_hdr_mode;
    gboolean pending_hdr_enabled;
    GtkWidget *monitor_switch;
    GtkWidget *monitor_volume;
    GtkWidget *audio_sync_scale;
    GtkWidget *stats_label;
    GtkWidget *toolbar;
    GtkWidget *root;
    GtkWidget *video_frame;
    GtkWidget *mode_badge_label;
    GtkWidget *device_badge_label;
    GtkWidget *audio_badge_label;
    GtkWidget *settings_frame;
    GtkWidget *controls_frame;
    GtkWidget *fullscreen_button;
    GtkWidget *feedback_label;
    GtkWidget *settings_window;
    GtkWidget *brand_subtitle_label;
    GtkWidget *quality_dropdown;
    GtkWidget *codec_dropdown;
    GtkWidget *container_dropdown;
    GtkWidget *stream_service_dropdown;
    GtkWidget *stream_server_entry;
    GtkWidget *stream_key_entry;
    GtkWidget *stream_bitrate_dropdown;
    GtkWidget *stream_button;
    GtkWidget *stream_status_label;
    GtkWidget *library_list;
    GtkWidget *library_summary_label;
    GtkWidget *vrr_monitor_switch;
    GtkWidget *vrr_status_label;

    GstElement *pipeline;
    GstElement *monitor_pipeline;
    guint bus_watch_id;
    guint stats_timer_id;
    guint watchdog_timer_id;
    guint recovery_timer_id;
    guint capture_restart_timer_id;
    guint record_stop_timeout_id;
    guint feedback_timeout_id;
    guint device_watch_timer_id;
    guint hdr_refresh_timer_id;
    guint apply_validation_timer_id;

    gchar *device;
    gchar *capture_device_name;
    gchar *capture_driver_name;
    gboolean is_gc575;
    gboolean device_connected;
    gchar *current_file;
    gchar *profile_output_dir;
    gchar *profile_audio_input;
    gchar *profile_audio_output_id;
    gchar *profile_stream_server;
    gboolean profile_monitor_enabled;
    gdouble profile_monitor_volume;
    gdouble profile_audio_sync_ms;
    gint64 record_started_us;
    gint64 pipeline_started_us;
    gint64 last_video_buffer_us;
    gboolean recording;
    gboolean hdr_enabled;
    gdouble hdr_peak_nits;
    gdouble hdr_paper_white_nits;
    gdouble hdr_black_level_nits;
    gdouble hdr_preview_saturation;
    gint hdr_calibration_step;
    gdouble hdr_calibration_original_peak_nits;
    gdouble hdr_calibration_original_paper_white_nits;
    gdouble hdr_calibration_original_black_level_nits;
    gboolean hdr_calibration_updating;
    gboolean streaming;
    gboolean stream_transition;
    gboolean pipeline_streaming;
    gboolean vrr_monitor_enabled;
    gboolean vrr_variable_detected;
    int stream_service;
    int stream_bitrate_kbps;
    int record_preset;
    int record_codec;
    int record_container;
    gint64 vrr_last_pts_ns;
    gint64 vrr_min_interval_ns;
    gint64 vrr_max_interval_ns;
    guint vrr_sample_count;
    gboolean have_sdr_restore_mode;
    int sdr_restore_format_index;
    int sdr_restore_resolution_index;
    int sdr_restore_fps;
    gboolean stopping;
    gboolean closing;
    gboolean rebuilding;
    gboolean record_transition;
    gboolean pipeline_recording;
    gboolean fullscreen;
    gboolean selection_update;
    int format_index;
    int resolution_index;
    int fps;
    int hdr_mode;
    int fps_values[MAX_FPS_OPTIONS];
    guint fps_count;
    guint recovery_count;
    gboolean recovery_waiting_for_frame;
    gboolean startup_safe_fallback_attempted;
    int requested_exit_code;
    gboolean recovery_exit_scheduled;
    gchar *transition_status_file;
    gboolean transition_complete_scheduled;
    guint transition_complete_timer_id;
    guint watchdog_stall_ticks;

    int active_format_index;
    int active_resolution_index;
    int active_fps;
    CaptureFormat active_source_format;
    int active_source_width;
    int active_source_height;
    int active_source_fps;
    gboolean have_active_mode;

    int last_good_format_index;
    int last_good_resolution_index;
    int last_good_fps;
    gboolean have_last_good_mode;
    int last_good_session_mode;
    int last_good_hdr_mode;
    gboolean last_good_hdr_enabled;
    gboolean last_good_native_p010_session;
    gboolean initial_session_persisted;

    int control_fd;
} OpenCentralApp;

typedef struct {
    OpenCentralApp *app;
    guint32 id;
} ControlBinding;

static gboolean gst_element_available(const gchar *factory_name);
static void populate_fps_dropdown(OpenCentralApp *app, int preferred_fps);
static gboolean build_pipeline(OpenCentralApp *app, gboolean recording);
static gboolean apply_capture_change_cb(gpointer user_data);
static gboolean validate_applied_mode_cb(gpointer user_data);
static void begin_failed_apply_rollback(OpenCentralApp *app, const gchar *reason);
static void finish_failed_apply_rollback(OpenCentralApp *app);
static gboolean finish_failed_apply_rollback_cb(gpointer user_data);
static void select_safe_hdr1080_mode(OpenCentralApp *app);
static const FormatOption *current_format(const OpenCentralApp *app);
static gboolean refresh_audio_outputs(OpenCentralApp *app, gboolean announce);
static const gchar *selected_audio_output_id(const OpenCentralApp *app);
static void update_mode_badge(OpenCentralApp *app);
static void update_hdr_toggle_ui(OpenCentralApp *app);
static void update_session_ui(OpenCentralApp *app);
static void sync_pending_settings_from_current(OpenCentralApp *app);
static void update_apply_ui(OpenCentralApp *app);
static void mark_capture_settings_pending(OpenCentralApp *app, const gchar *reason);
static void update_pending_hdr_ui(OpenCentralApp *app);
static void populate_pending_fps_dropdown(OpenCentralApp *app, int preferred_fps);
static void on_apply_clicked(GtkButton *button, gpointer user_data);
static void finish_apply_success(OpenCentralApp *app);
static gboolean finish_apply_without_rebuild_cb(gpointer user_data);
static void set_apply_blackout(OpenCentralApp *app, gboolean visible, const gchar *message);
static gboolean pending_settings_equal_current(const OpenCentralApp *app);
static gboolean pending_preview_reuses_active_pipeline(OpenCentralApp *app);
static void update_audio_badge_ui(OpenCentralApp *app);
static void on_session_mode_changed(GObject *object, GParamSpec *pspec, gpointer user_data);
static void apply_session_mode_in_process(OpenCentralApp *app, int session_mode);
static void persist_session_mode(OpenCentralApp *app);
static void persist_pending_session_mode(int session_mode);
static gboolean persist_initial_session_cb(gpointer user_data);
static void on_studio_check_clicked(GtkButton *button, gpointer user_data);
static void show_feedback(OpenCentralApp *app, const gchar *format, ...) G_GNUC_PRINTF(2, 3);
static void apply_selected_mode_change(OpenCentralApp *app,
                                       gboolean had_old_source,
                                       CaptureFormat old_source_format,
                                       int old_source_width,
                                       int old_source_height,
                                       int old_source_fps);
static const ResolutionOption *current_resolution(const OpenCentralApp *app);
static gboolean mode_uses_processing(CaptureFormat format, int width, int height, int fps);
static gboolean device_mode_supported(OpenCentralApp *app, CaptureFormat format, int width, int height, int fps);
static gboolean selectable_mode_supported(OpenCentralApp *app, CaptureFormat format, int width, int height, int fps);
static gboolean mode_uses_processing_for_device(OpenCentralApp *app, CaptureFormat format, int width, int height, int fps);
static void select_best_supported_mode(OpenCentralApp *app);
static void set_capture_selectors_sensitive(OpenCentralApp *app, gboolean sensitive);
static void enforce_native_p010_lock(OpenCentralApp *app);
static void stop_monitor_pipeline(OpenCentralApp *app);
static gboolean start_monitor_pipeline(OpenCentralApp *app);
static void stop_pipeline(OpenCentralApp *app);
static void close_control_handle(OpenCentralApp *app);
static void reopen_control_handle(OpenCentralApp *app);
static gboolean wait_for_capture_mode_ready(OpenCentralApp *app, CaptureFormat format, int width, int height);
static void request_gc575_recovery_exit(OpenCentralApp *app, const gchar *reason);
static gboolean complete_transition_status_cb(gpointer user_data);
static gboolean clear_transition_status_cb(gpointer user_data);
static __u32 capture_format_fourcc(CaptureFormat format);
static void schedule_preview_recovery(OpenCentralApp *app, const gchar *reason);
static void finish_recording_and_restore_preview(OpenCentralApp *app);
static void on_hdr_toggle_toggled(GtkToggleButton *button, gpointer user_data);
static void on_settings_clicked(GtkButton *button, gpointer user_data);
static gboolean on_settings_window_close_request(GtkWindow *window, gpointer user_data);
static void on_hdr_calibration_reset_clicked(GtkButton *button, gpointer user_data);
static void on_hdr_calibration_start_clicked(GtkButton *button, gpointer user_data);
static void on_hdr_calibration_back_clicked(GtkButton *button, gpointer user_data);
static void on_hdr_calibration_next_clicked(GtkButton *button, gpointer user_data);
static void on_hdr_calibration_cancel_clicked(GtkButton *button, gpointer user_data);
static void on_hdr_calibration_value_changed(GtkRange *range, gpointer user_data);
static void on_hdr_calibration_minus_clicked(GtkButton *button, gpointer user_data);
static void on_hdr_calibration_plus_clicked(GtkButton *button, gpointer user_data);
static gboolean on_hdr_calibration_window_close_request(GtkWindow *window, gpointer user_data);
static void update_hdr_calibration_wizard(OpenCentralApp *app);
static void hdr_calibration_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data);
static gboolean refresh_hdr_preview_cb(gpointer user_data);
static void schedule_hdr_preview_refresh(OpenCentralApp *app, const gchar *message);
static gboolean apply_hdr_calibration_to_preview(OpenCentralApp *app);
static gchar *make_hdr_preview_shader(const OpenCentralApp *app, HdrMode mode, CaptureFormat source_format);
static gboolean record_stop_timeout_cb(gpointer user_data);
static void set_status(OpenCentralApp *app, const gchar *format, ...) G_GNUC_PRINTF(2, 3);
static GtkWidget *make_info_row(const gchar *label_text, const gchar *value_text);
static gchar *detect_capture_device_name(OpenCentralApp *app);
static void load_device_profile(OpenCentralApp *app);
static void save_device_profile(OpenCentralApp *app);
static void refresh_recording_library(OpenCentralApp *app);
static gboolean device_watch_tick(gpointer user_data);
static void update_vrr_status(OpenCentralApp *app);
static void update_device_ui(OpenCentralApp *app);
static void finish_streaming_and_restore_preview(OpenCentralApp *app, const gchar *message);
static gchar *build_stream_endpoint(OpenCentralApp *app);
static gchar *make_h264_encoder_chain(gint bitrate_kbps, gint key_interval);
static void on_stream_clicked(GtkButton *button, gpointer user_data);
static void on_stream_settings_changed(GObject *object, GParamSpec *pspec, gpointer user_data);
static void on_stream_entry_changed(GtkEditable *editable, gpointer user_data);

static void
free_control_binding(gpointer data, GClosure *closure)
{
    (void)closure;
    g_free(data);
}

static gboolean
message_is_capture_audio_error(GstMessage *message, const gchar *debug)
{
    const gchar *source_name = NULL;

    if (message != NULL && GST_MESSAGE_SRC(message) != NULL) {
        source_name = GST_OBJECT_NAME(GST_MESSAGE_SRC(message));
    }

    return (source_name != NULL &&
            (g_str_has_prefix(source_name, "alsasrc") ||
             g_str_has_prefix(source_name, "pulsesrc") ||
             g_str_has_prefix(source_name, "pipewiresrc"))) ||
           (debug != NULL &&
            (g_strstr_len(debug, -1, "gstalsasrc") != NULL ||
             g_strstr_len(debug, -1, "Unable to set hw params for recording") != NULL));
}

static gchar *
detect_capture_device_name(OpenCentralApp *app)
{
    struct v4l2_capability cap = {0};

    if (app != NULL && app->control_fd >= 0 && ioctl(app->control_fd, VIDIOC_QUERYCAP, &cap) == 0) {
        g_free(app->capture_driver_name);
        app->capture_driver_name = cap.driver[0] != '\0'
            ? g_strdup((const gchar *)cap.driver)
            : g_strdup("V4L2");
        if (cap.card[0] != '\0') {
            return g_strdup((const gchar *)cap.card);
        }
    }

    if (app != NULL && app->device != NULL) {
        gchar *base = g_path_get_basename(app->device);
        gchar *fallback = g_strdup_printf("Capture device (%s)", base != NULL ? base : "unknown");
        g_free(base);
        return fallback;
    }

    return g_strdup("Capture device");
}

static gchar *
profile_group_name(OpenCentralApp *app)
{
    const gchar *identity = app->capture_device_name != NULL
        ? app->capture_device_name
        : (app->device != NULL ? app->device : "capture-device");
    gchar *digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, identity, -1);
    gchar *group = g_strdup_printf("device-%.16s", digest != NULL ? digest : "default");
    g_free(digest);
    return group;
}

static gchar *
profile_file_path(void)
{
    gchar *dir = g_build_filename(g_get_user_config_dir(), "linux-capture-studio", NULL);
    gchar *path;

    g_mkdir_with_parents(dir, 0755);
    path = g_build_filename(dir, "profiles.ini", NULL);
    g_free(dir);
    return path;
}

static gint
key_file_get_int_default(GKeyFile *key_file, const gchar *group, const gchar *key, gint fallback)
{
    GError *error = NULL;
    gint value = g_key_file_get_integer(key_file, group, key, &error);
    if (error != NULL) {
        g_clear_error(&error);
        return fallback;
    }
    return value;
}

static gboolean
key_file_get_bool_default(GKeyFile *key_file, const gchar *group, const gchar *key, gboolean fallback)
{
    GError *error = NULL;
    gboolean value = g_key_file_get_boolean(key_file, group, key, &error);
    if (error != NULL) {
        g_clear_error(&error);
        return fallback;
    }
    return value;
}

static gdouble
key_file_get_double_default(GKeyFile *key_file, const gchar *group, const gchar *key, gdouble fallback)
{
    GError *error = NULL;
    gdouble value = g_key_file_get_double(key_file, group, key, &error);
    if (error != NULL) {
        g_clear_error(&error);
        return fallback;
    }
    return value;
}

static void
load_device_profile(OpenCentralApp *app)
{
    GKeyFile *key_file = g_key_file_new();
    gchar *path = profile_file_path();
    gchar *group = profile_group_name(app);
    GError *error = NULL;

    if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, &error)) {
        g_clear_error(&error);
        g_key_file_unref(key_file);
        g_free(group);
        g_free(path);
        return;
    }

    app->format_index = CLAMP(key_file_get_int_default(key_file, group, "format", app->format_index), 0, FORMAT_COUNT - 1);
    app->resolution_index = CLAMP(key_file_get_int_default(key_file, group, "resolution", app->resolution_index), 0, RESOLUTION_COUNT - 1);
    app->fps = MAX(1, key_file_get_int_default(key_file, group, "fps", app->fps));
    app->hdr_mode = CLAMP(key_file_get_int_default(key_file, group, "hdr-mode", app->hdr_mode), 0, HDR_MODE_COUNT - 1);
    app->hdr_enabled = key_file_get_bool_default(key_file, group, "hdr-enabled", app->hdr_enabled);
    app->hdr_peak_nits = CLAMP(key_file_get_double_default(key_file, group, "hdr-preview-peak-nits", app->hdr_peak_nits), 400.0, 4000.0);
    app->hdr_paper_white_nits = CLAMP(key_file_get_double_default(key_file, group, "hdr-preview-paper-white-nits", app->hdr_paper_white_nits), 80.0, 400.0);
    app->hdr_black_level_nits = CLAMP(key_file_get_double_default(key_file, group, "hdr-preview-black-level-nits", app->hdr_black_level_nits), 0.0, 5.0);
    app->hdr_preview_saturation = CLAMP(key_file_get_double_default(key_file, group, "hdr-preview-saturation", app->hdr_preview_saturation), 0.50, 1.50);
    app->record_preset = CLAMP(key_file_get_int_default(key_file, group, "record-preset", app->record_preset), 0, RECORD_PRESET_COUNT - 1);
    app->record_codec = CLAMP(key_file_get_int_default(key_file, group, "record-codec", app->record_codec), 0, RECORD_CODEC_COUNT - 1);
    app->record_container = CLAMP(key_file_get_int_default(key_file, group, "record-container", app->record_container), 0, RECORD_CONTAINER_COUNT - 1);
    app->stream_service = CLAMP(key_file_get_int_default(key_file, group, "stream-service", app->stream_service), 0, STREAM_SERVICE_COUNT - 1);
    app->stream_bitrate_kbps = key_file_get_int_default(key_file, group, "stream-bitrate-kbps", app->stream_bitrate_kbps);
    app->vrr_monitor_enabled = key_file_get_bool_default(key_file, group, "vrr-monitor", app->vrr_monitor_enabled);

    g_clear_pointer(&app->profile_output_dir, g_free);
    g_clear_pointer(&app->profile_audio_input, g_free);
    app->profile_output_dir = g_key_file_get_string(key_file, group, "output-folder", NULL);
    app->profile_audio_input = g_key_file_get_string(key_file, group, "audio-input", NULL);
    app->profile_audio_output_id = g_key_file_get_string(key_file, group, "audio-output", NULL);
    g_clear_pointer(&app->profile_stream_server, g_free);
    app->profile_stream_server = g_key_file_get_string(key_file, group, "stream-server", NULL);
    app->profile_monitor_enabled = key_file_get_bool_default(key_file, group, "monitor-enabled", app->profile_monitor_enabled);
    app->profile_monitor_volume = key_file_get_double_default(key_file, group, "monitor-volume", app->profile_monitor_volume);
    app->profile_audio_sync_ms = key_file_get_double_default(key_file, group, "audio-sync-ms", app->profile_audio_sync_ms);

    app->sdr_restore_format_index = key_file_get_int_default(key_file, group, "sdr-format", app->format_index);
    app->sdr_restore_resolution_index = key_file_get_int_default(key_file, group, "sdr-resolution", app->resolution_index);
    app->sdr_restore_fps = key_file_get_int_default(key_file, group, "sdr-fps", app->fps);

    g_key_file_unref(key_file);
    g_free(group);
    g_free(path);
}

static void
save_hdr_calibration(OpenCentralApp *app)
{
    GKeyFile *key_file;
    gchar *path;
    gchar *group;
    gchar *data;
    gsize length = 0;
    GError *error = NULL;

    if (app == NULL) return;

    key_file = g_key_file_new();
    path = profile_file_path();
    group = profile_group_name(app);
    g_key_file_load_from_file(key_file, path, G_KEY_FILE_KEEP_COMMENTS, NULL);

    g_key_file_set_double(key_file, group, "hdr-preview-peak-nits", app->hdr_peak_nits);
    g_key_file_set_double(key_file, group, "hdr-preview-paper-white-nits", app->hdr_paper_white_nits);
    g_key_file_set_double(key_file, group, "hdr-preview-black-level-nits", app->hdr_black_level_nits);
    g_key_file_set_double(key_file, group, "hdr-preview-saturation", app->hdr_preview_saturation);

    data = g_key_file_to_data(key_file, &length, &error);
    if (data != NULL) g_file_set_contents(path, data, (gssize)length, &error);
    if (error != NULL) {
        g_printerr("Linux Capture Studio: could not save HDR calibration: %s\n", error->message);
        g_clear_error(&error);
    }

    g_free(data);
    g_key_file_unref(key_file);
    g_free(group);
    g_free(path);
}

static void
save_device_profile(OpenCentralApp *app)
{
    if (app != NULL && app->native_p010_session) {
        return;
    }
    GKeyFile *key_file = g_key_file_new();
    gchar *path = profile_file_path();
    gchar *group = profile_group_name(app);
    gchar *data;
    gsize length = 0;
    GError *error = NULL;

    g_key_file_load_from_file(key_file, path, G_KEY_FILE_KEEP_COMMENTS, NULL);

    if (app->format_dropdown != NULL) {
        guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->format_dropdown));
        if (selected < FORMAT_COUNT) app->format_index = (int)selected;
    }
    if (app->resolution_dropdown != NULL) {
        guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->resolution_dropdown));
        if (selected < RESOLUTION_COUNT) app->resolution_index = (int)selected;
    }
    if (app->quality_dropdown != NULL) {
        guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->quality_dropdown));
        if (selected < RECORD_PRESET_COUNT) app->record_preset = (int)selected;
    }
    if (app->codec_dropdown != NULL) {
        guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->codec_dropdown));
        if (selected < RECORD_CODEC_COUNT) app->record_codec = (int)selected;
    }
    if (app->container_dropdown != NULL) {
        guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->container_dropdown));
        if (selected < RECORD_CONTAINER_COUNT) app->record_container = (int)selected;
    }

    g_key_file_set_integer(key_file, group, "format", app->format_index);
    g_key_file_set_integer(key_file, group, "resolution", app->resolution_index);
    g_key_file_set_integer(key_file, group, "fps", app->fps);
    g_key_file_set_integer(key_file, group, "hdr-mode", app->hdr_mode);
    g_key_file_set_boolean(key_file, group, "hdr-enabled", app->hdr_enabled);
    g_key_file_set_integer(key_file, group, "record-preset", app->record_preset);
    g_key_file_set_integer(key_file, group, "record-codec", app->record_codec);
    g_key_file_set_integer(key_file, group, "record-container", app->record_container);
    g_key_file_set_integer(key_file, group, "stream-service", app->stream_service);
    g_key_file_set_integer(key_file, group, "stream-bitrate-kbps", app->stream_bitrate_kbps);
    g_key_file_set_boolean(key_file, group, "vrr-monitor", app->vrr_monitor_enabled);
    g_key_file_set_integer(key_file, group, "sdr-format", app->sdr_restore_format_index);
    g_key_file_set_integer(key_file, group, "sdr-resolution", app->sdr_restore_resolution_index);
    g_key_file_set_integer(key_file, group, "sdr-fps", app->sdr_restore_fps);

    if (app->output_entry != NULL) {
        g_key_file_set_string(key_file, group, "output-folder", gtk_editable_get_text(GTK_EDITABLE(app->output_entry)));
    }
    if (app->audio_entry != NULL) {
        g_key_file_set_string(key_file, group, "audio-input", gtk_editable_get_text(GTK_EDITABLE(app->audio_entry)));
    }
    if (app->stream_server_entry != NULL) {
        g_key_file_set_string(key_file, group, "stream-server", gtk_editable_get_text(GTK_EDITABLE(app->stream_server_entry)));
    }
    if (app->audio_output_dropdown != NULL) {
        const gchar *audio_output = selected_audio_output_id(app);
        g_key_file_set_string(key_file, group, "audio-output", audio_output != NULL ? audio_output : "");
    }
    if (app->monitor_switch != NULL) {
        g_key_file_set_boolean(key_file, group, "monitor-enabled", gtk_switch_get_active(GTK_SWITCH(app->monitor_switch)));
    }
    if (app->monitor_volume != NULL) {
        g_key_file_set_double(key_file, group, "monitor-volume", gtk_range_get_value(GTK_RANGE(app->monitor_volume)));
    }
    if (app->audio_sync_scale != NULL) {
        g_key_file_set_double(key_file, group, "audio-sync-ms", gtk_range_get_value(GTK_RANGE(app->audio_sync_scale)));
    }

    data = g_key_file_to_data(key_file, &length, &error);
    if (data != NULL) {
        g_file_set_contents(path, data, (gssize)length, &error);
    }
    if (error != NULL) {
        g_printerr("Linux Capture Studio: could not save profile: %s\n", error->message);
        g_clear_error(&error);
    }

    g_free(data);
    g_key_file_unref(key_file);
    g_free(group);
    g_free(path);
}

static RecordCodec
effective_record_codec(OpenCentralApp *app)
{
    RecordCodec codec = (RecordCodec)app->record_codec;

    if (codec == RECORD_CODEC_AUTO) {
        codec = app->record_preset == RECORD_PRESET_LOSSLESS
            ? RECORD_CODEC_FFV1 : RECORD_CODEC_H264;
    }
    if (app->hdr_enabled) {
        codec = RECORD_CODEC_FFV1;
    }
    if (codec == RECORD_CODEC_H264 &&
        ((!gst_element_available("x264enc") && !gst_element_available("openh264enc")) ||
         !gst_element_available("h264parse"))) {
        codec = RECORD_CODEC_FFV1;
    }
    return codec;
}

static RecordContainer
effective_record_container(OpenCentralApp *app)
{
    RecordContainer container = (RecordContainer)app->record_container;
    RecordCodec codec = effective_record_codec(app);

    if (container == RECORD_CONTAINER_MP4 &&
        (codec != RECORD_CODEC_H264 ||
         !gst_element_available("mp4mux") ||
         !gst_element_available("avenc_aac"))) {
        return RECORD_CONTAINER_MKV;
    }
    return container;
}

static gint
record_bitrate_kbps(OpenCentralApp *app)
{
    switch ((RecordPreset)app->record_preset) {
    case RECORD_PRESET_HIGH: return 50000;
    case RECORD_PRESET_BALANCED: return 24000;
    case RECORD_PRESET_COMPACT: return 10000;
    case RECORD_PRESET_LOSSLESS:
    default: return 50000;
    }
}

static gchar *
record_preset_description(OpenCentralApp *app)
{
    RecordCodec codec = effective_record_codec(app);
    RecordContainer container = effective_record_container(app);

    if (codec == RECORD_CODEC_FFV1) {
        return g_strdup_printf("Lossless FFV1 • %s", container == RECORD_CONTAINER_MP4 ? "MP4" : "MKV");
    }
    return g_strdup_printf("H.264 %d Mbps • %s", record_bitrate_kbps(app) / 1000,
                           container == RECORD_CONTAINER_MP4 ? "MP4" : "MKV");
}

static gchar *
make_h264_encoder_chain(gint bitrate_kbps, gint key_interval)
{
    if (gst_element_available("x264enc")) {
        return g_strdup_printf(
            "! x264enc bitrate=%d speed-preset=veryfast tune=zerolatency key-int-max=%d "
            "bframes=0 byte-stream=false ",
            bitrate_kbps,
            key_interval
        );
    }
    if (gst_element_available("openh264enc")) {
        return g_strdup_printf(
            "! openh264enc bitrate=%d max-bitrate=%d rate-control=bitrate "
            "complexity=low usage-type=screen gop-size=%d enable-frame-skip=true ",
            bitrate_kbps * 1000,
            bitrate_kbps * 1000,
            key_interval
        );
    }
    return NULL;
}

static gchar *
build_stream_endpoint(OpenCentralApp *app)
{
    const gchar *server;
    const gchar *key;
    gchar *clean_server;
    gchar *clean_key;
    gchar *endpoint;

    if (app->stream_server_entry == NULL || app->stream_key_entry == NULL) {
        return NULL;
    }

    server = gtk_editable_get_text(GTK_EDITABLE(app->stream_server_entry));
    key = gtk_editable_get_text(GTK_EDITABLE(app->stream_key_entry));
    if (server == NULL || server[0] == '\0' || key == NULL || key[0] == '\0') {
        return NULL;
    }
    if (!g_str_has_prefix(server, "rtmp://") && !g_str_has_prefix(server, "rtmps://")) {
        return NULL;
    }

    clean_server = g_strdup(server);
    clean_key = g_strdup(key);
    g_strstrip(clean_server);
    g_strstrip(clean_key);
    while (clean_server[0] != '\0' && clean_server[strlen(clean_server) - 1] == '/') {
        clean_server[strlen(clean_server) - 1] = '\0';
    }
    while (clean_key[0] == '/') {
        memmove(clean_key, clean_key + 1, strlen(clean_key));
    }
    if (clean_server[0] == '\0' || clean_key[0] == '\0') {
        g_free(clean_server);
        g_free(clean_key);
        return NULL;
    }

    endpoint = g_strdup_printf("%s/%s", clean_server, clean_key);
    g_free(clean_server);
    g_free(clean_key);
    return endpoint;
}

static void
on_stream_settings_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    (void)object;
    (void)pspec;
    if (app->stream_status_label != NULL) {
        gtk_label_set_text(GTK_LABEL(app->stream_status_label), "Coming Soon");
    }
}

static void
on_stream_entry_changed(GtkEditable *editable, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    (void)editable;
    save_device_profile(app);
}

static void
finish_streaming_and_restore_preview(OpenCentralApp *app, const gchar *message)
{
    stop_pipeline(app);
    app->pipeline_streaming = FALSE;
    app->streaming = FALSE;
    app->stream_transition = FALSE;
    if (app->stream_button != NULL) {
        gtk_button_set_label(GTK_BUTTON(app->stream_button), "Streaming — Coming Soon");
        gtk_widget_remove_css_class(app->stream_button, "streaming-active");
        gtk_widget_set_sensitive(app->stream_button, FALSE);
    }
    if (app->stream_status_label != NULL) {
        gtk_label_set_text(GTK_LABEL(app->stream_status_label), message != NULL ? message : "Ready");
    }
    if (app->record_button != NULL) {
        gtk_widget_set_sensitive(app->record_button, app->device_connected && app->fps_count > 0);
    }
    set_capture_selectors_sensitive(app, TRUE);
    if (!app->closing && app->device_connected) {
        build_pipeline(app, FALSE);
    }
    if (message != NULL) {
        show_feedback(app, "%s", message);
    }
}

static G_GNUC_UNUSED void
on_stream_clicked(GtkButton *button, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    (void)button;
    set_status(app, "YouTube and Twitch streaming are coming soon. Recording remains fully available.");
    show_feedback(app, "Streaming coming soon");
}


static gchar *
vrr_hardware_capability(OpenCentralApp *app)
{
    const gchar *name = app->capture_device_name != NULL ? app->capture_device_name : "";
    gchar *lower = g_ascii_strdown(name, -1);
    gboolean supported = app->is_gc575 ||
        (strstr(lower, "hd60 x") != NULL) ||
        (strstr(lower, "4k x") != NULL) ||
        (strstr(lower, "4k pro") != NULL) ||
        (strstr(lower, "4k60 pro") != NULL);
    gchar *result = g_strdup(supported
        ? "Supported by device • controlled by HDMI source and display"
        : "Not exposed through standard V4L2 controls");
    g_free(lower);
    return result;
}

static void
update_vrr_status(OpenCentralApp *app)
{
    gchar *text;

    if (app->vrr_status_label == NULL) return;
    if (!app->vrr_monitor_enabled) {
        gtk_label_set_text(GTK_LABEL(app->vrr_status_label), "Frame timing monitor is off");
        return;
    }
    if (app->vrr_sample_count < 30 || app->vrr_min_interval_ns <= 0 || app->vrr_max_interval_ns <= 0) {
        gtk_label_set_text(GTK_LABEL(app->vrr_status_label), "Measuring frame timing…");
        return;
    }

    gdouble fastest = (gdouble)GST_SECOND / (gdouble)app->vrr_min_interval_ns;
    gdouble slowest = (gdouble)GST_SECOND / (gdouble)app->vrr_max_interval_ns;
    gboolean was_variable = app->vrr_variable_detected;
    app->vrr_variable_detected = (fastest - slowest) > 1.0;
    if (app->vrr_variable_detected) {
        text = g_strdup_printf("Variable frame timing detected: %.1f–%.1f FPS", slowest, fastest);
    } else {
        text = g_strdup_printf("Stable frame timing: %.1f FPS", (fastest + slowest) / 2.0);
    }
    gtk_label_set_text(GTK_LABEL(app->vrr_status_label), text);
    g_free(text);
    if (was_variable != app->vrr_variable_detected) update_mode_badge(app);
}

static gboolean
query_capture_candidate(const gchar *path, gchar **card_out, gchar **driver_out)
{
    struct v4l2_capability cap = {0};
    guint32 caps;
    int fd = open(path, O_RDWR | O_NONBLOCK);

    if (fd < 0) return FALSE;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) != 0) {
        close(fd);
        return FALSE;
    }
    caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
    if ((caps & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE)) == 0) {
        close(fd);
        return FALSE;
    }
    if (card_out != NULL) *card_out = g_strdup((const gchar *)cap.card);
    if (driver_out != NULL) *driver_out = g_strdup((const gchar *)cap.driver);
    close(fd);
    return TRUE;
}

static gchar *
find_reconnected_capture_device(OpenCentralApp *app)
{
    glob_t paths = {0};
    gchar *fallback = NULL;

    if (glob("/dev/video*", 0, NULL, &paths) != 0) return NULL;
    for (size_t i = 0; i < paths.gl_pathc; i++) {
        gchar *card = NULL;
        if (!query_capture_candidate(paths.gl_pathv[i], &card, NULL)) continue;
        if (fallback == NULL) fallback = g_strdup(paths.gl_pathv[i]);
        if (app->capture_device_name != NULL && g_strcmp0(card, app->capture_device_name) == 0) {
            g_free(card);
            g_free(fallback);
            fallback = g_strdup(paths.gl_pathv[i]);
            break;
        }
        g_free(card);
    }
    globfree(&paths);
    return fallback;
}

static void
update_session_ui(OpenCentralApp *app)
{
    if (app == NULL || app->session_label == NULL) {
        return;
    }

    gtk_widget_remove_css_class(app->session_label, "hdr-session");
    gtk_widget_remove_css_class(app->session_label, "offline-session");

    if (!app->device_connected) {
        gtk_label_set_text(GTK_LABEL(app->session_label), "OFFLINE");
        gtk_widget_add_css_class(app->session_label, "offline-session");
    } else if (app->native_p010_session) {
        const gchar *label = "HDR60  •  1080 NATIVE";
        if (app->session_mode == SESSION_MODE_HDR60_1440) {
            label = "HDR60  •  1440 NATIVE";
        } else if (app->session_mode == SESSION_MODE_HDR60_4K) {
            label = "HDR60  •  4K NATIVE";
        }
        gtk_label_set_text(GTK_LABEL(app->session_label), label);
        gtk_widget_add_css_class(app->session_label, "hdr-session");
    } else if (app->hdr_enabled) {
        gtk_label_set_text(GTK_LABEL(app->session_label), "HDR OUTPUT");
        gtk_widget_add_css_class(app->session_label, "hdr-session");
    } else {
        gtk_label_set_text(GTK_LABEL(app->session_label), "LOW LATENCY");
    }
}

static void
update_audio_badge_ui(OpenCentralApp *app)
{
    if (app == NULL || app->audio_badge_label == NULL) {
        return;
    }

    gtk_widget_remove_css_class(app->audio_badge_label, "audio-live");
    gtk_widget_remove_css_class(app->audio_badge_label, "audio-offline");

    if (app->audio_capture_disabled) {
        gtk_label_set_text(GTK_LABEL(app->audio_badge_label), "○  AUDIO UNAVAILABLE");
        gtk_widget_add_css_class(app->audio_badge_label, "audio-offline");
    } else if (app->monitor_switch != NULL &&
               !gtk_switch_get_active(GTK_SWITCH(app->monitor_switch))) {
        gtk_label_set_text(GTK_LABEL(app->audio_badge_label), "○  AUDIO MUTED");
    } else if (app->monitor_pipeline != NULL) {
        gtk_label_set_text(GTK_LABEL(app->audio_badge_label), "●  AUDIO LIVE");
        gtk_widget_add_css_class(app->audio_badge_label, "audio-live");
    } else {
        gtk_label_set_text(GTK_LABEL(app->audio_badge_label), "○  AUDIO READY");
    }
}

static void
update_device_ui(OpenCentralApp *app)
{
    const gchar *name = app->capture_device_name != NULL ? app->capture_device_name : "Capture device";
    gchar *badge;

    if (app->brand_subtitle_label != NULL) {
        if (!app->device_connected) {
            gtk_label_set_text(GTK_LABEL(app->brand_subtitle_label), "Waiting for capture device");
        } else if (app->native_p010_session) {
            gchar *subtitle = g_strdup_printf("%s • NATIVE-RESOLUTION HDR60", name);
            gtk_label_set_text(GTK_LABEL(app->brand_subtitle_label), subtitle);
            g_free(subtitle);
        } else {
            gtk_label_set_text(GTK_LABEL(app->brand_subtitle_label), name);
        }
    }
    if (app->connection_label != NULL) {
        gtk_label_set_text(GTK_LABEL(app->connection_label), app->device_connected ? "●  CONNECTED" : "●  DISCONNECTED");
        if (app->device_connected) {
            gtk_widget_add_css_class(app->connection_label, "connected");
            gtk_widget_remove_css_class(app->connection_label, "disconnected");
        } else {
            gtk_widget_add_css_class(app->connection_label, "disconnected");
            gtk_widget_remove_css_class(app->connection_label, "connected");
        }
    }
    if (app->device_badge_label != NULL) {
        badge = g_strdup_printf("●  %s", app->device_connected ? name : "DEVICE DISCONNECTED");
        gtk_label_set_text(GTK_LABEL(app->device_badge_label), badge);
        g_free(badge);
    }
    if (app->record_button != NULL) {
        gtk_widget_set_sensitive(
            app->record_button,
            app->device_connected && app->fps_count > 0 && !app->record_transition &&
            !app->streaming && !app->stream_transition &&
            !app->settings_pending && !app->apply_in_progress
        );
    }
    if (app->stream_button != NULL) {
        gtk_widget_set_sensitive(
            app->stream_button,
            app->device_connected && !app->recording && !app->record_transition &&
            !app->settings_pending && !app->apply_in_progress
        );
    }
    update_session_ui(app);
    update_apply_ui(app);
    update_audio_badge_ui(app);
}

static gboolean
device_watch_tick(gpointer user_data)
{
    OpenCentralApp *app = user_data;
    gboolean exists = app->device != NULL && g_file_test(app->device, G_FILE_TEST_EXISTS);

    if (app->closing) return G_SOURCE_CONTINUE;

    if (app->device_connected && !exists) {
        app->device_connected = FALSE;
        if (app->streaming || app->pipeline_streaming || app->stream_transition) {
            finish_streaming_and_restore_preview(app, "Capture device disconnected — stream stopped");
        } else if (app->recording || app->pipeline_recording) {
            finish_recording_and_restore_preview(app);
        } else {
            stop_pipeline(app);
        }
        if (app->control_fd >= 0) {
            close(app->control_fd);
            app->control_fd = -1;
        }
        update_device_ui(app);
        show_feedback(app, "Capture device disconnected");
        return G_SOURCE_CONTINUE;
    }

    if (!app->device_connected) {
        gchar *new_device = find_reconnected_capture_device(app);
        if (new_device == NULL) return G_SOURCE_CONTINUE;

        int fd = open(new_device, O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            g_free(new_device);
            return G_SOURCE_CONTINUE;
        }
        g_free(app->device);
        app->device = new_device;
        app->control_fd = fd;
        g_free(app->capture_device_name);
        app->capture_device_name = detect_capture_device_name(app);
        app->device_connected = TRUE;
        app->vrr_last_pts_ns = GST_CLOCK_TIME_NONE;
        app->vrr_min_interval_ns = G_MAXINT64;
        app->vrr_max_interval_ns = 0;
        app->vrr_sample_count = 0;
        update_device_ui(app);
        if (app->native_p010_session) {
            enforce_native_p010_lock(app);
        } else {
            select_best_supported_mode(app);
            populate_fps_dropdown(app, app->fps);
            update_mode_badge(app);
        }
        show_feedback(app, "%s connected", app->capture_device_name);
        build_pipeline(app, FALSE);
    }
    return G_SOURCE_CONTINUE;
}

static void
close_control_handle(OpenCentralApp *app)
{
    if (app != NULL && app->control_fd >= 0) {
        close(app->control_fd);
        app->control_fd = -1;
    }
}

static void
reopen_control_handle(OpenCentralApp *app)
{
    if (app == NULL || app->control_fd >= 0 || app->device == NULL ||
        !g_file_test(app->device, G_FILE_TEST_EXISTS)) {
        return;
    }

    app->control_fd = open(app->device, O_RDWR | O_NONBLOCK);
    if (app->control_fd < 0 && errno != EBUSY) {
        g_printerr(
            "Linux Capture Studio: could not reopen %s for controls: %s\n",
            app->device,
            g_strerror(errno)
        );
    }
}

static gboolean
wait_for_capture_mode_ready(OpenCentralApp *app, CaptureFormat format, int width, int height)
{
    /* Never issue VIDIOC_TRY_FMT from the GTK main thread.  The GC575 can
     * block inside that ioctl while its previous stream is being released;
     * the RC6 coredump showed the UI thread trapped there during Apply.
     * GStreamer already negotiates the exact caps and reports failure on its
     * bus, so this preflight is intentionally limited to a nonblocking node
     * presence check.  The existing Apply retry/restore state machine handles
     * a device that needs more release time. */
    (void)format;
    (void)width;
    (void)height;

    if (app == NULL || app->device == NULL) {
        return FALSE;
    }
    if (!g_file_test(app->device, G_FILE_TEST_EXISTS)) {
        set_status(app, "The capture device disappeared while changing modes.");
        return FALSE;
    }
    return TRUE;
}

static gboolean
quit_for_gc575_recovery_cb(gpointer user_data)
{
    OpenCentralApp *app = user_data;

    if (app == NULL) {
        return G_SOURCE_REMOVE;
    }

    app->requested_exit_code = LCS_EXIT_RECOVER_GC575;
    app->closing = TRUE;
    if (app->gtk_app != NULL) {
        g_application_quit(G_APPLICATION(app->gtk_app));
    }
    return G_SOURCE_REMOVE;
}

static void
request_gc575_recovery_exit(OpenCentralApp *app, const gchar *reason)
{
    if (app == NULL || !app->is_gc575 || app->recovery_exit_scheduled) {
        return;
    }

    app->recovery_exit_scheduled = TRUE;
    set_status(
        app,
        "%s The launcher will perform one controlled GC575 recovery and reopen the requested native mode.",
        reason != NULL ? reason : "The GC575 capture engine is changing format families."
    );
    g_timeout_add(50, quit_for_gc575_recovery_cb, app);
}

static gboolean
clear_transition_status_cb(gpointer user_data)
{
    OpenCentralApp *app = user_data;

    if (app == NULL) {
        return G_SOURCE_REMOVE;
    }
    app->transition_complete_timer_id = 0;
    if (app->transition_status_file != NULL) {
        g_remove(app->transition_status_file);
    }
    return G_SOURCE_REMOVE;
}

static gboolean
complete_transition_status_cb(gpointer user_data)
{
    OpenCentralApp *app = user_data;

    if (app == NULL || app->transition_status_file == NULL) {
        return G_SOURCE_REMOVE;
    }

    g_file_set_contents(
        app->transition_status_file,
        "Native 4K60 is ready. Opening Linux Capture Studio…\n",
        -1,
        NULL
    );
    if (app->transition_complete_timer_id == 0) {
        app->transition_complete_timer_id = g_timeout_add(900, clear_transition_status_cb, app);
    }
    return G_SOURCE_REMOVE;
}

static gchar *
make_hdr_preview_shader(const OpenCentralApp *app, HdrMode mode, CaptureFormat source_format)
{
    gdouble peak_nits = app != NULL ? CLAMP(app->hdr_peak_nits, 400.0, 4000.0) : 1000.0;
    gdouble paper_white = app != NULL ? CLAMP(app->hdr_paper_white_nits, 80.0, 400.0) : 203.0;
    gdouble black_nits = app != NULL ? CLAMP(app->hdr_black_level_nits, 0.0, 5.0) : 0.0;
    gdouble saturation = app != NULL ? CLAMP(app->hdr_preview_saturation, 0.50, 1.50) : 1.06;
    gdouble peak_normalized = peak_nits / 10000.0;
    gdouble black_normalized = black_nits / 10000.0;
    gdouble peak_ratio = MAX(peak_nits / paper_white, 1.25);
    gboolean correction_enabled = app != NULL && mode != HDR_MODE_SDR &&
        app->hdr_tonemap_switch != NULL &&
        gtk_switch_get_active(GTK_SWITCH(app->hdr_tonemap_switch));

    /* Native HDR sessions keep a permanent GL shader stage in the preview.
     * Disabling preview correction now swaps this identity fragment in place
     * instead of destroying and reopening the GC575 V4L2 transport. */
    if (!correction_enabled) {
        return g_strdup(
            "#version 100\n"
            "#ifdef GL_ES\nprecision highp float;\n#endif\n"
            "varying vec2 v_texcoord; uniform sampler2D tex; "
            "void main(){gl_FragColor=texture2D(tex,v_texcoord);}"
        );
    }

    /* MJPEG/NV12/YUYV capture paths are 8-bit display-referred transports.
     * Treating those samples as ST-2084 and applying the PQ EOTF crushes the
     * entire image into the shadows.  Use a separate, nearly identity
     * display-referred compensation curve for those sources.  Genuine P010
     * sources continue through the PQ/HLG EOTF below. */
    if (source_format != CAPTURE_FORMAT_P010) {
        gdouble exposure = CLAMP((203.0 / paper_white) * 1.35, 0.70, 3.00);
        gdouble shoulder = CLAMP(paper_white / peak_nits, 0.03, 0.50);
        gdouble display_black = CLAMP(black_nits / 1000.0, 0.0, 0.005);

        return g_strdup_printf(
            "#version 100\n"
            "#ifdef GL_ES\nprecision highp float;\n#endif\n"
            "varying vec2 v_texcoord; uniform sampler2D tex; "
            "vec3 tolin(vec3 c){vec3 lo=c/12.92;vec3 hi=pow((c+0.055)/1.055,vec3(2.4));"
            "return mix(lo,hi,step(vec3(0.04045),c));} "
            "vec3 tosrgb(vec3 c){vec3 lo=12.92*c;vec3 hi=1.055*pow(max(c,vec3(0.0)),vec3(1.0/2.4))-0.055;"
            "return mix(lo,hi,step(vec3(0.0031308),c));} "
            "void main(){vec4 s=texture2D(tex,v_texcoord);vec3 l=max(tolin(clamp(s.rgb,0.0,1.0))-vec3(%.9f),vec3(0.0));"
            "l*=%.9f;vec3 t=l/(vec3(1.0)+%.9f*l);float y=dot(t,vec3(0.2126,0.7152,0.0722));"
            "t=clamp(mix(vec3(y),t,%.6f),0.0,1.0);gl_FragColor=vec4(tosrgb(t),s.a);}",
            display_black,
            exposure,
            shoulder,
            saturation
        );
    }

    if (mode == HDR_MODE_HLG) {
        return g_strdup_printf(
            "#version 100\n"
            "#ifdef GL_ES\nprecision highp float;\n#endif\n"
            "varying vec2 v_texcoord; uniform sampler2D tex; "
            "float h(float e){const float a=0.17883277;const float b=0.28466892;const float c=0.55991073;"
            "return e<=0.5?(e*e)/3.0:(exp((e-c)/a)+b)/12.0;} "
            "vec3 hlg(vec3 e){return vec3(h(e.r),h(e.g),h(e.b));} "
            "vec3 gamut(vec3 c){mat3 m=mat3(1.660491,-0.124550,-0.018151,-0.587641,1.132900,-0.100579,-0.072850,-0.008349,1.118730);return m*c;} "
            "vec3 curve(vec3 x,float pr){const float pw=0.72;vec3 low=x*pw;vec3 u=clamp((x-vec3(1.0))/max(pr-1.0,0.25),0.0,1.0);"
            "vec3 high=vec3(pw)+(1.0-pw)*(vec3(1.0)-exp(-3.0*u))/(1.0-exp(-3.0));return mix(low,high,step(vec3(1.0),x));} "
            "void main(){vec4 s=texture2D(tex,v_texcoord);vec3 l=max(gamut(hlg(clamp(s.rgb,0.0,1.0)))-vec3(%.9f),vec3(0.0));"
            "l=min(l,vec3(%.9f));vec3 t=curve(l*%.9f,%.9f);float y=dot(t,vec3(0.2126,0.7152,0.0722));"
            "t=clamp(mix(vec3(y),t,%.6f),0.0,1.0);vec3 o=pow(t,vec3(1.0/2.2));gl_FragColor=vec4(o,s.a);}",
            black_normalized,
            peak_normalized,
            peak_nits / paper_white,
            peak_ratio,
            saturation
        );
    }

    return g_strdup_printf(
        "#version 100\n"
        "#ifdef GL_ES\nprecision highp float;\n#endif\n"
        "varying vec2 v_texcoord; uniform sampler2D tex; "
        "vec3 pq(vec3 n){const float m1=0.1593017578125;const float m2=78.84375;"
        "const float c1=0.8359375;const float c2=18.8515625;const float c3=18.6875;"
        "vec3 p=pow(max(n,vec3(0.0)),vec3(1.0/m2));"
        "return pow(max(p-vec3(c1),vec3(0.0))/max(vec3(c2)-vec3(c3)*p,vec3(0.000001)),vec3(1.0/m1));} "
        "vec3 gamut(vec3 c){mat3 m=mat3(1.660491,-0.124550,-0.018151,-0.587641,1.132900,-0.100579,-0.072850,-0.008349,1.118730);return m*c;} "
        "vec3 curve(vec3 x,float pr){const float pw=0.72;vec3 low=x*pw;vec3 u=clamp((x-vec3(1.0))/max(pr-1.0,0.25),0.0,1.0);"
        "vec3 high=vec3(pw)+(1.0-pw)*(vec3(1.0)-exp(-3.0*u))/(1.0-exp(-3.0));return mix(low,high,step(vec3(1.0),x));} "
        "void main(){vec4 s=texture2D(tex,v_texcoord);vec3 l=max(gamut(pq(clamp(s.rgb,0.0,1.0)))-vec3(%.9f),vec3(0.0));"
        "l=min(l,vec3(%.9f));vec3 t=curve(l*%.9f,%.9f);float y=dot(t,vec3(0.2126,0.7152,0.0722));"
        "t=clamp(mix(vec3(y),t,%.6f),0.0,1.0);vec3 o=pow(t,vec3(1.0/2.2));gl_FragColor=vec4(o,s.a);}",
        black_normalized,
        peak_normalized,
        10000.0 / paper_white,
        peak_ratio,
        saturation
    );
}

static gboolean
gst_element_available(const gchar *factory_name)
{
    GstElementFactory *factory = gst_element_factory_find(factory_name);
    if (factory == NULL) {
        return FALSE;
    }
    gst_object_unref(factory);
    return TRUE;
}

static gboolean
gst_sink_supports_caps_feature(const gchar *factory_name, const gchar *feature_name)
{
    GstElementFactory *factory;
    const GList *templates;
    gboolean supported = FALSE;

    factory = gst_element_factory_find(factory_name);
    if (factory == NULL) {
        return FALSE;
    }

    templates = gst_element_factory_get_static_pad_templates(factory);
    for (const GList *item = templates; item != NULL && !supported; item = item->next) {
        GstStaticPadTemplate *templ = item->data;
        GstCaps *caps;

        if (templ == NULL || templ->direction != GST_PAD_SINK) {
            continue;
        }

        caps = gst_static_pad_template_get_caps(templ);
        if (caps == NULL) {
            continue;
        }

        for (guint i = 0; i < gst_caps_get_size(caps); i++) {
            const GstCapsFeatures *features = gst_caps_get_features(caps, i);
            if (features != NULL && gst_caps_features_contains(features, feature_name)) {
                supported = TRUE;
                break;
            }
        }
        gst_caps_unref(caps);
    }

    gst_object_unref(factory);
    return supported;
}


static gboolean
gst_element_has_property(const gchar *factory_name, const gchar *property_name)
{
    GstElement *element = gst_element_factory_make(factory_name, NULL);
    gboolean present = FALSE;

    if (element == NULL) {
        return FALSE;
    }

    present = g_object_class_find_property(
        G_OBJECT_GET_CLASS(element), property_name
    ) != NULL;
    gst_object_unref(element);
    return present;
}



static const gchar *
hdr_mode_name(HdrMode mode)
{
    if (mode < HDR_MODE_AUTO || mode > HDR_MODE_HLG) {
        return "Unknown";
    }
    return HDR_MODE_NAMES[mode];
}

static const gchar *
hdr_colorimetry(HdrMode mode)
{
    switch (mode) {
    case HDR_MODE_HDR10_PQ:
        return "bt2100-pq";
    case HDR_MODE_HLG:
        return "bt2100-hlg";
    default:
        return "bt709";
    }
}

static const gchar *
v4l2_colorspace_name(__u32 colorspace)
{
    switch (colorspace) {
    case V4L2_COLORSPACE_BT2020:
        return "BT.2020";
    case V4L2_COLORSPACE_REC709:
        return "Rec.709";
    case V4L2_COLORSPACE_SRGB:
        return "sRGB";
    case V4L2_COLORSPACE_JPEG:
        return "JPEG";
    default:
        return "Other";
    }
}

static const gchar *
v4l2_transfer_name(__u32 transfer)
{
    switch (transfer) {
    case V4L2_XFER_FUNC_SMPTE2084:
        return "SMPTE ST 2084 (PQ)";
    case V4L2_XFER_FUNC_709:
        return "Rec.709";
    case V4L2_XFER_FUNC_SRGB:
        return "sRGB";
    case V4L2_XFER_FUNC_DEFAULT:
        return "Default";
    default:
        return "Other";
    }
}

static gboolean
query_v4l2_format(OpenCentralApp *app, struct v4l2_pix_format *pixel)
{
    struct v4l2_format format = {0};

    if (app->control_fd < 0 || pixel == NULL) {
        return FALSE;
    }

    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(app->control_fd, VIDIOC_G_FMT, &format) < 0) {
        return FALSE;
    }

    *pixel = format.fmt.pix;
    return TRUE;
}

static HdrMode
detected_hdr_mode(OpenCentralApp *app)
{
    struct v4l2_pix_format pixel = {0};

    if (!query_v4l2_format(app, &pixel)) {
        return HDR_MODE_SDR;
    }

    if (pixel.colorspace == V4L2_COLORSPACE_BT2020 &&
        pixel.xfer_func == V4L2_XFER_FUNC_SMPTE2084) {
        return HDR_MODE_HDR10_PQ;
    }

    return HDR_MODE_SDR;
}

static HdrMode
effective_hdr_mode(OpenCentralApp *app)
{
    HdrMode selected = (HdrMode)app->hdr_mode;

    if (!app->hdr_enabled) {
        return HDR_MODE_SDR;
    }

    if (selected == HDR_MODE_AUTO) {
        return detected_hdr_mode(app);
    }
    return selected;
}

static gboolean
hdr_requested(OpenCentralApp *app)
{
    HdrMode mode = effective_hdr_mode(app);
    return mode == HDR_MODE_HDR10_PQ || mode == HDR_MODE_HLG;
}

static void
update_hdr_toggle_ui(OpenCentralApp *app)
{
    if (app->hdr_toggle_button == NULL) {
        return;
    }

    if (app->native_p010_session) {
        gtk_button_set_label(GTK_BUTTON(app->hdr_toggle_button), "HDR60 ON");
        gtk_widget_add_css_class(app->hdr_toggle_button, "hdr-toggle-on");
        gtk_widget_add_css_class(app->hdr_toggle_button, "hdr-locked");
        gtk_widget_remove_css_class(app->hdr_toggle_button, "hdr-toggle-off");
    } else if (app->hdr_enabled) {
        gtk_button_set_label(GTK_BUTTON(app->hdr_toggle_button), "HDR ON");
        gtk_widget_add_css_class(app->hdr_toggle_button, "hdr-toggle-on");
        gtk_widget_remove_css_class(app->hdr_toggle_button, "hdr-toggle-off");
        gtk_widget_remove_css_class(app->hdr_toggle_button, "hdr-locked");
    } else {
        gtk_button_set_label(GTK_BUTTON(app->hdr_toggle_button), "HDR OFF");
        gtk_widget_add_css_class(app->hdr_toggle_button, "hdr-toggle-off");
        gtk_widget_remove_css_class(app->hdr_toggle_button, "hdr-toggle-on");
        gtk_widget_remove_css_class(app->hdr_toggle_button, "hdr-locked");
    }
    update_session_ui(app);
}

static gboolean
selected_mode_is_native_p010(OpenCentralApp *app)
{
    const FormatOption *format = current_format(app);
    const ResolutionOption *resolution = current_resolution(app);

    if (app->native_p010_session) {
        return device_mode_supported(
            app, CAPTURE_FORMAT_P010, resolution->width, resolution->height, 60
        );
    }

    return format->format == CAPTURE_FORMAT_P010 &&
           device_mode_supported(
               app, format->format, resolution->width, resolution->height, app->fps
           );
}

static void
update_hdr_status(OpenCentralApp *app)
{
    struct v4l2_pix_format pixel = {0};
    HdrMode selected = (HdrMode)app->hdr_mode;
    HdrMode effective = effective_hdr_mode(app);
    gchar *text;

    if (app->hdr_status_label == NULL) {
        return;
    }

    if (!query_v4l2_format(app, &pixel)) {
        gtk_label_set_text(
            GTK_LABEL(app->hdr_status_label),
            "Signal metadata unavailable"
        );
        return;
    }

    text = g_strdup_printf(
        "Signal: %s / %s — %s%s%s",
        v4l2_colorspace_name(pixel.colorspace),
        v4l2_transfer_name(pixel.xfer_func),
        app->hdr_enabled ? "selected: " : "HDR off • selected: ",
        hdr_mode_name(selected),
        (effective == HDR_MODE_HDR10_PQ || effective == HDR_MODE_HLG)
            ? " (HDR armed)"
            : ""
    );
    gtk_label_set_text(GTK_LABEL(app->hdr_status_label), text);
    g_free(text);
}

static const FormatOption *
current_format(const OpenCentralApp *app)
{
    return &FORMATS[app->format_index];
}

static const ResolutionOption *
current_resolution(const OpenCentralApp *app)
{
    return &RESOLUTIONS[app->resolution_index];
}

static gboolean
format_is_mjpeg(const FormatOption *format)
{
    return format->format == CAPTURE_FORMAT_MJPEG;
}

static gboolean
format_is_10bit(const FormatOption *format)
{
    return format->format == CAPTURE_FORMAT_P010;
}

static __u32
capture_format_fourcc(CaptureFormat format)
{
    switch (format) {
    case CAPTURE_FORMAT_YUYV:
        return V4L2_PIX_FMT_YUYV;
    case CAPTURE_FORMAT_NV12:
        return V4L2_PIX_FMT_NV12;
    case CAPTURE_FORMAT_P010:
        return V4L2_PIX_FMT_P010;
    case CAPTURE_FORMAT_MJPEG:
        return V4L2_PIX_FMT_MJPEG;
    default:
        return 0;
    }
}

static gboolean
device_mode_supported(OpenCentralApp *app, CaptureFormat format, int width, int height, int fps)
{
    struct v4l2_fmtdesc fmt = {0};
    struct v4l2_frmsizeenum size = {0};
    struct v4l2_frmivalenum interval = {0};
    __u32 fourcc = capture_format_fourcc(format);
    int fd;
    gboolean own_fd = FALSE;
    gboolean format_found = FALSE;
    gboolean size_found = FALSE;
    gboolean result = FALSE;

    if (app == NULL || app->device == NULL || fourcc == 0 || fps <= 0) {
        return FALSE;
    }

    fd = app->control_fd;
    if (fd < 0) {
        fd = open(app->device, O_RDONLY | O_NONBLOCK);
        own_fd = fd >= 0;
    }
    if (fd < 0) {
        return FALSE;
    }

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (fmt.index = 0; ioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0; fmt.index++) {
        if (fmt.pixelformat == fourcc) {
            format_found = TRUE;
            break;
        }
    }
    if (!format_found) {
        if (own_fd) { close(fd); }
        return FALSE;
    }

    size.pixel_format = fourcc;
    for (size.index = 0; ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &size) == 0; size.index++) {
        if (size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
            if ((int)size.discrete.width == width && (int)size.discrete.height == height) {
                size_found = TRUE;
                break;
            }
        } else if (size.type == V4L2_FRMSIZE_TYPE_STEPWISE ||
                   size.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
            if (width >= (int)size.stepwise.min_width &&
                width <= (int)size.stepwise.max_width &&
                height >= (int)size.stepwise.min_height &&
                height <= (int)size.stepwise.max_height) {
                size_found = TRUE;
                break;
            }
        }
    }
    if (!size_found) {
        if (own_fd) { close(fd); }
        return FALSE;
    }

    interval.pixel_format = fourcc;
    interval.width = width;
    interval.height = height;
    for (interval.index = 0; ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &interval) == 0; interval.index++) {
        if (interval.type == V4L2_FRMIVAL_TYPE_DISCRETE && interval.discrete.numerator != 0) {
            gdouble actual = (gdouble)interval.discrete.denominator /
                             (gdouble)interval.discrete.numerator;
            if (ABS(actual - (gdouble)fps) < 0.6) {
                result = TRUE;
                break;
            }
        } else if (interval.type == V4L2_FRMIVAL_TYPE_STEPWISE ||
                   interval.type == V4L2_FRMIVAL_TYPE_CONTINUOUS) {
            result = TRUE;
            break;
        }
    }

    if (own_fd) { close(fd); }
    return result;
}

static gboolean
mode_supported(CaptureFormat format, int width, int height, int fps)
{
    for (int i = 0; i < SUPPORTED_MODE_COUNT; i++) {
        const SupportedMode *mode = &SUPPORTED_MODES[i];
        if (mode->format == format && mode->width == width &&
            mode->height == height && mode->fps == fps) {
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean
mode_uses_processing(CaptureFormat format, int width, int height, int fps)
{
    for (int i = 0; i < SUPPORTED_MODE_COUNT; i++) {
        const SupportedMode *mode = &SUPPORTED_MODES[i];
        if (mode->format == format && mode->width == width &&
            mode->height == height && mode->fps == fps) {
            return mode->processed;
        }
    }
    return FALSE;
}

static gboolean
native_source_mode_supported(OpenCentralApp *app, CaptureFormat format, int width, int height, int fps)
{
    return device_mode_supported(app, format, width, height, fps);
}

static gboolean
selectable_mode_supported(OpenCentralApp *app, CaptureFormat format, int width, int height, int fps)
{
    if (!mode_supported(format, width, height, fps)) {
        return FALSE;
    }
    if (device_mode_supported(app, format, width, height, fps)) {
        return TRUE;
    }
    if (format != CAPTURE_FORMAT_P010 || !mode_uses_processing(format, width, height, fps)) {
        return FALSE;
    }
    if (device_mode_supported(app, CAPTURE_FORMAT_NV12, width, height, fps) ||
        device_mode_supported(app, CAPTURE_FORMAT_MJPEG, width, height, fps) ||
        device_mode_supported(app, CAPTURE_FORMAT_YUYV, width, height, fps)) {
        return TRUE;
    }
    /* Never advertise 4K from a lower-resolution source. A selected 4K mode
     * must be backed by a native 3840x2160 transport at the requested rate. */
    return FALSE;
}

static gboolean
mode_uses_processing_for_device(OpenCentralApp *app, CaptureFormat format, int width, int height, int fps)
{
    if (app->is_gc575 && format == CAPTURE_FORMAT_P010 && !app->native_p010_session) {
        return TRUE;
    }
    return mode_uses_processing(format, width, height, fps) &&
           !device_mode_supported(app, format, width, height, fps);
}

static void
resolve_source_mode(OpenCentralApp *app,
                    CaptureFormat requested,
                    int output_width,
                    int output_height,
                    int output_fps,
                    CaptureFormat *source_format,
                    int *source_width,
                    int *source_height,
                    int *source_fps)
{
    *source_format = requested;
    *source_width = output_width;
    *source_height = output_height;
    *source_fps = output_fps;

    /* HDR60 must use a physical source at the selected resolution. Prefer a
     * native 10-bit P010 transport. If the Linux UVC interface exposes only
     * NV12 or MJPEG at 1440p/4K60, keep the transport at that native size and
     * convert pixel format only; never upscale 1080p or 1440p to claim 4K. */
    if (app->native_p010_session && requested == CAPTURE_FORMAT_P010) {
        if (device_mode_supported(app, CAPTURE_FORMAT_P010, output_width, output_height, 60)) {
            *source_format = CAPTURE_FORMAT_P010;
            *source_fps = 60;
            return;
        }
        if (device_mode_supported(app, CAPTURE_FORMAT_NV12, output_width, output_height, 60)) {
            *source_format = CAPTURE_FORMAT_NV12;
            *source_fps = 60;
            return;
        }
        if (device_mode_supported(app, CAPTURE_FORMAT_MJPEG, output_width, output_height, 60)) {
            *source_format = CAPTURE_FORMAT_MJPEG;
            *source_fps = 60;
            return;
        }
        if (device_mode_supported(app, CAPTURE_FORMAT_YUYV, output_width, output_height, 60)) {
            *source_format = CAPTURE_FORMAT_YUYV;
            *source_fps = 60;
            return;
        }
        /* Leave the requested native dimensions intact. Validation will reject
         * the mode rather than falling back to a smaller source and scaling. */
        *source_format = CAPTURE_FORMAT_P010;
        *source_fps = 60;
        return;
    }

    /* GC575 stability rule: never flip the physical UVC interface into P010
     * during preview or output changes. Repeated NV12/MJPEG <-> P010 format
     * changes can wedge the device firmware until a cold reboot. Keep the
     * transport on a stable 60 FPS NV12 source and create P010 only inside
     * the recording/output branch. */
    if (app->is_gc575 && requested == CAPTURE_FORMAT_P010 && !app->native_p010_session) {
        /* Keep the physical preview transport at a genuine native 60 FPS.
         * P010 is produced only in the output branch on the GC575 safe path. */
        if (output_width == 3840 && output_height == 2160 &&
            device_mode_supported(app, CAPTURE_FORMAT_NV12, 2560, 1440, 60)) {
            *source_format = CAPTURE_FORMAT_NV12;
            *source_width = 2560;
            *source_height = 1440;
            *source_fps = 60;
            return;
        }
        if (device_mode_supported(app, CAPTURE_FORMAT_NV12, output_width, output_height, 60)) {
            *source_format = CAPTURE_FORMAT_NV12;
            *source_fps = 60;
            return;
        }
        if (device_mode_supported(app, CAPTURE_FORMAT_NV12, output_width, output_height, output_fps)) {
            *source_format = CAPTURE_FORMAT_NV12;
            return;
        }
        if (device_mode_supported(app, CAPTURE_FORMAT_MJPEG, output_width, output_height, 60)) {
            *source_format = CAPTURE_FORMAT_MJPEG;
            *source_fps = 60;
            return;
        }
        if (device_mode_supported(app, CAPTURE_FORMAT_MJPEG, output_width, output_height, output_fps)) {
            *source_format = CAPTURE_FORMAT_MJPEG;
            return;
        }
    }

    if (device_mode_supported(app, requested, output_width, output_height, output_fps)) {
        return;
    }

    if (requested == CAPTURE_FORMAT_P010 &&
        mode_uses_processing(requested, output_width, output_height, output_fps)) {
        if (device_mode_supported(app, CAPTURE_FORMAT_NV12, output_width, output_height, output_fps)) {
            *source_format = CAPTURE_FORMAT_NV12;
            return;
        }
        if (device_mode_supported(app, CAPTURE_FORMAT_MJPEG, output_width, output_height, output_fps)) {
            *source_format = CAPTURE_FORMAT_MJPEG;
            return;
        }
        if (device_mode_supported(app, CAPTURE_FORMAT_YUYV, output_width, output_height, output_fps)) {
            *source_format = CAPTURE_FORMAT_YUYV;
            return;
        }
    }

    /* No lower-resolution fallback is allowed here. */
}

static void
selected_source_mode(const OpenCentralApp *app,
                     CaptureFormat *source_format,
                     int *source_width,
                     int *source_height,
                     int *source_fps)
{
    const FormatOption *format = current_format(app);
    const ResolutionOption *resolution = current_resolution(app);

    resolve_source_mode(
        (OpenCentralApp *)app,
        format->format,
        resolution->width,
        resolution->height,
        app->fps,
        source_format,
        source_width,
        source_height,
        source_fps
    );
}

static gboolean
source_modes_equal(CaptureFormat left_format,
                   int left_width,
                   int left_height,
                   int left_fps,
                   CaptureFormat right_format,
                   int right_width,
                   int right_height,
                   int right_fps)
{
    return left_format == right_format &&
           left_width == right_width &&
           left_height == right_height &&
           left_fps == right_fps;
}

static const gchar *
capture_format_name(CaptureFormat format)
{
    switch (format) {
    case CAPTURE_FORMAT_YUYV:
        return "YUYV";
    case CAPTURE_FORMAT_NV12:
        return "NV12";
    case CAPTURE_FORMAT_P010:
        return "P010";
    case CAPTURE_FORMAT_MJPEG:
        return "MJPEG";
    default:
        return "unknown";
    }
}

static void
set_capture_selectors_sensitive(OpenCentralApp *app, gboolean sensitive)
{
    gboolean direct_controls = sensitive && !app->native_p010_session;

    gtk_widget_set_sensitive(app->format_dropdown, direct_controls);
    /* HDR60 keeps format and frame rate locked, while resolution selects the
     * physical native transport. Changing 1080p/1440p/4K therefore rebuilds
     * the device mode instead of scaling a smaller source. */
    gtk_widget_set_sensitive(app->resolution_dropdown, sensitive);
    gtk_widget_set_sensitive(app->fps_dropdown, direct_controls && app->fps_count > 0);
    if (app->hdr_toggle_button != NULL) {
        gtk_widget_set_sensitive(app->hdr_toggle_button, sensitive);
    }
    if (app->quality_dropdown != NULL) gtk_widget_set_sensitive(app->quality_dropdown, sensitive);
    if (app->codec_dropdown != NULL) gtk_widget_set_sensitive(app->codec_dropdown, sensitive);
    if (app->container_dropdown != NULL) gtk_widget_set_sensitive(app->container_dropdown, sensitive);
}

static void
populate_fps_dropdown(OpenCentralApp *app, int preferred_fps)
{
    const FormatOption *format = current_format(app);
    const ResolutionOption *resolution = current_resolution(app);
    GtkStringList *list = gtk_string_list_new(NULL);
    guint selected = 0;

    app->selection_update = TRUE;
    app->fps_count = 0;

    for (int i = 0; i < SUPPORTED_MODE_COUNT; i++) {
        const SupportedMode *mode = &SUPPORTED_MODES[i];
        gchar text[32];

        if (mode->format != format->format || mode->width != resolution->width ||
            mode->height != resolution->height ||
            !selectable_mode_supported(app, mode->format, mode->width, mode->height, mode->fps)) {
            continue;
        }
        if (app->fps_count >= MAX_FPS_OPTIONS) {
            break;
        }

        app->fps_values[app->fps_count] = mode->fps;
        g_snprintf(text, sizeof(text), "%d FPS", mode->fps);
        gtk_string_list_append(list, text);
        if (mode->fps == preferred_fps) {
            selected = app->fps_count;
        }
        app->fps_count++;
    }

    if (app->fps_count == 0) {
        gtk_string_list_append(list, "Unavailable");
        app->fps = 0;
        selected = 0;
    } else {
        if (preferred_fps <= 0 || !selectable_mode_supported(
                app, format->format, resolution->width, resolution->height, preferred_fps)) {
            for (guint i = 0; i < app->fps_count; i++) {
                if (app->fps_values[i] == 60) {
                    selected = i;
                    break;
                }
            }
        }
        app->fps = app->fps_values[selected];
    }

    gtk_drop_down_set_model(GTK_DROP_DOWN(app->fps_dropdown), G_LIST_MODEL(list));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->fps_dropdown), selected);
    g_object_unref(list);
    gtk_widget_set_sensitive(app->fps_dropdown, app->fps_count > 0 && !app->recording);
    if (app->record_button != NULL && !app->recording && !app->record_transition &&
        !app->streaming && !app->stream_transition && !app->stopping) {
        gtk_widget_set_sensitive(app->record_button, app->fps_count > 0);
    }
    app->selection_update = FALSE;

    if (app->fps_count == 0 && app->status_label != NULL) {
        set_status(app, "%s is unavailable at %s", format->name, resolution->name);
    } else if (app->fps_count > 0 && app->status_label != NULL &&
               mode_uses_processing_for_device(
                   app,
                   format->format,
                   resolution->width,
                   resolution->height,
                   app->fps)) {
        CaptureFormat source;
        int source_width;
        int source_height;
        int source_fps;

        resolve_source_mode(
            app,
            format->format,
            resolution->width,
            resolution->height,
            app->fps,
            &source,
            &source_width,
            &source_height,
            &source_fps
        );
        set_status(
            app,
            "P010 processing mode: capture %s %dx%d at %d FPS, then scale/convert to P010 %dx%d at %d FPS.",
            capture_format_name(source),
            source_width,
            source_height,
            source_fps,
            resolution->width,
            resolution->height,
            app->fps
        );
    }
}

static void
enforce_native_p010_lock(OpenCentralApp *app)
{
    int resolution_index;

    if (app == NULL || !app->native_p010_session) return;

    if (app->session_mode == SESSION_MODE_HDR60_4K) resolution_index = 3;
    else if (app->session_mode == SESSION_MODE_HDR60_1440) resolution_index = 2;
    else resolution_index = 1;

    app->selection_update = TRUE;
    app->format_index = 2;
    app->resolution_index = resolution_index;
    app->fps = 60;
    app->hdr_mode = HDR_MODE_HDR10_PQ;
    app->hdr_enabled = TRUE;
    if (app->format_dropdown != NULL) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->format_dropdown), app->format_index);
    }
    if (app->resolution_dropdown != NULL) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->resolution_dropdown), app->resolution_index);
    }
    app->selection_update = FALSE;

    if (app->fps_dropdown != NULL) populate_fps_dropdown(app, 60);

    app->selection_update = TRUE;
    if (app->hdr_dropdown != NULL) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->hdr_dropdown), HDR_MODE_HDR10_PQ);
        gtk_widget_set_sensitive(app->hdr_dropdown, FALSE);
    }
    if (app->hdr_toggle_button != NULL) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->hdr_toggle_button), TRUE);
        gtk_widget_set_sensitive(app->hdr_toggle_button, TRUE);
    }
    if (app->format_dropdown != NULL) gtk_widget_set_sensitive(app->format_dropdown, FALSE);
    if (app->resolution_dropdown != NULL) gtk_widget_set_sensitive(app->resolution_dropdown, TRUE);
    if (app->fps_dropdown != NULL) gtk_widget_set_sensitive(app->fps_dropdown, FALSE);
    app->selection_update = FALSE;

    update_hdr_toggle_ui(app);
    update_hdr_status(app);
    update_mode_badge(app);
}

static void
select_best_supported_mode(OpenCentralApp *app)
{
    static const struct {
        CaptureFormat format;
        int width;
        int height;
        int fps;
    } preferred[] = {
        {CAPTURE_FORMAT_NV12, 2560, 1440, 60},
        {CAPTURE_FORMAT_NV12, 1920, 1080, 60},
        {CAPTURE_FORMAT_YUYV, 1920, 1080, 60},
        {CAPTURE_FORMAT_MJPEG, 2560, 1440, 60},
        {CAPTURE_FORMAT_MJPEG, 1920, 1080, 60},
        {CAPTURE_FORMAT_MJPEG, 3840, 2160, 30},
        {CAPTURE_FORMAT_NV12, 1280, 720, 60},
        {CAPTURE_FORMAT_YUYV, 1280, 720, 60},
        {CAPTURE_FORMAT_MJPEG, 1280, 720, 60},
    };

    if (selectable_mode_supported(
            app, current_format(app)->format, current_resolution(app)->width,
            current_resolution(app)->height, app->fps)) {
        return;
    }

    for (guint c = 0; c < G_N_ELEMENTS(preferred); c++) {
        int format_index = -1;
        int resolution_index = -1;

        if (!selectable_mode_supported(
                app, preferred[c].format, preferred[c].width,
                preferred[c].height, preferred[c].fps)) {
            continue;
        }
        for (int i = 0; i < FORMAT_COUNT; i++) {
            if (FORMATS[i].format == preferred[c].format) {
                format_index = i;
                break;
            }
        }
        for (int i = 0; i < RESOLUTION_COUNT; i++) {
            if (RESOLUTIONS[i].width == preferred[c].width &&
                RESOLUTIONS[i].height == preferred[c].height) {
                resolution_index = i;
                break;
            }
        }
        if (format_index < 0 || resolution_index < 0) {
            continue;
        }

        app->selection_update = TRUE;
        app->format_index = format_index;
        app->resolution_index = resolution_index;
        app->fps = preferred[c].fps;
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->format_dropdown), format_index);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->resolution_dropdown), resolution_index);
        app->selection_update = FALSE;
        return;
    }
}

static gboolean
current_mode_differs_from_last_good(const OpenCentralApp *app)
{
    return app->have_last_good_mode &&
           (app->format_index != app->last_good_format_index ||
            app->resolution_index != app->last_good_resolution_index ||
            app->fps != app->last_good_fps ||
            app->session_mode != app->last_good_session_mode ||
            app->hdr_mode != app->last_good_hdr_mode ||
            app->hdr_enabled != app->last_good_hdr_enabled ||
            app->native_p010_session != app->last_good_native_p010_session);
}

static void
restore_last_good_mode(OpenCentralApp *app)
{
    if (app == NULL || !app->have_last_good_mode) {
        return;
    }

    /* Restore the complete proven session, not only format/resolution.  RC8
     * kept native_p010_session set to the failed 4K request, so the old code
     * immediately re-enforced 4K and retried the broken MJPEG mode forever. */
    app->session_mode = app->last_good_session_mode;
    app->native_p010_session = app->last_good_native_p010_session;
    app->format_index = app->last_good_format_index;
    app->resolution_index = app->last_good_resolution_index;
    app->fps = app->last_good_fps;
    app->hdr_mode = app->last_good_hdr_mode;
    app->hdr_enabled = app->last_good_hdr_enabled;

    app->selection_update = TRUE;
    if (app->session_dropdown != NULL) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->session_dropdown), app->session_mode);
    }
    if (app->format_dropdown != NULL) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->format_dropdown), app->format_index);
    }
    if (app->resolution_dropdown != NULL) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->resolution_dropdown), app->resolution_index);
    }
    if (app->hdr_dropdown != NULL) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->hdr_dropdown), app->hdr_mode);
    }
    if (app->hdr_toggle_button != NULL) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->hdr_toggle_button), app->hdr_enabled);
    }
    app->selection_update = FALSE;

    if (app->native_p010_session) {
        enforce_native_p010_lock(app);
    } else {
        populate_fps_dropdown(app, app->last_good_fps);
    }
    sync_pending_settings_from_current(app);
    update_session_ui(app);
    update_hdr_toggle_ui(app);
    update_hdr_status(app);
    update_mode_badge(app);
}

static gchar *
default_recording_directory(void)
{
    const gchar *videos = g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS);
    if (videos != NULL && videos[0] != '\0') {
        return g_build_filename(videos, "Linux Capture Studio", NULL);
    }
    return g_build_filename(g_get_home_dir(), "Videos", "Linux Capture Studio", NULL);
}

static gchar *
quote_gst_string(const gchar *text)
{
    gchar *escaped = g_strescape(text != NULL ? text : "", NULL);
    gchar *quoted = g_strdup_printf("\"%s\"", escaped != NULL ? escaped : "");
    g_free(escaped);
    return quoted;
}

static gchar *
replace_first_token(const gchar *source,
                    const gchar *token,
                    const gchar *replacement)
{
    const gchar *match;
    GString *result;

    if (source == NULL || token == NULL || replacement == NULL) {
        return g_strdup(source != NULL ? source : "");
    }

    match = g_strstr_len(source, -1, token);
    if (match == NULL) {
        return g_strdup(source);
    }

    result = g_string_new_len(source, (gssize)(match - source));
    g_string_append(result, replacement);
    g_string_append(result, match + strlen(token));
    return g_string_free(result, FALSE);
}

static const gchar *
selected_audio_output_id(const OpenCentralApp *app)
{
    guint selected;

    if (app->audio_output_dropdown == NULL || app->audio_output_ids == NULL) {
        return "";
    }

    selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->audio_output_dropdown));
    if (selected >= app->audio_output_ids->len) {
        return "";
    }

    return g_ptr_array_index(app->audio_output_ids, selected);
}

static gchar *
make_monitor_sink_chain(OpenCentralApp *app)
{
    const gchar *sink_id = selected_audio_output_id(app);

    if (gst_element_available("pulsesink")) {
        if (sink_id != NULL && sink_id[0] != '\0') {
            gchar *sink_q = quote_gst_string(sink_id);
            gchar *chain = g_strdup_printf(
                "pulsesink name=monitor_sink client-name=LinuxCaptureStudio device=%s sync=false",
                sink_q
            );
            g_free(sink_q);
            return chain;
        }
        return g_strdup(
            "pulsesink name=monitor_sink client-name=LinuxCaptureStudio sync=false"
        );
    }

    return g_strdup("autoaudiosink name=monitor_sink sync=false");
}

static void
audio_output_model_add(GtkStringList *model,
                       GPtrArray *ids,
                       const gchar *label,
                       const gchar *id)
{
    gtk_string_list_append(model, label != NULL ? label : id);
    g_ptr_array_add(ids, g_strdup(id != NULL ? id : ""));
}

static gboolean
refresh_audio_outputs(OpenCentralApp *app, gboolean announce)
{
    gchar *stdout_text = NULL;
    gchar *stderr_text = NULL;
    gint exit_status = 0;
    GError *error = NULL;
    GtkStringList *model;
    GPtrArray *ids;
    gchar *previous_id;
    guint selected_index = 0;
    gboolean command_ok;

    if (app->audio_output_dropdown == NULL) {
        return FALSE;
    }

    previous_id = g_strdup(selected_audio_output_id(app));
    if ((previous_id == NULL || previous_id[0] == '\0') && app->profile_audio_output_id != NULL) {
        g_free(previous_id);
        previous_id = g_strdup(app->profile_audio_output_id);
    }
    model = gtk_string_list_new(NULL);
    ids = g_ptr_array_new_with_free_func(g_free);
    audio_output_model_add(model, ids, "System default (PipeWire)", "");

    command_ok = g_spawn_command_line_sync(
        "sh -c 'LC_ALL=C pactl list sinks'",
        &stdout_text,
        &stderr_text,
        &exit_status,
        &error
    );

    if (command_ok && exit_status == 0 && stdout_text != NULL) {
        gchar **lines = g_strsplit(stdout_text, "\n", -1);
        gchar *pending_name = NULL;

        for (guint i = 0; lines[i] != NULL; i++) {
            gchar *line = g_strstrip(lines[i]);

            if (g_str_has_prefix(line, "Name:")) {
                g_free(pending_name);
                pending_name = g_strdup(g_strstrip(line + strlen("Name:")));
                continue;
            }

            if (pending_name != NULL && g_str_has_prefix(line, "Description:")) {
                const gchar *description = g_strstrip(line + strlen("Description:"));
                gchar *label = g_strdup_printf(
                    "%s%s%s",
                    description[0] != '\0' ? description : pending_name,
                    description[0] != '\0' ? " — " : "",
                    description[0] != '\0' ? pending_name : ""
                );
                audio_output_model_add(model, ids, label, pending_name);
                g_free(label);
                g_clear_pointer(&pending_name, g_free);
            }
        }

        if (pending_name != NULL) {
            audio_output_model_add(model, ids, pending_name, pending_name);
            g_free(pending_name);
        }
        g_strfreev(lines);
    }

    if (app->audio_output_model != NULL) {
        g_object_unref(app->audio_output_model);
    }
    if (app->audio_output_ids != NULL) {
        g_ptr_array_unref(app->audio_output_ids);
    }
    app->audio_output_model = model;
    app->audio_output_ids = ids;

    for (guint i = 0; i < ids->len; i++) {
        const gchar *candidate = g_ptr_array_index(ids, i);
        if (g_strcmp0(candidate, previous_id) == 0) {
            selected_index = i;
            break;
        }
    }

    app->audio_output_update = TRUE;
    gtk_drop_down_set_model(
        GTK_DROP_DOWN(app->audio_output_dropdown),
        G_LIST_MODEL(app->audio_output_model)
    );
    gtk_drop_down_set_selected(
        GTK_DROP_DOWN(app->audio_output_dropdown), selected_index
    );
    app->audio_output_update = FALSE;

    if (announce) {
        if (ids->len > 1) {
            set_status(app, "Found %u monitor audio outputs", ids->len - 1);
        } else if (error != NULL) {
            set_status(app, "Could not enumerate PipeWire outputs: %s", error->message);
        } else {
            set_status(app, "No separate PipeWire outputs were found; using the system default");
        }
    }

    g_free(previous_id);
    g_free(stdout_text);
    g_free(stderr_text);
    g_clear_error(&error);
    return ids->len > 1;
}

static void
on_audio_refresh_clicked(GtkButton *button, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    (void)button;
    refresh_audio_outputs(app, TRUE);
}

static void
on_audio_output_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    const gchar *sink_id;
    GstElement *sink;

    (void)object;
    (void)pspec;

    if (app->audio_output_update) {
        return;
    }

    sink_id = selected_audio_output_id(app);
    if (app->monitor_pipeline != NULL) {
        sink = gst_bin_get_by_name(GST_BIN(app->monitor_pipeline), "monitor_sink");
    } else if (app->pipeline != NULL) {
        sink = gst_bin_get_by_name(GST_BIN(app->pipeline), "monitor_sink");
    } else {
        return;
    }
    if (sink == NULL) {
        set_status(app, "The current audio backend cannot switch outputs live");
        return;
    }

    if (g_object_class_find_property(G_OBJECT_GET_CLASS(sink), "device") == NULL) {
        gst_object_unref(sink);
        set_status(app, "Install the GStreamer PulseAudio plug-in to select a PipeWire output");
        return;
    }

    gst_element_set_state(sink, GST_STATE_READY);
    g_object_set(
        sink,
        "device",
        sink_id != NULL && sink_id[0] != '\0' ? sink_id : NULL,
        NULL
    );
    gst_element_sync_state_with_parent(sink);
    gst_object_unref(sink);

    set_status(
        app,
        "Monitor audio output: %s",
        sink_id != NULL && sink_id[0] != '\0' ? sink_id : "system default"
    );
    save_device_profile(app);
}

static gdouble
monitor_gain(OpenCentralApp *app)
{
    if (app->monitor_switch == NULL ||
        !gtk_switch_get_active(GTK_SWITCH(app->monitor_switch))) {
        return 0.0;
    }

    if (app->monitor_volume == NULL) {
        return 1.0;
    }

    return gtk_range_get_value(GTK_RANGE(app->monitor_volume));
}

static void
apply_monitor_gain(OpenCentralApp *app)
{
    GstElement *volume = NULL;

    if (app->monitor_pipeline != NULL) {
        volume = gst_bin_get_by_name(GST_BIN(app->monitor_pipeline), "monitor_volume");
    }
    if (volume == NULL && app->pipeline != NULL) {
        volume = gst_bin_get_by_name(GST_BIN(app->pipeline), "monitor_volume");
    }
    if (volume == NULL) {
        return;
    }

    g_object_set(volume, "volume", monitor_gain(app), NULL);
    gst_object_unref(volume);
}

static void
on_monitor_switch_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    (void)object;
    (void)pspec;
    apply_monitor_gain(app);
    update_audio_badge_ui(app);
    save_device_profile(app);
}

static void
on_monitor_volume_changed(GtkRange *range, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    (void)range;
    apply_monitor_gain(app);
    save_device_profile(app);
}

static gint64
audio_sync_offset_ns(OpenCentralApp *app)
{
    gdouble milliseconds = 0.0;

    if (app->audio_sync_scale != NULL) {
        milliseconds = gtk_range_get_value(GTK_RANGE(app->audio_sync_scale));
    }

    return (gint64)(milliseconds * (gdouble)GST_MSECOND);
}

static void
apply_audio_sync(OpenCentralApp *app)
{
    GstElement *sync_element = NULL;

    if (app->monitor_pipeline != NULL) {
        sync_element = gst_bin_get_by_name(GST_BIN(app->monitor_pipeline), "audio_sync");
    }
    if (sync_element == NULL && app->pipeline != NULL) {
        sync_element = gst_bin_get_by_name(GST_BIN(app->pipeline), "audio_sync");
    }
    if (sync_element == NULL) {
        return;
    }

    g_object_set(sync_element, "ts-offset", audio_sync_offset_ns(app), NULL);
    gst_object_unref(sync_element);
}

static void
on_audio_sync_changed(GtkRange *range, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    (void)range;
    apply_audio_sync(app);
    save_device_profile(app);
}

static void
stop_monitor_pipeline(OpenCentralApp *app)
{
    if (app == NULL || app->monitor_pipeline == NULL) {
        return;
    }

    gst_element_set_state(app->monitor_pipeline, GST_STATE_NULL);
    gst_element_get_state(app->monitor_pipeline, NULL, NULL, 1000 * GST_MSECOND);
    gst_object_unref(app->monitor_pipeline);
    app->monitor_pipeline = NULL;
    update_audio_badge_ui(app);
}

static gboolean
start_monitor_pipeline(OpenCentralApp *app)
{
    const gchar *audio_device;
    const gchar *backend_device;
    gboolean is_pulse;
    gchar *audio_q;
    gchar *source_chain;
    gchar *sink_chain;
    gchar *description;
    GError *error = NULL;
    GstStateChangeReturn state_result;

    stop_monitor_pipeline(app);

    if (app == NULL || app->audio_capture_disabled || app->audio_entry == NULL) {
        return FALSE;
    }

    audio_device = gtk_editable_get_text(GTK_EDITABLE(app->audio_entry));
    if (audio_device == NULL || audio_device[0] == '\0' || g_strcmp0(audio_device, "default") == 0) {
        audio_device = "default";
    }
    is_pulse = g_str_has_prefix(audio_device, "pulse:");
    backend_device = is_pulse ? audio_device + 6 : audio_device;
    audio_q = quote_gst_string(backend_device);

    if (is_pulse && gst_element_available("pulsesrc")) {
        source_chain = g_strdup_printf(
            "pulsesrc device=%s do-timestamp=true",
            audio_q
        );
    } else {
        source_chain = g_strdup_printf(
            "alsasrc device=%s do-timestamp=true",
            audio_q
        );
    }
    sink_chain = make_monitor_sink_chain(app);
    description = g_strdup_printf(
        "%s "
        "! queue max-size-time=200000000 leaky=downstream "
        "! audioconvert ! audioresample "
        "! audio/x-raw,format=F32LE,rate=48000,channels=2 "
        "! audiorate "
        "! identity name=audio_sync ts-offset=%" G_GINT64_FORMAT " "
        "! volume name=monitor_volume volume=%.3f mute=false "
        "! audioconvert ! audioresample "
        "! %s",
        source_chain,
        audio_sync_offset_ns(app),
        monitor_gain(app),
        sink_chain
    );

    g_print("Linux Capture Studio: starting independent live-audio monitor from %s.\n", audio_device);
    app->monitor_pipeline = gst_parse_launch(description, &error);
    g_free(description);
    g_free(sink_chain);
    g_free(source_chain);
    g_free(audio_q);

    if (app->monitor_pipeline == NULL) {
        g_printerr(
            "Linux Capture Studio: could not build live-audio monitor: %s\n",
            error != NULL ? error->message : "unknown error"
        );
        g_clear_error(&error);
        return FALSE;
    }

    state_result = gst_element_set_state(app->monitor_pipeline, GST_STATE_PLAYING);
    if (state_result != GST_STATE_CHANGE_FAILURE) {
        state_result = gst_element_get_state(
            app->monitor_pipeline,
            NULL,
            NULL,
            2500 * GST_MSECOND
        );
    }
    if (state_result == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Linux Capture Studio: live-audio monitor failed to start.\n");
        stop_monitor_pipeline(app);
        update_audio_badge_ui(app);
        return FALSE;
    }

    update_audio_badge_ui(app);
    return TRUE;
}

static void
set_fullscreen_mode(OpenCentralApp *app, gboolean fullscreen)
{
    app->fullscreen = fullscreen;

    if (fullscreen && app->settings_window != NULL) {
        gtk_widget_set_visible(app->settings_window, FALSE);
    }

    if (app->toolbar != NULL) {
        gtk_widget_set_visible(app->toolbar, !fullscreen);
    }
    if (app->settings_frame != NULL) {
        gtk_widget_set_visible(app->settings_frame, !fullscreen);
    }
    if (app->controls_frame != NULL) {
        gtk_widget_set_visible(app->controls_frame, !fullscreen);
    }
    if (app->status_label != NULL) {
        gtk_widget_set_visible(app->status_label, !fullscreen);
    }
    if (app->device_badge_label != NULL) {
        gtk_widget_set_visible(app->device_badge_label, !fullscreen);
    }
    if (app->mode_badge_label != NULL) {
        gtk_widget_set_visible(app->mode_badge_label, !fullscreen);
    }
    if (app->audio_badge_label != NULL) {
        gtk_widget_set_visible(app->audio_badge_label, !fullscreen);
    }
    if (app->root != NULL) {
        gtk_widget_set_margin_top(app->root, fullscreen ? 0 : 14);
        gtk_widget_set_margin_bottom(app->root, fullscreen ? 0 : 14);
        gtk_widget_set_margin_start(app->root, fullscreen ? 0 : 14);
        gtk_widget_set_margin_end(app->root, fullscreen ? 0 : 14);
    }
    if (app->video_frame != NULL) {
        if (fullscreen) {
            gtk_widget_remove_css_class(app->video_frame, "video-card");
            gtk_widget_add_css_class(app->video_frame, "fullscreen-stage");
        } else {
            gtk_widget_remove_css_class(app->video_frame, "fullscreen-stage");
            gtk_widget_add_css_class(app->video_frame, "video-card");
        }
    }
    if (fullscreen) {
        gtk_window_fullscreen(GTK_WINDOW(app->window));
    } else {
        gtk_window_unfullscreen(GTK_WINDOW(app->window));
    }
}

static void
toggle_fullscreen(OpenCentralApp *app)
{
    set_fullscreen_mode(app, !app->fullscreen);
}

static void
on_fullscreen_clicked(GtkButton *button, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    (void)button;
    toggle_fullscreen(app);
}

static gboolean
on_window_key_pressed(GtkEventControllerKey *controller,
                      guint keyval,
                      guint keycode,
                      GdkModifierType state,
                      gpointer user_data)
{
    OpenCentralApp *app = user_data;
    (void)controller;
    (void)keycode;
    (void)state;

    if (keyval == GDK_KEY_F11) {
        toggle_fullscreen(app);
        return TRUE;
    }

    if (keyval == GDK_KEY_Escape && app->fullscreen) {
        set_fullscreen_mode(app, FALSE);
        return TRUE;
    }

    return FALSE;
}

static void
on_picture_pressed(GtkGestureClick *gesture,
                   gint n_press,
                   gdouble x,
                   gdouble y,
                   gpointer user_data)
{
    OpenCentralApp *app = user_data;
    (void)gesture;
    (void)x;
    (void)y;

    if (n_press == 2) {
        toggle_fullscreen(app);
    }
}

static GstPadProbeReturn
on_video_buffer(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    (void)pad;

    if ((GST_PAD_PROBE_INFO_TYPE(info) &
         (GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST)) != 0) {
        GstBuffer *buffer = NULL;
        GstClockTime pts = GST_CLOCK_TIME_NONE;

        app->last_video_buffer_us = g_get_monotonic_time();
        app->watchdog_stall_ticks = 0;
        if (app->transition_status_file != NULL &&
            !app->transition_complete_scheduled) {
            app->transition_complete_scheduled = TRUE;
            g_idle_add(complete_transition_status_cb, app);
        }
        /* A recovery attempt is only successful after a real video buffer
         * arrives. gst_element_set_state(...PLAYING) may return success before
         * the asynchronous V4L2 TRY_FMT negotiation fails, so never reset the
         * retry counter merely because build_pipeline() returned TRUE. */
        app->recovery_waiting_for_frame = FALSE;
        if (!app->apply_rolling_back) {
            app->recovery_count = 0;
        }
        if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
            buffer = GST_PAD_PROBE_INFO_BUFFER(info);
            if (buffer != NULL) pts = GST_BUFFER_PTS(buffer);
        }
        if (app->vrr_monitor_enabled && GST_CLOCK_TIME_IS_VALID(pts)) {
            if (app->vrr_last_pts_ns != (gint64)GST_CLOCK_TIME_NONE &&
                (gint64)pts > app->vrr_last_pts_ns) {
                gint64 interval = (gint64)pts - app->vrr_last_pts_ns;
                if (interval > 1000000 && interval < GST_SECOND) {
                    app->vrr_min_interval_ns = MIN(app->vrr_min_interval_ns, interval);
                    app->vrr_max_interval_ns = MAX(app->vrr_max_interval_ns, interval);
                    app->vrr_sample_count++;
                }
            }
            app->vrr_last_pts_ns = (gint64)pts;
        }
        if (app->apply_in_progress && !app->apply_rolling_back) {
            app->apply_validation_frame_count++;
        }
        if (app->apply_rolling_back) {
            app->apply_validation_frame_count++;
            if (!app->rollback_finish_scheduled &&
                app->apply_validation_frame_count >= 10 &&
                g_get_monotonic_time() - app->apply_validation_started_us >= 300000) {
                app->rollback_finish_scheduled = TRUE;
                g_idle_add(finish_failed_apply_rollback_cb, app);
            }
        }
        if (!app->pipeline_recording && !app->pipeline_streaming && app->have_active_mode &&
            !app->apply_in_progress && !app->apply_rolling_back) {
            app->last_good_format_index = app->active_format_index;
            app->last_good_resolution_index = app->active_resolution_index;
            app->last_good_fps = app->active_fps;
            app->last_good_session_mode = app->session_mode;
            app->last_good_hdr_mode = app->hdr_mode;
            app->last_good_hdr_enabled = app->hdr_enabled;
            app->last_good_native_p010_session = app->native_p010_session;
            app->have_last_good_mode = TRUE;
            if (!app->initial_session_persisted) {
                app->initial_session_persisted = TRUE;
                g_idle_add(persist_initial_session_cb, app);
            }
        }
    }

    return GST_PAD_PROBE_OK;
}

static gboolean
recover_preview_cb(gpointer user_data)
{
    OpenCentralApp *app = user_data;

    app->recovery_timer_id = 0;
    if (app->closing || app->recording || app->stopping) {
        return G_SOURCE_REMOVE;
    }

    if (app->recovery_count >= 3) {
        if (app->apply_rolling_back && app->is_gc575 &&
            !app->rollback_safe_fallback_attempted) {
            app->rollback_safe_fallback_attempted = TRUE;
            app->recovery_waiting_for_frame = FALSE;
            stop_pipeline(app);
            select_safe_hdr1080_mode(app);
            persist_pending_session_mode(SESSION_MODE_HDR60_1080);
            set_status(app, "The previous mode could not be reopened. Resetting the GC575 once and returning to safe genuine P010 1080p60…");
            request_gc575_recovery_exit(app, "Rollback could not produce a real frame after three attempts.");
            return G_SOURCE_REMOVE;
        }

        /* No frame has ever validated the saved startup mode.  On the GC575,
         * one controlled launcher recovery into the proven 1080p60 P010 mode
         * is safer than reopening the same rejected NV12/MJPEG mode forever.
         * The environment marker prevents a second recovery-exit loop if the
         * safe mode itself cannot start. */
        if (app->is_gc575 && !app->have_last_good_mode &&
            !app->startup_safe_fallback_attempted) {
            app->startup_safe_fallback_attempted = TRUE;
            app->recovery_waiting_for_frame = FALSE;
            stop_pipeline(app);
            select_safe_hdr1080_mode(app);
            persist_pending_session_mode(SESSION_MODE_HDR60_1080);
            set_status(
                app,
                "The saved startup mode was rejected three times. Resetting the GC575 once and reopening in the proven 1080p60 P010 mode…"
            );
            show_feedback(app, "Recovering the GC575 into safe 1080p60");
            request_gc575_recovery_exit(
                app,
                "The saved startup mode could not initialize after three attempts."
            );
            return G_SOURCE_REMOVE;
        }

        set_status(
            app,
            "Capture recovery paused after three failed reopen attempts. "
            "The application will remain open instead of retrying indefinitely."
        );
        show_feedback(app, "Capture is paused — reconnect the device or choose a stable mode");
        app->recovery_waiting_for_frame = FALSE;
        return G_SOURCE_REMOVE;
    }

    app->recovery_count++;
    app->recovery_waiting_for_frame = FALSE;
    set_status(app, "Reopening the capture device after a stalled video signal (recovery %u of 3)…",
               app->recovery_count);

    if (build_pipeline(app, FALSE)) {
        /* PLAYING is provisional.  Rollback is not complete until
         * on_video_buffer() observes a real frame from the restored mode.
         * If no frame and no bus error arrive, retry after four seconds. */
        app->recovery_waiting_for_frame = TRUE;
        if (app->apply_rolling_back) {
            app->apply_validation_started_us = g_get_monotonic_time();
            app->apply_validation_frame_count = 0;
        }
        if (app->apply_rolling_back && app->recovery_timer_id == 0) {
            app->recovery_timer_id = g_timeout_add(4000, recover_preview_cb, app);
        }
    } else if (!app->closing && app->recovery_count < 3) {
        app->recovery_timer_id = g_timeout_add(1500, recover_preview_cb, app);
    } else if (!app->closing && app->apply_rolling_back) {
        app->recovery_timer_id = g_timeout_add(1500, recover_preview_cb, app);
    }

    return G_SOURCE_REMOVE;
}

static void
schedule_preview_recovery(OpenCentralApp *app, const gchar *reason)
{
    if (app->closing || app->recording || app->stopping ||
        app->recovery_timer_id != 0) {
        return;
    }

    set_status(app, "%s Reopening the capture device automatically…", reason);
    app->recovery_timer_id = g_timeout_add(300, recover_preview_cb, app);
}

static gboolean
video_watchdog_tick(gpointer user_data)
{
    OpenCentralApp *app = user_data;
    gint64 now;
    gint64 silent_us;
    gint64 limit_us;

    if (app->closing || app->pipeline == NULL || app->rebuilding ||
        app->stopping || app->apply_in_progress || app->apply_rolling_back) {
        return G_SOURCE_CONTINUE;
    }

    now = g_get_monotonic_time();
    if (app->pipeline_started_us == 0) {
        return G_SOURCE_CONTINUE;
    }

    {
        gboolean native_4k_mjpeg =
            app->active_source_format == CAPTURE_FORMAT_MJPEG &&
            app->active_source_width >= 3840 &&
            app->active_source_height >= 2160 &&
            app->active_source_fps >= 50;

        if (app->last_video_buffer_us == 0) {
            silent_us = now - app->pipeline_started_us;
            /* The GC575 needs extra time after an xHCI reset before its first
             * native 4K MJPEG payload.  Eight seconds caused a false recovery
             * while the card was still completing its normal reconnect. */
            limit_us = (native_4k_mjpeg ? 25 : 8) * G_USEC_PER_SEC;
        } else {
            silent_us = now - app->last_video_buffer_us;
            /* 4K MJPEG can pause briefly while the UVC engine and CPU decoder
             * refill after a caps change.  Keep the fast three-second watchdog
             * for ordinary modes, but avoid tearing down a healthy 4K stream. */
            limit_us = (native_4k_mjpeg ? 12 : 3) * G_USEC_PER_SEC;
        }

        if (silent_us < limit_us) {
            app->watchdog_stall_ticks = 0;
            return G_SOURCE_CONTINUE;
        }

        app->watchdog_stall_ticks++;
        if (native_4k_mjpeg && app->watchdog_stall_ticks < 2) {
            return G_SOURCE_CONTINUE;
        }
    }

    if (app->recording || app->pipeline_recording) {
        app->record_transition = TRUE;
        app->stopping = TRUE;
        gtk_widget_set_sensitive(app->record_button, FALSE);
        gtk_button_set_label(GTK_BUTTON(app->record_button), "Finalizing…");
        set_status(app,
                   "Capture video stalled while recording; finalizing the file safely…");
        if (app->record_stop_timeout_id != 0) {
            g_source_remove(app->record_stop_timeout_id);
        }
        app->record_stop_timeout_id = g_timeout_add_seconds(5, record_stop_timeout_cb, app);
        if (!gst_element_send_event(app->pipeline, gst_event_new_eos())) {
            finish_recording_and_restore_preview(app);
        }
    } else {
        schedule_preview_recovery(
            app,
            "The capture stream stopped delivering frames after a signal or caps change."
        );
    }

    return G_SOURCE_CONTINUE;
}

static gboolean
hide_feedback_cb(gpointer user_data)
{
    OpenCentralApp *app = user_data;

    app->feedback_timeout_id = 0;
    if (app->feedback_label != NULL) {
        gtk_widget_set_visible(app->feedback_label, FALSE);
    }
    return G_SOURCE_REMOVE;
}

static void
show_feedback(OpenCentralApp *app, const gchar *format, ...)
{
    va_list args;
    gchar *text;

    if (app->feedback_label == NULL) {
        return;
    }

    va_start(args, format);
    text = g_strdup_vprintf(format, args);
    va_end(args);

    gtk_label_set_text(GTK_LABEL(app->feedback_label), text);
    gtk_widget_set_visible(app->feedback_label, TRUE);
    g_free(text);

    if (app->feedback_timeout_id != 0) {
        g_source_remove(app->feedback_timeout_id);
    }
    app->feedback_timeout_id = g_timeout_add_seconds(3, hide_feedback_cb, app);
}

static void
set_status(OpenCentralApp *app, const gchar *format, ...)
{
    va_list args;
    gchar *text;

    va_start(args, format);
    text = g_strdup_vprintf(format, args);
    va_end(args);

    if (app->status_label != NULL) gtk_label_set_text(GTK_LABEL(app->status_label), text);
    g_print("Linux Capture Studio: %s\n", text);
    g_free(text);
}

static gboolean
query_control(int fd, guint32 id, struct v4l2_queryctrl *query)
{
    memset(query, 0, sizeof(*query));
    query->id = id;
    return ioctl(fd, VIDIOC_QUERYCTRL, query) == 0 &&
           !(query->flags & V4L2_CTRL_FLAG_DISABLED);
}

static gboolean
get_control_value(int fd, guint32 id, gint *value)
{
    struct v4l2_control ctrl = {.id = id, .value = 0};
    if (ioctl(fd, VIDIOC_G_CTRL, &ctrl) != 0) {
        return FALSE;
    }
    *value = ctrl.value;
    return TRUE;
}

static void
on_control_changed(GtkRange *range, gpointer user_data)
{
    ControlBinding *binding = user_data;
    struct v4l2_control ctrl;

    if (binding->app->control_fd < 0) {
        return;
    }

    ctrl.id = binding->id;
    ctrl.value = (gint)gtk_range_get_value(range);

    if (ioctl(binding->app->control_fd, VIDIOC_S_CTRL, &ctrl) != 0) {
        set_status(binding->app, "Control update failed: %s", g_strerror(errno));
    }
}

static GtkWidget *
create_control_row(OpenCentralApp *app, const gchar *label_text, guint32 id)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *label = gtk_label_new(label_text);
    GtkWidget *scale;
    struct v4l2_queryctrl query;
    gint value = 0;
    ControlBinding *binding;

    gtk_widget_set_size_request(label, 92, -1);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_append(GTK_BOX(row), label);

    if (app->control_fd >= 0 && query_control(app->control_fd, id, &query)) {
        if (!get_control_value(app->control_fd, id, &value)) {
            value = query.default_value;
        }
        scale = gtk_scale_new_with_range(
            GTK_ORIENTATION_HORIZONTAL,
            query.minimum,
            query.maximum,
            query.step > 0 ? query.step : 1
        );
        gtk_range_set_value(GTK_RANGE(scale), value);
        gtk_scale_set_draw_value(GTK_SCALE(scale), TRUE);

        binding = g_new0(ControlBinding, 1);
        binding->app = app;
        binding->id = id;
        g_signal_connect_data(
            scale,
            "value-changed",
            G_CALLBACK(on_control_changed),
            binding,
            free_control_binding,
            0
        );
    } else {
        scale = gtk_label_new("Unavailable");
        gtk_widget_set_sensitive(scale, FALSE);
    }

    gtk_widget_set_hexpand(scale, TRUE);
    gtk_box_append(GTK_BOX(row), scale);
    return row;
}

static gchar *
make_output_file(OpenCentralApp *app)
{
    const gchar *requested_dir = gtk_editable_get_text(GTK_EDITABLE(app->output_entry));
    gchar *output_dir;
    GDateTime *now;
    gchar *stamp;
    gchar *filename;
    gchar *full_path;
    const FormatOption *format = current_format(app);
    const ResolutionOption *resolution = current_resolution(app);

    if (requested_dir == NULL || requested_dir[0] == '\0') {
        output_dir = default_recording_directory();
    } else {
        output_dir = g_strdup(requested_dir);
    }

    if (g_mkdir_with_parents(output_dir, 0755) != 0) {
        set_status(app, "Cannot create output directory %s: %s", output_dir, g_strerror(errno));
        g_free(output_dir);
        return NULL;
    }

    now = g_date_time_new_now_local();
    stamp = g_date_time_format(now, "%Y-%m-%d_%H-%M-%S");
    filename = g_strdup_printf(
        "LinuxCaptureStudio_%s_%dx%d_%dfps_%s.%s",
        format->name,
        resolution->width,
        resolution->height,
        app->fps,
        stamp,
        effective_record_container(app) == RECORD_CONTAINER_MP4 ? "mp4" : "mkv"
    );
    full_path = g_build_filename(output_dir, filename, NULL);

    g_free(filename);
    g_free(stamp);
    g_date_time_unref(now);
    g_free(output_dir);
    return full_path;
}

static void
attach_video_paintable(OpenCentralApp *app)
{
    GstElement *sink;
    GdkPaintable *paintable = NULL;

    sink = gst_bin_get_by_name(GST_BIN(app->pipeline), "videosink");
    if (sink == NULL) {
        set_status(app, "gtk4paintablesink was not found");
        return;
    }

    g_object_get(sink, "paintable", &paintable, NULL);
    gst_object_unref(sink);

    if (paintable == NULL) {
        set_status(app, "The video sink did not provide a GTK paintable");
        return;
    }

    gtk_picture_set_paintable(GTK_PICTURE(app->picture), paintable);
    g_object_unref(paintable);
}

static void
finish_recording_and_restore_preview(OpenCentralApp *app)
{
    gchar *finished_file = g_strdup(app->current_file);

    if (app->record_stop_timeout_id != 0) {
        g_source_remove(app->record_stop_timeout_id);
        app->record_stop_timeout_id = 0;
    }
    if (app->hdr_refresh_timer_id != 0) {
        g_source_remove(app->hdr_refresh_timer_id);
        app->hdr_refresh_timer_id = 0;
    }

    stop_pipeline(app);
    app->pipeline_recording = FALSE;
    app->recording = FALSE;
    app->stopping = FALSE;
    app->record_transition = FALSE;
    app->record_started_us = 0;
    gtk_button_set_label(GTK_BUTTON(app->record_button), "●  Record");
    gtk_widget_set_sensitive(app->record_button, TRUE);
    if (app->stream_button != NULL) gtk_widget_set_sensitive(app->stream_button, FALSE);
    set_capture_selectors_sensitive(app, TRUE);
    gtk_widget_set_sensitive(app->output_entry, TRUE);
    gtk_widget_set_sensitive(app->audio_entry, TRUE);

    if (!app->closing) {
        if (build_pipeline(app, FALSE)) {
            if (finished_file != NULL) {
                set_status(app, "Recording finalized: %s", finished_file);
                refresh_recording_library(app);
            } else {
                set_status(app, "Live preview restored");
            }
        }
    }

    g_clear_pointer(&app->current_file, g_free);
    g_free(finished_file);
}

static gboolean
record_stop_timeout_cb(gpointer user_data)
{
    OpenCentralApp *app = user_data;

    app->record_stop_timeout_id = 0;
    if (!app->closing && (app->recording || app->pipeline_recording || app->stopping)) {
        set_status(app, "Recording finalization timed out; closing the file and restoring preview…");
        finish_recording_and_restore_preview(app);
    }

    return G_SOURCE_REMOVE;
}

static gboolean
on_bus_message(GstBus *bus, GstMessage *message, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    (void)bus;

    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
        GError *error = NULL;
        gchar *debug = NULL;
        gboolean capture_audio_error;
        gboolean hdr_preview_shader_error;
        const gchar *message_source_name;

        gst_message_parse_error(message, &error, &debug);
        capture_audio_error = message_is_capture_audio_error(message, debug);
        message_source_name = GST_OBJECT_NAME(GST_MESSAGE_SRC(message));
        hdr_preview_shader_error =
            (message_source_name != NULL &&
             g_str_has_prefix(message_source_name, "hdr_preview_shader")) ||
            (error != NULL && error->message != NULL &&
             g_strrstr(error->message, "shader compilation failed") != NULL) ||
            (debug != NULL &&
             (g_strrstr(debug, "gstglfiltershader") != NULL ||
              g_strrstr(debug, "fragment shader compilation failed") != NULL));
        set_status(app, "Capture error: %s", error != NULL ? error->message : "unknown error");
        app->recovery_waiting_for_frame = FALSE;
        if (debug != NULL) {
            g_printerr("GStreamer debug: %s\n", debug);
        }

        if (hdr_preview_shader_error &&
            !app->streaming && !app->pipeline_streaming && !app->stream_transition &&
            !app->recording && !app->pipeline_recording && !app->record_transition &&
            !app->stopping) {
            app->hdr_preview_gl_failed = TRUE;
            g_clear_error(&error);
            g_free(debug);
            stop_pipeline(app);
            app->recovery_count = 0;
            set_status(
                app,
                "The OpenGL HDR preview shader failed. Reopening the preview with a safe fallback; capture and HDR recording remain unchanged."
            );
            show_feedback(app, "HDR preview correction fallback activated");
            schedule_preview_recovery(
                app,
                "The HDR preview shader was rejected; this is not a capture-device failure."
            );
            break;
        }

        if (capture_audio_error &&
            !app->streaming && !app->pipeline_streaming && !app->stream_transition &&
            !app->recording && !app->pipeline_recording && !app->record_transition &&
            !app->stopping) {
            app->audio_capture_disabled = TRUE;
            g_clear_error(&error);
            g_free(debug);
            stop_pipeline(app);
            set_status(
                app,
                "Capture-card audio timed out. Keeping the video preview alive with a silent audio fallback."
            );
            schedule_preview_recovery(
                app,
                "The capture audio endpoint was unavailable; video preview will continue without live capture audio."
            );
            break;
        }

        if (capture_audio_error) {
            app->audio_capture_disabled = TRUE;
        }

        g_clear_error(&error);
        g_free(debug);

        if (app->apply_in_progress && !app->apply_rolling_back &&
            !app->recording && !app->pipeline_recording &&
            !app->streaming && !app->pipeline_streaming) {
            begin_failed_apply_rollback(
                app,
                "The selected capture mode was rejected by the GC575; restoring the proven mode."
            );
            break;
        }

        if (app->streaming || app->pipeline_streaming || app->stream_transition) {
            finish_streaming_and_restore_preview(app, "Streaming stopped after a connection or pipeline error");
        } else if (app->recording || app->pipeline_recording || app->stopping ||
                   (app->record_transition && app->current_file != NULL)) {
            finish_recording_and_restore_preview(app);
        } else {
            gboolean restore_previous = current_mode_differs_from_last_good(app);
            gboolean startup_4k_failed = app->is_gc575 && !app->have_last_good_mode &&
                app->session_mode == SESSION_MODE_HDR60_4K;

            stop_pipeline(app);
            if (startup_4k_failed) {
                select_safe_hdr1080_mode(app);
                schedule_preview_recovery(
                    app,
                    "Native 4K60 was rejected after recovery; returning to the safe genuine P010 1080p60 mode."
                );
            } else if (restore_previous) {
                restore_last_good_mode(app);
                schedule_preview_recovery(
                    app,
                    "The selected mode was rejected; restoring the last working mode."
                );
            } else {
                schedule_preview_recovery(
                    app, "The capture pipeline reported an error."
                );
            }
        }
        break;
    }
    case GST_MESSAGE_EOS:
        if (app->streaming || app->pipeline_streaming || app->stream_transition) {
            finish_streaming_and_restore_preview(app, "Streaming stopped after a connection or pipeline error");
        } else if (app->recording || app->pipeline_recording || app->stopping ||
                   (app->record_transition && app->current_file != NULL)) {
            finish_recording_and_restore_preview(app);
        } else {
            gboolean restore_previous = current_mode_differs_from_last_good(app);
            gboolean startup_4k_failed = app->is_gc575 && !app->have_last_good_mode &&
                app->session_mode == SESSION_MODE_HDR60_4K;

            stop_pipeline(app);
            if (startup_4k_failed) {
                select_safe_hdr1080_mode(app);
                schedule_preview_recovery(
                    app,
                    "Native 4K60 ended before stable video; returning to the safe genuine P010 1080p60 mode."
                );
            } else if (restore_previous) {
                restore_last_good_mode(app);
                schedule_preview_recovery(
                    app,
                    "The selected mode ended before producing stable video; restoring the last working mode."
                );
            } else {
                schedule_preview_recovery(
                    app, "The capture stream ended unexpectedly."
                );
            }
        }
        break;
    case GST_MESSAGE_LATENCY:
        if (app->pipeline != NULL) {
            gst_bin_recalculate_latency(GST_BIN(app->pipeline));
        }
        break;
    default:
        break;
    }

    return G_SOURCE_CONTINUE;
}

static void
stop_pipeline(OpenCentralApp *app)
{
    GstStateChangeReturn state_result;

    stop_monitor_pipeline(app);
    app->have_active_mode = FALSE;
    close_control_handle(app);

    if (app->bus_watch_id != 0) {
        g_source_remove(app->bus_watch_id);
        app->bus_watch_id = 0;
    }

    if (app->picture != NULL &&
        gtk_picture_get_paintable(GTK_PICTURE(app->picture)) != NULL) {
        gtk_picture_set_paintable(GTK_PICTURE(app->picture), NULL);
    }

    if (app->pipeline != NULL) {
        state_result = gst_element_set_state(app->pipeline, GST_STATE_NULL);
        if (state_result != GST_STATE_CHANGE_FAILURE) {
            /* Wait even when set_state() reports SUCCESS: some UVC devices
             * release their streaming interface a little later. Reopening the
             * GC575 too quickly is what produced repeated VIDIOC_TRY_FMT EIO. */
            gst_element_get_state(app->pipeline, NULL, NULL, 2000 * GST_MSECOND);
        }
        gst_object_unref(app->pipeline);
        app->pipeline = NULL;
        if (!app->closing) {
            g_usleep((app->is_gc575 ? 700u : 200u) * 1000u);
        }
    }

    app->pipeline_recording = FALSE;
    app->pipeline_streaming = FALSE;
    app->pipeline_started_us = 0;
    app->last_video_buffer_us = 0;
    app->watchdog_stall_ticks = 0;
}

static gboolean
build_pipeline(OpenCentralApp *app, gboolean recording)
{
    const FormatOption *format;
    const ResolutionOption *resolution;

    if (app->native_p010_session) {
        enforce_native_p010_lock(app);
    }
    format = current_format(app);
    resolution = current_resolution(app);
    CaptureFormat source_format;
    int source_width;
    int source_height;
    int source_fps;
    gboolean processed_p010;
    gboolean needs_scaling;
    gboolean gpu_postproc;
    HdrMode hdr_mode;
    gboolean hdr_active;
    gboolean hdr_tonemap;
    gboolean hdr_shader_gl;
    gboolean hdr_tonemap_va;
    gboolean gl_preview;
    const gchar *record_colorimetry;
    gchar *preview_chain = NULL;
    gchar *hdr_preview_shader_source = NULL;

    resolve_source_mode(
        (OpenCentralApp *)app,
        format->format,
        resolution->width,
        resolution->height,
        app->fps,
        &source_format,
        &source_width,
        &source_height,
        &source_fps
    );
    needs_scaling = source_width != resolution->width || source_height != resolution->height;
    processed_p010 = format_is_10bit(format) &&
        (source_format != CAPTURE_FORMAT_P010 || needs_scaling || source_fps != app->fps);
    hdr_mode = effective_hdr_mode(app);
    hdr_active = hdr_requested(app);
    record_colorimetry = hdr_colorimetry(hdr_mode);
    /* The desktop preview is normally SDR even while the capture/recording
     * source is genuine PQ/HLG P010.  Tone-map only the preview branch so the
     * image does not look gray or washed out; recording remains untouched. */
    hdr_tonemap = hdr_active &&
        (source_format == CAPTURE_FORMAT_P010 || app->native_p010_session) &&
        app->hdr_tonemap_switch != NULL &&
        gtk_switch_get_active(GTK_SWITCH(app->hdr_tonemap_switch));
    /* Keep one permanent shader stage for every native HDR session.  HDR
     * correction, calibration and the correction on/off switch can then be
     * applied by replacing the fragment shader without reopening v4l2src. */
    hdr_shader_gl = app->native_p010_session &&
        !app->hdr_preview_gl_failed &&
        g_strcmp0(g_getenv("LINUX_CAPTURE_STUDIO_DISABLE_HDR_PREVIEW_CORRECTION"), "1") != 0 &&
        gst_element_available("glupload") &&
        gst_element_available("glcolorconvert") &&
        gst_element_available("glshader");
    hdr_tonemap_va = hdr_tonemap && source_format == CAPTURE_FORMAT_P010 && !hdr_shader_gl &&
        gst_element_available("vapostproc") &&
        gst_element_has_property("vapostproc", "hdr-tone-mapping");
    gl_preview = !hdr_tonemap &&
        g_strcmp0(g_getenv("LINUX_CAPTURE_STUDIO_ENABLE_GL_PREVIEW"), "1") == 0 &&
        gst_element_available("glupload") &&
        gst_element_available("glcolorconvert") &&
        gst_sink_supports_caps_feature("gtk4paintablesink", "memory:GLMemory");
    gpu_postproc = processed_p010 &&
        g_strcmp0(g_getenv("OPENCENTRAL_ENABLE_VA"), "1") == 0 &&
        gst_element_available("vaupload") &&
        gst_element_available("vapostproc") &&
        gst_element_available("vadownload");
    const gchar *audio_device = gtk_editable_get_text(GTK_EDITABLE(app->audio_entry));
    gboolean audio_is_pulse = audio_device != NULL && g_str_has_prefix(audio_device, "pulse:");
    const gchar *audio_backend_device = audio_is_pulse ? audio_device + 6 : audio_device;
    gchar *device_q = quote_gst_string(app->device);
    gchar *audio_q = quote_gst_string(
        audio_backend_device != NULL && audio_backend_device[0] != '\0'
            ? audio_backend_device : "default"
    );
    gchar *audio_source_token = NULL;
    gchar *audio_source_replacement = NULL;
    gchar *file_q = NULL;
    gchar *pipeline_description;
    gchar *video_caps;
    const gchar *record_pixel_format;
    const gchar *source_decode;
    gchar *processed_chain = NULL;
    const gchar *output_rate_chain;
    RecordCodec record_codec = effective_record_codec(app);
    RecordContainer record_container = effective_record_container(app);
    gboolean compressed_recording = record_codec == RECORD_CODEC_H264;
    gboolean streaming = !recording && (app->stream_transition || app->streaming);
    gboolean preview_only = !recording && !streaming;
    gchar *stream_endpoint = NULL;
    gchar *stream_endpoint_q = NULL;
    gchar *stream_encoder_chain = NULL;
    gchar *record_encoder_chain = NULL;
    const gchar *stream_sink_factory = NULL;
    gint stream_fps = MIN(app->fps, 60);
    output_rate_chain = source_fps != app->fps
        ? "! videorate drop-only=true "
        : "";
    gdouble audio_gain = monitor_gain(app);
    gint64 audio_offset_ns = audio_sync_offset_ns(app);
    GError *error = NULL;
    GstBus *bus;
    GstStateChangeReturn state_result;
    GstElement *capture;
    GstPad *capture_src_pad;

    if (app->rebuilding) {
        return FALSE;
    }

    if (app->fps <= 0 || !selectable_mode_supported(
            app, format->format, resolution->width, resolution->height, app->fps)) {
        set_status(app, "%s %s at %d FPS is not exposed by %s",
                   format->name, resolution->name, app->fps,
                   app->capture_device_name != NULL ? app->capture_device_name : "the selected device");
        g_free(audio_q);
        g_free(device_q);
        return FALSE;
    }

    if (recording && hdr_active &&
        ((app->is_gc575 && !app->native_p010_session) ||
         (!app->native_p010_session && !selected_mode_is_native_p010(app)))) {
        set_status(
            app,
            app->is_gc575 && !app->native_p010_session
                ? "Choose an HDR60 session from the Session menu. Smooth SDR uses an 8-bit NV12 transport."
                : "Real HDR capture requires a stable native P010 source; processed P010, NV12, and MJPEG are not true 10-bit HDR sources."
        );
        g_free(audio_q);
        g_free(device_q);
        return FALSE;
    }

    if (streaming && hdr_active) {
        set_status(app, "RTMP streaming currently requires SDR. Turn HDR off and try again.");
        g_free(audio_q);
        g_free(device_q);
        return FALSE;
    }

    if (!native_source_mode_supported(
            app, source_format, source_width, source_height, source_fps)) {
        set_status(
            app,
            "No native source from %s can supply %s %dx%d at %d FPS",
            app->capture_device_name != NULL ? app->capture_device_name : "the selected device",
            capture_format_name(source_format),
            source_width,
            source_height,
            source_fps
        );
        g_free(audio_q);
        g_free(device_q);
        return FALSE;
    }

    if (source_format == CAPTURE_FORMAT_MJPEG) {
        video_caps = g_strdup_printf(
            "image/jpeg,width=%d,height=%d,framerate=%d/1",
            source_width,
            source_height,
            source_fps
        );
    } else {
        const gchar *raw_format = source_format == CAPTURE_FORMAT_P010
            ? "P010_10LE"
            : (source_format == CAPTURE_FORMAT_YUYV ? "YUY2" : "NV12");
        video_caps = g_strdup_printf(
            "video/x-raw,format=%s,width=%d,height=%d,framerate=%d/1",
            raw_format,
            source_width,
            source_height,
            source_fps
        );
    }
    source_decode = source_format == CAPTURE_FORMAT_MJPEG ? "! jpegdec " : "";

    if (hdr_shader_gl) {
        if (!hdr_tonemap) {
            g_print("Linux Capture Studio: persistent native HDR preview renderer active; correction is currently bypassed without reopening the capture device.\n");
        } else if (source_format == CAPTURE_FORMAT_P010) {
            g_print("Linux Capture Studio: source-aware PQ/HLG HDR preview mapping active; recording remains untouched.\n");
        } else {
            g_print("Linux Capture Studio: 8-bit HDR preview compensation active; PQ decoding is disabled for the %s source to prevent shadow crushing.\n",
                    capture_format_name(source_format));
        }

        if (source_format == CAPTURE_FORMAT_P010) {
            preview_chain = g_strdup_printf(
                "! videoconvert n-threads=0 "
                "! video/x-raw,format=P010_10LE,colorimetry=%s "
                "! glupload ! glcolorconvert "
                "! video/x-raw(memory:GLMemory),format=RGBA,texture-target=2D "
                "! glshader name=hdr_preview_shader "
                "! video/x-raw(memory:GLMemory),format=RGBA,texture-target=2D "
                "! glcolorconvert "
                "! video/x-raw(memory:GLMemory),format=RGBA,texture-target=2D "
                "! gtk4paintablesink name=videosink sync=false qos=false async=false "
                "reconfigure-on-window-resize=never ",
                record_colorimetry
            );
        } else {
            preview_chain = g_strdup(
                "! videoconvert n-threads=0 "
                "! video/x-raw,format=RGBA,colorimetry=bt709 "
                "! glupload ! glcolorconvert "
                "! video/x-raw(memory:GLMemory),format=RGBA,texture-target=2D "
                "! glshader name=hdr_preview_shader "
                "! video/x-raw(memory:GLMemory),format=RGBA,texture-target=2D "
                "! glcolorconvert "
                "! video/x-raw(memory:GLMemory),format=RGBA,texture-target=2D "
                "! gtk4paintablesink name=videosink sync=false qos=false async=false "
                "reconfigure-on-window-resize=never "
            );
        }
    } else if (hdr_tonemap_va) {
        preview_chain = g_strdup_printf(
            "! videoconvert n-threads=0 "
            "! video/x-raw,format=P010_10LE,colorimetry=%s "
            "! vapostproc hdr-tone-mapping=true disable-passthrough=true "
            "! video/x-raw,format=BGRx,colorimetry=bt709 "
            "! gtk4paintablesink name=videosink sync=false qos=false async=false "
            "reconfigure-on-window-resize=never ",
            record_colorimetry
        );
    } else if (gl_preview) {
        preview_chain = g_strdup(
            "! glupload ! glcolorconvert "
            "! video/x-raw(memory:GLMemory),format=RGBA,texture-target=2D "
            "! gtk4paintablesink name=videosink sync=false qos=false async=false "
            "reconfigure-on-window-resize=never "
        );
    } else {
        preview_chain = g_strdup(
            "! videoconvert n-threads=0 dither=none "
            "! video/x-raw,format=BGRx "
            "! gtk4paintablesink name=videosink sync=false qos=false async=false "
            "reconfigure-on-window-resize=never "
        );
    }

    /* Processed P010 is an output/recording format.  Do not run the live
     * preview through NV12 -> P010 -> BGRx, because that wastes two full-frame
     * conversions and makes 1440p60/4K60 unstable.  The preview stays on the
     * native source; only the recording branch performs 10-bit conversion. */
    if (processed_p010 && gpu_postproc) {
        processed_chain = g_strdup_printf(
            "%s"
            "! vaupload "
            "! vapostproc "
            "! video/x-raw(memory:VAMemory),format=P010_10LE,width=%d,height=%d,framerate=%d/1 "
            "! vadownload "
            "! video/x-raw,format=P010_10LE,width=%d,height=%d,framerate=%d/1 ",
            output_rate_chain,
            resolution->width,
            resolution->height,
            app->fps,
            resolution->width,
            resolution->height,
            app->fps
        );
    } else if (processed_p010) {
        processed_chain = g_strdup_printf(
            "%s"
            "! videoconvertscale method=lanczos n-threads=0 dither=none "
            "matrix-mode=none chroma-mode=none gamma-mode=none primaries-mode=none "
            "! video/x-raw,format=P010_10LE,width=%d,height=%d,framerate=%d/1 ",
            output_rate_chain,
            resolution->width,
            resolution->height,
            app->fps
        );
    }

    if (recording && !compressed_recording && !format_is_mjpeg(format) &&
        !gst_element_available("avenc_ffv1")) {
        set_status(
            app,
            "NV12/P010 lossless recording needs avenc_ffv1. Install the GStreamer libav plug-in."
        );
        g_free(video_caps);
        g_free(processed_chain);
        g_free(audio_q);
        g_free(device_q);
        return FALSE;
    }

    if (recording && compressed_recording) {
        record_encoder_chain = make_h264_encoder_chain(
            record_bitrate_kbps(app),
            MAX(30, app->fps * 2)
        );
        if (record_encoder_chain == NULL) {
            set_status(app, "H.264 recording is unavailable; use Lossless FFV1 or install x264enc/OpenH264.");
            g_free(video_caps);
            g_free(processed_chain);
            g_free(audio_q);
            g_free(device_q);
            return FALSE;
        }
    }

    if (streaming) {
        stream_sink_factory = gst_element_available("rtmp2sink")
            ? "rtmp2sink"
            : (gst_element_available("rtmpsink") ? "rtmpsink" : NULL);
        stream_endpoint = build_stream_endpoint(app);
        stream_encoder_chain = make_h264_encoder_chain(
            app->stream_bitrate_kbps,
            MAX(30, stream_fps * 2)
        );
        if (stream_sink_factory == NULL ||
            !gst_element_available("flvmux") ||
            !gst_element_available("h264parse") ||
            !gst_element_available("avenc_aac") ||
            stream_endpoint == NULL ||
            stream_encoder_chain == NULL) {
            set_status(
                app,
                "Streaming needs flvmux, rtmp2sink/rtmpsink, H.264, h264parse, avenc_aac, and a valid endpoint."
            );
            g_free(stream_encoder_chain);
            g_free(stream_endpoint);
            g_free(record_encoder_chain);
            g_free(video_caps);
            g_free(processed_chain);
            g_free(audio_q);
            g_free(device_q);
            return FALSE;
        }
        stream_endpoint_q = quote_gst_string(stream_endpoint);
    }

    app->rebuilding = TRUE;
    stop_pipeline(app);

    if (!wait_for_capture_mode_ready(app, source_format, source_width, source_height)) {
        reopen_control_handle(app);
        app->rebuilding = FALSE;
        set_status(
            app,
            "The requested capture mode was not ready. The window will remain open and the previous mode will be restored."
        );
        return FALSE;
    }

    if (streaming) {
        pipeline_description = g_strdup_printf(
            "flvmux name=stream_mux streamable=true "
            "! %s location=%s async=false sync=false "
            "v4l2src name=capture device=%s do-timestamp=true io-mode=mmap "
            "! %s "
            "! tee name=video_tee "
            "video_tee. ! queue max-size-buffers=1 leaky=downstream "
            "%s"
            "%s"
            "video_tee. ! queue max-size-time=3000000000 leaky=downstream "
            "%s"
            "! videoconvertscale n-threads=0 "
            "! videorate drop-only=true "
            "! video/x-raw,format=I420,width=%d,height=%d,framerate=%d/1 "
            "%s"
            "! h264parse config-interval=-1 "
            "! queue max-size-time=3000000000 ! stream_mux. "
            "alsasrc device=%s do-timestamp=true "
            "! queue max-size-time=3000000000 "
            "! audioconvert ! audioresample "
            "! audio/x-raw,format=S16LE,rate=48000,channels=2 "
            "! audiorate ! tee name=audio_tee "
            "audio_tee. ! queue max-size-time=3000000000 "
            "! audioconvert ! audioresample "
            "! avenc_aac bitrate=160000 ! aacparse "
            "! queue max-size-time=3000000000 ! stream_mux. "
            "audio_tee. ! queue max-size-time=500000000 leaky=downstream "
            "! identity name=audio_sync ts-offset=%" G_GINT64_FORMAT " "
            "! volume name=monitor_volume volume=%.3f "
            "! audioconvert ! audioresample "
            "! autoaudiosink sync=true",
            stream_sink_factory,
            stream_endpoint_q,
            device_q,
            video_caps,
            source_decode,
            preview_chain,
            source_decode,
            resolution->width,
            resolution->height,
            stream_fps,
            stream_encoder_chain,
            audio_q,
            audio_offset_ns,
            audio_gain
        );
    } else if (recording) {
        file_q = quote_gst_string(app->current_file);

        if (compressed_recording) {
            const gchar *mux_decl = record_container == RECORD_CONTAINER_MP4
                ? "mp4mux name=mux faststart=true "
                : "matroskamux name=mux writing-app=LinuxCaptureStudio-0.6.30 ";
            const gchar *audio_record_chain = record_container == RECORD_CONTAINER_MP4
                ? "! audioconvert ! audioresample ! avenc_aac bitrate=192000 ! aacparse ! mux. "
                : "! audioconvert ! audio/x-raw,format=S16LE,rate=48000,channels=2 ! mux. ";
            pipeline_description = g_strdup_printf(
                "v4l2src name=capture device=%s do-timestamp=true io-mode=mmap "
                "! %s "
                "%s"
                "! tee name=video_tee "
                "video_tee. ! queue max-size-buffers=1 leaky=downstream "
                "%s"
                "%s"
                "! filesink location=%s async=false "
                "video_tee. ! queue max-size-time=5000000000 "
                "%s"
                "! videoconvert n-threads=0 "
                "! video/x-raw,format=I420 "
                "%s"
                "! h264parse config-interval=-1 ! mux. "
                "alsasrc device=%s do-timestamp=true "
                "! queue max-size-time=5000000000 "
                "! audioconvert ! audioresample "
                "! audio/x-raw,rate=48000,channels=2 "
                "! audiorate ! tee name=audio_tee "
                "audio_tee. ! queue max-size-time=5000000000 "
                "%s"
                "audio_tee. ! queue max-size-time=500000000 leaky=downstream "
                "! identity name=audio_sync ts-offset=%" G_GINT64_FORMAT " "
                "! volume name=monitor_volume volume=%.3f "
                "! audioconvert ! audioresample "
                "! autoaudiosink sync=true",
                device_q,
                video_caps,
                source_decode,
                preview_chain,
                mux_decl,
                file_q,
                processed_chain != NULL ? processed_chain : "",
                record_encoder_chain,
                audio_q,
                audio_record_chain,
                audio_offset_ns,
                audio_gain
            );
        } else if (format_is_mjpeg(format)) {
            pipeline_description = g_strdup_printf(
                "v4l2src name=capture device=%s do-timestamp=true io-mode=mmap "
                "! %s "
                "! tee name=video_tee "
                "video_tee. ! queue max-size-buffers=1 leaky=downstream "
                "! jpegdec "
                "%s"
                "matroskamux name=mux writing-app=LinuxCaptureStudio-0.6.30 "
                "! filesink location=%s async=false "
                "video_tee. ! queue max-size-time=3000000000 "
                "! jpegparse ! mux. "
                "alsasrc device=%s do-timestamp=true "
                "! queue max-size-time=3000000000 "
                "! audioconvert ! audioresample "
                "! audio/x-raw,rate=48000,channels=2 "
                "! audiorate "
                "! tee name=audio_tee "
                "audio_tee. ! queue max-size-time=3000000000 "
                "! audioconvert "
                "! audio/x-raw,format=S16LE,rate=48000,channels=2 "
                "! mux. "
                "audio_tee. ! queue max-size-time=500000000 leaky=downstream "
                "! identity name=audio_sync ts-offset=%" G_GINT64_FORMAT " "
                "! volume name=monitor_volume volume=%.3f "
                "! audioconvert ! audioresample "
                "! autoaudiosink sync=true",
                device_q,
                video_caps,
                preview_chain,
                file_q,
                audio_q,
                audio_offset_ns,
                audio_gain
            );
        } else if (processed_p010) {
            pipeline_description = g_strdup_printf(
                "v4l2src name=capture device=%s do-timestamp=true io-mode=mmap "
                "! %s "
                "%s"
                "! tee name=video_tee "
                /* Native, low-latency preview branch. */
                "video_tee. ! queue max-size-buffers=1 leaky=downstream "
                "%s"
                /* Expensive P010 scaling/conversion is isolated here. */
                "matroskamux name=mux writing-app=LinuxCaptureStudio-0.6.30 "
                "! filesink location=%s async=false "
                "video_tee. ! queue max-size-time=5000000000 "
                "%s"
                "! videoconvertscale n-threads=0 dither=none matrix-mode=none chroma-mode=none gamma-mode=none primaries-mode=none "
                "! video/x-raw,format=I420_10LE "
                "! avenc_ffv1 coder=2 context=1 slices=16 slicecrc=on "
                "! mux. "
                "alsasrc device=%s do-timestamp=true "
                "! queue max-size-time=5000000000 "
                "! audioconvert ! audioresample "
                "! audio/x-raw,rate=48000,channels=2 "
                "! audiorate "
                "! tee name=audio_tee "
                "audio_tee. ! queue max-size-time=5000000000 "
                "! audioconvert "
                "! audio/x-raw,format=S16LE,rate=48000,channels=2 "
                "! mux. "
                "audio_tee. ! queue max-size-time=500000000 leaky=downstream "
                "! identity name=audio_sync ts-offset=%" G_GINT64_FORMAT " "
                "! volume name=monitor_volume volume=%.3f "
                "! audioconvert ! audioresample "
                "! autoaudiosink sync=true",
                device_q,
                video_caps,
                source_decode,
                preview_chain,
                file_q,
                processed_chain,
                audio_q,
                audio_offset_ns,
                audio_gain
            );
        } else {
            record_pixel_format = format_is_10bit(format) ? "I420_10LE" : "I420";
            pipeline_description = g_strdup_printf(
                "v4l2src name=capture device=%s do-timestamp=true io-mode=mmap "
                "! %s "
                "! tee name=video_tee "
                "video_tee. ! queue max-size-buffers=1 leaky=downstream "
                "%s"
                "matroskamux name=mux writing-app=LinuxCaptureStudio-0.6.30 "
                "! filesink location=%s async=false "
                "video_tee. ! queue max-size-time=5000000000 "
                "! videoconvertscale n-threads=0 dither=none matrix-mode=none chroma-mode=none gamma-mode=none primaries-mode=none "
                "! video/x-raw,format=%s,colorimetry=%s "
                "! avenc_ffv1 coder=2 context=1 slices=16 slicecrc=on "
                "! mux. "
                "alsasrc device=%s do-timestamp=true "
                "! queue max-size-time=5000000000 "
                "! audioconvert ! audioresample "
                "! audio/x-raw,rate=48000,channels=2 "
                "! audiorate "
                "! tee name=audio_tee "
                "audio_tee. ! queue max-size-time=5000000000 "
                "! audioconvert "
                "! audio/x-raw,format=S16LE,rate=48000,channels=2 "
                "! mux. "
                "audio_tee. ! queue max-size-time=500000000 leaky=downstream "
                "! identity name=audio_sync ts-offset=%" G_GINT64_FORMAT " "
                "! volume name=monitor_volume volume=%.3f "
                "! audioconvert ! audioresample "
                "! autoaudiosink sync=true",
                device_q,
                video_caps,
                preview_chain,
                file_q,
                record_pixel_format,
                record_colorimetry,
                audio_q,
                audio_offset_ns,
                audio_gain
            );
        }
    } else if (format_is_mjpeg(format)) {
        pipeline_description = g_strdup_printf(
            "v4l2src name=capture device=%s do-timestamp=true io-mode=mmap "
            "! %s "
            "! queue max-size-buffers=1 leaky=downstream "
            "! jpegdec "
            "%s"
            "alsasrc device=%s do-timestamp=true "
            "! queue max-size-time=500000000 leaky=downstream "
            "! audioconvert ! audioresample "
            "! audio/x-raw,rate=48000,channels=2 "
            "! audiorate "
            "! identity name=audio_sync ts-offset=%" G_GINT64_FORMAT " "
            "! volume name=monitor_volume volume=%.3f "
            "! autoaudiosink sync=true",
            device_q,
            video_caps,
            preview_chain,
            audio_q,
            audio_offset_ns,
            audio_gain
        );
    } else if (processed_p010) {
        pipeline_description = g_strdup_printf(
            "v4l2src name=capture device=%s do-timestamp=true io-mode=mmap "
            "! %s "
            "%s"
            "! queue max-size-buffers=1 leaky=downstream "
            "%s"
            "alsasrc device=%s do-timestamp=true "
            "! queue max-size-time=500000000 leaky=downstream "
            "! audioconvert ! audioresample "
            "! audio/x-raw,rate=48000,channels=2 "
            "! audiorate "
            "! identity name=audio_sync ts-offset=%" G_GINT64_FORMAT " "
            "! volume name=monitor_volume volume=%.3f "
            "! autoaudiosink sync=true",
            device_q,
            video_caps,
            source_decode,
            preview_chain,
            audio_q,
            audio_offset_ns,
            audio_gain
        );
    } else {
        pipeline_description = g_strdup_printf(
            "v4l2src name=capture device=%s do-timestamp=true io-mode=mmap "
            "! %s "
            "! queue max-size-buffers=1 leaky=downstream "
            "%s"
            "alsasrc device=%s do-timestamp=true "
            "! queue max-size-time=500000000 leaky=downstream "
            "! audioconvert ! audioresample "
            "! audio/x-raw,rate=48000,channels=2 "
            "! audiorate "
            "! identity name=audio_sync ts-offset=%" G_GINT64_FORMAT " "
            "! volume name=monitor_volume volume=%.3f "
            "! autoaudiosink sync=true",
            device_q,
            video_caps,
            preview_chain,
            audio_q,
            audio_offset_ns,
            audio_gain
        );
    }

    audio_source_token = g_strdup_printf(
        "alsasrc device=%s do-timestamp=true",
        audio_q
    );
    if (audio_is_pulse) {
        gchar *with_pulse_audio;
        audio_source_replacement = g_strdup_printf(
            "pulsesrc device=%s do-timestamp=true",
            audio_q
        );
        with_pulse_audio = replace_first_token(
            pipeline_description,
            audio_source_token,
            audio_source_replacement
        );
        g_free(pipeline_description);
        pipeline_description = with_pulse_audio;
    }

    if (preview_only || app->audio_capture_disabled) {
        const gchar *active_audio_source = audio_is_pulse
            ? audio_source_replacement : audio_source_token;
        gchar *with_silent_audio = replace_first_token(
            pipeline_description,
            active_audio_source,
            "audiotestsrc is-live=true wave=silence do-timestamp=true"
        );
        g_free(pipeline_description);
        pipeline_description = with_silent_audio;
    }

    g_free(video_caps);
    g_free(processed_chain);
    g_free(preview_chain);
    g_free(stream_endpoint_q);
    g_free(stream_endpoint);
    g_free(stream_encoder_chain);
    g_free(record_encoder_chain);
    g_free(file_q);
    g_free(audio_source_token);
    g_free(audio_source_replacement);
    g_free(audio_q);
    g_free(device_q);

    {
        gchar *monitor_sink_chain = preview_only
            ? g_strdup("fakesink sync=false")
            : make_monitor_sink_chain(app);
        gchar *with_selected_sink = replace_first_token(
            pipeline_description,
            "autoaudiosink sync=true",
            monitor_sink_chain
        );
        g_free(monitor_sink_chain);
        g_free(pipeline_description);
        pipeline_description = with_selected_sink;
    }

    if (streaming) {
        g_print("Linux Capture Studio: streaming pipeline prepared (endpoint hidden)\n");
    } else {
        g_print("Pipeline: %s\n", pipeline_description);
    }
    if (hdr_shader_gl) {
        hdr_preview_shader_source = make_hdr_preview_shader(app, hdr_mode, source_format);
    }
    app->pipeline = gst_parse_launch(pipeline_description, &error);
    g_free(pipeline_description);

    if (app->pipeline == NULL) {
        g_clear_pointer(&hdr_preview_shader_source, g_free);
        set_status(app, "Could not build pipeline: %s", error != NULL ? error->message : "unknown error");
        g_clear_error(&error);
        reopen_control_handle(app);
        app->rebuilding = FALSE;
        return FALSE;
    }

    if (hdr_preview_shader_source != NULL) {
        GstElement *hdr_preview_shader = gst_bin_get_by_name(
            GST_BIN(app->pipeline), "hdr_preview_shader"
        );
        if (hdr_preview_shader == NULL) {
            set_status(app, "HDR preview correction element was not created; using the safe preview path.");
            app->hdr_preview_gl_failed = TRUE;
            gst_element_set_state(app->pipeline, GST_STATE_NULL);
            gst_object_unref(app->pipeline);
            app->pipeline = NULL;
            g_clear_pointer(&hdr_preview_shader_source, g_free);
            reopen_control_handle(app);
            app->rebuilding = FALSE;
            return build_pipeline(app, recording);
        }
        /* Set the shader as a direct gchararray property so real newline
         * characters reach GLSL.  Passing it through gst_parse_launch()
         * escaped them as literal \n text and broke #version parsing. */
        g_object_set(hdr_preview_shader, "fragment", hdr_preview_shader_source, NULL);
        gst_object_unref(hdr_preview_shader);
        g_clear_pointer(&hdr_preview_shader_source, g_free);
    }

    capture = gst_bin_get_by_name(GST_BIN(app->pipeline), "capture");
    if (capture != NULL) {
        capture_src_pad = gst_element_get_static_pad(capture, "src");
        if (capture_src_pad != NULL) {
            gst_pad_add_probe(
                capture_src_pad,
                GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST,
                on_video_buffer,
                app,
                NULL
            );
            gst_object_unref(capture_src_pad);
        }
        gst_object_unref(capture);
    }

    attach_video_paintable(app);

    bus = gst_element_get_bus(app->pipeline);
    app->bus_watch_id = gst_bus_add_watch(bus, on_bus_message, app);
    gst_object_unref(bus);

    app->pipeline_recording = recording;
    app->pipeline_streaming = streaming;
    app->active_format_index = app->format_index;
    app->active_resolution_index = app->resolution_index;
    app->active_fps = app->fps;
    app->active_source_format = source_format;
    app->active_source_width = source_width;
    app->active_source_height = source_height;
    app->active_source_fps = source_fps;
    app->have_active_mode = TRUE;
    app->pipeline_started_us = g_get_monotonic_time();
    app->last_video_buffer_us = 0;
    app->watchdog_stall_ticks = 0;

    state_result = gst_element_set_state(app->pipeline, GST_STATE_PLAYING);
    if (state_result == GST_STATE_CHANGE_FAILURE) {
        set_status(app, "Failed to start the %s pipeline", streaming ? "streaming" : (recording ? "recording" : "preview"));
        stop_pipeline(app);
        reopen_control_handle(app);
        app->rebuilding = FALSE;
        return FALSE;
    }

    reopen_control_handle(app);

    if (preview_only && !app->audio_capture_disabled) {
        if (!start_monitor_pipeline(app)) {
            g_printerr("Linux Capture Studio: live audio monitor could not start; video remains active.\n");
        }
    }

    if (streaming) {
        set_status(
            app,
            "Streaming %s %dx%d at %d FPS with H.264 at %d Mbps",
            format->name,
            resolution->width,
            resolution->height,
            stream_fps,
            app->stream_bitrate_kbps / 1000
        );
    } else if (recording) {
        gchar *preset_description = record_preset_description(app);
        if (processed_p010) {
            set_status(
                app,
                "Recording P010 %dx%d at %d FPS from native %s %dx%d at %d FPS%s (%s) to %s",
                resolution->width,
                resolution->height,
                app->fps,
                capture_format_name(source_format),
                source_width,
                source_height,
                source_fps,
                gpu_postproc ? " with VA-API GPU processing" :
                    (needs_scaling ? " with threaded Lanczos CPU processing" : " with threaded CPU conversion"),
                preset_description,
                app->current_file
            );
        } else {
            set_status(
                app,
                "Recording %s %dx%d at %d FPS (%s) to %s",
                format->name,
                resolution->width,
                resolution->height,
                app->fps,
                format_is_mjpeg(format) && !compressed_recording ? "MJPEG source passthrough" : preset_description,
                app->current_file
            );
        }
        g_free(preset_description);
    } else if (processed_p010) {
        if (app->native_p010_session) {
            if (source_format == CAPTURE_FORMAT_P010) {
                set_status(
                    app,
                    "HDR60 native source: genuine P010 %dx%d at %d FPS%s. No resolution scaling is active.",
                    source_width,
                    source_height,
                    source_fps,
                    gl_preview ? " through the GPU renderer" : " through the stable CPU renderer"
                );
            } else {
                set_status(
                    app,
                    "HDR60 native source: %s %dx%d at %d FPS%s. Resolution is native; source-aware 8-bit HDR preview compensation is active and P010 output does not upscale.",
                    capture_format_name(source_format),
                    source_width,
                    source_height,
                    source_fps,
                    gl_preview ? " through the GPU renderer" : " through the stable CPU renderer"
                );
            }
        } else {
            set_status(
                app,
                "Low-latency native preview: %s %dx%d at %d FPS%s. P010 %dx%d at %d FPS preserves the source samples losslessly in the output branch; an 8-bit NV12 source cannot gain new 10-bit detail.",
                capture_format_name(source_format),
                source_width,
                source_height,
                source_fps,
                gl_preview ? " through the GPU renderer" : " through the stable CPU renderer",
                resolution->width,
                resolution->height,
                app->fps
            );
        }
    } else {
        if (app->native_p010_session && source_format == CAPTURE_FORMAT_P010) {
            set_status(
                app,
                app->audio_capture_disabled
                    ? "HDR60 preview: genuine P010 %dx%d at %d FPS (capture audio unavailable)"
                    : "HDR60 preview: genuine P010 %dx%d at %d FPS with independent live audio monitoring",
                source_width,
                source_height,
                source_fps
            );
        } else {
            set_status(
                app,
                app->audio_capture_disabled
                    ? "Live preview: %s — %s %dx%d at %d FPS (capture audio unavailable)"
                    : "Live preview: %s — %s %dx%d at %d FPS",
                app->device,
                format->name,
                resolution->width,
                resolution->height,
                app->fps
            );
        }
    }

    update_mode_badge(app);
    app->rebuilding = FALSE;
    return TRUE;
}

static void
on_record_clicked(GtkButton *button, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    (void)button;

    if (app->settings_pending || app->apply_in_progress) {
        show_feedback(app, "Apply the pending capture settings before recording");
        return;
    }
    if (app->record_transition || app->rebuilding || app->stopping || app->stream_transition) {
        return;
    }
    if (app->streaming || app->pipeline_streaming) {
        show_feedback(app, "Stop streaming before recording");
        return;
    }

    if (!app->recording) {
        if (app->is_gc575 && !app->native_p010_session &&
            current_format(app)->format == CAPTURE_FORMAT_P010 && app->hdr_enabled) {
            app->hdr_enabled = FALSE;
            if (app->hdr_toggle_button != NULL) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->hdr_toggle_button), FALSE);
            }
            update_hdr_toggle_ui(app);
            update_hdr_status(app);
            show_feedback(app, "HDR disabled: GC575 safe P010 preserves the native SDR samples without a physical P010 format switch");
        }

        app->current_file = make_output_file(app);
        if (app->current_file == NULL) {
            return;
        }

        app->record_transition = TRUE;
        gtk_widget_set_sensitive(app->record_button, FALSE);
        if (app->stream_button != NULL) gtk_widget_set_sensitive(app->stream_button, FALSE);
        gtk_button_set_label(GTK_BUTTON(app->record_button), "Starting…");
        set_capture_selectors_sensitive(app, FALSE);
        gtk_widget_set_sensitive(app->output_entry, FALSE);
        gtk_widget_set_sensitive(app->audio_entry, FALSE);
        set_status(app, "Opening the recording pipeline…");

        if (!build_pipeline(app, TRUE)) {
            gboolean retry_lossless = effective_record_codec(app) == RECORD_CODEC_H264;

            if (retry_lossless) {
                app->record_codec = RECORD_CODEC_FFV1;
                app->record_container = RECORD_CONTAINER_MKV;
                if (app->codec_dropdown != NULL) {
                    app->selection_update = TRUE;
                    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->codec_dropdown), RECORD_CODEC_FFV1);
                    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->container_dropdown), RECORD_CONTAINER_MKV);
                    app->selection_update = FALSE;
                }
                if (app->current_file != NULL) {
                    g_remove(app->current_file);
                    g_clear_pointer(&app->current_file, g_free);
                }
                app->current_file = make_output_file(app);
                save_device_profile(app);
                if (app->current_file == NULL) {
                    retry_lossless = FALSE;
                }
                show_feedback(app, "Compressed recording failed — retrying with the proven lossless path");
            }

            if (!retry_lossless || !build_pipeline(app, TRUE)) {
                app->record_transition = FALSE;
                app->pipeline_recording = FALSE;
                gtk_button_set_label(GTK_BUTTON(app->record_button), "●  Record");
                gtk_widget_set_sensitive(app->record_button, TRUE);
                if (app->stream_button != NULL) gtk_widget_set_sensitive(app->stream_button, FALSE);
                set_capture_selectors_sensitive(app, TRUE);
                gtk_widget_set_sensitive(app->output_entry, TRUE);
                gtk_widget_set_sensitive(app->audio_entry, TRUE);
                g_clear_pointer(&app->current_file, g_free);
                build_pipeline(app, FALSE);
                show_feedback(app, "Recording could not start — check the terminal error");
                return;
            }
        }

        app->recording = TRUE;
        app->record_transition = FALSE;
        app->stopping = FALSE;
        app->record_started_us = g_get_monotonic_time();
        gtk_button_set_label(GTK_BUTTON(app->record_button), "■  Stop");
        gtk_widget_set_sensitive(app->record_button, TRUE);
        return;
    }

    if (app->pipeline == NULL || !app->pipeline_recording) {
        set_status(app, "The recording pipeline is not active; restoring preview…");
        finish_recording_and_restore_preview(app);
        return;
    }

    app->record_transition = TRUE;
    app->stopping = TRUE;
    gtk_widget_set_sensitive(app->record_button, FALSE);
    gtk_button_set_label(GTK_BUTTON(app->record_button), "Finalizing…");
    set_status(app, "Finalizing recording…");

    if (app->record_stop_timeout_id != 0) {
        g_source_remove(app->record_stop_timeout_id);
    }
    app->record_stop_timeout_id = g_timeout_add_seconds(5, record_stop_timeout_cb, app);

    if (!gst_element_send_event(app->pipeline, gst_event_new_eos())) {
        finish_recording_and_restore_preview(app);
    }
}

static void
apply_selected_mode_change(OpenCentralApp *app,
                           gboolean had_old_source,
                           CaptureFormat old_source_format,
                           int old_source_width,
                           int old_source_height,
                           int old_source_fps)
{
    CaptureFormat new_source_format;
    int new_source_width;
    int new_source_height;
    int new_source_fps;
    const FormatOption *format = current_format(app);
    const ResolutionOption *resolution = current_resolution(app);

    if (app->fps_count == 0 || app->fps <= 0) {
        show_feedback(app, "%s at %s is unavailable", format->name, resolution->name);
        return;
    }

    selected_source_mode(
        app,
        &new_source_format,
        &new_source_width,
        &new_source_height,
        &new_source_fps
    );

    if (app->capture_restart_timer_id != 0) {
        g_source_remove(app->capture_restart_timer_id);
        app->capture_restart_timer_id = 0;
    }

    if (had_old_source && source_modes_equal(
            old_source_format,
            old_source_width,
            old_source_height,
            old_source_fps,
            new_source_format,
            new_source_width,
            new_source_height,
            new_source_fps)) {
        app->last_good_format_index = app->format_index;
        app->last_good_resolution_index = app->resolution_index;
        app->last_good_fps = app->fps;
        app->last_good_session_mode = app->session_mode;
        app->last_good_hdr_mode = app->hdr_mode;
        app->last_good_hdr_enabled = app->hdr_enabled;
        app->last_good_native_p010_session = app->native_p010_session;
        app->have_last_good_mode = TRUE;
        app->recovery_count = 0;

        if (mode_uses_processing_for_device(
                app,
                format->format,
                resolution->width,
                resolution->height,
                app->fps)) {
            show_feedback(
                app,
                "Output set to %s %dx%d %d FPS — live source remains %s %dx%d %d FPS",
                format->name,
                resolution->width,
                resolution->height,
                app->fps,
                capture_format_name(new_source_format),
                new_source_width,
                new_source_height,
                new_source_fps
            );
            set_status(
                app,
                "Safe preview transport: %s %dx%d %d FPS. %s %dx%d %d FPS will be created only in the recording/output branch.",
                capture_format_name(new_source_format),
                new_source_width,
                new_source_height,
                new_source_fps,
                format->name,
                resolution->width,
                resolution->height,
                app->fps
            );
        } else {
            show_feedback(
                app,
                "Active: %s %dx%d at %d FPS",
                capture_format_name(new_source_format),
                new_source_width,
                new_source_height,
                new_source_fps
            );
            set_status(
                app,
                "Preview remains open at %s %dx%d %d FPS; no device restart was needed.",
                capture_format_name(new_source_format),
                new_source_width,
                new_source_height,
                new_source_fps
            );
        }
        update_mode_badge(app);
        return;
    }

    app->recovery_count = 0;
    show_feedback(
        app,
        "Applying %s %dx%d at %d FPS…",
        capture_format_name(new_source_format),
        new_source_width,
        new_source_height,
        new_source_fps
    );
    set_status(
        app,
        "Switching the physical capture source to %s %dx%d at %d FPS…",
        capture_format_name(new_source_format),
        new_source_width,
        new_source_height,
        new_source_fps
    );
    app->capture_restart_timer_id = g_timeout_add(300, apply_capture_change_cb, app);
}

static gboolean
apply_capture_change_cb(gpointer user_data)
{
    OpenCentralApp *app = user_data;

    app->capture_restart_timer_id = 0;
    if (app->closing || app->recovery_exit_scheduled) {
        return G_SOURCE_REMOVE;
    }
    if (app->rebuilding) {
        app->capture_restart_timer_id = g_timeout_add(150, apply_capture_change_cb, app);
        return G_SOURCE_REMOVE;
    }
    if (!app->closing && !app->recording && !app->record_transition &&
        !app->streaming && !app->stream_transition && app->fps_count > 0) {
        if (app->native_p010_session) {
            enforce_native_p010_lock(app);
        }
        if (build_pipeline(app, FALSE)) {
            app->recovery_count = 0;
            if (app->apply_in_progress) {
                /* PLAYING only means negotiation started.  Commit Apply only
                 * after a real video buffer arrives; TRY_FMT errors are
                 * asynchronous and previously appeared after RC8 announced
                 * success. */
                app->apply_validation_started_us = g_get_monotonic_time();
                app->apply_validation_frame_count = 0;
                if (app->apply_validation_timer_id != 0) {
                    g_source_remove(app->apply_validation_timer_id);
                }
                app->apply_validation_timer_id = g_timeout_add(100, validate_applied_mode_cb, app);
                set_status(app, "Validating the new capture mode before committing it…");
            }
        } else if (app->apply_in_progress) {
            begin_failed_apply_rollback(
                app,
                "The selected mode could not start; restoring the proven mode."
            );
        }
    }

    return G_SOURCE_REMOVE;
}

static gboolean
validate_applied_mode_cb(gpointer user_data)
{
    OpenCentralApp *app = user_data;
    gint64 now;

    if (app == NULL) return G_SOURCE_REMOVE;
    if (app->closing || !app->apply_in_progress || app->apply_rolling_back) {
        app->apply_validation_timer_id = 0;
        return G_SOURCE_REMOVE;
    }

    now = g_get_monotonic_time();
    if (app->pipeline != NULL && app->last_video_buffer_us > 0 &&
        app->last_video_buffer_us >= app->pipeline_started_us &&
        app->apply_validation_frame_count >= 30 &&
        now - app->last_video_buffer_us < 500000 &&
        now - app->apply_validation_started_us >= 800000) {
        app->apply_validation_timer_id = 0;
        finish_apply_success(app);
        return G_SOURCE_REMOVE;
    }

    if (now - app->apply_validation_started_us >= 10000000) {
        app->apply_validation_timer_id = 0;
        begin_failed_apply_rollback(
            app,
            "The selected mode produced no stable video; restoring the proven mode."
        );
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void
select_safe_hdr1080_mode(OpenCentralApp *app)
{
    if (app == NULL) return;
    app->session_mode = SESSION_MODE_HDR60_1080;
    app->native_p010_session = TRUE;
    app->format_index = 2;
    app->resolution_index = 1;
    app->fps = 60;
    app->hdr_mode = HDR_MODE_HDR10_PQ;
    app->hdr_enabled = TRUE;
    enforce_native_p010_lock(app);
    sync_pending_settings_from_current(app);
    update_session_ui(app);
}

static gboolean
finish_failed_apply_rollback_cb(gpointer user_data)
{
    OpenCentralApp *app = user_data;
    if (app == NULL) return G_SOURCE_REMOVE;
    app->rollback_finish_scheduled = FALSE;
    if (!app->closing && app->apply_rolling_back &&
        app->pipeline != NULL && app->last_video_buffer_us >= app->pipeline_started_us) {
        finish_failed_apply_rollback(app);
    }
    return G_SOURCE_REMOVE;
}

static void
finish_failed_apply_rollback(OpenCentralApp *app)
{
    if (app == NULL) return;
    if (app->apply_validation_timer_id != 0) {
        g_source_remove(app->apply_validation_timer_id);
        app->apply_validation_timer_id = 0;
    }
    if (app->recovery_timer_id != 0) {
        g_source_remove(app->recovery_timer_id);
        app->recovery_timer_id = 0;
    }
    app->apply_rolling_back = FALSE;
    app->rollback_finish_scheduled = FALSE;
    app->rollback_safe_fallback_attempted = FALSE;
    app->recovery_count = 0;
    app->apply_in_progress = FALSE;
    app->settings_pending = FALSE;
    set_apply_blackout(app, FALSE, NULL);
    sync_pending_settings_from_current(app);
    set_capture_selectors_sensitive(app, TRUE);
    if (app->session_dropdown != NULL) gtk_widget_set_sensitive(app->session_dropdown, TRUE);
    if (app->record_button != NULL) gtk_widget_set_sensitive(app->record_button, app->pipeline != NULL);
    update_apply_ui(app);
    update_session_ui(app);
    update_hdr_toggle_ui(app);
    update_hdr_status(app);
    update_mode_badge(app);
    show_feedback(app, "Requested mode was rejected — restored a verified working capture mode");
    set_status(app, "The GC575 rejected the requested mode. The proven previous mode has been restored safely.");
}

static void
begin_failed_apply_rollback(OpenCentralApp *app, const gchar *reason)
{
    if (app == NULL || app->apply_rolling_back) return;
    if (app->apply_validation_timer_id != 0) {
        g_source_remove(app->apply_validation_timer_id);
        app->apply_validation_timer_id = 0;
    }
    if (app->capture_restart_timer_id != 0) {
        g_source_remove(app->capture_restart_timer_id);
        app->capture_restart_timer_id = 0;
    }
    if (app->recovery_timer_id != 0) {
        g_source_remove(app->recovery_timer_id);
        app->recovery_timer_id = 0;
    }

    app->apply_rolling_back = TRUE;
    app->rollback_finish_scheduled = FALSE;
    app->rollback_safe_fallback_attempted = FALSE;
    app->apply_validation_started_us = 0;
    app->apply_validation_frame_count = 0;
    set_apply_blackout(app, TRUE, "Restoring previous mode…");
    stop_pipeline(app);

    if (app->have_last_good_mode) {
        restore_last_good_mode(app);
    } else {
        /* Startup safety net: genuine P010 1080p60 is the card's proven HDR
         * baseline when no prior frame exists in this process. */
        select_safe_hdr1080_mode(app);
    }

    app->recovery_count = 0;
    schedule_preview_recovery(app, reason != NULL ? reason : "Restoring the previous capture mode.");
}

static const gchar *
session_mode_state_name(int session_mode)
{
    switch ((SessionMode)session_mode) {
    case SESSION_MODE_HDR60_1080: return "hdr1080";
    case SESSION_MODE_HDR60_1440: return "hdr1440";
    case SESSION_MODE_HDR60_4K: return "hdr4k";
    case SESSION_MODE_SMOOTH_SDR:
    default: return "smooth";
    }
}

static void
persist_session_mode(OpenCentralApp *app)
{
    gchar *directory;
    gchar *path;

    if (app == NULL) return;
    directory = g_build_filename(g_get_user_state_dir(), "linux-capture-studio", NULL);
    path = g_build_filename(directory, "session-mode", NULL);
    if (g_mkdir_with_parents(directory, 0700) == 0) {
        g_file_set_contents(path, session_mode_state_name(app->session_mode), -1, NULL);
    }
    g_free(path);
    g_free(directory);
}

static void
persist_pending_session_mode(int session_mode)
{
    gchar *directory;
    gchar *path;

    if (session_mode < 0 || session_mode >= SESSION_MODE_COUNT) return;
    directory = g_build_filename(g_get_user_state_dir(), "linux-capture-studio", NULL);
    path = g_build_filename(directory, "pending-session-mode", NULL);
    if (g_mkdir_with_parents(directory, 0700) == 0) {
        g_file_set_contents(path, session_mode_state_name(session_mode), -1, NULL);
    }
    g_free(path);
    g_free(directory);
}

static gboolean
persist_initial_session_cb(gpointer user_data)
{
    OpenCentralApp *app = user_data;
    if (app != NULL && !app->closing) persist_session_mode(app);
    return G_SOURCE_REMOVE;
}

static void
sync_pending_settings_from_current(OpenCentralApp *app)
{
    if (app == NULL) return;
    app->pending_session_mode = app->session_mode;
    app->pending_format_index = app->format_index;
    app->pending_resolution_index = app->resolution_index;
    app->pending_fps = app->fps;
    app->pending_hdr_mode = app->hdr_mode;
    app->pending_hdr_enabled = app->hdr_enabled;
}

static gboolean
pending_settings_equal_current(const OpenCentralApp *app)
{
    if (app == NULL) return TRUE;

    return app->pending_session_mode == app->session_mode &&
           app->pending_format_index == app->format_index &&
           app->pending_resolution_index == app->resolution_index &&
           app->pending_fps == app->fps &&
           app->pending_hdr_mode == app->hdr_mode &&
           app->pending_hdr_enabled == app->hdr_enabled;
}

static void
resolve_pending_source_mode(OpenCentralApp *app,
                            CaptureFormat *source_format,
                            int *source_width,
                            int *source_height,
                            int *source_fps)
{
    gboolean saved_native;
    int saved_session;
    const FormatOption *format;
    const ResolutionOption *resolution;

    if (app == NULL || source_format == NULL || source_width == NULL ||
        source_height == NULL || source_fps == NULL ||
        app->pending_format_index < 0 || app->pending_format_index >= FORMAT_COUNT ||
        app->pending_resolution_index < 0 || app->pending_resolution_index >= RESOLUTION_COUNT) {
        return;
    }

    format = &FORMATS[app->pending_format_index];
    resolution = &RESOLUTIONS[app->pending_resolution_index];
    saved_native = app->native_p010_session;
    saved_session = app->session_mode;

    app->native_p010_session = app->pending_session_mode != SESSION_MODE_SMOOTH_SDR;
    app->session_mode = app->pending_session_mode;
    resolve_source_mode(
        app,
        format->format,
        resolution->width,
        resolution->height,
        app->pending_fps,
        source_format,
        source_width,
        source_height,
        source_fps
    );

    app->native_p010_session = saved_native;
    app->session_mode = saved_session;
}

static gboolean
pending_preview_reuses_active_pipeline(OpenCentralApp *app)
{
    CaptureFormat target_format = CAPTURE_FORMAT_NV12;
    int target_width = 0;
    int target_height = 0;
    int target_fps = 0;
    gboolean current_native;
    gboolean pending_native;

    if (app == NULL || app->pipeline == NULL || !app->have_active_mode ||
        app->pipeline_recording || app->pipeline_streaming) {
        return FALSE;
    }

    current_native = app->session_mode != SESSION_MODE_SMOOTH_SDR;
    pending_native = app->pending_session_mode != SESSION_MODE_SMOOTH_SDR;

    /* Only a real SDR/native-session boundary changes the physical transport.
     * HDR signal mode and preview correction changes are shader-only when the
     * resolved V4L2 source is unchanged. */
    if (current_native != pending_native) {
        return FALSE;
    }

    resolve_pending_source_mode(
        app, &target_format, &target_width, &target_height, &target_fps
    );

    return source_modes_equal(
        app->active_source_format,
        app->active_source_width,
        app->active_source_height,
        app->active_source_fps,
        target_format,
        target_width,
        target_height,
        target_fps
    );
}

static void
update_apply_ui(OpenCentralApp *app)
{
    if (app == NULL || app->apply_button == NULL) return;

    gtk_widget_remove_css_class(app->apply_button, "apply-pending");
    gtk_widget_remove_css_class(app->apply_button, "apply-running");
    gtk_widget_remove_css_class(app->apply_button, "apply-done");

    if (app->apply_in_progress) {
        gtk_button_set_label(GTK_BUTTON(app->apply_button), "Applying…");
        gtk_widget_add_css_class(app->apply_button, "apply-running");
        gtk_widget_set_sensitive(app->apply_button, FALSE);
        if (app->pending_label != NULL) {
            gtk_label_set_text(GTK_LABEL(app->pending_label), "PREVIEW PAUSED");
            gtk_widget_set_visible(app->pending_label, TRUE);
        }
    } else if (app->settings_pending) {
        gtk_button_set_label(GTK_BUTTON(app->apply_button), "Apply Changes");
        gtk_widget_add_css_class(app->apply_button, "apply-pending");
        gtk_widget_set_sensitive(
            app->apply_button,
            !app->recording && !app->record_transition &&
            !app->streaming && !app->stream_transition &&
            app->device_connected
        );
        if (app->pending_label != NULL) {
            gtk_label_set_text(GTK_LABEL(app->pending_label), "CHANGES PENDING");
            gtk_widget_set_visible(app->pending_label, TRUE);
        }
    } else {
        gtk_button_set_label(GTK_BUTTON(app->apply_button), "Applied ✓");
        gtk_widget_add_css_class(app->apply_button, "apply-done");
        gtk_widget_set_sensitive(app->apply_button, FALSE);
        if (app->pending_label != NULL) {
            gtk_widget_set_visible(app->pending_label, FALSE);
        }
    }
}

static void
update_pending_hdr_ui(OpenCentralApp *app)
{
    if (app == NULL || app->hdr_toggle_button == NULL) return;
    gtk_widget_remove_css_class(app->hdr_toggle_button, "hdr-toggle-on");
    gtk_widget_remove_css_class(app->hdr_toggle_button, "hdr-toggle-off");
    gtk_widget_remove_css_class(app->hdr_toggle_button, "hdr-locked");
    if (app->pending_hdr_enabled) {
        gtk_button_set_label(GTK_BUTTON(app->hdr_toggle_button), "HDR ON");
        gtk_widget_add_css_class(app->hdr_toggle_button, "hdr-toggle-on");
    } else {
        gtk_button_set_label(GTK_BUTTON(app->hdr_toggle_button), "HDR OFF");
        gtk_widget_add_css_class(app->hdr_toggle_button, "hdr-toggle-off");
    }
}

static void
mark_capture_settings_pending(OpenCentralApp *app, const gchar *reason)
{
    if (app == NULL || app->apply_in_progress) return;
    app->settings_pending = TRUE;
    update_pending_hdr_ui(app);
    update_apply_ui(app);
    if (app->record_button != NULL) gtk_widget_set_sensitive(app->record_button, FALSE);
    if (app->stream_button != NULL) gtk_widget_set_sensitive(app->stream_button, FALSE);
    if (reason != NULL) show_feedback(app, "%s — press Apply Changes", reason);
}

static void
populate_pending_fps_dropdown(OpenCentralApp *app, int preferred_fps)
{
    int saved_format;
    int saved_resolution;
    int saved_fps;
    int saved_session;
    gboolean saved_native;
    gboolean saved_hdr;
    int saved_hdr_mode;

    if (app == NULL) return;

    if (app->pending_session_mode != SESSION_MODE_SMOOTH_SDR) {
        GtkStringList *list = gtk_string_list_new(NULL);
        gtk_string_list_append(list, "60 FPS");
        app->selection_update = TRUE;
        gtk_drop_down_set_model(GTK_DROP_DOWN(app->fps_dropdown), G_LIST_MODEL(list));
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->fps_dropdown), 0);
        g_object_unref(list);
        app->fps_values[0] = 60;
        app->fps_count = 1;
        app->pending_fps = 60;
        gtk_widget_set_sensitive(app->fps_dropdown, FALSE);
        app->selection_update = FALSE;
        return;
    }

    saved_format = app->format_index;
    saved_resolution = app->resolution_index;
    saved_fps = app->fps;
    saved_session = app->session_mode;
    saved_native = app->native_p010_session;
    saved_hdr = app->hdr_enabled;
    saved_hdr_mode = app->hdr_mode;

    app->format_index = app->pending_format_index;
    app->resolution_index = app->pending_resolution_index;
    app->fps = preferred_fps;
    app->session_mode = app->pending_session_mode;
    app->native_p010_session = FALSE;
    app->hdr_enabled = app->pending_hdr_enabled;
    app->hdr_mode = app->pending_hdr_mode;
    populate_fps_dropdown(app, preferred_fps);
    app->pending_fps = app->fps;

    app->format_index = saved_format;
    app->resolution_index = saved_resolution;
    app->fps = saved_fps;
    app->session_mode = saved_session;
    app->native_p010_session = saved_native;
    app->hdr_enabled = saved_hdr;
    app->hdr_mode = saved_hdr_mode;
}

static void
set_apply_blackout(OpenCentralApp *app, gboolean visible, const gchar *message)
{
    if (app == NULL || app->apply_blackout == NULL) return;

    if (app->apply_blackout_label != NULL && message != NULL) {
        gtk_label_set_text(GTK_LABEL(app->apply_blackout_label), message);
    }
    gtk_widget_set_visible(app->apply_blackout, visible);
}

static void
finish_apply_success(OpenCentralApp *app)
{
    if (app == NULL) return;
    set_apply_blackout(app, FALSE, NULL);
    app->apply_in_progress = FALSE;
    app->settings_pending = FALSE;
    sync_pending_settings_from_current(app);
    set_capture_selectors_sensitive(app, TRUE);
    if (app->session_dropdown != NULL) gtk_widget_set_sensitive(app->session_dropdown, TRUE);
    if (app->stream_button != NULL) gtk_widget_set_sensitive(app->stream_button, FALSE);
    if (app->record_button != NULL) gtk_widget_set_sensitive(app->record_button, app->fps_count > 0);
    update_apply_ui(app);
    update_hdr_toggle_ui(app);
    update_hdr_status(app);
    update_mode_badge(app);
    update_device_ui(app);
    if (app->have_active_mode) {
        app->last_good_format_index = app->active_format_index;
        app->last_good_resolution_index = app->active_resolution_index;
        app->last_good_fps = app->active_fps;
        app->last_good_session_mode = app->session_mode;
        app->last_good_hdr_mode = app->hdr_mode;
        app->last_good_hdr_enabled = app->hdr_enabled;
        app->last_good_native_p010_session = app->native_p010_session;
        app->have_last_good_mode = TRUE;
    }
    persist_session_mode(app);
    save_device_profile(app);
    show_feedback(
        app,
        "Applied: %s • %s • %s • %d FPS",
        SESSION_MODE_NAMES[app->session_mode],
        current_format(app)->name,
        current_resolution(app)->name,
        app->fps
    );
}

static gboolean
finish_apply_without_rebuild_cb(gpointer user_data)
{
    OpenCentralApp *app = user_data;

    if (app == NULL) return G_SOURCE_REMOVE;
    app->capture_restart_timer_id = 0;
    if (app->closing) return G_SOURCE_REMOVE;

    /* The physical source and preview renderer stayed alive behind the
     * confirmation blackout. Update the active output description so
     * recordings use the newly applied size. */
    app->active_format_index = app->format_index;
    app->active_resolution_index = app->resolution_index;
    app->active_fps = app->fps;
    apply_hdr_calibration_to_preview(app);
    finish_apply_success(app);
    set_status(
        app,
        "Preview/output settings applied live after the 0.5-second confirmation blackout; the capture transport stayed active."
    );
    return G_SOURCE_REMOVE;
}

static void
on_apply_clicked(GtkButton *button, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    gboolean reuse_active_preview;
    (void)button;

    if (app == NULL || app->apply_in_progress) return;
    if (!app->settings_pending) {
        show_feedback(app, "All capture settings are already applied");
        return;
    }
    if (app->recording || app->record_transition || app->streaming || app->stream_transition) {
        show_feedback(app, "Stop recording or streaming before applying capture settings");
        return;
    }

    /* Changing a selector away and back can leave the button pending even
     * though the requested mode equals the active mode.  Never tear down a
     * working HDR/GL pipeline for a no-op Apply. */
    if (pending_settings_equal_current(app)) {
        app->settings_pending = FALSE;
        sync_pending_settings_from_current(app);
        update_apply_ui(app);
        if (app->record_button != NULL) {
            gtk_widget_set_sensitive(app->record_button, app->fps_count > 0);
        }
        show_feedback(app, "Already applied — the capture pipeline was not restarted");
        set_status(app, "The selected capture settings are already active.");
        return;
    }

    reuse_active_preview = pending_preview_reuses_active_pipeline(app);

    /* The GC575 firmware can reject a live P010 -> 4K MJPEG family switch
     * even though both modes enumerate correctly.  Perform that one risky
     * transition through the launcher's existing controller-recovery path,
     * then reopen directly in native 4K.  This avoids wedging /dev/video0. */
    if (app->is_gc575 && app->have_active_mode &&
        app->pending_session_mode == SESSION_MODE_HDR60_4K) {
        CaptureFormat target_format = CAPTURE_FORMAT_P010;
        int target_width = 0;
        int target_height = 0;
        int target_fps = 0;
        resolve_pending_source_mode(
            app, &target_format, &target_width, &target_height, &target_fps
        );
        if (target_format == CAPTURE_FORMAT_MJPEG &&
            target_width == 3840 && target_height == 2160 && target_fps == 60 &&
            !source_modes_equal(
                app->active_source_format, app->active_source_width,
                app->active_source_height, app->active_source_fps,
                target_format, target_width, target_height, target_fps
            )) {
            app->apply_in_progress = TRUE;
            app->settings_pending = FALSE;
            update_apply_ui(app);
            set_capture_selectors_sensitive(app, FALSE);
            if (app->session_dropdown != NULL) gtk_widget_set_sensitive(app->session_dropdown, FALSE);
            set_apply_blackout(app, TRUE, "Preparing native 4K60…");
            persist_pending_session_mode(app->pending_session_mode);
            set_status(app, "Resetting the GC575 format engine once, then reopening directly in native 4K60…");
            show_feedback(app, "Safe GC575 4K transition started");
            stop_pipeline(app);
            request_gc575_recovery_exit(app, "Preparing the native 4K60 transport.");
            return;
        }
    }

    if (!app->native_p010_session && app->pending_session_mode != SESSION_MODE_SMOOTH_SDR) {
        app->sdr_restore_format_index = app->format_index;
        app->sdr_restore_resolution_index = app->resolution_index;
        app->sdr_restore_fps = app->fps;
        app->have_sdr_restore_mode = TRUE;
    }

    app->session_mode = app->pending_session_mode;
    app->native_p010_session = app->session_mode != SESSION_MODE_SMOOTH_SDR;
    app->format_index = app->pending_format_index;
    app->resolution_index = app->pending_resolution_index;
    app->fps = app->pending_fps;
    app->hdr_mode = app->pending_hdr_mode;
    app->hdr_enabled = app->pending_hdr_enabled;

    if (app->native_p010_session) enforce_native_p010_lock(app);

    app->apply_in_progress = TRUE;
    app->settings_pending = FALSE;
    update_apply_ui(app);
    set_capture_selectors_sensitive(app, FALSE);
    if (app->session_dropdown != NULL) gtk_widget_set_sensitive(app->session_dropdown, FALSE);
    if (app->stream_button != NULL) gtk_widget_set_sensitive(app->stream_button, FALSE);
    if (app->record_button != NULL) gtk_widget_set_sensitive(app->record_button, FALSE);

    if (app->capture_restart_timer_id != 0) {
        g_source_remove(app->capture_restart_timer_id);
        app->capture_restart_timer_id = 0;
    }

    if (reuse_active_preview) {
        /* The requested settings resolve to the same physical source. Keep the
         * proven capture transport alive, but cover the preview for 500 ms so
         * Apply still has an unmistakable visual confirmation. */
        set_apply_blackout(app, TRUE, "Applying settings…");
        show_feedback(app, "Applying output settings — confirmation blackout active…");
        set_status(
            app,
            "Applying output settings with a 0.5-second confirmation blackout; the capture transport remains active."
        );
        app->capture_restart_timer_id = g_timeout_add(500, finish_apply_without_rebuild_cb, app);
        return;
    }

    set_apply_blackout(app, TRUE, "Switching capture mode…");
    show_feedback(app, "Applying capture settings — preview will resume in about 3 seconds…");
    set_status(app, "Stopping the capture pipeline briefly so the device can release and apply the new mode safely…");
    stop_pipeline(app);
    app->recovery_count = 0;
    app->capture_restart_timer_id = g_timeout_add(2800, apply_capture_change_cb, app);
}

static G_GNUC_UNUSED void
apply_session_mode_in_process(OpenCentralApp *app, int session_mode)
{
    gboolean had_old_source;
    CaptureFormat old_source_format = CAPTURE_FORMAT_NV12;
    int old_source_width = 0;
    int old_source_height = 0;
    int old_source_fps = 0;

    if (app == NULL || session_mode < 0 || session_mode >= SESSION_MODE_COUNT) return;
    if (app->recording || app->record_transition || app->streaming || app->stream_transition) {
        show_feedback(app, "Stop recording or streaming before changing capture session");
        return;
    }

    had_old_source = app->fps > 0 && app->pipeline != NULL && !app->pipeline_recording;
    if (had_old_source) {
        selected_source_mode(app, &old_source_format, &old_source_width, &old_source_height, &old_source_fps);
    }

    if (!app->native_p010_session && session_mode != SESSION_MODE_SMOOTH_SDR) {
        app->sdr_restore_format_index = app->format_index;
        app->sdr_restore_resolution_index = app->resolution_index;
        app->sdr_restore_fps = app->fps;
        app->have_sdr_restore_mode = TRUE;
    }

    app->session_mode = session_mode;
    app->native_p010_session = session_mode != SESSION_MODE_SMOOTH_SDR;
    app->selection_update = TRUE;

    if (app->native_p010_session) {
        app->format_index = 2;
        app->resolution_index = session_mode == SESSION_MODE_HDR60_4K ? 3 :
                                (session_mode == SESSION_MODE_HDR60_1440 ? 2 : 1);
        app->fps = 60;
        app->hdr_mode = HDR_MODE_HDR10_PQ;
        app->hdr_enabled = TRUE;
    } else {
        app->hdr_enabled = FALSE;
        app->hdr_mode = HDR_MODE_HDR10_PQ;
        if (app->have_sdr_restore_mode) {
            app->format_index = app->sdr_restore_format_index;
            app->resolution_index = app->sdr_restore_resolution_index;
            app->fps = app->sdr_restore_fps;
        } else {
            app->format_index = 1;
            app->resolution_index = 2;
            app->fps = 60;
        }
    }

    if (app->session_dropdown != NULL) gtk_drop_down_set_selected(GTK_DROP_DOWN(app->session_dropdown), app->session_mode);
    if (app->format_dropdown != NULL) gtk_drop_down_set_selected(GTK_DROP_DOWN(app->format_dropdown), app->format_index);
    if (app->resolution_dropdown != NULL) gtk_drop_down_set_selected(GTK_DROP_DOWN(app->resolution_dropdown), app->resolution_index);
    if (app->hdr_dropdown != NULL) gtk_drop_down_set_selected(GTK_DROP_DOWN(app->hdr_dropdown), app->hdr_mode);
    app->selection_update = FALSE;

    populate_fps_dropdown(app, app->fps);
    app->selection_update = TRUE;
    if (app->hdr_toggle_button != NULL) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->hdr_toggle_button), app->hdr_enabled);
    }
    app->selection_update = FALSE;

    set_capture_selectors_sensitive(app, TRUE);
    if (app->hdr_dropdown != NULL) gtk_widget_set_sensitive(app->hdr_dropdown, !app->native_p010_session);
    update_hdr_toggle_ui(app);
    update_hdr_status(app);
    update_mode_badge(app);
    update_device_ui(app);
    persist_session_mode(app);

    show_feedback(app, "Applying %s inside the current window…", SESSION_MODE_NAMES[session_mode]);
    apply_selected_mode_change(app, had_old_source, old_source_format, old_source_width, old_source_height, old_source_fps);
}

static void
on_session_mode_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    guint selected;
    (void)object;
    (void)pspec;

    if (app->selection_update || app->session_dropdown == NULL || app->apply_in_progress) return;
    selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->session_dropdown));
    if (selected >= (guint)SESSION_MODE_COUNT) return;
    if (app->recording || app->record_transition || app->streaming || app->stream_transition) {
        app->selection_update = TRUE;
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->session_dropdown), app->pending_session_mode);
        app->selection_update = FALSE;
        show_feedback(app, "Stop recording or streaming before changing capture session");
        return;
    }

    app->pending_session_mode = (int)selected;
    if (selected == SESSION_MODE_HDR60_1080 || selected == SESSION_MODE_HDR60_1440 || selected == SESSION_MODE_HDR60_4K) {
        app->pending_format_index = 2;
        app->pending_resolution_index = selected == SESSION_MODE_HDR60_4K ? 3 :
                                        (selected == SESSION_MODE_HDR60_1440 ? 2 : 1);
        app->pending_fps = 60;
        app->pending_hdr_mode = HDR_MODE_HDR10_PQ;
        app->pending_hdr_enabled = TRUE;
    } else {
        app->pending_hdr_enabled = FALSE;
        app->pending_hdr_mode = HDR_MODE_HDR10_PQ;
        if (app->native_p010_session) {
            if (app->have_sdr_restore_mode) {
                app->pending_format_index = app->sdr_restore_format_index;
                app->pending_resolution_index = app->sdr_restore_resolution_index;
                app->pending_fps = app->sdr_restore_fps;
            } else {
                app->pending_format_index = 1;
                app->pending_resolution_index = 2;
                app->pending_fps = 60;
            }
        } else if (app->pending_format_index < 0 || app->pending_format_index >= FORMAT_COUNT) {
            app->pending_format_index = 1;
            app->pending_resolution_index = 2;
            app->pending_fps = 60;
        }
    }

    app->selection_update = TRUE;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->format_dropdown), app->pending_format_index);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->resolution_dropdown), app->pending_resolution_index);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->hdr_dropdown), app->pending_hdr_mode);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->hdr_toggle_button), app->pending_hdr_enabled);
    app->selection_update = FALSE;
    populate_pending_fps_dropdown(app, app->pending_fps);
    gtk_widget_set_sensitive(app->format_dropdown, selected == SESSION_MODE_SMOOTH_SDR);
    gtk_widget_set_sensitive(app->fps_dropdown, selected == SESSION_MODE_SMOOTH_SDR && app->fps_count > 0);
    gtk_widget_set_sensitive(app->resolution_dropdown, TRUE);
    mark_capture_settings_pending(app, SESSION_MODE_NAMES[selected]);
}

static void
on_studio_check_clicked(GtkButton *button, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    gboolean video_ok;
    gboolean audio_ok;
    gboolean recording_ok;
    gboolean hdr_ok;
    gchar *text;

    (void)button;

    video_ok = app->device_connected && app->control_fd >= 0;
    audio_ok = !app->audio_capture_disabled;
    recording_ok = gst_element_available("avenc_ffv1") && gst_element_available("matroskamux");
    if (app->native_p010_session) {
        CaptureFormat source_format;
        int source_width;
        int source_height;
        int source_fps;
        const FormatOption *format = current_format(app);
        const ResolutionOption *resolution = current_resolution(app);
        resolve_source_mode(
            app,
            format->format,
            resolution->width,
            resolution->height,
            app->fps,
            &source_format,
            &source_width,
            &source_height,
            &source_fps
        );
        hdr_ok = native_source_mode_supported(
            app, source_format, source_width, source_height, source_fps
        );
    } else {
        hdr_ok = !app->is_gc575 ||
            device_mode_supported(app, CAPTURE_FORMAT_P010, 1920, 1080, 60);
    }

    text = g_strdup_printf(
        "Video %s  •  Audio %s  •  Recording %s  •  Streaming Coming Soon  •  HDR60 %s",
        video_ok ? "✓" : "✕",
        audio_ok ? "✓" : "✕",
        recording_ok ? "✓" : "✕",
        hdr_ok ? "✓" : "✕"
    );
    if (app->studio_check_label != NULL) {
        gtk_label_set_text(GTK_LABEL(app->studio_check_label), text);
    }
    show_feedback(app, "%s", text);
    g_free(text);
}

static void
on_capture_setting_changed(GObject *dropdown, GParamSpec *pspec, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    guint selected;
    int preferred_fps;
    (void)pspec;

    if (app->selection_update || app->apply_in_progress ||
        app->recording || app->record_transition || app->streaming || app->stream_transition) return;

    if (app->pending_session_mode != SESSION_MODE_SMOOTH_SDR) {
        if (dropdown == G_OBJECT(app->resolution_dropdown)) {
            selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->resolution_dropdown));
            if (selected == 1) app->pending_session_mode = SESSION_MODE_HDR60_1080;
            else if (selected == 2) app->pending_session_mode = SESSION_MODE_HDR60_1440;
            else if (selected == 3) app->pending_session_mode = SESSION_MODE_HDR60_4K;
            else {
                app->selection_update = TRUE;
                gtk_drop_down_set_selected(GTK_DROP_DOWN(app->resolution_dropdown), app->pending_resolution_index);
                app->selection_update = FALSE;
                show_feedback(app, "HDR60 supports native 1080p, native 1440p, or native 4K");
                return;
            }
            app->pending_resolution_index = (int)selected;
            app->selection_update = TRUE;
            gtk_drop_down_set_selected(GTK_DROP_DOWN(app->session_dropdown), app->pending_session_mode);
            app->selection_update = FALSE;
            mark_capture_settings_pending(app, "HDR60 native source resolution changed");
        }
        return;
    }

    preferred_fps = app->pending_fps > 0 ? app->pending_fps : 60;
    if (dropdown == G_OBJECT(app->format_dropdown)) {
        selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->format_dropdown));
        if (selected >= (guint)FORMAT_COUNT) return;
        app->pending_format_index = (int)selected;
        if (FORMATS[selected].format != CAPTURE_FORMAT_P010 && app->pending_hdr_enabled) {
            app->pending_hdr_enabled = FALSE;
            app->selection_update = TRUE;
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->hdr_toggle_button), FALSE);
            app->selection_update = FALSE;
        }
        populate_pending_fps_dropdown(app, preferred_fps);
        mark_capture_settings_pending(app, "Capture format changed");
    } else if (dropdown == G_OBJECT(app->resolution_dropdown)) {
        selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->resolution_dropdown));
        if (selected >= (guint)RESOLUTION_COUNT) return;
        app->pending_resolution_index = (int)selected;
        populate_pending_fps_dropdown(app, preferred_fps);
        mark_capture_settings_pending(app, "Resolution changed");
    } else if (dropdown == G_OBJECT(app->fps_dropdown)) {
        selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->fps_dropdown));
        if (selected >= app->fps_count) return;
        app->pending_fps = app->fps_values[selected];
        mark_capture_settings_pending(app, "Frame rate changed");
    }
}

static void
on_hdr_toggle_toggled(GtkToggleButton *button, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    gboolean enable;

    if (app->selection_update || app->apply_in_progress) return;
    if (app->recording || app->record_transition || app->streaming || app->stream_transition) {
        app->selection_update = TRUE;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->hdr_toggle_button), app->pending_hdr_enabled);
        app->selection_update = FALSE;
        show_feedback(app, "Stop recording or streaming before changing HDR");
        return;
    }

    enable = gtk_toggle_button_get_active(button);
    app->pending_hdr_enabled = enable;

    if (app->is_gc575) {
        app->pending_session_mode = enable ?
            (app->pending_session_mode == SESSION_MODE_SMOOTH_SDR ? SESSION_MODE_HDR60_1080 : app->pending_session_mode) :
            SESSION_MODE_SMOOTH_SDR;
        if (enable) {
            app->pending_format_index = 2;
            app->pending_resolution_index = app->pending_session_mode == SESSION_MODE_HDR60_4K ? 3 :
                                            (app->pending_session_mode == SESSION_MODE_HDR60_1440 ? 2 : 1);
            app->pending_fps = 60;
            app->pending_hdr_mode = HDR_MODE_HDR10_PQ;
        } else if (app->have_sdr_restore_mode) {
            app->pending_format_index = app->sdr_restore_format_index;
            app->pending_resolution_index = app->sdr_restore_resolution_index;
            app->pending_fps = app->sdr_restore_fps;
        } else {
            app->pending_format_index = 1;
            app->pending_resolution_index = 2;
            app->pending_fps = 60;
        }
        app->selection_update = TRUE;
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->session_dropdown), app->pending_session_mode);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->format_dropdown), app->pending_format_index);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->resolution_dropdown), app->pending_resolution_index);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->hdr_dropdown), app->pending_hdr_mode);
        app->selection_update = FALSE;
        populate_pending_fps_dropdown(app, app->pending_fps);
        gtk_widget_set_sensitive(app->format_dropdown, !enable);
        gtk_widget_set_sensitive(app->fps_dropdown, !enable && app->fps_count > 0);
    } else if (enable) {
        app->pending_hdr_mode = HDR_MODE_HDR10_PQ;
        app->pending_format_index = 2;
        if (app->pending_resolution_index > 2) app->pending_resolution_index = 2;
        populate_pending_fps_dropdown(app, app->pending_resolution_index <= 1 ? 60 : 30);
    }

    mark_capture_settings_pending(app, enable ? "HDR enabled" : "HDR disabled");
}

static void
on_hdr_setting_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    guint selected;
    (void)object;
    (void)pspec;

    if (app->selection_update || app->apply_in_progress) return;
    selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->hdr_dropdown));
    if (selected == GTK_INVALID_LIST_POSITION || selected >= (guint)HDR_MODE_COUNT) return;
    app->pending_hdr_mode = (int)selected;
    if (selected == HDR_MODE_SDR && app->pending_hdr_enabled) {
        app->pending_hdr_enabled = FALSE;
        app->selection_update = TRUE;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->hdr_toggle_button), FALSE);
        app->selection_update = FALSE;
    }
    mark_capture_settings_pending(app, "HDR signal mode changed");
}

static void
on_hdr_tonemap_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    gboolean enabled;

    (void)object;
    (void)pspec;

    enabled = gtk_switch_get_active(GTK_SWITCH(app->hdr_tonemap_switch));
    if (!enabled) {
        set_status(app, "HDR preview correction disabled. HDR may look gray on an SDR desktop; recording remains unchanged.");
        schedule_hdr_preview_refresh(app, "Disabling HDR preview correction…");
        return;
    }

    if (gst_element_available("glupload") &&
        gst_element_available("glcolorconvert") &&
        gst_element_available("glshader")) {
        set_status(
            app,
            "Automatic HDR preview correction is enabled. Recording remains genuine HDR and is not tone-mapped."
        );
        schedule_hdr_preview_refresh(app, "Enabling HDR preview correction…");
        return;
    }

    if (gst_element_available("vapostproc") &&
        gst_element_has_property("vapostproc", "hdr-tone-mapping")) {
        set_status(
            app,
            "VA-API HDR-to-SDR preview is enabled."
        );
        schedule_hdr_preview_refresh(app, "Enabling HDR preview correction…");
        return;
    }

    gtk_switch_set_active(GTK_SWITCH(app->hdr_tonemap_switch), FALSE);
    set_status(
        app,
        "Automatic HDR preview correction is unavailable on the current graphics backend. "
        "Use Settings → Device → Run Studio Check to review the installed components. "
        "HDR recording remains available."
    );
}

static gboolean
update_stats(gpointer user_data)
{
    OpenCentralApp *app = user_data;

    if (app == NULL || app->closing ||
        app->stats_label == NULL || !GTK_IS_WIDGET(app->stats_label) ||
        app->record_button == NULL || !GTK_IS_WIDGET(app->record_button)) {
        if (app != NULL) app->stats_timer_id = 0;
        return G_SOURCE_REMOVE;
    }

    update_hdr_status(app);
    update_vrr_status(app);

    if (!app->recording || app->current_file == NULL) {
        gtk_widget_remove_css_class(app->stats_label, "recording");
        gtk_widget_remove_css_class(app->record_button, "recording-active");
        gtk_label_set_text(GTK_LABEL(app->stats_label), "");
        gtk_widget_set_visible(app->stats_label, FALSE);
        return G_SOURCE_CONTINUE;
    }

    gtk_widget_set_visible(app->stats_label, TRUE);
    gtk_widget_add_css_class(app->stats_label, "recording");
    gtk_widget_add_css_class(app->record_button, "recording-active");

    gint64 elapsed = (g_get_monotonic_time() - app->record_started_us) / G_USEC_PER_SEC;
    goffset size = 0;
    GFile *file = g_file_new_for_path(app->current_file);
    GFileInfo *info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_SIZE, G_FILE_QUERY_INFO_NONE, NULL, NULL);
    if (info != NULL) {
        size = g_file_info_get_size(info);
        g_object_unref(info);
    }
    g_object_unref(file);

    gchar *size_text = g_format_size_full(size, G_FORMAT_SIZE_IEC_UNITS);
    gchar *text = g_strdup_printf(
        "●  REC  %02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT "   %s",
        elapsed / 3600,
        (elapsed / 60) % 60,
        elapsed % 60,
        size_text
    );
    gtk_label_set_text(GTK_LABEL(app->stats_label), text);
    g_free(text);
    g_free(size_text);

    return G_SOURCE_CONTINUE;
}

static gboolean
on_close_request(GtkWindow *window, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    (void)window;

    app->closing = TRUE;
    app->streaming = FALSE;
    app->stream_transition = FALSE;
    if (app->settings_window != NULL) gtk_widget_set_visible(app->settings_window, FALSE);
    if (app->hdr_calibration_window != NULL) gtk_widget_set_visible(app->hdr_calibration_window, FALSE);
    save_device_profile(app);

    if (app->stats_timer_id != 0) {
        g_source_remove(app->stats_timer_id);
        app->stats_timer_id = 0;
    }
    if (app->watchdog_timer_id != 0) {
        g_source_remove(app->watchdog_timer_id);
        app->watchdog_timer_id = 0;
    }
    if (app->device_watch_timer_id != 0) {
        g_source_remove(app->device_watch_timer_id);
        app->device_watch_timer_id = 0;
    }
    if (app->feedback_timeout_id != 0) {
        g_source_remove(app->feedback_timeout_id);
        app->feedback_timeout_id = 0;
    }
    if (app->recovery_timer_id != 0) {
        g_source_remove(app->recovery_timer_id);
        app->recovery_timer_id = 0;
    }
    if (app->capture_restart_timer_id != 0) {
        g_source_remove(app->capture_restart_timer_id);
        app->capture_restart_timer_id = 0;
    }
    if (app->record_stop_timeout_id != 0) {
        g_source_remove(app->record_stop_timeout_id);
        app->record_stop_timeout_id = 0;
    }
    if (app->hdr_refresh_timer_id != 0) {
        g_source_remove(app->hdr_refresh_timer_id);
        app->hdr_refresh_timer_id = 0;
    }
    if (app->apply_validation_timer_id != 0) {
        g_source_remove(app->apply_validation_timer_id);
        app->apply_validation_timer_id = 0;
    }

    if ((app->recording || app->pipeline_recording) && app->pipeline != NULL) {
        GstBus *bus;
        GstMessage *message;

        gst_element_send_event(app->pipeline, gst_event_new_eos());
        bus = gst_element_get_bus(app->pipeline);
        message = gst_bus_timed_pop_filtered(
            bus,
            5 * GST_SECOND,
            GST_MESSAGE_EOS | GST_MESSAGE_ERROR
        );
        if (message != NULL) {
            gst_message_unref(message);
        }
        gst_object_unref(bus);
    }

    stop_pipeline(app);
    return FALSE;
}

static void
on_settings_clicked(GtkButton *button, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    (void)button;
    if (app->settings_window != NULL) {
        gtk_window_present(GTK_WINDOW(app->settings_window));
    }
}

static void
on_settings_close_clicked(GtkButton *button, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    (void)button;
    if (app->settings_window != NULL) {
        gtk_widget_set_visible(app->settings_window, FALSE);
    }
}

static gboolean
on_settings_window_close_request(GtkWindow *window, gpointer user_data)
{
    (void)user_data;
    gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    return TRUE;
}

static gboolean
apply_hdr_calibration_to_preview(OpenCentralApp *app)
{
    GstElement *shader;
    gchar *source;

    if (app == NULL || app->pipeline == NULL) return FALSE;
    shader = gst_bin_get_by_name(GST_BIN(app->pipeline), "hdr_preview_shader");
    if (shader == NULL) return FALSE;

    source = make_hdr_preview_shader(
        app,
        effective_hdr_mode(app),
        app->have_active_mode ? app->active_source_format : CAPTURE_FORMAT_P010
    );
    g_object_set(shader, "fragment", source, NULL);
    {
        GstPad *sink_pad = gst_element_get_static_pad(shader, "sink");
        if (sink_pad != NULL) {
            gst_pad_send_event(sink_pad, gst_event_new_reconfigure());
            gst_object_unref(sink_pad);
        }
    }
    gst_object_unref(shader);
    g_free(source);
    return TRUE;
}

static void
on_hdr_calibration_reset_clicked(GtkButton *button, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    (void)button;
    if (app == NULL) return;

    app->hdr_peak_nits = 1000.0;
    app->hdr_paper_white_nits = 203.0;
    app->hdr_black_level_nits = 0.0;
    app->hdr_preview_saturation = 1.06;
    save_hdr_calibration(app);
    apply_hdr_calibration_to_preview(app);
    schedule_hdr_preview_refresh(app, "Resetting HDR preview calibration…");
    if (app->hdr_calibration_status_label != NULL) {
        gtk_label_set_text(GTK_LABEL(app->hdr_calibration_status_label),
                           "Current: 1000-nit peak • 203-nit reference white • 0.00-nit black");
    }
    show_feedback(app, "HDR calibration reset to defaults");
}

static gboolean
refresh_hdr_preview_cb(gpointer user_data)
{
    OpenCentralApp *app = user_data;

    if (app == NULL) return G_SOURCE_REMOVE;
    app->hdr_refresh_timer_id = 0;
    if (app->closing || app->recording || app->streaming) {
        set_apply_blackout(app, FALSE, NULL);
        return G_SOURCE_REMOVE;
    }
    if (app->rebuilding || app->apply_in_progress || app->capture_restart_timer_id != 0) {
        app->hdr_refresh_timer_id = g_timeout_add(250, refresh_hdr_preview_cb, app);
        return G_SOURCE_REMOVE;
    }

    if (apply_hdr_calibration_to_preview(app)) {
        set_status(app, "HDR preview settings applied live; the GC575 capture transport remained open.");
    } else {
        /* Preview-only controls must never reopen v4l2src.  A missing shader
         * is reported instead of risking another GC575 TRY_FMT transition. */
        set_status(app, "HDR preview settings were saved, but the live shader is unavailable. The capture transport was left untouched.");
    }
    set_apply_blackout(app, FALSE, NULL);
    return G_SOURCE_REMOVE;
}

static void
schedule_hdr_preview_refresh(OpenCentralApp *app, const gchar *message)
{
    if (app == NULL || app->closing || app->recording || app->streaming) return;
    if (app->hdr_refresh_timer_id != 0) {
        g_source_remove(app->hdr_refresh_timer_id);
        app->hdr_refresh_timer_id = 0;
    }
    set_apply_blackout(app, TRUE, message != NULL ? message : "Updating HDR preview…");
    app->hdr_refresh_timer_id = g_timeout_add(500, refresh_hdr_preview_cb, app);
}

static void
hdr_calibration_set_preview_values(OpenCentralApp *app)
{
    gdouble value;

    if (app == NULL || app->hdr_calibration_scale == NULL) return;
    value = gtk_range_get_value(GTK_RANGE(app->hdr_calibration_scale));
    switch (app->hdr_calibration_step) {
    case 0: app->hdr_peak_nits = value; break;
    case 1: app->hdr_paper_white_nits = value; break;
    case 2: app->hdr_black_level_nits = value; break;
    default: break;
    }
    apply_hdr_calibration_to_preview(app);
}

static void
hdr_calibration_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    double cx = width / 2.0;
    double cy = height / 2.0;
    double radius = MIN(width, height) * 0.20;
    double value = 0.0;
    (void)area;

    if (app != NULL && app->hdr_calibration_scale != NULL) {
        value = gtk_range_get_value(GTK_RANGE(app->hdr_calibration_scale));
    }

    cairo_set_source_rgb(cr, 0.008, 0.010, 0.014);
    cairo_paint(cr);

    if (app == NULL || app->hdr_calibration_step == 0) {
        double visibility = 0.12 * (1.0 - CLAMP((value - 400.0) / 3600.0, 0.0, 1.0));
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_arc(cr, cx, cy, radius, 0.0, 2.0 * G_PI);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 1.0 - visibility, 1.0 - visibility, 1.0 - visibility);
        cairo_arc(cr, cx, cy, radius * 0.48, 0.0, 2.0 * G_PI);
        cairo_fill(cr);
        for (int i = 0; i < 12; i++) {
            double a = i * G_PI / 6.0;
            double x1 = cx + cos(a) * radius * 1.18;
            double y1 = cy + sin(a) * radius * 1.18;
            double x2 = cx + cos(a) * radius * 1.48;
            double y2 = cy + sin(a) * radius * 1.48;
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_set_line_width(cr, MAX(3.0, radius * 0.08));
            cairo_move_to(cr, x1, y1);
            cairo_line_to(cr, x2, y2);
            cairo_stroke(cr);
        }
    } else if (app->hdr_calibration_step == 1) {
        double level = 0.22 + 0.70 * CLAMP((value - 80.0) / 320.0, 0.0, 1.0);
        double w = width * 0.58;
        double h = height * 0.45;
        cairo_set_source_rgb(cr, 0.025, 0.028, 0.034);
        cairo_rectangle(cr, cx - w * 0.62, cy - h * 0.62, w * 1.24, h * 1.24);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, level, level, level);
        cairo_rectangle(cr, cx - w / 2.0, cy - h / 2.0, w, h);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.18);
        cairo_set_line_width(cr, 3.0);
        cairo_rectangle(cr, cx - w / 2.0, cy - h / 2.0, w, h);
        cairo_stroke(cr);
    } else {
        double symbol = 0.006 + 0.085 * CLAMP(value / 5.0, 0.0, 1.0);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_paint(cr);
        cairo_set_source_rgb(cr, symbol, symbol, symbol);
        cairo_arc(cr, cx, cy, radius * 1.10, 0.0, 2.0 * G_PI);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_arc(cr, cx, cy, radius * 0.62, 0.0, 2.0 * G_PI);
        cairo_fill(cr);
    }
}

static void
update_hdr_calibration_wizard(OpenCentralApp *app)
{
    const gchar *title;
    const gchar *instruction;
    gchar *step_text;
    gchar *value_text;
    double value;

    if (app == NULL || app->hdr_calibration_scale == NULL) return;
    app->hdr_calibration_updating = TRUE;
    switch (app->hdr_calibration_step) {
    case 0:
        title = "Maximum luminance";
        instruction = "Increase the value until the inner symbol is only barely visible, then go back one step. This sets the HDR highlight ceiling.";
        gtk_range_set_range(GTK_RANGE(app->hdr_calibration_scale), 400.0, 4000.0);
        gtk_range_set_increments(GTK_RANGE(app->hdr_calibration_scale), 50.0, 200.0);
        gtk_range_set_value(GTK_RANGE(app->hdr_calibration_scale), app->hdr_peak_nits);
        gtk_scale_set_digits(GTK_SCALE(app->hdr_calibration_scale), 0);
        break;
    case 1:
        title = "Reference white";
        instruction = "Adjust until the large white panel looks bright and clean without being uncomfortable. This controls menus, subtitles, and normal bright objects.";
        gtk_range_set_range(GTK_RANGE(app->hdr_calibration_scale), 80.0, 400.0);
        gtk_range_set_increments(GTK_RANGE(app->hdr_calibration_scale), 1.0, 10.0);
        gtk_range_set_value(GTK_RANGE(app->hdr_calibration_scale), app->hdr_paper_white_nits);
        gtk_scale_set_digits(GTK_SCALE(app->hdr_calibration_scale), 0);
        break;
    default:
        title = "Black level";
        instruction = "Raise the value until the dark symbol becomes visible, then lower it until it is barely visible. This prevents crushed shadow detail.";
        gtk_range_set_range(GTK_RANGE(app->hdr_calibration_scale), 0.0, 5.0);
        gtk_range_set_increments(GTK_RANGE(app->hdr_calibration_scale), 0.05, 0.25);
        gtk_range_set_value(GTK_RANGE(app->hdr_calibration_scale), app->hdr_black_level_nits);
        gtk_scale_set_digits(GTK_SCALE(app->hdr_calibration_scale), 2);
        break;
    }
    app->hdr_calibration_updating = FALSE;

    step_text = g_strdup_printf("STEP %d OF 3", app->hdr_calibration_step + 1);
    gtk_label_set_text(GTK_LABEL(app->hdr_calibration_step_label), step_text);
    g_free(step_text);
    gtk_label_set_text(GTK_LABEL(app->hdr_calibration_title_label), title);
    gtk_label_set_text(GTK_LABEL(app->hdr_calibration_instruction_label), instruction);
    value = gtk_range_get_value(GTK_RANGE(app->hdr_calibration_scale));
    value_text = app->hdr_calibration_step == 2
        ? g_strdup_printf("%.2f nits", value)
        : g_strdup_printf("%.0f nits", value);
    gtk_label_set_text(GTK_LABEL(app->hdr_calibration_value_label), value_text);
    g_free(value_text);
    gtk_widget_set_sensitive(app->hdr_calibration_back_button, app->hdr_calibration_step > 0);
    gtk_button_set_label(GTK_BUTTON(app->hdr_calibration_next_button), app->hdr_calibration_step == 2 ? "Finish" : "Next");
    gtk_widget_queue_draw(app->hdr_calibration_drawing);
}

static void
on_hdr_calibration_value_changed(GtkRange *range, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    gchar *value_text;
    double value;
    (void)range;

    if (app == NULL || app->hdr_calibration_updating) return;
    hdr_calibration_set_preview_values(app);
    value = gtk_range_get_value(GTK_RANGE(app->hdr_calibration_scale));
    value_text = app->hdr_calibration_step == 2
        ? g_strdup_printf("%.2f nits", value)
        : g_strdup_printf("%.0f nits", value);
    gtk_label_set_text(GTK_LABEL(app->hdr_calibration_value_label), value_text);
    g_free(value_text);
    gtk_widget_queue_draw(app->hdr_calibration_drawing);
}

static void
on_hdr_calibration_start_clicked(GtkButton *button, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    GtkWidget *root;
    GtkWidget *header;
    GtkWidget *controls;
    GtkWidget *cancel;
    GtkWidget *minus;
    GtkWidget *plus;
    (void)button;

    if (app == NULL) return;
    app->hdr_calibration_original_peak_nits = app->hdr_peak_nits;
    app->hdr_calibration_original_paper_white_nits = app->hdr_paper_white_nits;
    app->hdr_calibration_original_black_level_nits = app->hdr_black_level_nits;
    app->hdr_calibration_step = 0;

    if (app->hdr_calibration_window == NULL) {
        app->hdr_calibration_window = gtk_window_new();
        gtk_window_set_application(GTK_WINDOW(app->hdr_calibration_window), app->gtk_app);
        gtk_window_set_title(GTK_WINDOW(app->hdr_calibration_window), "Linux Capture Studio — HDR Calibration");
        gtk_window_set_default_size(GTK_WINDOW(app->hdr_calibration_window), 1040, 760);
        gtk_window_set_resizable(GTK_WINDOW(app->hdr_calibration_window), TRUE);
        gtk_window_set_modal(GTK_WINDOW(app->hdr_calibration_window), FALSE);
        gtk_window_set_hide_on_close(GTK_WINDOW(app->hdr_calibration_window), TRUE);
        gtk_widget_add_css_class(app->hdr_calibration_window, "hdr-wizard-window");

        root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
        gtk_widget_set_margin_top(root, 24);
        gtk_widget_set_margin_bottom(root, 24);
        gtk_widget_set_margin_start(root, 28);
        gtk_widget_set_margin_end(root, 28);
        gtk_window_set_child(GTK_WINDOW(app->hdr_calibration_window), root);

        header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        app->hdr_calibration_step_label = gtk_label_new("STEP 1 OF 3");
        gtk_label_set_xalign(GTK_LABEL(app->hdr_calibration_step_label), 0.0f);
        gtk_widget_add_css_class(app->hdr_calibration_step_label, "wizard-step");
        app->hdr_calibration_title_label = gtk_label_new("Maximum luminance");
        gtk_label_set_xalign(GTK_LABEL(app->hdr_calibration_title_label), 0.0f);
        gtk_widget_add_css_class(app->hdr_calibration_title_label, "wizard-title");
        app->hdr_calibration_instruction_label = gtk_label_new(NULL);
        gtk_label_set_xalign(GTK_LABEL(app->hdr_calibration_instruction_label), 0.0f);
        gtk_label_set_wrap(GTK_LABEL(app->hdr_calibration_instruction_label), TRUE);
        gtk_widget_add_css_class(app->hdr_calibration_instruction_label, "wizard-instruction");
        gtk_box_append(GTK_BOX(header), app->hdr_calibration_step_label);
        gtk_box_append(GTK_BOX(header), app->hdr_calibration_title_label);
        gtk_box_append(GTK_BOX(header), app->hdr_calibration_instruction_label);
        gtk_box_append(GTK_BOX(root), header);

        app->hdr_calibration_drawing = gtk_drawing_area_new();
        gtk_widget_set_vexpand(app->hdr_calibration_drawing, TRUE);
        gtk_widget_set_hexpand(app->hdr_calibration_drawing, TRUE);
        gtk_widget_set_size_request(app->hdr_calibration_drawing, 640, 390);
        gtk_widget_add_css_class(app->hdr_calibration_drawing, "wizard-pattern");
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app->hdr_calibration_drawing), hdr_calibration_draw, app, NULL);
        gtk_box_append(GTK_BOX(root), app->hdr_calibration_drawing);

        controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        minus = gtk_button_new_with_label("−");
        plus = gtk_button_new_with_label("+");
        gtk_widget_add_css_class(minus, "wizard-adjust-button");
        gtk_widget_add_css_class(plus, "wizard-adjust-button");
        app->hdr_calibration_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 400.0, 4000.0, 50.0);
        gtk_scale_set_draw_value(GTK_SCALE(app->hdr_calibration_scale), FALSE);
        gtk_widget_set_hexpand(app->hdr_calibration_scale, TRUE);
        app->hdr_calibration_value_label = gtk_label_new("1000 nits");
        gtk_widget_set_size_request(app->hdr_calibration_value_label, 110, -1);
        gtk_widget_add_css_class(app->hdr_calibration_value_label, "wizard-value");
        gtk_box_append(GTK_BOX(controls), minus);
        gtk_box_append(GTK_BOX(controls), app->hdr_calibration_scale);
        gtk_box_append(GTK_BOX(controls), plus);
        gtk_box_append(GTK_BOX(controls), app->hdr_calibration_value_label);
        gtk_box_append(GTK_BOX(root), controls);

        controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        cancel = gtk_button_new_with_label("Cancel");
        app->hdr_calibration_back_button = gtk_button_new_with_label("Back");
        app->hdr_calibration_next_button = gtk_button_new_with_label("Next");
        gtk_widget_add_css_class(cancel, "secondary-button");
        gtk_widget_add_css_class(app->hdr_calibration_back_button, "secondary-button");
        gtk_widget_add_css_class(app->hdr_calibration_next_button, "apply-button");
        gtk_widget_set_hexpand(cancel, TRUE);
        gtk_widget_set_halign(cancel, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(controls), cancel);
        gtk_box_append(GTK_BOX(controls), app->hdr_calibration_back_button);
        gtk_box_append(GTK_BOX(controls), app->hdr_calibration_next_button);
        gtk_box_append(GTK_BOX(root), controls);

        g_signal_connect(app->hdr_calibration_scale, "value-changed", G_CALLBACK(on_hdr_calibration_value_changed), app);
        g_signal_connect(minus, "clicked", G_CALLBACK(on_hdr_calibration_minus_clicked), app);
        g_signal_connect(plus, "clicked", G_CALLBACK(on_hdr_calibration_plus_clicked), app);
        g_signal_connect(app->hdr_calibration_back_button, "clicked", G_CALLBACK(on_hdr_calibration_back_clicked), app);
        g_signal_connect(app->hdr_calibration_next_button, "clicked", G_CALLBACK(on_hdr_calibration_next_clicked), app);
        g_signal_connect(cancel, "clicked", G_CALLBACK(on_hdr_calibration_cancel_clicked), app);
        g_signal_connect(app->hdr_calibration_window, "close-request", G_CALLBACK(on_hdr_calibration_window_close_request), app);
    }

    update_hdr_calibration_wizard(app);
    gtk_window_present(GTK_WINDOW(app->hdr_calibration_window));
}

static void
hdr_calibration_adjust_by(OpenCentralApp *app, double direction)
{
    GtkAdjustment *adjustment;
    double step;
    double value;
    if (app == NULL || app->hdr_calibration_scale == NULL) return;
    adjustment = gtk_range_get_adjustment(GTK_RANGE(app->hdr_calibration_scale));
    step = gtk_adjustment_get_step_increment(adjustment);
    value = gtk_range_get_value(GTK_RANGE(app->hdr_calibration_scale));
    gtk_range_set_value(GTK_RANGE(app->hdr_calibration_scale), value + direction * step);
}

static void
on_hdr_calibration_minus_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    hdr_calibration_adjust_by(user_data, -1.0);
}

static void
on_hdr_calibration_plus_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    hdr_calibration_adjust_by(user_data, 1.0);
}

static void
on_hdr_calibration_back_clicked(GtkButton *button, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    (void)button;
    if (app == NULL || app->hdr_calibration_step <= 0) return;
    hdr_calibration_set_preview_values(app);
    app->hdr_calibration_step--;
    update_hdr_calibration_wizard(app);
}

static void
on_hdr_calibration_next_clicked(GtkButton *button, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    gboolean live_applied;
    (void)button;
    if (app == NULL) return;
    hdr_calibration_set_preview_values(app);
    if (app->hdr_calibration_step < 2) {
        app->hdr_calibration_step++;
        update_hdr_calibration_wizard(app);
        return;
    }

    save_hdr_calibration(app);
    live_applied = apply_hdr_calibration_to_preview(app);
    if (app->hdr_tonemap_switch != NULL &&
        !gtk_switch_get_active(GTK_SWITCH(app->hdr_tonemap_switch))) {
        gtk_switch_set_active(GTK_SWITCH(app->hdr_tonemap_switch), TRUE);
    }
    /* Recreate only the preview renderer after Finish.  This guarantees that
     * the saved shader is active even on GL drivers that do not recompile a
     * changed fragment property while PLAYING. */
    schedule_hdr_preview_refresh(app, "Applying calibrated HDR preview…");
    if (app->hdr_calibration_status_label != NULL) {
        gchar *text = g_strdup_printf(
            "Calibrated: %.0f-nit peak • %.0f-nit reference white • %.2f-nit black",
            app->hdr_peak_nits,
            app->hdr_paper_white_nits,
            app->hdr_black_level_nits
        );
        gtk_label_set_text(GTK_LABEL(app->hdr_calibration_status_label), text);
        g_free(text);
    }
    gtk_widget_set_visible(app->hdr_calibration_window, FALSE);
    show_feedback(app, live_applied ? "HDR calibration applied; clean preview refresh scheduled" : "HDR calibration saved; clean preview refresh scheduled");
}

static void
on_hdr_calibration_cancel_clicked(GtkButton *button, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    (void)button;
    if (app == NULL) return;
    app->hdr_peak_nits = app->hdr_calibration_original_peak_nits;
    app->hdr_paper_white_nits = app->hdr_calibration_original_paper_white_nits;
    app->hdr_black_level_nits = app->hdr_calibration_original_black_level_nits;
    apply_hdr_calibration_to_preview(app);
    if (app->hdr_calibration_window != NULL) {
        gtk_widget_set_visible(app->hdr_calibration_window, FALSE);
    }
}

static gboolean
on_hdr_calibration_window_close_request(GtkWindow *window, gpointer user_data)
{
    on_hdr_calibration_cancel_clicked(NULL, user_data);
    gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    return TRUE;
}

static void
on_profile_entry_changed(GtkEditable *editable, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    save_device_profile(app);
    if (GTK_WIDGET(editable) == app->output_entry) {
        refresh_recording_library(app);
    } else if (GTK_WIDGET(editable) == app->audio_entry) {
        app->audio_capture_disabled = FALSE;
        show_feedback(app, "Capture audio will be tested again when the pipeline reopens");
    }
}

static void
on_record_options_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    gchar *description;
    (void)object;
    (void)pspec;

    if (app->quality_dropdown != NULL) {
        guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->quality_dropdown));
        if (selected < RECORD_PRESET_COUNT) app->record_preset = (int)selected;
    }
    if (app->codec_dropdown != NULL) {
        guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->codec_dropdown));
        if (selected < RECORD_CODEC_COUNT) app->record_codec = (int)selected;
    }
    if (app->container_dropdown != NULL) {
        guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->container_dropdown));
        if (selected < RECORD_CONTAINER_COUNT) app->record_container = (int)selected;
    }

    description = record_preset_description(app);
    show_feedback(app, "Recording preset: %s", description);
    g_free(description);
    save_device_profile(app);
}

static void
on_vrr_monitor_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    (void)object;
    (void)pspec;
    app->vrr_monitor_enabled = gtk_switch_get_active(GTK_SWITCH(app->vrr_monitor_switch));
    app->vrr_last_pts_ns = (gint64)GST_CLOCK_TIME_NONE;
    app->vrr_min_interval_ns = G_MAXINT64;
    app->vrr_max_interval_ns = 0;
    app->vrr_sample_count = 0;
    app->vrr_variable_detected = FALSE;
    update_vrr_status(app);
    save_device_profile(app);
}

static void
launch_path(const gchar *path)
{
    GError *error = NULL;
    gchar *uri = g_filename_to_uri(path, NULL, &error);
    if (uri != NULL) {
        g_app_info_launch_default_for_uri(uri, NULL, &error);
        g_free(uri);
    }
    if (error != NULL) {
        g_printerr("Linux Capture Studio: could not open %s: %s\n", path, error->message);
        g_clear_error(&error);
    }
}

static void
on_library_play_clicked(GtkButton *button, gpointer user_data)
{
    const gchar *path = g_object_get_data(G_OBJECT(button), "recording-path");
    (void)user_data;
    if (path != NULL) launch_path(path);
}

static void
on_library_trash_clicked(GtkButton *button, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    const gchar *path = g_object_get_data(G_OBJECT(button), "recording-path");
    GFile *file;
    GError *error = NULL;

    if (path == NULL) return;
    file = g_file_new_for_path(path);
    if (!g_file_trash(file, NULL, &error)) {
        show_feedback(app, "Could not move recording to Trash");
        g_clear_error(&error);
    } else {
        show_feedback(app, "Recording moved to Trash");
        refresh_recording_library(app);
    }
    g_object_unref(file);
}

static void
on_library_open_folder_clicked(GtkButton *button, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    const gchar *requested = app->output_entry != NULL
        ? gtk_editable_get_text(GTK_EDITABLE(app->output_entry)) : NULL;
    gchar *dir = (requested != NULL && requested[0] != '\0')
        ? g_strdup(requested) : default_recording_directory();
    (void)button;
    launch_path(dir);
    g_free(dir);
}

static void
on_library_refresh_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    refresh_recording_library(user_data);
}

static gint
string_ptr_compare(gconstpointer a, gconstpointer b)
{
    const gchar * const *sa = a;
    const gchar * const *sb = b;
    return g_strcmp0(*sa, *sb);
}

static void
refresh_recording_library(OpenCentralApp *app)
{
    const gchar *requested;
    gchar *directory;
    GDir *dir;
    const gchar *name;
    GPtrArray *paths;
    guint count = 0;
    guint64 total_size = 0;

    if (app->library_list == NULL) return;
    while (gtk_widget_get_first_child(app->library_list) != NULL) {
        gtk_list_box_remove(GTK_LIST_BOX(app->library_list), gtk_widget_get_first_child(app->library_list));
    }

    requested = app->output_entry != NULL
        ? gtk_editable_get_text(GTK_EDITABLE(app->output_entry)) : NULL;
    directory = (requested != NULL && requested[0] != '\0')
        ? g_strdup(requested) : default_recording_directory();
    paths = g_ptr_array_new_with_free_func(g_free);
    dir = g_dir_open(directory, 0, NULL);
    if (dir != NULL) {
        while ((name = g_dir_read_name(dir)) != NULL) {
            if (g_str_has_suffix(name, ".mkv") || g_str_has_suffix(name, ".mp4")) {
                g_ptr_array_add(paths, g_build_filename(directory, name, NULL));
            }
        }
        g_dir_close(dir);
    }
    g_ptr_array_sort(paths, string_ptr_compare);

    for (gint i = (gint)paths->len - 1; i >= 0; i--) {
        const gchar *path = g_ptr_array_index(paths, i);
        GFile *file = g_file_new_for_path(path);
        GFileInfo *info = g_file_query_info(file,
            G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME "," G_FILE_ATTRIBUTE_STANDARD_SIZE "," G_FILE_ATTRIBUTE_TIME_MODIFIED,
            G_FILE_QUERY_INFO_NONE, NULL, NULL);
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget *title;
        GtkWidget *meta;
        GtkWidget *play = gtk_button_new_from_icon_name("media-playback-start-symbolic");
        GtkWidget *trash = gtk_button_new_from_icon_name("user-trash-symbolic");
        gchar *size_text = NULL;
        gchar *meta_text = NULL;
        const gchar *display_name = g_path_get_basename(path);

        if (info != NULL) {
            goffset size = g_file_info_get_size(info);
            guint64 modified_seconds = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
            GDateTime *modified = modified_seconds > 0 ? g_date_time_new_from_unix_local((gint64)modified_seconds) : NULL;
            gchar *date_text = modified != NULL ? g_date_time_format(modified, "%Y-%m-%d  %H:%M") : g_strdup("");
            size_text = g_format_size_full(size, G_FORMAT_SIZE_IEC_UNITS);
            meta_text = g_strdup_printf("%s  •  %s", date_text, size_text);
            total_size += (guint64)size;
            g_free(date_text);
            if (modified != NULL) g_date_time_unref(modified);
        } else {
            meta_text = g_strdup("Recording");
        }

        title = gtk_label_new(display_name);
        gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_MIDDLE);
        gtk_widget_add_css_class(title, "library-title");
        meta = gtk_label_new(meta_text);
        gtk_label_set_xalign(GTK_LABEL(meta), 0.0f);
        gtk_widget_add_css_class(meta, "library-meta");
        gtk_box_append(GTK_BOX(labels), title);
        gtk_box_append(GTK_BOX(labels), meta);
        gtk_widget_set_hexpand(labels, TRUE);
        gtk_box_append(GTK_BOX(box), labels);
        gtk_widget_add_css_class(play, "library-action");
        gtk_widget_add_css_class(trash, "library-action");
        g_object_set_data_full(G_OBJECT(play), "recording-path", g_strdup(path), g_free);
        g_object_set_data_full(G_OBJECT(trash), "recording-path", g_strdup(path), g_free);
        g_signal_connect(play, "clicked", G_CALLBACK(on_library_play_clicked), app);
        g_signal_connect(trash, "clicked", G_CALLBACK(on_library_trash_clicked), app);
        gtk_box_append(GTK_BOX(box), play);
        gtk_box_append(GTK_BOX(box), trash);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
        gtk_list_box_append(GTK_LIST_BOX(app->library_list), row);

        count++;
        g_free((gchar *)display_name);
        g_free(meta_text);
        g_free(size_text);
        if (info != NULL) g_object_unref(info);
        g_object_unref(file);
    }

    if (app->library_summary_label != NULL) {
        gchar *total_text = g_format_size_full(total_size, G_FORMAT_SIZE_IEC_UNITS);
        gchar *summary = g_strdup_printf("%u recording%s  •  %s", count, count == 1 ? "" : "s", total_text);
        gtk_label_set_text(GTK_LABEL(app->library_summary_label), summary);
        g_free(summary);
        g_free(total_text);
    }
    if (count == 0) {
        GtkWidget *empty = gtk_label_new("No recordings in this folder yet");
        gtk_widget_add_css_class(empty, "library-empty");
        gtk_list_box_append(GTK_LIST_BOX(app->library_list), empty);
    }

    g_ptr_array_unref(paths);
    g_free(directory);
}

static GtkWidget *
make_labeled_entry(const gchar *label_text, GtkWidget **entry_out, const gchar *initial)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *label = gtk_label_new(label_text);
    GtkWidget *entry = gtk_entry_new();

    gtk_widget_set_size_request(label, 92, -1);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_editable_set_text(GTK_EDITABLE(entry), initial != NULL ? initial : "");
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_box_append(GTK_BOX(row), label);
    gtk_box_append(GTK_BOX(row), entry);
    *entry_out = entry;
    return row;
}

static GtkWidget *
make_selector_group(const gchar *caption, GtkWidget *control)
{
    GtkWidget *group = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *label = gtk_label_new(caption);

    gtk_widget_add_css_class(group, "selector-group");
    gtk_widget_add_css_class(label, "selector-caption");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(control, "premium-dropdown");
    gtk_box_append(GTK_BOX(group), label);
    gtk_box_append(GTK_BOX(group), control);
    return group;
}

static GtkWidget *
make_settings_heading(const gchar *title, const gchar *subtitle)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *title_label = gtk_label_new(title);
    GtkWidget *subtitle_label = gtk_label_new(subtitle);

    gtk_widget_add_css_class(title_label, "settings-title");
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
    gtk_widget_add_css_class(subtitle_label, "settings-subtitle");
    gtk_label_set_xalign(GTK_LABEL(subtitle_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(subtitle_label), TRUE);
    gtk_box_append(GTK_BOX(box), title_label);
    gtk_box_append(GTK_BOX(box), subtitle_label);
    return box;
}

static void
update_mode_badge(OpenCentralApp *app)
{
    const FormatOption *format;
    const ResolutionOption *resolution;
    const gchar *hdr_text;
    gchar *text;

    if (app->mode_badge_label == NULL) {
        return;
    }

    format = current_format(app);
    resolution = current_resolution(app);
    if (format == NULL || resolution == NULL || app->fps <= 0) {
        gtk_label_set_text(GTK_LABEL(app->mode_badge_label), "Detecting signal…");
        return;
    }

    hdr_text = !app->hdr_enabled ? "HDR OFF" : (app->hdr_mode >= 0 && app->hdr_mode < HDR_MODE_COUNT
        ? HDR_MODE_NAMES[app->hdr_mode]
        : "Auto");
    {
        CaptureFormat source_format;
        int source_width;
        int source_height;
        int source_fps;

        selected_source_mode(
            app,
            &source_format,
            &source_width,
            &source_height,
            &source_fps
        );

        if (source_format != format->format ||
            source_width != resolution->width ||
            source_height != resolution->height ||
            source_fps != app->fps) {
            text = g_strdup_printf(
                "SOURCE %s %dx%d %d FPS  →  OUTPUT %s %dx%d %d FPS  •  %s",
                capture_format_name(source_format),
                source_width,
                source_height,
                source_fps,
                format->name,
                resolution->width,
                resolution->height,
                app->fps,
                hdr_text
            );
        } else {
            text = g_strdup_printf(
                "%s  •  %dx%d  •  %d FPS  •  %s",
                format->name,
                resolution->width,
                resolution->height,
                app->fps,
                hdr_text
            );
        }
    }
    if (app->vrr_variable_detected) {
        gchar *with_vrr = g_strconcat(text, "  •  VRR", NULL);
        g_free(text);
        text = with_vrr;
    }
    gtk_label_set_text(GTK_LABEL(app->mode_badge_label), text);
    g_free(text);
}

static GtkWidget *
make_info_row(const gchar *label_text, const gchar *value_text)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *label = gtk_label_new(label_text);
    GtkWidget *value = gtk_label_new(value_text != NULL ? value_text : "Unavailable");

    gtk_widget_add_css_class(row, "setting-row");
    gtk_widget_set_size_request(label, 132, -1);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, "info-label");

    gtk_label_set_xalign(GTK_LABEL(value), 1.0f);
    gtk_label_set_ellipsize(GTK_LABEL(value), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(value, TRUE);
    gtk_widget_add_css_class(value, "info-value");

    gtk_box_append(GTK_BOX(row), label);
    gtk_box_append(GTK_BOX(row), value);
    return row;
}

static void
install_ui_css(void)
{
    static const char css[] =
        "window { background: #090b10; color: #eef1f7; }"
        ".app-root { background: #090b10; }"
        ".premium-header {"
        "  background-image: linear-gradient(to bottom right, #151a24, #0f131b);"
        "  border: 1px solid rgba(255,255,255,0.08);"
        "  border-radius: 16px; padding: 12px;"
        "  box-shadow: 0 10px 28px rgba(0,0,0,0.35);"
        "}"
        ".brand-tile { background: #6d4cff; border-radius: 12px; padding: 9px; }"
        ".brand-title { font-size: 20px; font-weight: 800; color: #ffffff; }"
        ".brand-subtitle { font-size: 12px; color: #9ea7b8; }"
        ".capture-strip {"
        "  background: rgba(7,9,14,0.72); border: 1px solid rgba(255,255,255,0.06);"
        "  border-radius: 12px; padding: 8px 10px;"
        "}"
        ".capture-caption { font-size: 11px; font-weight: 700; color: #8f98aa; }"
        ".selector-group { min-width: 120px; }"
        ".selector-caption { font-size: 10px; font-weight: 700; color: #8f98aa; }"
        ".premium-dropdown > button {"
        "  background: #181e29; color: #f6f7fb; border: 1px solid rgba(255,255,255,0.09);"
        "  border-radius: 9px; min-height: 34px; padding: 4px 10px;"
        "}"
        ".premium-dropdown > button:hover { background: #202735; border-color: rgba(143,116,255,0.42); }"
        ".record-button {"
        "  background: #ef3f58; color: #ffffff; border: 1px solid #ff7286;"
        "  border-radius: 11px; min-height: 42px; padding: 0 18px; font-weight: 800;"
        "  box-shadow: 0 6px 18px rgba(239,63,88,0.28);"
        "}"
        ".record-button:hover { background: #ff5068; }"
        ".record-button.recording-active {"
        "  background: #b91f3b; border-color: #ff5068;"
        "  box-shadow: 0 6px 20px rgba(239,63,88,0.40);"
        "}"
        ".hdr-toggle {"
        "  border-radius: 11px; min-height: 42px; padding: 0 14px; font-weight: 800;"
        "  border: 1px solid rgba(255,255,255,0.10);"
        "}"
        ".hdr-toggle-on {"
        "  background: #1f8f57; color: #ffffff; border-color: #39d98a;"
        "  box-shadow: 0 6px 18px rgba(33,179,111,0.28);"
        "}"
        ".hdr-toggle-off {"
        "  background: #2a313d; color: #c5ccda; border-color: rgba(255,255,255,0.10);"
        "}"
        ".icon-button {"
        "  background: #171c26; color: #e9edf5; border: 1px solid rgba(255,255,255,0.08);"
        "  border-radius: 11px; min-width: 42px; min-height: 42px;"
        "}"
        ".icon-button:hover { background: #222a38; border-color: rgba(143,116,255,0.42); }"
        ".connection-pill {"
        "  background: rgba(34,184,115,0.12); color: #9ff0c3;"
        "  border: 1px solid rgba(62,218,146,0.28); border-radius: 999px;"
        "  padding: 6px 11px; font-size: 11px; font-weight: 800;"
        "}"
        ".connection-pill.disconnected { background: rgba(239,63,88,0.13); color: #ffb7c3; border-color: rgba(239,63,88,0.38); }"
        ".session-pill {"
        "  background: rgba(42,49,61,0.72); color: #cfd6e5;"
        "  border: 1px solid rgba(255,255,255,0.10); border-radius: 999px;"
        "  padding: 6px 13px; font-size: 11px; font-weight: 800;"
        "}"
        ".session-pill.hdr-session { background: rgba(31,143,87,0.17); color: #aef2c7; border-color: rgba(57,217,138,0.40); }"
        ".session-pill.offline-session { background: rgba(239,63,88,0.13); color: #ffb7c3; border-color: rgba(239,63,88,0.38); }"
        ".stream-button { background: #1769aa; color: #ffffff; border: 1px solid #4aa3df; border-radius: 11px; min-height: 42px; padding: 0 14px; font-weight: 750; }"
        ".stream-button:hover { background: #2182c7; border-color: #75c4f3; }"
        ".stream-button.streaming-active { background: #d1354d; border-color: #ff6b80; box-shadow: 0 6px 18px rgba(209,53,77,0.28); }"
        ".apply-button { background: #252c38; color: #aeb7c8; border: 1px solid rgba(255,255,255,0.10); border-radius: 11px; min-height: 42px; padding: 0 15px; font-weight: 800; }"
        ".apply-button.apply-pending { background: #6d4cff; color: #ffffff; border-color: #9b83ff; box-shadow: 0 6px 20px rgba(109,76,255,0.34); }"
        ".apply-button.apply-pending:hover { background: #8063ff; }"
        ".apply-button.apply-running { background: #a66b16; color: #fff1d0; border-color: #e6a944; }"
        ".apply-button.apply-done { background: rgba(31,143,87,0.17); color: #9fecc0; border-color: rgba(57,217,138,0.34); }"
        ".apply-blackout { background: #000000; }"
        ".apply-blackout-label { color: #ffffff; font-size: 18px; font-weight: 800; background: rgba(18,22,30,0.86); border: 1px solid rgba(255,255,255,0.18); border-radius: 12px; padding: 12px 18px; }"
        ".pending-label { color: #baaaff; font-size: 10px; font-weight: 850; letter-spacing: 0.05em; }"
        "/* hidden center status pill retained internally */ .stats-pill {"
        "  background: rgba(109,76,255,0.16); color: #d8ceff;"
        "  border: 1px solid rgba(139,112,255,0.35); border-radius: 999px;"
        "  padding: 7px 14px; font-weight: 700;"
        "}"
        ".stats-pill.recording {"
        "  background: rgba(239,63,88,0.18); color: #ffd8df;"
        "  border-color: rgba(255,101,124,0.48);"
        "}"
        ".video-card {"
        "  background: #000000; border: 1px solid rgba(255,255,255,0.09);"
        "  border-radius: 20px; padding: 1px;"
        "  box-shadow: 0 16px 44px rgba(0,0,0,0.48);"
        "}"
        ".fullscreen-stage { background: #000000; border: none; border-radius: 0; padding: 0; box-shadow: none; }"
        ".stage-badge {"
        "  background: rgba(8,11,17,0.72); color: #f4f6fb;"
        "  border: 1px solid rgba(255,255,255,0.11); border-radius: 999px;"
        "  padding: 6px 11px; font-size: 11px; font-weight: 700;"
        "}"
        ".mode-badge { color: #d7ddff; }"
        ".device-badge { color: #aef2c7; }"
        ".audio-badge { color: #aab4c5; }"
        ".audio-badge.audio-live { color: #8ff0bd; border-color: rgba(57,217,138,0.30); }"
        ".audio-badge.audio-offline { color: #ffb7c3; border-color: rgba(239,63,88,0.34); }"
        ".action-feedback {"
        "  background: rgba(12,16,24,0.92); color: #ffffff;"
        "  border: 1px solid rgba(132,103,255,0.60); border-radius: 999px;"
        "  padding: 9px 16px; font-weight: 700;"
        "  box-shadow: 0 10px 26px rgba(0,0,0,0.44);"
        "}"
        ".status-bar {"
        "  background: #10141c; border: 1px solid rgba(255,255,255,0.07);"
        "  border-radius: 12px; padding: 9px 12px;"
        "}"
        ".status-text { color: #c8cfdb; }"
        ".footer-hint { color: #737d90; font-size: 11px; }"
        ".premium-popover > contents {"
        "  background: #10141c; border: 1px solid rgba(255,255,255,0.10);"
        "  border-radius: 16px; box-shadow: 0 18px 48px rgba(0,0,0,0.55);"
        "}"
        "window.settings-window { background: #0c1017; }"
        ".settings-shell { padding: 15px; background: #0c1017; }"
        ".settings-header { font-size: 18px; font-weight: 800; color: #ffffff; }"
        ".settings-header-subtitle { color: #8e98aa; font-size: 11px; }"
        ".settings-close-button { background: transparent; color: #aeb6c6; border-radius: 9px; min-width: 34px; min-height: 34px; }"
        ".settings-close-button:hover { background: rgba(239,63,88,0.14); color: #ff9cac; }"
        ".settings-tabs button {"
        "  background: transparent; color: #9ba5b7; border-radius: 9px; padding: 7px 12px;"
        "}"
        ".settings-tabs button:checked { background: #6d4cff; color: #ffffff; }"
        ".settings-page {"
        "  background: #151a23; border: 1px solid rgba(255,255,255,0.07);"
        "  border-radius: 13px; padding: 15px;"
        "}"
        ".settings-title { font-size: 16px; font-weight: 800; color: #ffffff; }"
        ".settings-subtitle { color: #8f99aa; font-size: 11px; margin-bottom: 8px; }"
        ".settings-subheading { color: #ffffff; font-size: 14px; font-weight: 800; }"
        ".hdr-calibration-box { background: #111620; border: 1px solid rgba(123,98,255,0.24); border-radius: 12px; padding: 12px; }"
        ".hdr-wizard-window { background: #080b10; }"
        ".wizard-step { color: #8fa7ff; font-weight: 800; }"
        ".wizard-title { color: #f6f8ff; font-size: 26px; font-weight: 900; }"
        ".wizard-instruction { color: #c4cada; font-size: 15px; }"
        ".wizard-pattern { background: #000; border: 1px solid rgba(255,255,255,0.16); border-radius: 14px; }"
        ".wizard-value { color: #ffffff; font-size: 18px; font-weight: 800; }"
        ".wizard-adjust-button { min-width: 48px; min-height: 42px; font-size: 22px; font-weight: 900; }"
        ".setting-row { background: #111620; border: 1px solid rgba(255,255,255,0.035); border-radius: 10px; padding: 9px; }"
        ".info-label { color: #8f99aa; font-weight: 700; }"
        ".info-value { color: #f2f4f8; font-weight: 600; }"
        ".shortcut-note { color: #7f899b; font-size: 11px; margin-top: 6px; }"
        ".vrr-status { color: #b9c8e8; font-size: 11px; }"
        ".secondary-button { background: #202735; color: #e8ecf5; border: 1px solid rgba(255,255,255,0.10); border-radius: 9px; padding: 7px 12px; }"
        ".recording-library row { background: #111620; border: 1px solid rgba(255,255,255,0.045); border-radius: 10px; margin-bottom: 6px; padding: 9px; }"
        ".library-title { color: #f3f5fa; font-weight: 700; }"
        ".library-meta, .library-summary { color: #8f99aa; font-size: 11px; }"
        ".library-empty { color: #7f899b; padding: 30px; }"
        ".library-action { background: #1b2230; color: #dce2ee; border-radius: 8px; min-width: 36px; min-height: 36px; }"
        ".setting-row:hover { background: #141b27; border-color: rgba(125,101,255,0.16); }"
        ".locked-control > button { background: rgba(31,143,87,0.12); border-color: rgba(57,217,138,0.32); color: #b9f5d1; }"
        ".hdr-toggle.hdr-locked { background: #176b45; border-color: #39d98a; color: #ffffff; }"
        "switch { min-width: 44px; min-height: 24px; }"
        "switch:checked { background: #26a269; }"
        "";
    GtkCssProvider *provider = gtk_css_provider_new();
    GdkDisplay *display = gdk_display_get_default();

    gtk_css_provider_load_from_string(provider, css);
    if (display != NULL) {
        gtk_style_context_add_provider_for_display(
            display,
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
        );
    }
    g_object_unref(provider);
}

static void
on_activate(GtkApplication *gtk_app, gpointer user_data)
{
    OpenCentralApp *app = user_data;
    GtkWidget *header_row;
    GtkWidget *brand_box;
    GtkWidget *brand_icon_tile;
    GtkWidget *brand_icon;
    GtkWidget *brand_text;
    GtkWidget *brand_title;
    GtkWidget *brand_subtitle;
    GtkWidget *header_actions;
    GtkWidget *header_center;
    GtkWidget *capture_row;
    GtkWidget *capture_spacer;
    GtkWidget *video_overlay;
    GtkWidget *blackout_center;
    GtkEventController *key_controller;
    GtkGesture *picture_gesture;
    GtkStringList *session_names;
    GtkStringList *format_names;
    GtkStringList *resolution_names;
    GtkStringList *fps_names;
    GtkStringList *hdr_names;
    GtkStringList *quality_names;
    GtkStringList *codec_names;
    GtkStringList *container_names;
    GtkStringList *stream_service_names;
    GtkStringList *stream_bitrate_names;
    GtkWidget *settings_content;
    GtkWidget *settings_header;
    GtkWidget *settings_header_title;
    GtkWidget *settings_header_subtitle;
    GtkWidget *settings_stack;
    GtkWidget *settings_switcher;
    GtkWidget *device_page;
    GtkWidget *recording_page;
    GtkWidget *streaming_page;
    GtkWidget *image_page;
    GtkWidget *audio_page;
    GtkWidget *library_page;
    gchar *default_output;

    app->gtk_app = gtk_app;
    install_ui_css();
    app->window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(
        GTK_WINDOW(app->window),
        app->native_p010_session
            ? "Linux Capture Studio 0.6.30 RC12 — Native HDR60"
            : "Linux Capture Studio 0.6.30 RC12"
    );
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1440, 900);

    app->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(app->root, "app-root");
    gtk_widget_set_margin_top(app->root, 14);
    gtk_widget_set_margin_bottom(app->root, 14);
    gtk_widget_set_margin_start(app->root, 14);
    gtk_widget_set_margin_end(app->root, 14);
    gtk_window_set_child(GTK_WINDOW(app->window), app->root);

    app->toolbar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(app->toolbar, "premium-header");
    gtk_box_append(GTK_BOX(app->root), app->toolbar);

    header_row = gtk_center_box_new();
    gtk_box_append(GTK_BOX(app->toolbar), header_row);

    brand_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_valign(brand_box, GTK_ALIGN_CENTER);
    brand_icon_tile = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(brand_icon_tile, "brand-tile");
    brand_icon = gtk_image_new_from_icon_name("camera-video-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(brand_icon), 22);
    gtk_box_append(GTK_BOX(brand_icon_tile), brand_icon);
    gtk_box_append(GTK_BOX(brand_box), brand_icon_tile);

    brand_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    brand_title = gtk_label_new("Linux Capture Studio");
    gtk_widget_add_css_class(brand_title, "brand-title");
    gtk_label_set_xalign(GTK_LABEL(brand_title), 0.0f);
    if (app->native_p010_session) {
        gchar *native_subtitle = g_strdup_printf(
            "%s • NATIVE-RESOLUTION HDR",
            app->capture_device_name != NULL ? app->capture_device_name : "Capture device"
        );
        brand_subtitle = gtk_label_new(native_subtitle);
        g_free(native_subtitle);
    } else {
        brand_subtitle = gtk_label_new(app->capture_device_name != NULL ? app->capture_device_name : "Capture device");
    }
    app->brand_subtitle_label = brand_subtitle;
    gtk_widget_add_css_class(brand_subtitle, "brand-subtitle");
    gtk_label_set_xalign(GTK_LABEL(brand_subtitle), 0.0f);
    gtk_box_append(GTK_BOX(brand_text), brand_title);
    gtk_box_append(GTK_BOX(brand_text), brand_subtitle);
    gtk_box_append(GTK_BOX(brand_box), brand_text);
    gtk_center_box_set_start_widget(GTK_CENTER_BOX(header_row), brand_box);

    app->stats_label = gtk_label_new("");
    gtk_widget_add_css_class(app->stats_label, "stats-pill");
    gtk_widget_set_visible(app->stats_label, FALSE);

    header_center = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(header_center, GTK_ALIGN_CENTER);
    app->session_label = gtk_label_new(app->native_p010_session ? "HDR60  •  P010 LOCKED" : "LOW LATENCY");
    gtk_widget_add_css_class(app->session_label, "session-pill");
    gtk_widget_set_tooltip_text(app->session_label, "Active capture path");
    gtk_box_append(GTK_BOX(header_center), app->session_label);
    gtk_center_box_set_center_widget(GTK_CENTER_BOX(header_row), header_center);

    header_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    app->connection_label = gtk_label_new("●  CONNECTED");
    gtk_widget_add_css_class(app->connection_label, "connection-pill");
    gtk_widget_set_valign(app->connection_label, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(app->connection_label, app->capture_device_name != NULL ? app->capture_device_name : "Capture device connected");
    gtk_box_append(GTK_BOX(header_actions), app->connection_label);

    app->settings_button = gtk_button_new_from_icon_name("preferences-system-symbolic");
    gtk_widget_add_css_class(app->settings_button, "icon-button");
    gtk_widget_set_tooltip_text(app->settings_button, "Capture settings");
    gtk_box_append(GTK_BOX(header_actions), app->settings_button);

    app->fullscreen_button = gtk_button_new_from_icon_name("view-fullscreen-symbolic");
    gtk_widget_add_css_class(app->fullscreen_button, "icon-button");
    gtk_widget_set_tooltip_text(app->fullscreen_button, "Fullscreen (F11)");
    gtk_box_append(GTK_BOX(header_actions), app->fullscreen_button);
    gtk_center_box_set_end_widget(GTK_CENTER_BOX(header_row), header_actions);

    capture_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(capture_row, "capture-strip");
    gtk_widget_set_valign(capture_row, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(app->toolbar), capture_row);

    session_names = gtk_string_list_new(NULL);
    for (int i = 0; i < SESSION_MODE_COUNT; i++) {
        gtk_string_list_append(session_names, SESSION_MODE_NAMES[i]);
    }
    app->session_dropdown = gtk_drop_down_new(G_LIST_MODEL(session_names), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->session_dropdown), app->session_mode);
    gtk_widget_set_size_request(app->session_dropdown, 210, -1);
    gtk_widget_set_tooltip_text(
        app->session_dropdown,
        "One integrated launcher manages Smooth SDR and all HDR60 output sessions"
    );
    gtk_box_append(GTK_BOX(capture_row), make_selector_group("SESSION", app->session_dropdown));

    format_names = gtk_string_list_new(NULL);
    for (int i = 0; i < FORMAT_COUNT; i++) {
        gtk_string_list_append(format_names, FORMATS[i].name);
    }
    app->format_dropdown = gtk_drop_down_new(G_LIST_MODEL(format_names), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->format_dropdown), app->format_index);
    gtk_widget_set_size_request(app->format_dropdown, 118, -1);
    gtk_box_append(GTK_BOX(capture_row), make_selector_group("FORMAT", app->format_dropdown));

    resolution_names = gtk_string_list_new(NULL);
    for (int i = 0; i < RESOLUTION_COUNT; i++) {
        gtk_string_list_append(resolution_names, RESOLUTIONS[i].name);
    }
    app->resolution_dropdown = gtk_drop_down_new(G_LIST_MODEL(resolution_names), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->resolution_dropdown), app->resolution_index);
    gtk_widget_set_size_request(app->resolution_dropdown, 172, -1);
    gtk_box_append(GTK_BOX(capture_row), make_selector_group("RESOLUTION", app->resolution_dropdown));

    fps_names = gtk_string_list_new(NULL);
    gtk_string_list_append(fps_names, "Loading…");
    app->fps_dropdown = gtk_drop_down_new(G_LIST_MODEL(fps_names), NULL);
    gtk_widget_set_size_request(app->fps_dropdown, 112, -1);
    gtk_box_append(GTK_BOX(capture_row), make_selector_group("FRAME RATE", app->fps_dropdown));

    capture_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(capture_spacer, TRUE);
    gtk_box_append(GTK_BOX(capture_row), capture_spacer);

    app->pending_label = gtk_label_new("CHANGES PENDING");
    gtk_widget_add_css_class(app->pending_label, "pending-label");
    gtk_widget_set_visible(app->pending_label, FALSE);
    gtk_box_append(GTK_BOX(capture_row), app->pending_label);

    app->apply_button = gtk_button_new_with_label("Applied ✓");
    gtk_widget_add_css_class(app->apply_button, "apply-button");
    gtk_widget_add_css_class(app->apply_button, "apply-done");
    gtk_widget_set_tooltip_text(app->apply_button, "Apply the selected capture session, format, resolution, frame rate, and HDR mode");
    gtk_widget_set_sensitive(app->apply_button, FALSE);
    gtk_box_append(GTK_BOX(capture_row), app->apply_button);

    gtk_widget_set_visible(app->stats_label, FALSE);
    gtk_box_append(GTK_BOX(capture_row), app->stats_label);

    app->stream_button = gtk_button_new_with_label("Streaming — Coming Soon");
    gtk_widget_add_css_class(app->stream_button, "stream-button");
    gtk_widget_set_tooltip_text(app->stream_button, "YouTube and Twitch streaming are coming soon");
    gtk_widget_set_sensitive(app->stream_button, FALSE);
    gtk_box_append(GTK_BOX(capture_row), app->stream_button);

    app->hdr_toggle_button = gtk_toggle_button_new_with_label("HDR OFF");
    gtk_widget_add_css_class(app->hdr_toggle_button, "hdr-toggle");
    gtk_widget_set_tooltip_text(app->hdr_toggle_button, "Enable or disable HDR recording/output mode");
    gtk_box_append(GTK_BOX(capture_row), app->hdr_toggle_button);

    app->record_button = gtk_button_new_with_label("●  Record");
    gtk_widget_add_css_class(app->record_button, "record-button");
    gtk_widget_set_valign(app->record_button, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(capture_row), app->record_button);

    video_overlay = gtk_overlay_new();
    gtk_widget_set_hexpand(video_overlay, TRUE);
    gtk_widget_set_vexpand(video_overlay, TRUE);

    app->picture = gtk_picture_new();
    gtk_picture_set_can_shrink(GTK_PICTURE(app->picture), TRUE);
    gtk_picture_set_content_fit(GTK_PICTURE(app->picture), GTK_CONTENT_FIT_CONTAIN);
    gtk_widget_set_hexpand(app->picture, TRUE);
    gtk_widget_set_vexpand(app->picture, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(video_overlay), app->picture);

    app->device_badge_label = gtk_label_new(NULL);
    gtk_widget_add_css_class(app->device_badge_label, "stage-badge");
    gtk_widget_add_css_class(app->device_badge_label, "device-badge");
    gtk_widget_set_halign(app->device_badge_label, GTK_ALIGN_START);
    gtk_widget_set_valign(app->device_badge_label, GTK_ALIGN_START);
    gtk_widget_set_margin_top(app->device_badge_label, 14);
    gtk_widget_set_margin_start(app->device_badge_label, 14);
    {
        gchar *badge_text = g_strdup_printf(
            "●  %s  •  %s",
            app->capture_device_name != NULL ? app->capture_device_name : "Capture device connected",
            app->capture_driver_name != NULL ? app->capture_driver_name : "V4L2"
        );
        gtk_label_set_text(GTK_LABEL(app->device_badge_label), badge_text);
        g_free(badge_text);
    }
    gtk_overlay_add_overlay(GTK_OVERLAY(video_overlay), app->device_badge_label);

    app->audio_badge_label = gtk_label_new("○  AUDIO READY");
    gtk_widget_add_css_class(app->audio_badge_label, "stage-badge");
    gtk_widget_add_css_class(app->audio_badge_label, "audio-badge");
    gtk_widget_set_halign(app->audio_badge_label, GTK_ALIGN_START);
    gtk_widget_set_valign(app->audio_badge_label, GTK_ALIGN_END);
    gtk_widget_set_margin_bottom(app->audio_badge_label, 14);
    gtk_widget_set_margin_start(app->audio_badge_label, 14);
    gtk_overlay_add_overlay(GTK_OVERLAY(video_overlay), app->audio_badge_label);

    app->mode_badge_label = gtk_label_new("Detecting signal…");
    gtk_widget_add_css_class(app->mode_badge_label, "stage-badge");
    gtk_widget_add_css_class(app->mode_badge_label, "mode-badge");
    gtk_widget_set_halign(app->mode_badge_label, GTK_ALIGN_END);
    gtk_widget_set_valign(app->mode_badge_label, GTK_ALIGN_START);
    gtk_widget_set_margin_top(app->mode_badge_label, 14);
    gtk_widget_set_margin_end(app->mode_badge_label, 14);
    gtk_overlay_add_overlay(GTK_OVERLAY(video_overlay), app->mode_badge_label);

    app->feedback_label = gtk_label_new("");
    gtk_widget_add_css_class(app->feedback_label, "action-feedback");
    gtk_widget_set_halign(app->feedback_label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(app->feedback_label, GTK_ALIGN_END);
    gtk_widget_set_margin_bottom(app->feedback_label, 20);
    gtk_widget_set_visible(app->feedback_label, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(video_overlay), app->feedback_label);

    app->apply_blackout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(app->apply_blackout, "apply-blackout");
    gtk_widget_set_hexpand(app->apply_blackout, TRUE);
    gtk_widget_set_vexpand(app->apply_blackout, TRUE);
    gtk_widget_set_halign(app->apply_blackout, GTK_ALIGN_FILL);
    gtk_widget_set_valign(app->apply_blackout, GTK_ALIGN_FILL);
    gtk_widget_set_can_target(app->apply_blackout, FALSE);

    blackout_center = gtk_center_box_new();
    gtk_widget_set_hexpand(blackout_center, TRUE);
    gtk_widget_set_vexpand(blackout_center, TRUE);
    app->apply_blackout_label = gtk_label_new("Applying settings…");
    gtk_widget_add_css_class(app->apply_blackout_label, "apply-blackout-label");
    gtk_center_box_set_center_widget(GTK_CENTER_BOX(blackout_center), app->apply_blackout_label);
    gtk_box_append(GTK_BOX(app->apply_blackout), blackout_center);
    gtk_widget_set_visible(app->apply_blackout, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(video_overlay), app->apply_blackout);

    app->video_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(app->video_frame, "video-card");
    gtk_widget_set_hexpand(app->video_frame, TRUE);
    gtk_widget_set_vexpand(app->video_frame, TRUE);
    gtk_frame_set_child(GTK_FRAME(app->video_frame), video_overlay);
    gtk_box_append(GTK_BOX(app->root), app->video_frame);

    picture_gesture = gtk_gesture_click_new();
    gtk_widget_add_controller(app->picture, GTK_EVENT_CONTROLLER(picture_gesture));

    app->settings_window = gtk_window_new();
    gtk_window_set_application(GTK_WINDOW(app->settings_window), gtk_app);
    gtk_window_set_title(GTK_WINDOW(app->settings_window), "Linux Capture Studio — Settings");
    gtk_window_set_default_size(GTK_WINDOW(app->settings_window), 840, 680);
    gtk_window_set_resizable(GTK_WINDOW(app->settings_window), TRUE);
    gtk_window_set_modal(GTK_WINDOW(app->settings_window), FALSE);
    /* Keep Settings as an independent application window.  On KDE/Wayland a
     * transient child can feel pinned to the main preview; a normal top-level
     * window is freely movable and resizable. */
    gtk_window_set_hide_on_close(GTK_WINDOW(app->settings_window), TRUE);
    gtk_widget_add_css_class(app->settings_window, "settings-window");

    settings_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(settings_content, "settings-shell");
    gtk_widget_set_margin_top(settings_content, 14);
    gtk_widget_set_margin_bottom(settings_content, 14);
    gtk_widget_set_margin_start(settings_content, 14);
    gtk_widget_set_margin_end(settings_content, 14);
    gtk_window_set_child(GTK_WINDOW(app->settings_window), settings_content);

    settings_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    {
        GtkWidget *settings_header_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
        GtkWidget *settings_close = gtk_button_new_from_icon_name("window-close-symbolic");
        settings_header_title = gtk_label_new("Capture Settings");
        gtk_widget_add_css_class(settings_header_title, "settings-header");
        gtk_label_set_xalign(GTK_LABEL(settings_header_title), 0.0f);
        settings_header_subtitle = gtk_label_new("Profiles, recording, streaming, VRR, HDR calibration, image, and sound");
        gtk_widget_add_css_class(settings_header_subtitle, "settings-header-subtitle");
        gtk_label_set_xalign(GTK_LABEL(settings_header_subtitle), 0.0f);
        gtk_box_append(GTK_BOX(settings_header_text), settings_header_title);
        gtk_box_append(GTK_BOX(settings_header_text), settings_header_subtitle);
        gtk_widget_set_hexpand(settings_header_text, TRUE);
        gtk_box_append(GTK_BOX(settings_header), settings_header_text);
        gtk_widget_add_css_class(settings_close, "settings-close-button");
        gtk_widget_set_tooltip_text(settings_close, "Close settings");
        gtk_box_append(GTK_BOX(settings_header), settings_close);
        g_signal_connect(settings_close, "clicked", G_CALLBACK(on_settings_close_clicked), app);
    }
    gtk_box_append(GTK_BOX(settings_content), settings_header);

    settings_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(settings_stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(settings_stack), 180);
    gtk_widget_set_size_request(settings_stack, 760, 560);

    settings_switcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(settings_switcher), GTK_STACK(settings_stack));
    gtk_widget_add_css_class(settings_switcher, "settings-tabs");
    gtk_widget_set_halign(settings_switcher, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(settings_content), settings_switcher);
    gtk_box_append(GTK_BOX(settings_content), settings_stack);

    device_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(device_page, "settings-page");
    gtk_box_append(
        GTK_BOX(device_page),
        make_settings_heading(
            "Connected Device",
            "Linux Capture Studio reads formats and frame rates directly from the active V4L2 device."
        )
    );
    gtk_box_append(GTK_BOX(device_page), make_info_row("Device", app->capture_device_name));
    gtk_box_append(GTK_BOX(device_page), make_info_row("Driver", app->capture_driver_name));
    gtk_box_append(GTK_BOX(device_page), make_info_row("Video node", app->device));
    gtk_box_append(
        GTK_BOX(device_page),
        make_info_row(
            "Recovery",
            app->is_gc575 ? "Manual GC575 recovery available" : "Standard UVC recovery"
        )
    );
    gtk_box_append(
        GTK_BOX(device_page),
        make_info_row(
            "Capture path",
            app->native_p010_session ? "Native-resolution HDR60 (format/FPS locked)" : "Low-latency UVC preview"
        )
    );
    gtk_box_append(
        GTK_BOX(device_page),
        make_info_row(
            "Audio path",
            app->audio_capture_disabled ? "Unavailable" : "Independent PipeWire/Pulse monitor"
        )
    );
    {
        gchar *capability = vrr_hardware_capability(app);
        gtk_box_append(GTK_BOX(device_page), make_info_row("VRR passthrough", capability));
        g_free(capability);
    }
    {
        GtkWidget *vrr_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *vrr_label = gtk_label_new("VRR monitor");
        gtk_widget_add_css_class(vrr_row, "setting-row");
        gtk_widget_set_size_request(vrr_label, 132, -1);
        gtk_label_set_xalign(GTK_LABEL(vrr_label), 0.0f);
        gtk_box_append(GTK_BOX(vrr_row), vrr_label);
        app->vrr_monitor_switch = gtk_switch_new();
        gtk_switch_set_active(GTK_SWITCH(app->vrr_monitor_switch), app->vrr_monitor_enabled);
        gtk_widget_set_tooltip_text(app->vrr_monitor_switch, "Measure incoming frame timestamps without forcing a fixed frame cadence");
        gtk_box_append(GTK_BOX(vrr_row), app->vrr_monitor_switch);
        app->vrr_status_label = gtk_label_new("Measuring frame timing…");
        gtk_label_set_xalign(GTK_LABEL(app->vrr_status_label), 0.0f);
        gtk_widget_set_hexpand(app->vrr_status_label, TRUE);
        gtk_widget_add_css_class(app->vrr_status_label, "vrr-status");
        gtk_box_append(GTK_BOX(vrr_row), app->vrr_status_label);
        gtk_box_append(GTK_BOX(device_page), vrr_row);
    }
    {
        GtkWidget *check_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *check_button = gtk_button_new_with_label("Run Studio Check");
        gtk_widget_add_css_class(check_row, "setting-row");
        gtk_widget_add_css_class(check_button, "secondary-button");
        app->studio_check_label = gtk_label_new("Video, audio, recording, streaming, and HDR60 can be checked here.");
        gtk_label_set_xalign(GTK_LABEL(app->studio_check_label), 0.0f);
        gtk_label_set_wrap(GTK_LABEL(app->studio_check_label), TRUE);
        gtk_widget_set_hexpand(app->studio_check_label, TRUE);
        gtk_box_append(GTK_BOX(check_row), check_button);
        gtk_box_append(GTK_BOX(check_row), app->studio_check_label);
        gtk_box_append(GTK_BOX(device_page), check_row);
        g_signal_connect(check_button, "clicked", G_CALLBACK(on_studio_check_clicked), app);
    }
    {
        GtkWidget *shortcut = gtk_label_new("F11: Fullscreen   •   Esc: Exit fullscreen   •   Double-click preview: Toggle fullscreen");
        gtk_label_set_xalign(GTK_LABEL(shortcut), 0.0f);
        gtk_label_set_wrap(GTK_LABEL(shortcut), TRUE);
        gtk_widget_add_css_class(shortcut, "shortcut-note");
        gtk_box_append(GTK_BOX(device_page), shortcut);
    }
    gtk_stack_add_titled(GTK_STACK(settings_stack), device_page, "device", "Device");

    recording_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(recording_page, "settings-page");
    gtk_box_append(
        GTK_BOX(recording_page),
        make_settings_heading(
            "Recording",
            "Choose where files are stored. Capture quality follows the main studio controls."
        )
    );
    default_output = app->profile_output_dir != NULL
        ? g_strdup(app->profile_output_dir) : default_recording_directory();
    {
        GtkWidget *row = make_labeled_entry("Output folder", &app->output_entry, default_output);
        gtk_widget_add_css_class(row, "setting-row");
        gtk_box_append(GTK_BOX(recording_page), row);
    }
    g_free(default_output);

    quality_names = gtk_string_list_new(NULL);
    for (int i = 0; i < RECORD_PRESET_COUNT; i++) gtk_string_list_append(quality_names, RECORD_PRESET_NAMES[i]);
    app->quality_dropdown = gtk_drop_down_new(G_LIST_MODEL(quality_names), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->quality_dropdown), app->record_preset);
    gtk_widget_set_hexpand(app->quality_dropdown, TRUE);
    gtk_widget_add_css_class(app->quality_dropdown, "premium-dropdown");
    {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *label = gtk_label_new("Quality preset");
        gtk_widget_add_css_class(row, "setting-row");
        gtk_widget_set_size_request(label, 132, -1);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_box_append(GTK_BOX(row), label);
        gtk_box_append(GTK_BOX(row), app->quality_dropdown);
        gtk_box_append(GTK_BOX(recording_page), row);
    }

    codec_names = gtk_string_list_new(NULL);
    for (int i = 0; i < RECORD_CODEC_COUNT; i++) gtk_string_list_append(codec_names, RECORD_CODEC_NAMES[i]);
    app->codec_dropdown = gtk_drop_down_new(G_LIST_MODEL(codec_names), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->codec_dropdown), app->record_codec);
    gtk_widget_set_hexpand(app->codec_dropdown, TRUE);
    gtk_widget_add_css_class(app->codec_dropdown, "premium-dropdown");
    container_names = gtk_string_list_new(NULL);
    for (int i = 0; i < RECORD_CONTAINER_COUNT; i++) gtk_string_list_append(container_names, RECORD_CONTAINER_NAMES[i]);
    app->container_dropdown = gtk_drop_down_new(G_LIST_MODEL(container_names), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->container_dropdown), app->record_container);
    gtk_widget_set_hexpand(app->container_dropdown, TRUE);
    gtk_widget_add_css_class(app->container_dropdown, "premium-dropdown");
    {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *codec_label = gtk_label_new("Codec");
        GtkWidget *container_label = gtk_label_new("Container");
        gtk_widget_add_css_class(row, "setting-row");
        gtk_widget_set_size_request(codec_label, 72, -1);
        gtk_label_set_xalign(GTK_LABEL(codec_label), 0.0f);
        gtk_box_append(GTK_BOX(row), codec_label);
        gtk_box_append(GTK_BOX(row), app->codec_dropdown);
        gtk_widget_set_margin_start(container_label, 10);
        gtk_box_append(GTK_BOX(row), container_label);
        gtk_box_append(GTK_BOX(row), app->container_dropdown);
        gtk_box_append(GTK_BOX(recording_page), row);
    }

    {
        GtkWidget *help = gtk_label_new(
            "Lossless uses the proven FFV1/MKV path. H.264 presets use x264enc or OpenH264 when available and automatically fall back to lossless recording if the compressed pipeline cannot start."
        );
        gtk_label_set_wrap(GTK_LABEL(help), TRUE);
        gtk_label_set_xalign(GTK_LABEL(help), 0.0f);
        gtk_widget_add_css_class(help, "dim-label");
        gtk_box_append(GTK_BOX(recording_page), help);
    }
    gtk_stack_add_titled(GTK_STACK(settings_stack), recording_page, "recording", "Recording");

    streaming_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(streaming_page, "settings-page");
    gtk_box_append(
        GTK_BOX(streaming_page),
        make_settings_heading(
            "Live Streaming — Coming Soon",
            "YouTube and Twitch integration are visible for the final design, but disabled in this release. Recording remains fully available."
        )
    );
    stream_service_names = gtk_string_list_new(NULL);
    gtk_string_list_append(stream_service_names, STREAM_SERVICE_NAMES[STREAM_SERVICE_YOUTUBE]);
    gtk_string_list_append(stream_service_names, STREAM_SERVICE_NAMES[STREAM_SERVICE_TWITCH]);
    app->stream_service_dropdown = gtk_drop_down_new(G_LIST_MODEL(stream_service_names), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->stream_service_dropdown), app->stream_service);
    gtk_widget_add_css_class(app->stream_service_dropdown, "premium-dropdown");
    gtk_widget_set_hexpand(app->stream_service_dropdown, TRUE);
    gtk_widget_set_sensitive(app->stream_service_dropdown, FALSE);
    {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *label = gtk_label_new("Service");
        gtk_widget_add_css_class(row, "setting-row");
        gtk_widget_set_size_request(label, 132, -1);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_box_append(GTK_BOX(row), label);
        gtk_box_append(GTK_BOX(row), app->stream_service_dropdown);
        gtk_box_append(GTK_BOX(streaming_page), row);
    }
    {
        const gchar *saved_server = app->profile_stream_server != NULL ? app->profile_stream_server : "";
        GtkWidget *row = make_labeled_entry("Server URL", &app->stream_server_entry, saved_server);
        gtk_widget_add_css_class(row, "setting-row");
        gtk_entry_set_placeholder_text(GTK_ENTRY(app->stream_server_entry), "Coming Soon");
        gtk_widget_set_sensitive(app->stream_server_entry, FALSE);
        gtk_box_append(GTK_BOX(streaming_page), row);
    }
    {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *label = gtk_label_new("Stream key");
        gtk_widget_add_css_class(row, "setting-row");
        gtk_widget_set_size_request(label, 132, -1);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_box_append(GTK_BOX(row), label);
        app->stream_key_entry = gtk_password_entry_new();
        gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(app->stream_key_entry), TRUE);
        g_object_set(app->stream_key_entry, "placeholder-text", "Paste stream key", NULL);
        gtk_widget_set_hexpand(app->stream_key_entry, TRUE);
        gtk_widget_set_sensitive(app->stream_key_entry, FALSE);
        gtk_box_append(GTK_BOX(row), app->stream_key_entry);
        gtk_box_append(GTK_BOX(streaming_page), row);
    }
    stream_bitrate_names = gtk_string_list_new(NULL);
    for (int i = 0; i < STREAM_BITRATE_COUNT; i++) {
        gtk_string_list_append(stream_bitrate_names, STREAM_BITRATE_NAMES[i]);
    }
    app->stream_bitrate_dropdown = gtk_drop_down_new(G_LIST_MODEL(stream_bitrate_names), NULL);
    {
        guint bitrate_selected = 0;
        for (guint i = 0; i < STREAM_BITRATE_COUNT; i++) {
            if (STREAM_BITRATE_VALUES[i] == app->stream_bitrate_kbps) bitrate_selected = i;
        }
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->stream_bitrate_dropdown), bitrate_selected);
    }
    gtk_widget_add_css_class(app->stream_bitrate_dropdown, "premium-dropdown");
    gtk_widget_set_sensitive(app->stream_bitrate_dropdown, FALSE);
    {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *label = gtk_label_new("Video bitrate");
        gtk_widget_add_css_class(row, "setting-row");
        gtk_widget_set_size_request(label, 132, -1);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_box_append(GTK_BOX(row), label);
        gtk_box_append(GTK_BOX(row), app->stream_bitrate_dropdown);
        gtk_box_append(GTK_BOX(streaming_page), row);
    }
    {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *label = gtk_label_new("Status");
        gtk_widget_add_css_class(row, "setting-row");
        gtk_widget_set_size_request(label, 132, -1);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_box_append(GTK_BOX(row), label);
        app->stream_status_label = gtk_label_new("Coming Soon");
        gtk_label_set_xalign(GTK_LABEL(app->stream_status_label), 0.0f);
        gtk_widget_set_hexpand(app->stream_status_label, TRUE);
        gtk_box_append(GTK_BOX(row), app->stream_status_label);
        gtk_box_append(GTK_BOX(streaming_page), row);
    }
    {
        GtkWidget *note = gtk_label_new(
            "YouTube and Twitch streaming will be enabled in a later release after service authentication, error recovery, and long-duration testing are complete."
        );
        gtk_label_set_wrap(GTK_LABEL(note), TRUE);
        gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
        gtk_widget_add_css_class(note, "dim-label");
        gtk_box_append(GTK_BOX(streaming_page), note);
    }
    gtk_stack_add_titled(GTK_STACK(settings_stack), streaming_page, "streaming", "Streaming");

    image_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(image_page, "settings-page");
    gtk_box_append(
        GTK_BOX(image_page),
        make_settings_heading(
            "Image & HDR",
            "Preserve native HDR for recording and tune the selected device image controls."
        )
    );
    hdr_names = gtk_string_list_new(NULL);
    for (int i = 0; i < HDR_MODE_COUNT; i++) {
        gtk_string_list_append(hdr_names, HDR_MODE_NAMES[i]);
    }
    app->hdr_dropdown = gtk_drop_down_new(G_LIST_MODEL(hdr_names), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->hdr_dropdown), app->hdr_mode);
    gtk_widget_set_size_request(app->hdr_dropdown, 180, -1);
    {
        GtkWidget *hdr_mode_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *hdr_mode_label = gtk_label_new("HDR signal mode");
        gtk_widget_add_css_class(hdr_mode_row, "setting-row");
        gtk_widget_set_size_request(hdr_mode_label, 150, -1);
        gtk_label_set_xalign(GTK_LABEL(hdr_mode_label), 0.0f);
        gtk_box_append(GTK_BOX(hdr_mode_row), hdr_mode_label);
        gtk_widget_add_css_class(app->hdr_dropdown, "premium-dropdown");
        gtk_box_append(GTK_BOX(hdr_mode_row), app->hdr_dropdown);
        gtk_box_append(GTK_BOX(image_page), hdr_mode_row);
    }
    {
        GtkWidget *hdr_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *tone_label = gtk_label_new("HDR preview correction");
        gtk_widget_add_css_class(hdr_row, "setting-row");
        gtk_widget_set_size_request(tone_label, 150, -1);
        gtk_label_set_xalign(GTK_LABEL(tone_label), 0.0f);
        gtk_box_append(GTK_BOX(hdr_row), tone_label);

        app->hdr_tonemap_switch = gtk_switch_new();
        gtk_switch_set_active(GTK_SWITCH(app->hdr_tonemap_switch), TRUE);
        gtk_widget_set_tooltip_text(
            app->hdr_tonemap_switch,
            "Automatically map HDR to the desktop preview while preserving genuine HDR recording"
        );
        gtk_box_append(GTK_BOX(hdr_row), app->hdr_tonemap_switch);

        app->hdr_status_label = gtk_label_new("Signal metadata unavailable");
        gtk_widget_set_hexpand(app->hdr_status_label, TRUE);
        gtk_label_set_xalign(GTK_LABEL(app->hdr_status_label), 0.0f);
        gtk_label_set_wrap(GTK_LABEL(app->hdr_status_label), TRUE);
        gtk_box_append(GTK_BOX(hdr_row), app->hdr_status_label);
        gtk_box_append(GTK_BOX(image_page), hdr_row);
    }
    {
        GtkWidget *calibration_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        GtkWidget *title = gtk_label_new("Console-style HDR calibration");
        GtkWidget *note = gtk_label_new(
            "A guided three-screen calibration for maximum luminance, reference white, and black level. "
            "It changes only the software preview tone mapper; genuine HDR recording samples remain untouched."
        );
        GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *start_calibration = gtk_button_new_with_label("Start HDR Calibration");
        GtkWidget *reset_calibration = gtk_button_new_with_label("Reset to Defaults");

        gtk_widget_add_css_class(calibration_box, "hdr-calibration-box");
        gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
        gtk_widget_add_css_class(title, "settings-subheading");
        gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
        gtk_label_set_wrap(GTK_LABEL(note), TRUE);
        gtk_widget_add_css_class(note, "dim-label");
        gtk_box_append(GTK_BOX(calibration_box), title);
        gtk_box_append(GTK_BOX(calibration_box), note);

        gtk_widget_add_css_class(start_calibration, "apply-button");
        gtk_widget_add_css_class(reset_calibration, "secondary-button");
        gtk_box_append(GTK_BOX(actions), start_calibration);
        gtk_box_append(GTK_BOX(actions), reset_calibration);
        gtk_box_append(GTK_BOX(calibration_box), actions);

        {
            gchar *calibration_text = g_strdup_printf(
                "Current: %.0f-nit peak • %.0f-nit reference white • %.2f-nit black",
                app->hdr_peak_nits,
                app->hdr_paper_white_nits,
                app->hdr_black_level_nits
            );
            app->hdr_calibration_status_label = gtk_label_new(calibration_text);
            g_free(calibration_text);
        }
        gtk_label_set_xalign(GTK_LABEL(app->hdr_calibration_status_label), 0.0f);
        gtk_label_set_wrap(GTK_LABEL(app->hdr_calibration_status_label), TRUE);
        gtk_widget_set_hexpand(app->hdr_calibration_status_label, TRUE);
        gtk_box_append(GTK_BOX(calibration_box), app->hdr_calibration_status_label);
        gtk_box_append(GTK_BOX(image_page), calibration_box);

        g_signal_connect(start_calibration, "clicked", G_CALLBACK(on_hdr_calibration_start_clicked), app);
        g_signal_connect(reset_calibration, "clicked", G_CALLBACK(on_hdr_calibration_reset_clicked), app);
    }
    {
        GtkWidget *row = create_control_row(app, "Brightness", V4L2_CID_BRIGHTNESS);
        gtk_widget_add_css_class(row, "setting-row");
        gtk_box_append(GTK_BOX(image_page), row);
    }
    {
        GtkWidget *row = create_control_row(app, "Contrast", V4L2_CID_CONTRAST);
        gtk_widget_add_css_class(row, "setting-row");
        gtk_box_append(GTK_BOX(image_page), row);
    }
    {
        GtkWidget *row = create_control_row(app, "Saturation", V4L2_CID_SATURATION);
        gtk_widget_add_css_class(row, "setting-row");
        gtk_box_append(GTK_BOX(image_page), row);
    }
    {
        GtkWidget *row = create_control_row(app, "Hue", V4L2_CID_HUE);
        gtk_widget_add_css_class(row, "setting-row");
        gtk_box_append(GTK_BOX(image_page), row);
    }
    {
        GtkWidget *image_scroller = gtk_scrolled_window_new();
        gtk_scrolled_window_set_policy(
            GTK_SCROLLED_WINDOW(image_scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC
        );
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(image_scroller), image_page);
        gtk_stack_add_titled(GTK_STACK(settings_stack), image_scroller, "image", "Image & HDR");
    }

    audio_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(audio_page, "settings-page");
    gtk_box_append(
        GTK_BOX(audio_page),
        make_settings_heading(
            "Sound",
            "Choose the capture input and route monitored audio to any connected output."
        )
    );
    {
        const gchar *audio_env = g_getenv("LINUX_CAPTURE_STUDIO_AUDIO");
        const gchar *audio_default = audio_env != NULL && audio_env[0] != '\0'
            ? audio_env : app->profile_audio_input;
        GtkWidget *row = make_labeled_entry(
            "Capture input",
            &app->audio_entry,
            audio_default != NULL && audio_default[0] != '\0' ? audio_default : "default"
        );
        gtk_widget_add_css_class(row, "setting-row");
        gtk_box_append(GTK_BOX(audio_page), row);
    }
    {
        GtkWidget *output_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *output_label = gtk_label_new("Monitor output");
        GtkStringList *empty_model = gtk_string_list_new(NULL);
        gtk_widget_add_css_class(output_row, "setting-row");
        gtk_widget_set_size_request(output_label, 110, -1);
        gtk_label_set_xalign(GTK_LABEL(output_label), 0.0f);
        gtk_box_append(GTK_BOX(output_row), output_label);

        gtk_string_list_append(empty_model, "System default (PipeWire)");
        app->audio_output_dropdown = gtk_drop_down_new(G_LIST_MODEL(empty_model), NULL);
        gtk_widget_add_css_class(app->audio_output_dropdown, "premium-dropdown");
        gtk_widget_set_hexpand(app->audio_output_dropdown, TRUE);
        gtk_box_append(GTK_BOX(output_row), app->audio_output_dropdown);

        app->audio_refresh_button = gtk_button_new_from_icon_name("view-refresh-symbolic");
        gtk_widget_add_css_class(app->audio_refresh_button, "icon-button");
        gtk_widget_set_tooltip_text(app->audio_refresh_button, "Refresh connected audio outputs");
        gtk_box_append(GTK_BOX(output_row), app->audio_refresh_button);
        gtk_box_append(GTK_BOX(audio_page), output_row);
    }
    {
        GtkWidget *monitor_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *monitor_label = gtk_label_new("Monitor audio");
        GtkWidget *volume_label = gtk_label_new("Volume");
        gtk_widget_add_css_class(monitor_row, "setting-row");
        gtk_widget_set_size_request(monitor_label, 110, -1);
        gtk_label_set_xalign(GTK_LABEL(monitor_label), 0.0f);
        gtk_box_append(GTK_BOX(monitor_row), monitor_label);

        app->monitor_switch = gtk_switch_new();
        gtk_switch_set_active(GTK_SWITCH(app->monitor_switch), app->profile_monitor_enabled);
        gtk_box_append(GTK_BOX(monitor_row), app->monitor_switch);

        gtk_widget_set_margin_start(volume_label, 12);
        gtk_box_append(GTK_BOX(monitor_row), volume_label);

        app->monitor_volume = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.5, 0.05);
        gtk_range_set_value(GTK_RANGE(app->monitor_volume), app->profile_monitor_volume);
        gtk_scale_set_draw_value(GTK_SCALE(app->monitor_volume), TRUE);
        gtk_widget_set_hexpand(app->monitor_volume, TRUE);
        gtk_box_append(GTK_BOX(monitor_row), app->monitor_volume);
        gtk_box_append(GTK_BOX(audio_page), monitor_row);
    }
    {
        GtkWidget *sync_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *sync_label = gtk_label_new("Audio sync");
        gtk_widget_add_css_class(sync_row, "setting-row");
        gtk_widget_set_size_request(sync_label, 110, -1);
        gtk_label_set_xalign(GTK_LABEL(sync_label), 0.0f);
        gtk_box_append(GTK_BOX(sync_row), sync_label);

        app->audio_sync_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -500.0, 500.0, 5.0);
        gtk_range_set_value(GTK_RANGE(app->audio_sync_scale), app->profile_audio_sync_ms);
        gtk_scale_set_draw_value(GTK_SCALE(app->audio_sync_scale), TRUE);
        gtk_scale_set_value_pos(GTK_SCALE(app->audio_sync_scale), GTK_POS_RIGHT);
        gtk_scale_add_mark(GTK_SCALE(app->audio_sync_scale), 0.0, GTK_POS_BOTTOM, "0 ms");
        gtk_widget_set_tooltip_text(app->audio_sync_scale, "Positive values delay audio; negative values advance audio");
        gtk_widget_set_hexpand(app->audio_sync_scale, TRUE);
        gtk_box_append(GTK_BOX(sync_row), app->audio_sync_scale);
        gtk_box_append(GTK_BOX(audio_page), sync_row);
    }
    gtk_stack_add_titled(GTK_STACK(settings_stack), audio_page, "audio", "Sound");

    library_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(library_page, "settings-page");
    gtk_box_append(
        GTK_BOX(library_page),
        make_settings_heading(
            "Recording Library",
            "Play completed captures, open the recording folder, or move files to Trash."
        )
    );
    {
        GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *open_folder = gtk_button_new_with_label("Open Folder");
        GtkWidget *refresh = gtk_button_new_from_icon_name("view-refresh-symbolic");
        app->library_summary_label = gtk_label_new("Loading recordings…");
        gtk_label_set_xalign(GTK_LABEL(app->library_summary_label), 0.0f);
        gtk_widget_set_hexpand(app->library_summary_label, TRUE);
        gtk_widget_add_css_class(app->library_summary_label, "library-summary");
        gtk_box_append(GTK_BOX(toolbar), app->library_summary_label);
        gtk_widget_add_css_class(open_folder, "secondary-button");
        gtk_widget_add_css_class(refresh, "icon-button");
        gtk_box_append(GTK_BOX(toolbar), open_folder);
        gtk_box_append(GTK_BOX(toolbar), refresh);
        gtk_box_append(GTK_BOX(library_page), toolbar);
        g_signal_connect(open_folder, "clicked", G_CALLBACK(on_library_open_folder_clicked), app);
        g_signal_connect(refresh, "clicked", G_CALLBACK(on_library_refresh_clicked), app);
    }
    {
        GtkWidget *scroller = gtk_scrolled_window_new();
        gtk_widget_set_vexpand(scroller, TRUE);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        app->library_list = gtk_list_box_new();
        gtk_list_box_set_selection_mode(GTK_LIST_BOX(app->library_list), GTK_SELECTION_NONE);
        gtk_widget_add_css_class(app->library_list, "recording-library");
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), app->library_list);
        gtk_box_append(GTK_BOX(library_page), scroller);
    }
    gtk_stack_add_titled(GTK_STACK(settings_stack), library_page, "library", "Library");

    app->settings_frame = NULL;
    app->status_label = NULL;
    sync_pending_settings_from_current(app);
    update_apply_ui(app);

    g_signal_connect(app->settings_button, "clicked", G_CALLBACK(on_settings_clicked), app);
    g_signal_connect(app->settings_window, "close-request", G_CALLBACK(on_settings_window_close_request), app);
    g_signal_connect(app->record_button, "clicked", G_CALLBACK(on_record_clicked), app);
    g_signal_connect(app->apply_button, "clicked", G_CALLBACK(on_apply_clicked), app);
    g_signal_connect(app->session_dropdown, "notify::selected", G_CALLBACK(on_session_mode_changed), app);
    g_signal_connect(app->format_dropdown, "notify::selected", G_CALLBACK(on_capture_setting_changed), app);
    g_signal_connect(app->resolution_dropdown, "notify::selected", G_CALLBACK(on_capture_setting_changed), app);
    g_signal_connect(app->fps_dropdown, "notify::selected", G_CALLBACK(on_capture_setting_changed), app);
    g_signal_connect(app->hdr_dropdown, "notify::selected", G_CALLBACK(on_hdr_setting_changed), app);
    g_signal_connect(app->hdr_toggle_button, "toggled", G_CALLBACK(on_hdr_toggle_toggled), app);
    g_signal_connect(app->hdr_tonemap_switch, "notify::active", G_CALLBACK(on_hdr_tonemap_changed), app);
    g_signal_connect(app->quality_dropdown, "notify::selected", G_CALLBACK(on_record_options_changed), app);
    g_signal_connect(app->codec_dropdown, "notify::selected", G_CALLBACK(on_record_options_changed), app);
    g_signal_connect(app->container_dropdown, "notify::selected", G_CALLBACK(on_record_options_changed), app);
    g_signal_connect(app->stream_service_dropdown, "notify::selected", G_CALLBACK(on_stream_settings_changed), app);
    g_signal_connect(app->stream_bitrate_dropdown, "notify::selected", G_CALLBACK(on_stream_settings_changed), app);
    g_signal_connect(app->stream_server_entry, "changed", G_CALLBACK(on_stream_entry_changed), app);
    g_signal_connect(app->vrr_monitor_switch, "notify::active", G_CALLBACK(on_vrr_monitor_changed), app);
    g_signal_connect(app->output_entry, "changed", G_CALLBACK(on_profile_entry_changed), app);
    g_signal_connect(app->audio_entry, "changed", G_CALLBACK(on_profile_entry_changed), app);
    g_signal_connect(app->audio_output_dropdown, "notify::selected", G_CALLBACK(on_audio_output_changed), app);
    g_signal_connect(app->audio_refresh_button, "clicked", G_CALLBACK(on_audio_refresh_clicked), app);
    g_signal_connect(app->monitor_switch, "notify::active", G_CALLBACK(on_monitor_switch_changed), app);
    g_signal_connect(app->monitor_volume, "value-changed", G_CALLBACK(on_monitor_volume_changed), app);
    g_signal_connect(app->audio_sync_scale, "value-changed", G_CALLBACK(on_audio_sync_changed), app);
    g_signal_connect(app->fullscreen_button, "clicked", G_CALLBACK(on_fullscreen_clicked), app);
    g_signal_connect(picture_gesture, "pressed", G_CALLBACK(on_picture_pressed), app);
    g_signal_connect(app->window, "close-request", G_CALLBACK(on_close_request), app);

    key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_window_key_pressed), app);
    gtk_widget_add_controller(app->window, key_controller);

    if (app->native_p010_session) {
        enforce_native_p010_lock(app);
        gtk_widget_add_css_class(app->format_dropdown, "locked-control");
        gtk_widget_add_css_class(app->fps_dropdown, "locked-control");
        gtk_widget_set_tooltip_text(
            app->format_dropdown,
            "HDR60 uses a native transport at the selected resolution; 4K opens a true 3840×2160/60 source and never scales 1080p"
        );
        gtk_widget_set_tooltip_text(
            app->resolution_dropdown,
            "Change HDR60 native capture resolution; Apply briefly restarts the device mode"
        );
    } else {
        select_best_supported_mode(app);
        populate_fps_dropdown(app, app->fps);
        app->selection_update = TRUE;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->hdr_toggle_button), app->hdr_enabled);
        app->selection_update = FALSE;
        update_hdr_toggle_ui(app);
    }
    refresh_audio_outputs(app, FALSE);
    update_mode_badge(app);
    update_device_ui(app);
    update_session_ui(app);
    sync_pending_settings_from_current(app);
    app->settings_pending = FALSE;
    app->apply_in_progress = FALSE;
    update_apply_ui(app);
    update_audio_badge_ui(app);
    update_vrr_status(app);
    on_stream_settings_changed(G_OBJECT(app->stream_service_dropdown), NULL, app);
    refresh_recording_library(app);

    app->stats_timer_id = g_timeout_add_seconds(1, update_stats, app);
    app->watchdog_timer_id = g_timeout_add_seconds(1, video_watchdog_tick, app);
    app->device_watch_timer_id = g_timeout_add_seconds(2, device_watch_tick, app);

    gtk_window_present(GTK_WINDOW(app->window));
    if (app->device_connected) build_pipeline(app, FALSE);
}

int
main(int argc, char **argv)
{
    GtkApplication *gtk_app;
    OpenCentralApp app = {0};
    const gchar *device_env;
    const gchar *application_id;
    GApplicationFlags application_flags;
    int result;

    gst_init(&argc, &argv);

    device_env = g_getenv("LINUX_CAPTURE_STUDIO_DEVICE");
    if (device_env == NULL || device_env[0] == '\0') {
        device_env = g_getenv("OPENCENTRAL_DEVICE");
    }
    app.device = g_strdup(
        device_env != NULL && device_env[0] != '\0'
            ? device_env
            : "/dev/video0"
    );
    app.format_index = 1;
    app.resolution_index = 2;
    app.fps = 60;
    app.hdr_mode = HDR_MODE_HDR10_PQ;
    app.hdr_enabled = FALSE;
    app.hdr_peak_nits = 1000.0;
    app.hdr_paper_white_nits = 203.0;
    app.hdr_black_level_nits = 0.0;
    app.hdr_preview_saturation = 1.06;
    app.audio_capture_disabled =
        g_strcmp0(g_getenv("LINUX_CAPTURE_STUDIO_DISABLE_CAPTURE_AUDIO"), "1") == 0;
    app.startup_safe_fallback_attempted =
        g_strcmp0(g_getenv("LINUX_CAPTURE_STUDIO_RECOVERY_RESTART"), "1") == 0;
    {
        const gchar *transition_status_env =
            g_getenv("LINUX_CAPTURE_STUDIO_TRANSITION_STATUS_FILE");
        if (transition_status_env != NULL && transition_status_env[0] != '\0') {
            app.transition_status_file = g_strdup(transition_status_env);
        }
    }
    app.native_p010_session =
        g_strcmp0(g_getenv("LINUX_CAPTURE_STUDIO_NATIVE_P010"), "1") == 0;
    {
        const gchar *session_env = g_getenv("LINUX_CAPTURE_STUDIO_SESSION_MODE");
        if (g_strcmp0(session_env, "hdr1080") == 0) {
            app.session_mode = SESSION_MODE_HDR60_1080;
        } else if (g_strcmp0(session_env, "hdr1440") == 0) {
            app.session_mode = SESSION_MODE_HDR60_1440;
        } else if (g_strcmp0(session_env, "hdr4k") == 0) {
            app.session_mode = SESSION_MODE_HDR60_4K;
        } else {
            app.session_mode = SESSION_MODE_SMOOTH_SDR;
        }
    }
    app.record_preset = RECORD_PRESET_LOSSLESS;
    app.record_codec = RECORD_CODEC_AUTO;
    app.record_container = RECORD_CONTAINER_MKV;
    app.stream_service = STREAM_SERVICE_YOUTUBE;
    app.stream_bitrate_kbps = 9000;
    app.vrr_monitor_enabled = TRUE;
    app.vrr_last_pts_ns = (gint64)GST_CLOCK_TIME_NONE;
    app.vrr_min_interval_ns = G_MAXINT64;
    app.profile_monitor_enabled = TRUE;
    app.profile_monitor_volume = 1.0;
    app.profile_audio_sync_ms = 0.0;
    app.last_good_format_index = app.format_index;
    app.last_good_resolution_index = app.resolution_index;
    app.last_good_fps = app.fps;
    app.control_fd = -1;
    app.control_fd = open(app.device, O_RDWR | O_NONBLOCK);
    if (app.control_fd < 0) {
        g_printerr("Linux Capture Studio: cannot open %s for controls: %s\n", app.device, g_strerror(errno));
    }
    app.capture_device_name = detect_capture_device_name(&app);
    app.is_gc575 = g_strcmp0(g_getenv("LINUX_CAPTURE_STUDIO_VENDOR_ID"), "07ca") == 0 &&
                     g_strcmp0(g_getenv("LINUX_CAPTURE_STUDIO_MODEL_ID"), "0575") == 0;
    if (!app.is_gc575 && app.capture_device_name != NULL) {
        gchar *capture_name_lower = g_ascii_strdown(app.capture_device_name, -1);
        app.is_gc575 = strstr(capture_name_lower, "live gamer 4k 2.1") != NULL ||
                       strstr(capture_name_lower, "live gamer 4") != NULL;
        g_free(capture_name_lower);
    }
    app.device_connected = app.control_fd >= 0;
    load_device_profile(&app);
    if (app.is_gc575 && app.native_p010_session) {
        const gchar *output_mode = g_getenv("LINUX_CAPTURE_STUDIO_HDR60_OUTPUT");
        app.format_index = 2; /* P010 */
        app.hdr_mode = HDR_MODE_HDR10_PQ;
        app.hdr_enabled = TRUE;
        app.fps = 60;
        if (g_strcmp0(output_mode, "2160p60") == 0 || g_strcmp0(output_mode, "4k60") == 0) {
            app.resolution_index = 3;
        } else if (g_strcmp0(output_mode, "1440p60") == 0) {
            app.resolution_index = 2;
        } else {
            app.resolution_index = 1;
        }
    }
    app.last_good_format_index = app.format_index;
    app.last_good_resolution_index = app.resolution_index;
    app.last_good_fps = app.fps;

    application_id = g_getenv("LINUX_CAPTURE_STUDIO_APP_ID");
    if (application_id == NULL || application_id[0] == '\0') {
        application_id = "io.github.linuxcapturestudio.LinuxCaptureStudio";
    }
    application_flags = G_APPLICATION_DEFAULT_FLAGS;
    gtk_app = gtk_application_new(application_id, application_flags);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(on_activate), &app);

    if (app.native_p010_session) {
        g_print(
            "Linux Capture Studio: starting integrated native-resolution HDR session (%s).\n",
            g_getenv("LINUX_CAPTURE_STUDIO_HDR60_OUTPUT") != NULL
                ? g_getenv("LINUX_CAPTURE_STUDIO_HDR60_OUTPUT")
                : "1080p60"
        );
    }
    result = g_application_run(G_APPLICATION(gtk_app), argc, argv);

    if (app.stats_timer_id != 0) {
        g_source_remove(app.stats_timer_id);
    }
    if (app.watchdog_timer_id != 0) {
        g_source_remove(app.watchdog_timer_id);
    }
    if (app.recovery_timer_id != 0) {
        g_source_remove(app.recovery_timer_id);
    }
    if (app.capture_restart_timer_id != 0) {
        g_source_remove(app.capture_restart_timer_id);
    }
    if (app.record_stop_timeout_id != 0) {
        g_source_remove(app.record_stop_timeout_id);
    }
    if (app.feedback_timeout_id != 0) {
        g_source_remove(app.feedback_timeout_id);
    }
    if (app.device_watch_timer_id != 0) {
        g_source_remove(app.device_watch_timer_id);
    }
    if (app.hdr_refresh_timer_id != 0) {
        g_source_remove(app.hdr_refresh_timer_id);
    }
    if (app.transition_complete_timer_id != 0) {
        g_source_remove(app.transition_complete_timer_id);
    }
    if (app.pipeline != NULL || app.monitor_pipeline != NULL) {
        stop_pipeline(&app);
    }
    if (app.control_fd >= 0) {
        close(app.control_fd);
    }
    g_clear_pointer(&app.current_file, g_free);
    if (app.audio_output_model != NULL) {
        g_object_unref(app.audio_output_model);
    }
    if (app.audio_output_ids != NULL) {
        g_ptr_array_unref(app.audio_output_ids);
    }
    g_free(app.profile_output_dir);
    g_free(app.profile_audio_input);
    g_free(app.profile_audio_output_id);
    g_free(app.profile_stream_server);
    g_free(app.capture_driver_name);
    g_free(app.capture_device_name);
    if (result == 0 && app.transition_status_file != NULL) {
        g_remove(app.transition_status_file);
    }
    g_free(app.transition_status_file);
    g_free(app.device);
    g_object_unref(gtk_app);

    return app.requested_exit_code != 0 ? app.requested_exit_code : result;
}
