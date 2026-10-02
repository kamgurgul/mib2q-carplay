#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "alt111.h"

#ifndef MAP_ANON
#define MAP_ANON MAP_ANONYMOUS
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/*
 * MU1440 CarPlay AltScreen Gen-2 vehicle candidate.
 *
 * This source deliberately forks the vehicle-proven Run143 handshake/crypto
 * boundary but replaces UI ownership and local-consumer synchronization with
 * the independently tested alt111 C99 core. Run143 remains untouched.
 *
 *
 * Target baseline (hard gate in installer):
 *   MHI2_ER_SKG13_P4526_MU1440
 *   /mnt/app/eso/lib/libairplay.so
 *   SHA256 193A4FD9101EC2AA05E7159CFA307B96500810D379CA74A194F172ADC13A46B5
 *
 * IRC-parity design:
 *  - Keep the exact MU1440 stock AirPlay implementation and its platform media ABI.
 *  - Replay the recovered MHI2Q IRC negotiation semantics around that stock core.
 *  - Advertise root enabledFeatures before stream 111 is selected.
 *  - Clone stock display[0], remove the reference-proven non-portable fields, and
 *    append a minimally modified AltScreen display without forcing display type=111.
 *  - Pass the original SETUP request to stock first; on 111, clone the requested
 *    stream descriptor and append dataPort + streamID=111 to the stock response.
 *  - Keep startup showUI/forceKeyFrame disabled unless parity-specific opt-in is set.
 *  - Prologue-hook stock Setup/Start/TearDown with the vehicle-proven MAP_FIXED
 *    private-page fallback because stock local binding bypasses plain interposition.
 *  - Reuse stock per-screen AES derivation/AES-CTR and receive stream 111 on TCP 6031.
 *  - Convert AVCC H.264 to Annex-B and expose it on 127.0.0.1:19820.
 *  - Main CarPlay stream 110 remains stock-owned.
 */

/*
 * Build targets
 *   default             Skoda MHI2 / MU1440 (vehicle-proven; behaviour unchanged).
 *   ALT111_TARGET_MHI2Q Audi MHI2Q / MU13xx (Qualcomm APQ8064, stock libairplay
 *                       210.81, Cinemo iAP2). Built by scripts/build_altscreen_hook.sh
 *                       and preloaded into dio_manager beside the mib2q-carplay-rgi
 *                       hook. Differences vs MU1440, each for a recorded reason:
 *    - constructor-free: no thread/dlsym/patch runs from the ELF constructor
 *      (mib2q-carplay-rgi K1004 loader-lock finding). Everything starts from the
 *      first interposed AirPlay call; /info always precedes SETUP.
 *    - PLT interposition by default: the exported AirPlayReceiverSession* symbols
 *      are the hooks (offline MU1438/MU1440 compare found genuine JUMP_SLOT
 *      routes). Inline prologue patching stays available behind
 *      ALTSCREEN111_INLINE_HOOKS=1.
 *    - stock SETUP is retried without stream 111 if it rejects the combined
 *      request (210.81 has no type-111 branch).
 *    - defaults: 1440x540 B9-class cluster canvas, stream 111 on 7100 with a
 *      PF-permitted fallback list (6030 is the stock stream-110 port).
 *    - the loopback Annex-B tee is unchanged; on MHI2Q it is consumed by
 *      altscreen_render (mib2q-carplay-rgi) into cluster displayable 99, not the
 *      MU1440 MPEG-TS/MOST writer.
 *  The stock session key observer is unchanged from the MU1440 build.
 */
#ifdef ALT111_TARGET_MHI2Q
#define ALT111_TARGET_NAME "mhi2q"
/*
 * The unit filters inbound TCP on the CarPlay link with PF. Its allowlist for
 * interface carplay0 is, verbatim from `pfctl -sr`:
 *   5000:5001, 5010, 6000:6001, 6030, 6100, 6200, 7000:7001, 7100
 * A dataPort outside that set is accepted by the phone and then never connected
 * to (the SYN is dropped), which looks exactly like a silent stall.
 *
 * 6030 is NOT free: it is the stock main-screen (stream 110) data port. Stock
 * only listens on it for the moment between the 110 SETUP and the phone's
 * connect, so `netstat` on an idle unit never shows it, but every log set has
 * the phone connected to :6030 even with our advertisement off. With our 111
 * listener sitting on 6030 the stock screen never gets its connection, stays
 * INITIALIZING, and the phone tears the whole session down after ~10 s: black
 * main screen, cluster map for a few seconds, dongle reconnect loop. Stock also
 * owns 5000 (RTSP) and 5001 (events). 7100 (legacy AirPlay mirroring, unused by
 * a CarPlay session) is the default; override at runtime with
 * /mnt/app/mibr-carplay111.port. Stock ports are refused even when configured.
 *
 * Ephemeral fallback is therefore DISABLED here: a kernel-assigned port is
 * guaranteed to be outside the allowlist, so falling back would bind a port the
 * phone can never reach. If the chosen port does not bind, the remaining
 * PF-permitted non-stock ports are tried instead.
 */
#define ALT111_DEFAULT_PORT 7100
#define ALT111_DEFAULT_ALLOW_EPHEMERAL 0
#define ALT111_DEFAULT_WIDTH 1440
#define ALT111_DEFAULT_HEIGHT 540
#define ALT111_DEFAULT_WIDTH_MM 290
#else
#define ALT111_TARGET_NAME "mu1440"
#define ALT111_DEFAULT_PORT 6031
#define ALT111_DEFAULT_ALLOW_EPHEMERAL 0
#define ALT111_DEFAULT_WIDTH 1010
#define ALT111_DEFAULT_HEIGHT 376
#define ALT111_DEFAULT_WIDTH_MM 200
#endif

typedef const void *CFTypeRef;
typedef const void *CFStringRef;
typedef const void *CFDictionaryRef;
typedef void *CFMutableDictionaryRef;
typedef const void *CFArrayRef;
typedef void *CFMutableArrayRef;
typedef const void *CFDataRef;
typedef unsigned long CFTypeID;
typedef long CFIndex;
typedef int32_t OSStatus;
typedef unsigned char Boolean;
typedef void *AirPlayReceiverSessionRef;

#define K_NO_ERR 0
#define K_STREAM_ALT 111
#define K_SCREEN_HDR_SIZE 128
#define K_SCREEN_VIDEO_FRAME 0
#define K_SCREEN_VIDEO_CONFIG 1
#define K_SCREEN_KEEPALIVE 2
#define K_SCREEN_FORCE_KEYFRAME 3
#define K_SCREEN_IGNORE 4
#define K_SCREEN_KEEPALIVE_BODY 5
#define CF_UTF8 0x08000100u
#define MAX_SCREEN_BODY (8u * 1024u * 1024u)
#define ALT_UUID_DEFAULT "b7e6c5a0-2222-4000-8000-000000000002"
#define ALT_URL_DEFAULT  "maps:/car/instrumentcluster"
#define ALT_URL_MAP      "maps:/car/instrumentcluster/map"
#define ALT_URL_INSTRUCTIONCARD "maps:/car/instrumentcluster/instructioncard"
#define AIRPLAY_FEATURE_BIT26 (1ULL << 26)

/* Opaque storage. Stock AES_CTR_Context is smaller than this on the target. */
typedef union {
    uint64_t align;
    unsigned char bytes[512];
} aes_ctr_storage_t;

typedef struct {
    uint32_t bodySize;
    uint8_t opcode;
    uint8_t smallParam[3];
    uint8_t params[15 * 8];
} AirPlayScreenHeaderCompat;

typedef OSStatus (*fn_setup_t)(AirPlayReceiverSessionRef, CFDictionaryRef, CFDictionaryRef *);
typedef OSStatus (*fn_start_t)(AirPlayReceiverSessionRef, void *);
typedef void (*fn_teardown_t)(AirPlayReceiverSessionRef, CFDictionaryRef, OSStatus, Boolean *);
typedef CFDictionaryRef (*fn_serverinfo_t)(AirPlayReceiverSessionRef, CFArrayRef, uint8_t *, OSStatus *);
typedef void (*fn_command_completion_t)(OSStatus, CFDictionaryRef, void *);
typedef OSStatus (*fn_sendcmd_t)(AirPlayReceiverSessionRef, CFDictionaryRef, fn_command_completion_t, void *);
typedef OSStatus (*fn_platform_control_t)(AirPlayReceiverSessionRef, uint32_t, CFStringRef, CFTypeRef, CFDictionaryRef, CFDictionaryRef *);
typedef OSStatus (*fn_session_control_t)(AirPlayReceiverSessionRef, uint32_t, CFStringRef, CFTypeRef, CFDictionaryRef, CFDictionaryRef *);
typedef struct {
    int32_t screen;
    int32_t main_audio;
    int32_t phone;
    int32_t speech;
    int32_t speech_detail;
    int32_t turns;
} mibr_mode_state_t;
typedef OSStatus (*fn_make_mode_state_t)(AirPlayReceiverSessionRef, CFDictionaryRef, mibr_mode_state_t *);
typedef OSStatus (*fn_aes_cbc_init_t)(void *, const uint8_t[16], const uint8_t[16], Boolean);
typedef OSStatus (*fn_aes_ctr_init_t)(void *, const uint8_t[16], const uint8_t[16]);
typedef OSStatus (*fn_aes_ctr_update_t)(void *, const void *, size_t, void *);
typedef void (*fn_aes_ctr_final_t)(void *);
typedef void (*fn_derive_screen_t)(const void *, size_t, uint64_t, uint8_t[16], uint8_t[16]);

/* CFLite function types. */
typedef CFStringRef (*fn_cfstr_create_t)(void *, const char *, uint32_t);
typedef const char *(*fn_cfstr_cstr_ptr_t)(CFStringRef, uint32_t);
typedef Boolean (*fn_cfstr_cstr_t)(CFStringRef, char *, CFIndex, uint32_t);
typedef CFTypeID (*fn_cfget_typeid_t)(CFTypeRef);
typedef CFTypeID (*fn_cftypeid_t)(void);
typedef int64_t (*fn_cfget_i64_t)(CFTypeRef, OSStatus *);
typedef Boolean (*fn_cfbool_get_t)(CFTypeRef);
typedef CFIndex (*fn_cfdict_count_t)(CFDictionaryRef);
typedef void (*fn_cfdict_keys_t)(CFDictionaryRef, const void **, const void **);
typedef CFTypeRef (*fn_cfdict_get_t)(CFDictionaryRef, CFTypeRef);
typedef void (*fn_cfdict_set_t)(CFMutableDictionaryRef, CFTypeRef, CFTypeRef);
typedef void (*fn_cfdict_remove_t)(CFMutableDictionaryRef, CFTypeRef);
typedef int64_t (*fn_cfdict_get_i64_t)(CFDictionaryRef, CFStringRef, OSStatus *);
typedef void (*fn_cfdict_set_i64_t)(CFMutableDictionaryRef, CFStringRef, int64_t);
typedef CFMutableDictionaryRef (*fn_cfdict_new_t)(void *, CFIndex, const void *, const void *);
typedef CFIndex (*fn_cfarr_count_t)(CFArrayRef);
typedef CFTypeRef (*fn_cfarr_get_t)(CFArrayRef, CFIndex);
typedef void (*fn_cfarr_append_t)(CFMutableArrayRef, CFTypeRef);
typedef CFMutableArrayRef (*fn_cfarr_new_t)(void *, CFIndex, const void *);
typedef CFMutableArrayRef (*fn_cfarr_copy_t)(void *, CFIndex, CFArrayRef);
typedef const uint8_t *(*fn_cfdata_ptr_t)(CFDataRef);
typedef CFIndex (*fn_cfdata_len_t)(CFDataRef);
typedef CFTypeRef (*fn_cfretain_t)(CFTypeRef);
typedef void (*fn_cfrelease_t)(CFTypeRef);

static fn_setup_t g_setup_trampoline;
static fn_start_t g_start_trampoline;
static fn_teardown_t g_teardown_trampoline;
static fn_platform_control_t g_platform_control_trampoline;
static fn_session_control_t g_session_control_trampoline;
/*
 * Preserve exact stock entry points separately from executable trampolines.
 * This guarantees a true fail-closed fallback when an inline hook cannot be
 * installed: externally-preempted calls still delegate to stock instead of
 * dereferencing a NULL trampoline.
 */
static fn_setup_t g_real_setup;
static fn_start_t g_real_start;
static fn_teardown_t g_real_teardown;
static fn_platform_control_t g_real_platform_control;
static fn_session_control_t g_real_session_control;
static fn_make_mode_state_t g_make_mode_state;
static fn_serverinfo_t g_real_serverinfo;
static fn_sendcmd_t g_sendcmd;
static fn_aes_cbc_init_t g_real_aes_cbc_init;
static fn_aes_ctr_init_t g_aes_ctr_init;
static fn_aes_ctr_update_t g_aes_ctr_update;
static fn_aes_ctr_final_t g_aes_ctr_final;
static fn_derive_screen_t g_derive_screen;

static fn_cfstr_create_t p_CFStringCreateWithCString;
static fn_cfstr_cstr_ptr_t p_CFStringGetCStringPtr;
static fn_cfstr_cstr_t p_CFStringGetCString;
static fn_cfget_typeid_t p_CFGetTypeID;
static fn_cftypeid_t p_CFStringGetTypeID;
static fn_cftypeid_t p_CFDictionaryGetTypeID;
static fn_cftypeid_t p_CFArrayGetTypeID;
static fn_cftypeid_t p_CFNumberGetTypeID;
static fn_cftypeid_t p_CFBooleanGetTypeID;
static fn_cftypeid_t p_CFDataGetTypeID;
static fn_cfget_i64_t p_CFGetInt64;
static fn_cfbool_get_t p_CFBooleanGetValue;
static fn_cfdata_len_t p_CFDataGetLength;
static fn_cfdata_ptr_t p_CFDataGetBytePtr;
static fn_cfdict_count_t p_CFDictionaryGetCount;
static fn_cfdict_keys_t p_CFDictionaryGetKeysAndValues;
static fn_cfdict_get_t p_CFDictionaryGetValue;
static fn_cfdict_set_t p_CFDictionarySetValue;
static fn_cfdict_remove_t p_CFDictionaryRemoveValue;
static fn_cfdict_get_i64_t p_CFDictionaryGetInt64;
static fn_cfdict_set_i64_t p_CFDictionarySetInt64;
static fn_cfdict_new_t p_CFDictionaryCreateMutable;
static fn_cfarr_count_t p_CFArrayGetCount;
static fn_cfarr_get_t p_CFArrayGetValueAtIndex;
static fn_cfarr_append_t p_CFArrayAppendValue;
static fn_cfarr_new_t p_CFArrayCreateMutable;
static fn_cfarr_copy_t p_CFArrayCreateMutableCopy;
static fn_cfretain_t p_CFRetain;
static fn_cfrelease_t p_CFRelease;
static const void *p_dict_key_callbacks;
static const void *p_dict_val_callbacks;
static const void *p_array_callbacks;
static CFTypeRef p_cfl_boolean_false;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_heartbeat_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_capture_lock = PTHREAD_MUTEX_INITIALIZER;
static AirPlayReceiverSessionRef g_active_session;
static uint8_t g_master_key[16];
static int g_master_valid;
static uintptr_t g_security_fn;

static int g_alt_listen = -1;
static int g_alt_client = -1;
static int g_alt_listener_ipv6;
static pthread_t g_alt_thread;
static int g_alt_thread_started;
static aes_ctr_storage_t g_alt_aes;
static int g_alt_aes_valid;

static int g_tee_listen = -1;
static int g_tee_client = -1;
static uint64_t g_tee_consumer_generation;
static pthread_t g_tee_thread;
static int g_tee_started;

static int g_capture_listen = -1;
static int g_capture_client = -1;
static int g_capture_port = 19821;
static pthread_t g_capture_thread;
static int g_capture_started;
static int g_capture_wait_idr = 1;
static uint64_t g_capture_bytes;
static uint64_t g_capture_chunks;
static uint64_t g_capture_drops;

static int g_enabled = 1;
static int g_alt_port = ALT111_DEFAULT_PORT;
static int g_tee_port = 19820;
static int g_width = ALT111_DEFAULT_WIDTH;
static int g_height = ALT111_DEFAULT_HEIGHT;
static int g_width_mm = ALT111_DEFAULT_WIDTH_MM;
static int g_height_mm = (ALT111_DEFAULT_WIDTH_MM * ALT111_DEFAULT_HEIGHT) / ALT111_DEFAULT_WIDTH;
static int g_fps = 30;
static const char *g_fps_override_path = "/mnt/app/root/mibr-carplay111-fps";
static int g_auto_show = 0;
/* 0 = leave /info byte-for-byte stock (no AltScreen advertisement). Diagnostic
 * lever: if a sender only works with this off, the advertisement is the cause. */
static int g_advertise = 1;
static int g_viewareas = 1;
static const char *g_viewareas_marker = "/mnt/app/root/mibr-carplay111-viewareas.enabled";
static const char *g_safearea_config_path = "/mnt/app/root/mibr-carplay111-safearea.conf";
static const char *g_autoshow_disable_marker = "/mnt/app/root/mibr-carplay111-autoshow.disabled";
static const char *g_url_map_marker = "/mnt/app/root/mibr-carplay111-url-map.enabled";
static const char *g_url_mode_path = "/tmp/mibr-alt111-url-mode";
/*
 * Runtime navigation composition controls.
 *
 * The master query marker keeps the historic bare-URL behaviour as the
 * fail-safe default.  Surface and appearance values live under /mnt/app/root
 * so the same switches can be used for one-off vehicle A/B tests and later
 * persistent operation.  instructioncard is intentionally kept query-free,
 * matching observed Apple Simulator behaviour.
 */
static const char *g_nav_query_enable_marker = "/mnt/app/root/mibr-carplay111-nav-query.enabled";
static const char *g_nav_surface_path = "/mnt/app/root/mibr-carplay111-nav.surface";
static const char *g_nav_eta_path = "/mnt/app/root/mibr-carplay111-nav.showETA";
static const char *g_nav_speed_path = "/mnt/app/root/mibr-carplay111-nav.showSpeedLimit";
static const char *g_nav_compass_path = "/mnt/app/root/mibr-carplay111-nav.showCompass";
static const char *g_nav_maneuver_path = "/mnt/app/root/mibr-carplay111-nav.maneuverLayout";
/*
 * Stream-111 data-port override, re-read at every SETUP.
 *
 * The unit runs a PF packet filter (pflog0 is UP) and only permits certain
 * inbound ports on the CarPlay link, so the dataPort we advertise must be one PF
 * allows -- otherwise the phone accepts the stream but its connection never
 * arrives. This file lets the port be A/B tested over SSH with no rebuild and no
 * config edit: echo 6200 > /mnt/app/mibr-carplay111.port
 */
static const char *g_port_override_path = "/mnt/app/mibr-carplay111.port";
/*
 * Same idea for the /info advertisement itself. Creating this file makes
 * AirPlayCopyServerInfo return the stock dictionary byte-for-byte and stops the
 * SETUP path from adding enabledFeatures, so the unit negotiates CarPlay exactly
 * as it does without us -- the A/B that separates "our advertisement is rejected"
 * from "our video path misbehaves" on a unit whose stock /info carries no
 * enabledFeatures and no altScreen URLs at all:
 *     touch /mnt/app/mibr-carplay111.noadvertise   # stock /info, then reconnect
 *     rm    /mnt/app/mibr-carplay111.noadvertise   # back to AltScreen
 * Re-read per session, so no rebuild and no reboot.
 */
static const char *g_advertise_off_marker = "/mnt/app/mibr-carplay111.noadvertise";
static const char *g_bit26_on_marker = "/mnt/app/root/mibr-carplay111-bit26.force-on";
static const char *g_bit26_off_marker = "/mnt/app/root/mibr-carplay111-bit26.force-off";
static char g_alt_uuid[96] = ALT_UUID_DEFAULT;
static char g_alt_url[160] = ALT_URL_DEFAULT;
static const char *g_log_path = "/tmp/altscreen111.log";
static const char *g_state_path = "/tmp/mibr-carplay111.state";
static const char *g_heartbeat_path = "/tmp/mibr-carplay111.heartbeat";
static const char *g_capture_status_path = "/tmp/mibr-alt111-capture.status";
static int g_streaming;
static int g_video_config_seen;
static uint64_t g_last_heartbeat_ms;

/* Gen-2 core: all access is serialized through g2_core_lock. */
static pthread_mutex_t g2_core_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g2_status_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g2_core_cv = PTHREAD_COND_INITIALIZER;
static struct alt111_profile g2_profile;
static struct alt111_control g2_control;
static struct alt111_video g2_video;
static struct alt111_resync g2_resync;
static mibr_mode_state_t g2_last_mode_state;
static unsigned g2_last_mode_valid;
static uint64_t g2_mode_sequence;
static uint64_t g2_control_session;
static uint64_t g2_video_stream;
static uint64_t g2_last_dispatched_request;
static uint64_t g2_last_completed_request;
static int g2_last_completion_status;
static unsigned g2_command_ready;
/* Net steering-wheel MapScale steps still to send as changeMapZoomLevel
 * (sign as received from the VC roller). Guarded by g2_core_lock. */
static int g2_zoom_pending;
static pthread_t g2_control_thread;
static pthread_t g2_output_thread;
static int g2_workers_started;
static pthread_once_t g_runtime_once = PTHREAD_ONCE_INIT;
static int g_runtime_ok;
#ifdef ALT111_TARGET_MHI2Q
static int g_mhi2q_want_inline;
static void gen2_install_inline_hooks(void);
#endif
static int gen2_configure_profile(void);
static const char *g2_status_path = "/tmp/mibr-alt111-gen2.status";
static const char *g2_reacquire_marker = "/tmp/mibr-alt111-gen2-reacquire";
static const char *g2_diag_stop_marker = "/tmp/mibr-alt111-stop-only";
static const char *g2_diag_show_marker = "/tmp/mibr-alt111-show-only";
static const char *g2_diag_keyframe_marker = "/tmp/mibr-alt111-keyframe-only";
static const char *g2_diag_zoom_in_marker = "/tmp/mibr-alt111-zoom-in";
static const char *g2_diag_zoom_out_marker = "/tmp/mibr-alt111-zoom-out";
/* Present = a positive roller MapScale step zooms IN (default: OUT). */
static const char *g2_zoom_inverted_marker = "/mnt/app/root/mibr-carplay111-zoom.inverted";
static const char *g2_resync_enable_marker = "/tmp/mibr-alt111-resync.enabled";
static const char *g2_resync_arm_marker = "/tmp/mibr-alt111-resync-arm";
static const char *g2_d2_enable_marker = "/tmp/mibr-alt111-keyframe-policy.enabled";
static const char *g2_d2_persist_marker = "/mnt/app/root/mibr-alt111-keyframe-policy.enabled";

#define G2_D2_EVENT_DELAY_MS 250u
#define G2_D2_MIN_GAP_MS 1000u
#define G2_D2_WATCHDOG_MS 1000u
#define G2_D2_SOURCE_TURNS 0x01u
#define G2_D2_SOURCE_SUGGEST_UI 0x02u

static unsigned g2_d2_was_enabled;
static unsigned g2_d2_pending_sources;
static uint64_t g2_d2_pending_due_ms;
static uint64_t g2_d2_last_request_ms;
static uint64_t g2_d2_last_seen_idrs;
static uint64_t g2_d2_last_idr_ms;
static uint64_t g2_d2_event_triggers;
static uint64_t g2_d2_watchdog_triggers;
static uint64_t g2_d2_coalesced;

static void gen2_publish_status(void);
static void gen2_control_projection_on(void);
static void gen2_control_release(void);
static void gen2_set_command_ready(unsigned ready);
static void gen2_video_begin_current(void);
static void gen2_video_end_current(void);
static void gen2_close_consumer(void);
static void gen2_resync_poll(void);
static void gen2_d2_schedule(unsigned source, const char *label);
static void tee_drop_client_locked(void);
static void capture_drop_client_locked(void);
static void capture_mirror_send(const uint8_t *p, size_t n,
                                const struct alt111_output_ticket *ticket);
static int start_capture_server(void);
static void gen2_runtime_ready(void);
static void gen2_resolve_stock_targets(void);

static void logf_u2(const char *fmt, ...)
{
    char buf[768];
    int fd;
    int n;
    va_list ap;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    n = snprintf(buf, sizeof(buf), "%ld.%03ld [altscreen111] ", (long)ts.tv_sec, ts.tv_nsec / 1000000L);
    if (n < 0) return;
    va_start(ap, fmt);
    n += vsnprintf(buf + n, (n < (int)sizeof(buf)) ? sizeof(buf) - (size_t)n : 0, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof(buf) - 2) n = (int)sizeof(buf) - 2;
    buf[n++] = '\n';
    fd = open(g_log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) { (void)write(fd, buf, (size_t)n); close(fd); }
}

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000L);
}

static unsigned gen2_d2_enabled(void)
{
    return (access(g2_d2_enable_marker,F_OK)==0 ||
            access(g2_d2_persist_marker,F_OK)==0) ? 1u : 0u;
}

static void gen2_d2_schedule(unsigned source, const char *label)
{
    uint64_t now = monotonic_ms();
    uint64_t due = now + G2_D2_EVENT_DELAY_MS;
    uint64_t last_request = 0;
    unsigned active = 0;
    unsigned pending = 0;
    int enabled = gen2_d2_enabled() ? 1 : 0;

    if(!enabled) return;

    pthread_mutex_lock(&g2_core_lock);
    if(g2_d2_last_request_ms && due < g2_d2_last_request_ms + G2_D2_MIN_GAP_MS)
        due = g2_d2_last_request_ms + G2_D2_MIN_GAP_MS;

    if(g2_resync.state == ALT111_RESYNC_NEED_IDR) {
        ++g2_d2_coalesced;
        active = 1;
    } else {
        /*
         * Re-arm the debounce window from the newest event.  This intentionally
         * coalesces suggestUI + turns bursts into one IDR request after the
         * scene transition has had time to land on Stream 111.
         */
        g2_d2_pending_sources |= source;
        g2_d2_pending_due_ms = due;
        ++g2_d2_event_triggers;
        pending = g2_d2_pending_sources;
    }
    last_request = g2_d2_last_request_ms;
    pthread_mutex_unlock(&g2_core_lock);

    logf_u2("GEN2 D2 EVENT source=%s action=%s pending=0x%x due_ms=%llu last_request_ms=%llu",
            label?label:"unknown",active?"coalesced-active":"scheduled",pending,
            (unsigned long long)due,(unsigned long long)last_request);
}

static void capture_publish_status(const char *state)
{
    char b[320];
    int fd, n, client, wait_idr;
    uint64_t bytes, chunks, drops;

    pthread_mutex_lock(&g_capture_lock);
    client = g_capture_client >= 0 ? 1 : 0;
    wait_idr = g_capture_wait_idr;
    bytes = g_capture_bytes;
    chunks = g_capture_chunks;
    drops = g_capture_drops;
    pthread_mutex_unlock(&g_capture_lock);

    n = snprintf(b, sizeof(b),
                 "state=%s\nport=%d\nclient=%d\nwait_idr=%d\nbytes=%llu\nchunks=%llu\ndrops=%llu\n",
                 state ? state : "unknown", g_capture_port, client, wait_idr,
                 (unsigned long long)bytes,
                 (unsigned long long)chunks,
                 (unsigned long long)drops);
    if (n <= 0) return;
    if ((size_t)n >= sizeof(b)) n = (int)sizeof(b) - 1;
    fd = open(g_capture_status_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        (void)write(fd, b, (size_t)n);
        close(fd);
    }
}

static void publish_state(const char *state)
{
    int fd;
    size_t n;
    if (!state) return;
    fd = open(g_state_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        n = strlen(state);
        (void)write(fd, state, n);
        (void)write(fd, "\n", 1);
        close(fd);
    }
}

static void publish_video_heartbeat_internal(int force)
{
    char b[48];
    int fd;
    int n;
    uint64_t now = monotonic_ms();

    pthread_mutex_lock(&g_heartbeat_lock);
    if (!force && now && g_last_heartbeat_ms && now - g_last_heartbeat_ms < 250u) {
        pthread_mutex_unlock(&g_heartbeat_lock);
        return;
    }
    if (force && now <= g_last_heartbeat_ms) now = g_last_heartbeat_ms + 1u;
    g_last_heartbeat_ms = now;
    n = snprintf(b, sizeof(b), "%llu\n", (unsigned long long)now);
    if (n > 0) {
        fd = open(g_heartbeat_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            (void)write(fd, b, (size_t)n);
            close(fd);
        }
    }
    pthread_mutex_unlock(&g_heartbeat_lock);
}

static void publish_video_heartbeat(void)
{
    publish_video_heartbeat_internal(0);
}

static void publish_video_heartbeat_force(void)
{
    publish_video_heartbeat_internal(1);
}

static void publish_video_heartbeat_source_arm(void)
{
    int need_arm;
    pthread_mutex_lock(&g_heartbeat_lock);
    need_arm = (g_last_heartbeat_ms == 0);
    pthread_mutex_unlock(&g_heartbeat_lock);
    if (need_arm) publish_video_heartbeat_force();
}

static void clear_video_heartbeat(void)
{
    pthread_mutex_lock(&g_heartbeat_lock);
    g_last_heartbeat_ms = 0;
    (void)unlink(g_heartbeat_path);
    pthread_mutex_unlock(&g_heartbeat_lock);
}

static void clear_video_observer(void)
{
    g_streaming = 0;
    g_video_config_seen = 0;
    clear_video_heartbeat();
}

static int env_i(const char *name, int defv)
{
    const char *s = getenv(name);
    return (s && *s) ? atoi(s) : defv;
}

static void env_s(const char *name, char *dst, size_t cap, const char *defv)
{
    const char *s = getenv(name);
    if (!s || !*s) s = defv;
    snprintf(dst, cap, "%s", s);
}

static CFStringRef s_cf(const char *s);

/*
 * Persistent A/B controls are intentionally file-based so the vehicle can
 * switch protocol variants without replacing/recompiling the injected binary.
 */
static int read_trimmed_value(const char *path, char *out, size_t cap)
{
    int fd;
    ssize_t n;
    if (!path || !out || cap < 2u) return -1;
    out[0] = '\0';
    fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    n = read(fd, out, cap - 1u);
    close(fd);
    if (n <= 0) return -1;
    out[n] = '\0';
    while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r' ||
                     out[n-1] == ' ' || out[n-1] == '\t'))
        out[--n] = '\0';
    return n > 0 ? 0 : -1;
}

static void load_safearea_config(int *x, int *y, int *w, int *h)
{
    char b[192];
    int fd, n;
    int tx=0, ty=0, tw=g_width, th=g_height;

    if(!x||!y||!w||!h) return;
    *x=0; *y=0; *w=g_width; *h=g_height;

    fd=open(g_safearea_config_path,O_RDONLY);
    if(fd<0) return;
    n=(int)read(fd,b,sizeof(b)-1u);
    close(fd);
    if(n<=0) return;
    b[n]='\0';

    if(sscanf(b,"x=%d\ny=%d\nw=%d\nh=%d",&tx,&ty,&tw,&th)!=4){
        logf_u2("GEN2 SafeArea config invalid path=%s; using full canvas",
                g_safearea_config_path);
        return;
    }

    if(tx<0||ty<0||tw<1||th<1||tx>=g_width||ty>=g_height||
       tx+tw>g_width||ty+th>g_height){
        logf_u2("GEN2 SafeArea config out-of-range x=%d y=%d w=%d h=%d full=%dx%d; using full canvas",
                tx,ty,tw,th,g_width,g_height);
        return;
    }

    *x=tx; *y=ty; *w=tw; *h=th;
}

static const char *nav_surface_url(char *surface, size_t surface_cap)
{
    char mode[32];

    /*
     * Volatile vehicle-test override has highest priority for backwards
     * compatibility with the existing A/B harness.
     */
    if (read_trimmed_value(g_url_mode_path, mode, sizeof(mode)) == 0) {
        if (strcmp(mode, "base") == 0) {
            snprintf(surface, surface_cap, "%s", "base");
            return ALT_URL_DEFAULT;
        }
        if (strcmp(mode, "map") == 0) {
            snprintf(surface, surface_cap, "%s", "map");
            return ALT_URL_MAP;
        }
        if (strcmp(mode, "instructioncard") == 0) {
            snprintf(surface, surface_cap, "%s", "instructioncard");
            return ALT_URL_INSTRUCTIONCARD;
        }
    }

    if (read_trimmed_value(g_nav_surface_path, mode, sizeof(mode)) == 0) {
        if (strcmp(mode, "base") == 0) {
            snprintf(surface, surface_cap, "%s", "base");
            return ALT_URL_DEFAULT;
        }
        if (strcmp(mode, "map") == 0) {
            snprintf(surface, surface_cap, "%s", "map");
            return ALT_URL_MAP;
        }
        if (strcmp(mode, "instructioncard") == 0) {
            snprintf(surface, surface_cap, "%s", "instructioncard");
            return ALT_URL_INSTRUCTIONCARD;
        }
    }

    /* Preserve the earlier persistent base/map A/B marker as fallback. */
    if (access(g_url_map_marker, F_OK) == 0) {
        snprintf(surface, surface_cap, "%s", "map");
        return ALT_URL_MAP;
    }
    snprintf(surface, surface_cap, "%s", "base");
    return g_alt_url;
}

static void nav_tristate_value(const char *path, char *out, size_t cap,
                               const char *defv)
{
    char value[24];
    if (read_trimmed_value(path, value, sizeof(value)) == 0 &&
        (strcmp(value, "yes") == 0 || strcmp(value, "no") == 0 ||
         strcmp(value, "user") == 0 || strcmp(value, "1") == 0 ||
         strcmp(value, "0") == 0)) {
        snprintf(out, cap, "%s", value);
        return;
    }
    snprintf(out, cap, "%s", defv);
}

static void nav_maneuver_value(char *out, size_t cap)
{
    char value[32];
    if (read_trimmed_value(g_nav_maneuver_path, value, sizeof(value)) != 0 ||
        strcmp(value, "none") == 0 || value[0] == '\0') {
        out[0] = '\0';
        return;
    }
    if (strcmp(value, "left") == 0 || strcmp(value, "leftAligned") == 0) {
        snprintf(out, cap, "%s", "leftAligned");
        return;
    }
    if (strcmp(value, "right") == 0 || strcmp(value, "rightAligned") == 0) {
        snprintf(out, cap, "%s", "rightAligned");
        return;
    }
    if (strcmp(value, "top") == 0 || strcmp(value, "topAligned") == 0) {
        snprintf(out, cap, "%s", "topAligned");
        return;
    }
    out[0] = '\0';
}

static void active_alt_url_copy(char *out, size_t cap)
{
    char surface[24];
    char eta[24], speed[24], compass[24], maneuver[32];
    const char *base;

    if (!out || cap == 0u) return;
    out[0] = '\0';
    base = nav_surface_url(surface, sizeof(surface));

    /*
     * Compatibility/default mode: keep the exact historical bare URL.
     * Apple Simulator observations also keep instructioncard bare even when
     * map/base appearance query parameters are in use.
     */
    if (access(g_nav_query_enable_marker, F_OK) != 0 ||
        strcmp(surface, "instructioncard") == 0) {
        snprintf(out, cap, "%s", base);
        return;
    }

    nav_tristate_value(g_nav_speed_path, speed, sizeof(speed), "user");
    nav_tristate_value(g_nav_compass_path, compass, sizeof(compass), "user");
    nav_tristate_value(g_nav_eta_path, eta, sizeof(eta), "yes");
    nav_maneuver_value(maneuver, sizeof(maneuver));

    snprintf(out, cap,
             "%s?showSpeedLimit=%s&showCompass=%s&showETA=%s&maneuverLayout=%s",
             base, speed, compass, eta, maneuver);
}

/* 0 = preserve stock, +1 = force bit 26, -1 = clear bit 26. */
static int airplay_bit26_mode(void)
{
    if (access(g_bit26_on_marker, F_OK) == 0) return 1;
    if (access(g_bit26_off_marker, F_OK) == 0) return -1;
    return 0;
}

static void apply_airplay_bit26_ab(CFMutableDictionaryRef info)
{
    CFStringRef k;
    OSStatus err = 0;
    int mode;
    int64_t before, after;

    if (!info) return;
    mode = airplay_bit26_mode();
    if (!mode) return;

    k = s_cf("features");
    if (!k) return;
    before = p_CFDictionaryGetInt64(info, k, &err);
    if (err) {
        logf_u2("GEN2 A/B bit26 mode=%s root features unavailable; unchanged",
                mode > 0 ? "force-on" : "force-off");
        p_CFRelease(k);
        return;
    }

    after = (int64_t)(mode > 0
        ? ((uint64_t)before | AIRPLAY_FEATURE_BIT26)
        : ((uint64_t)before & ~AIRPLAY_FEATURE_BIT26));
    if (after != before) p_CFDictionarySetInt64(info, k, after);
    logf_u2("GEN2 A/B bit26 mode=%s features=0x%llx->0x%llx",
            mode > 0 ? "force-on" : "force-off",
            (unsigned long long)(uint64_t)before,
            (unsigned long long)(uint64_t)after);
    p_CFRelease(k);
}

static void *sym_next(const char *name)
{
    void *p = dlsym(RTLD_NEXT, name);
    if (!p) p = dlsym(RTLD_DEFAULT, name);
    return p;
}

static int init_api(void)
{
#define RESOLVE(dst, name) do { dst = (void *)sym_next(name); if (!(dst)) { logf_u2("missing symbol %s", name); return -1; } } while (0)
    RESOLVE(g_real_serverinfo, "AirPlayCopyServerInfo");
    RESOLVE(g_sendcmd, "AirPlayReceiverSessionSendCommand");
    RESOLVE(g_real_aes_cbc_init, "AES_CBCFrame_Init");
    RESOLVE(g_aes_ctr_init, "AES_CTR_Init");
    RESOLVE(g_aes_ctr_update, "AES_CTR_Update");
    RESOLVE(g_aes_ctr_final, "AES_CTR_Final");
    RESOLVE(g_derive_screen, "AirPlay_DeriveAESKeySHA512ForScreen");
    RESOLVE(p_CFStringCreateWithCString, "CFStringCreateWithCString");
    RESOLVE(p_CFStringGetCStringPtr, "CFStringGetCStringPtr");
    RESOLVE(p_CFStringGetCString, "CFStringGetCString");
    RESOLVE(p_CFGetTypeID, "CFGetTypeID");
    RESOLVE(p_CFStringGetTypeID, "CFStringGetTypeID");
    RESOLVE(p_CFDictionaryGetTypeID, "CFDictionaryGetTypeID");
    RESOLVE(p_CFArrayGetTypeID, "CFArrayGetTypeID");
    RESOLVE(p_CFNumberGetTypeID, "CFNumberGetTypeID");
    RESOLVE(p_CFBooleanGetTypeID, "CFBooleanGetTypeID");
    RESOLVE(p_CFDataGetTypeID, "CFDataGetTypeID");
    RESOLVE(p_CFGetInt64, "CFGetInt64");
    RESOLVE(p_CFBooleanGetValue, "CFBooleanGetValue");
    RESOLVE(p_CFDataGetLength, "CFDataGetLength");
    RESOLVE(p_CFDataGetBytePtr, "CFDataGetBytePtr");
    RESOLVE(p_CFDictionaryGetCount, "CFDictionaryGetCount");
    RESOLVE(p_CFDictionaryGetKeysAndValues, "CFDictionaryGetKeysAndValues");
    RESOLVE(p_CFDictionaryGetValue, "CFDictionaryGetValue");
    RESOLVE(p_CFDictionarySetValue, "CFDictionarySetValue");
    RESOLVE(p_CFDictionaryRemoveValue, "CFDictionaryRemoveValue");
    RESOLVE(p_CFDictionaryGetInt64, "CFDictionaryGetInt64");
    RESOLVE(p_CFDictionarySetInt64, "CFDictionarySetInt64");
    RESOLVE(p_CFDictionaryCreateMutable, "CFDictionaryCreateMutable");
    RESOLVE(p_CFArrayGetCount, "CFArrayGetCount");
    RESOLVE(p_CFArrayGetValueAtIndex, "CFArrayGetValueAtIndex");
    RESOLVE(p_CFArrayAppendValue, "CFArrayAppendValue");
    RESOLVE(p_CFArrayCreateMutable, "CFArrayCreateMutable");
    RESOLVE(p_CFArrayCreateMutableCopy, "CFArrayCreateMutableCopy");
    RESOLVE(p_CFRetain, "CFRetain");
    RESOLVE(p_CFRelease, "CFRelease");
    p_dict_key_callbacks = dlsym(RTLD_DEFAULT, "kCFLDictionaryKeyCallBacksCFLTypes");
    p_dict_val_callbacks = dlsym(RTLD_DEFAULT, "kCFLDictionaryValueCallBacksCFLTypes");
    p_array_callbacks = dlsym(RTLD_DEFAULT, "kCFLArrayCallBacksCFLTypes");
    {
        CFTypeRef *false_slot = (CFTypeRef *)dlsym(RTLD_DEFAULT, "kCFLBooleanFalse");
        p_cfl_boolean_false = false_slot ? *false_slot : NULL;
    }
    if (!p_dict_key_callbacks || !p_dict_val_callbacks || !p_array_callbacks ||
        !p_cfl_boolean_false) {
        logf_u2("missing CFLite callback tables/boolean singleton");
        return -1;
    }
    g_security_fn = (uintptr_t)sym_next("AirPlayReceiverSessionSetSecurityInfo");
    if (!g_security_fn) { logf_u2("missing AirPlayReceiverSessionSetSecurityInfo"); return -1; }
#undef RESOLVE
    return 0;
}

static CFStringRef s_cf(const char *s)
{
    return p_CFStringCreateWithCString(NULL, s, CF_UTF8);
}

static CFMutableDictionaryRef dict_new(void)
{
    return p_CFDictionaryCreateMutable(NULL, 0, p_dict_key_callbacks, p_dict_val_callbacks);
}

static CFMutableDictionaryRef dict_clone(CFDictionaryRef src)
{
    CFMutableDictionaryRef dst;
    CFIndex n, i;
    const void **keys, **vals;
    if (!src) return NULL;
    dst = dict_new();
    if (!dst) return NULL;
    n = p_CFDictionaryGetCount(src);
    if (n <= 0) return dst;
    keys = calloc((size_t)n, sizeof(*keys));
    vals = calloc((size_t)n, sizeof(*vals));
    if (!keys || !vals) { free(keys); free(vals); p_CFRelease(dst); return NULL; }
    p_CFDictionaryGetKeysAndValues(src, keys, vals);
    for (i = 0; i < n; ++i) p_CFDictionarySetValue(dst, keys[i], vals[i]);
    free(keys); free(vals);
    return dst;
}

static int stream_type(CFDictionaryRef d)
{
    OSStatus e = 0;
    CFStringRef k = s_cf("type");
    int64_t v = p_CFDictionaryGetInt64(d, k, &e);
    p_CFRelease(k);
    return e ? -1 : (int)v;
}

static uint64_t stream_connection_id(CFDictionaryRef d)
{
    OSStatus e = 0;
    CFStringRef k = s_cf("streamConnectionID");
    int64_t v = p_CFDictionaryGetInt64(d, k, &e);
    p_CFRelease(k);
    return e ? 0 : (uint64_t)v;
}

static CFArrayRef get_streams(CFDictionaryRef d)
{
    CFStringRef k;
    CFArrayRef a;
    if (!d) return NULL;
    k = s_cf("streams");
    a = (CFArrayRef)p_CFDictionaryGetValue(d, k);
    p_CFRelease(k);
    return a;
}

static int contains_stream_type(CFDictionaryRef request, int wanted)
{
    CFArrayRef a = get_streams(request);
    CFIndex i, n;
    if (!a) return 0;
    n = p_CFArrayGetCount(a);
    for (i = 0; i < n; ++i) {
        CFDictionaryRef sd = (CFDictionaryRef)p_CFArrayGetValueAtIndex(a, i);
        if (stream_type(sd) == wanted) return 1;
    }
    return 0;
}

static int contains_stream111(CFDictionaryRef request, CFDictionaryRef *outDesc, int *outOtherCount)
{
    CFArrayRef a = get_streams(request);
    CFIndex i, n;
    int found = 0, other = 0;
    if (outDesc) *outDesc = NULL;
    if (!a) { if (outOtherCount) *outOtherCount = 0; return 0; }
    n = p_CFArrayGetCount(a);
    for (i = 0; i < n; ++i) {
        CFDictionaryRef sd = (CFDictionaryRef)p_CFArrayGetValueAtIndex(a, i);
        if (stream_type(sd) == K_STREAM_ALT) {
            found = 1;
            if (outDesc && !*outDesc) *outDesc = sd;
        } else ++other;
    }
    if (outOtherCount) *outOtherCount = other;
    return found;
}

static CFMutableDictionaryRef clone_without_111(CFDictionaryRef request)
{
    CFMutableDictionaryRef d = dict_clone(request);
    CFArrayRef a = get_streams(request);
    CFMutableArrayRef b;
    CFStringRef k;
    CFIndex i, n;
    if (!d || !a) return d;
    b = p_CFArrayCreateMutable(NULL, 0, p_array_callbacks);
    if (!b) return d;
    n = p_CFArrayGetCount(a);
    for (i = 0; i < n; ++i) {
        CFDictionaryRef sd = (CFDictionaryRef)p_CFArrayGetValueAtIndex(a, i);
        if (stream_type(sd) != K_STREAM_ALT) p_CFArrayAppendValue(b, sd);
    }
    k = s_cf("streams");
    p_CFDictionarySetValue(d, k, b);
    p_CFRelease(k);
    p_CFRelease(b);
    return d;
}

/*
 * Recovered MHI2Q IRC behavior: every successful /info or SETUP response
 * advertises the AltScreen capability at the root before the peer has to
 * choose stream 111. The reference replaces enabledFeatures with
 * ["altScreen","viewAreas"] rather than waiting for a 111 request.
 */
#ifdef ALT111_TARGET_MHI2Q
/* Stock dio_manager's inbound TCP ports on carplay0 (RTSP, events, stream 110). */
static const int g_stock_ports[] = { 5000, 5001, 6030 };
/* PF-permitted ports stock was never seen using, tried in order if one won't bind. */
static const int g_alt_port_fallbacks[] = { 7100, 6200, 6100, 5010, 7001, 7000 };
#endif

static int stock_port_reserved(int port)
{
#ifdef ALT111_TARGET_MHI2Q
    size_t i;
    for(i=0;i<sizeof(g_stock_ports)/sizeof(g_stock_ports[0]);++i)
        if(g_stock_ports[i]==port) return 1;
#else
    (void)port;
#endif
    return 0;
}

static int current_alt_port(void)
{
    char b[16];
    int v;
    if(read_trimmed_value(g_port_override_path,b,sizeof(b))!=0) v=g_alt_port;
    else {
        v=atoi(b);
        if(v<=0 || v>=65536){
            logf_u2("stream111 port override %s invalid ('%s'); keeping %d",
                    g_port_override_path,b,g_alt_port);
            v=g_alt_port;
        }
    }
    if(stock_port_reserved(v)){
        logf_u2("stream111 port %d is a stock CarPlay port (6030 = main screen stream 110); "
                "using %d instead", v, ALT111_DEFAULT_PORT);
        return ALT111_DEFAULT_PORT;
    }
    return v;
}

/* Build-time/env gate, overridable per session by the marker file. */
static int advertise_enabled(void)
{
    if(!g_advertise) return 0;
    if(access(g_advertise_off_marker,F_OK)==0) return 0;
    return 1;
}

static int current_advertised_fps(void)
{
    char b[16];
    int fd, v;
    ssize_t n;

    fd=open(g_fps_override_path,O_RDONLY);
    if(fd<0)return g_fps;
    n=read(fd,b,sizeof(b)-1);
    close(fd);
    if(n<=0)return g_fps;
    b[n]='\0';
    v=atoi(b);
    if(v==20||v==25||v==30)return v;
    return g_fps;
}

/* ASCII compare of a CFString. Local so this works before the flight helpers. */
static int cf_is_ascii(CFStringRef v, const char *want)
{
    char buf[64];
    const char *p;
    if(!v || !want) return 0;
    p = p_CFStringGetCStringPtr(v, CF_UTF8);
    if(!p){
        if(!p_CFStringGetCString(v, buf, (CFIndex)sizeof(buf), CF_UTF8)) return 0;
        p = buf;
    }
    return strcmp(p, want) == 0;
}

/*
 * MERGE our capability flags into the existing enabledFeatures array.
 *
 * This used to build a fresh array and overwrite the key, which DESTROYED every
 * feature stock advertised. A sender that needs one of those features then fails
 * to negotiate -- observed on this platform as wireless CarPlay (dongle) no
 * longer connecting, and as the phone never requesting stream 111 (so the
 * cluster kept showing only the maneuver arrow). Preserve stock, append ours
 * only when absent, and never duplicate.
 */
static void set_reference_enabled_features(CFMutableDictionaryRef response)
{
    CFStringRef k = NULL, alt = NULL, va = NULL;
    CFTypeRef existing;
    CFMutableArrayRef a = NULL;
    CFIndex i, stock_n = 0;
    int have_alt = 0, have_va = 0, changed = 0;

    if(!response) return;
    k = s_cf("enabledFeatures");
    if(!k) return;

    existing = (CFTypeRef)p_CFDictionaryGetValue(response, k);
    if(existing && p_CFGetTypeID(existing) == p_CFArrayGetTypeID()){
        a = p_CFArrayCreateMutableCopy(NULL, 0, (CFArrayRef)existing);
        if(a) stock_n = p_CFArrayGetCount(a);
    }else{
        a = p_CFArrayCreateMutable(NULL, 0, p_array_callbacks);
    }
    if(!a){ p_CFRelease(k); return; }

    for(i = 0; i < stock_n; ++i){
        CFTypeRef it = p_CFArrayGetValueAtIndex(a, i);
        if(!it || p_CFGetTypeID(it) != p_CFStringGetTypeID()) continue;
        if(cf_is_ascii((CFStringRef)it, "altScreen")) have_alt = 1;
        else if(cf_is_ascii((CFStringRef)it, "viewAreas")) have_va = 1;
    }

    if(!have_alt){
        alt = s_cf("altScreen");
        if(alt){ p_CFArrayAppendValue(a, alt); changed = 1; }
    }
    if(g_viewareas && !have_va){
        va = s_cf("viewAreas");
        if(va){ p_CFArrayAppendValue(a, va); changed = 1; }
    }

    /* Only write the key back when we actually added something, or when stock
     * had no (array) value at all. Never replace a stock array with a copy for
     * no reason. */
    if(changed || !existing || p_CFGetTypeID(existing) != p_CFArrayGetTypeID())
        p_CFDictionarySetValue(response, k, a);

    logf_u2("GEN2 enabledFeatures merge: stockEntries=%ld altScreen=%s viewAreas=%s total=%ld",
            (long)stock_n,
            have_alt ? "already-present" : (alt ? "appended" : "FAILED"),
            g_viewareas ? (have_va ? "already-present" : (va ? "appended" : "FAILED"))
                        : "disabled",
            (long)p_CFArrayGetCount(a));

    if(va) p_CFRelease(va);
    if(alt) p_CFRelease(alt);
    p_CFRelease(a);
    p_CFRelease(k);
}

/* Log what stock advertised, so a sender-negotiation failure is diagnosable. */
static void log_stock_enabled_features(CFDictionaryRef base)
{
    CFStringRef k;
    CFTypeRef v;
    CFIndex i, n;
    if(!base) return;
    k = s_cf("enabledFeatures");
    if(!k) return;
    v = (CFTypeRef)p_CFDictionaryGetValue(base, k);
    p_CFRelease(k);
    if(!v){ logf_u2("GEN2 stock /info enabledFeatures=<absent>"); return; }
    if(p_CFGetTypeID(v) != p_CFArrayGetTypeID()){
        logf_u2("GEN2 stock /info enabledFeatures=<non-array>");
        return;
    }
    n = p_CFArrayGetCount((CFArrayRef)v);
    logf_u2("GEN2 stock /info enabledFeatures count=%ld", (long)n);
    for(i = 0; i < n && i < 12; ++i){
        CFTypeRef it = p_CFArrayGetValueAtIndex((CFArrayRef)v, i);
        char b[64];
        const char *pp = NULL;
        if(it && p_CFGetTypeID(it) == p_CFStringGetTypeID()){
            pp = p_CFStringGetCStringPtr((CFStringRef)it, CF_UTF8);
            if(!pp && p_CFStringGetCString((CFStringRef)it, b, (CFIndex)sizeof(b), CF_UTF8))
                pp = b;
        }
        logf_u2("GEN2 stock /info enabledFeatures[%ld]=%s", (long)i, pp ? pp : "<non-string>");
    }
}

/* Clone the exact requested 111 descriptor and preserve unknown peer fields. */
static void append_alt_setup_response(CFMutableDictionaryRef response,
                                      CFDictionaryRef requested,
                                      int data_port)
{
    CFStringRef kstreams = NULL, kport = NULL, kstreamid = NULL;
    CFArrayRef old = NULL;
    CFMutableArrayRef a = NULL;
    CFMutableDictionaryRef sd = NULL;

    if(!response || !requested) return;
    kstreams = s_cf("streams");
    old = (CFArrayRef)p_CFDictionaryGetValue(response,kstreams);
    a = old ? p_CFArrayCreateMutableCopy(NULL,0,old)
            : p_CFArrayCreateMutable(NULL,0,p_array_callbacks);
    sd = dict_clone(requested);
    kport = s_cf("dataPort");
    kstreamid = s_cf("streamID");

    if(a && sd && kport && kstreamid) {
        p_CFDictionarySetInt64(sd,kport,data_port);
        p_CFDictionarySetInt64(sd,kstreamid,K_STREAM_ALT);
        p_CFArrayAppendValue(a,sd);
        p_CFDictionarySetValue(response,kstreams,a);
    }

    if(sd) p_CFRelease(sd);
    if(a) p_CFRelease(a);
    if(kstreamid) p_CFRelease(kstreamid);
    if(kport) p_CFRelease(kport);
    if(kstreams) p_CFRelease(kstreams);
}

/*
 * Two deliberately different network boundaries:
 *  - stream 111 is inbound from the iPhone and therefore must be reachable on
 *    the CarPlay link (INADDR_ANY / actual head-unit address);
 *  - the decoded H.264 tee is local-only and must never leave loopback.
 *
 * Only the iPhone-facing listener may fall back to an ephemeral port because
 * its actual dataPort is returned in SETUP. The renderer tee is configured by
 * URL and must fail closed if its fixed port is occupied.
 */
static int bind_listener_ipv4(int preferred, int *out_port, int loopback, int allow_ephemeral)
{
    int fd, one = 1;
    struct sockaddr_in sa;
    socklen_t sl = sizeof(sa);
    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(loopback ? INADDR_LOOPBACK : INADDR_ANY);
    sa.sin_port = htons((uint16_t)preferred);
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        if (!allow_ephemeral) { close(fd); return -1; }
        sa.sin_port = 0;
        if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) { close(fd); return -1; }
    }
    if (listen(fd, 2) < 0) { close(fd); return -1; }
    if (getsockname(fd, (struct sockaddr *)&sa, &sl) == 0 && out_port) *out_port = ntohs(sa.sin_port);
    return fd;
}

static int bind_listener_stream111(int preferred, int *out_port, int allow_ephemeral)
{
#ifdef AF_INET6
    int fd,one=1,off=0;
    struct sockaddr_in6 sa6;
    socklen_t sl6=sizeof(sa6);
    fd=socket(AF_INET6,SOCK_STREAM,IPPROTO_TCP);
    if(fd>=0){
        setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
#ifdef IPV6_V6ONLY
        (void)setsockopt(fd,IPPROTO_IPV6,IPV6_V6ONLY,&off,sizeof(off));
#endif
        memset(&sa6,0,sizeof(sa6));
        sa6.sin6_family=AF_INET6;
        sa6.sin6_port=htons((uint16_t)preferred);
        if(bind(fd,(struct sockaddr *)&sa6,sizeof(sa6))<0){
            if(allow_ephemeral){
                sa6.sin6_port=0;
                if(bind(fd,(struct sockaddr *)&sa6,sizeof(sa6))<0){close(fd);fd=-1;}
            }else{
                close(fd);
                fd=-1;
            }
        }
        if(fd>=0){
            if(listen(fd,1)==0){
                if(getsockname(fd,(struct sockaddr *)&sa6,&sl6)==0 && out_port)*out_port=ntohs(sa6.sin6_port);
                g_alt_listener_ipv6=1;
                return fd;
            }
            close(fd);
        }
    }
#endif
    g_alt_listener_ipv6=0;
    return bind_listener_ipv4(preferred,out_port,0,allow_ephemeral);
}

static CFMutableDictionaryRef command_force_keyframe(void);

static void set_active_session(AirPlayReceiverSessionRef s)
{
    AirPlayReceiverSessionRef old = NULL, keep = NULL;

    if (s) keep = (AirPlayReceiverSessionRef)p_CFRetain(s);
    pthread_mutex_lock(&g_lock);
    if (g_active_session == s) {
        pthread_mutex_unlock(&g_lock);
        if (keep) p_CFRelease(keep);
        return;
    }
    old = g_active_session;
    g_active_session = keep;
    pthread_mutex_unlock(&g_lock);
    if (old) p_CFRelease(old);
}

static AirPlayReceiverSessionRef retain_active_session(void)
{
    AirPlayReceiverSessionRef s = NULL;
    pthread_mutex_lock(&g_lock);
    if (g_active_session) s = (AirPlayReceiverSessionRef)p_CFRetain(g_active_session);
    pthread_mutex_unlock(&g_lock);
    return s;
}

/* ---------------- Gen-2 video adapter ---------------- */

static void gen2_close_consumer(void)
{
    pthread_mutex_lock(&g_lock);
    tee_drop_client_locked();
    pthread_mutex_unlock(&g_lock);

    pthread_mutex_lock(&g2_core_lock);
    alt111_video_detach(&g2_video);
    alt111_resync_cancel(&g2_resync, ALT111_RESYNC_CANCEL_CONSUMER);
    pthread_cond_broadcast(&g2_core_cv);
    pthread_mutex_unlock(&g2_core_lock);
}

static void gen2_video_begin_current(void)
{
    uint64_t session;
    gen2_close_consumer();
    pthread_mutex_lock(&g2_core_lock);
    alt111_resync_cancel(&g2_resync, ALT111_RESYNC_CANCEL_STREAM);
    session = g2_control_session ? g2_control_session : (g2_control.session + 1u);
    g2_video_stream = alt111_video_begin(&g2_video, session);
    pthread_cond_broadcast(&g2_core_cv);
    pthread_mutex_unlock(&g2_core_lock);
    gen2_publish_status();
}

static void gen2_video_end_current(void)
{
    pthread_mutex_lock(&g2_core_lock);
    alt111_resync_cancel(&g2_resync, ALT111_RESYNC_CANCEL_STREAM);
    if (g2_video_stream) (void)alt111_video_end(&g2_video, g2_video_stream);
    g2_video_stream = 0;
    pthread_cond_broadcast(&g2_core_cv);
    pthread_mutex_unlock(&g2_core_lock);

    pthread_mutex_lock(&g_lock);
    tee_drop_client_locked();
    pthread_mutex_unlock(&g_lock);

    pthread_mutex_lock(&g_capture_lock);
    capture_drop_client_locked();
    pthread_mutex_unlock(&g_capture_lock);
    capture_publish_status("listening");
    gen2_publish_status();
}

static int gen2_video_config(const uint8_t *p, size_t n)
{
    int rc;
    pthread_mutex_lock(&g2_core_lock);
    rc = g2_video_stream ? alt111_video_config(&g2_video, g2_video_stream, p, n) : ALT111_STALE;
    pthread_cond_broadcast(&g2_core_cv);
    pthread_mutex_unlock(&g2_core_lock);
    if (rc == ALT111_RESTART_CONSUMER || rc < 0) {
        logf_u2("gen2 VideoConfig rc=%d -> consumer reset", rc);
        gen2_close_consumer();
    }
    gen2_publish_status();
    return rc;
}

static int gen2_video_submit_au(const uint8_t *p, size_t n, int *accepted, int *consumer_attached)
{
    int rc;
    uint64_t before, after_count;
    if (accepted) *accepted = 0;
    if (consumer_attached) *consumer_attached = 0;
    pthread_mutex_lock(&g2_core_lock);
    before = g2_video.source_aus;
    rc = g2_video_stream ? alt111_video_submit(&g2_video, g2_video_stream, p, n, 1) : ALT111_STALE;
    after_count = g2_video.source_aus;
    if (accepted && after_count > before) *accepted = 1;
    if (consumer_attached) *consumer_attached = g2_video.attached ? 1 : 0;
    pthread_cond_broadcast(&g2_core_cv);
    pthread_mutex_unlock(&g2_core_lock);
    if (rc == ALT111_RESTART_CONSUMER || rc < 0) {
        logf_u2("gen2 video AU rc=%d body=%zu -> consumer reset", rc, n);
        gen2_close_consumer();
    }
    return rc;
}

static void gen2_keyframe_intent(void)
{
    int rc = ALT111_WAIT;
    pthread_mutex_lock(&g2_core_lock);
    if (g2_control_session)
        rc = alt111_control_keyframe(&g2_control, g2_control_session);
    pthread_cond_broadcast(&g2_core_cv);
    pthread_mutex_unlock(&g2_core_lock);
    logf_u2("gen2 consumer keyframe intent rc=%d", rc);
}

static void gen2_consumer_attach(void)
{
    int rc;
    uint64_t generation = 0;
    pthread_mutex_lock(&g2_core_lock);
    if (g2_video.attached) alt111_video_detach(&g2_video);
    rc = alt111_video_attach(&g2_video);
    if (rc == ALT111_OK) generation = g2_video.consumer;
    pthread_cond_broadcast(&g2_core_cv);
    pthread_mutex_unlock(&g2_core_lock);

    if (rc == ALT111_OK) {
        pthread_mutex_lock(&g_lock);
        if (g_tee_client >= 0) g_tee_consumer_generation = generation;
        pthread_mutex_unlock(&g_lock);
    }
    logf_u2("gen2 renderer consumer attach rc=%d generation=%llu",
            rc,(unsigned long long)generation);
    if (rc == ALT111_OK) gen2_keyframe_intent();
    gen2_publish_status();
}

static void *gen2_output_worker(void *arg)
{
    uint8_t *copy = NULL;
    size_t cap = 0;
    (void)arg;

    for (;;) {
        const uint8_t *p = NULL;
        size_t n = 0, chunk = 0;
        struct alt111_output_ticket ticket;
        int prc, fd, send_failed = 0, would_block = 0, stale_socket = 0;
        int arc = ALT111_WAIT, primed_before = 0, primed_after = 0;
        uint64_t delivered_before = 0, delivered_after = 0;
        ssize_t sent = 0;

        /*
         * Keep the core generation stable from the final peek through the
         * nonblocking socket write and advance. This closes the last race where
         * a codec/consumer generation could be invalidated after bytes were
         * copied but before they were sent.
         *
         * g_lock is nested only inside g2_core_lock here. All other adapter
         * paths release g_lock before acquiring g2_core_lock, so there is no
         * reverse nested order.
         */
        pthread_mutex_lock(&g2_core_lock);
        prc = alt111_video_peek(&g2_video, &p, &n, &ticket);
        if (prc != ALT111_OK || !n) {
            pthread_mutex_unlock(&g2_core_lock);
            usleep(5000);
            continue;
        }

        chunk = n > 65536u ? 65536u : n;
        if (cap < chunk) {
            uint8_t *next = (uint8_t *)realloc(copy, chunk);
            if (!next) {
                pthread_mutex_unlock(&g2_core_lock);
                usleep(10000);
                continue;
            }
            copy = next;
            cap = chunk;
        }
        memcpy(copy, p, chunk);

        delivered_before = g2_video.delivered_aus;
        primed_before = g2_video.consumer_primed;

        pthread_mutex_lock(&g_lock);
        fd = g_tee_client;
        if (fd >= 0 && g_tee_consumer_generation != ticket.consumer) {
            stale_socket = 1;
        } else if (fd >= 0) {
            do {
                sent = send(fd, copy, chunk, MSG_NOSIGNAL);
            } while (sent < 0 && errno == EINTR);

            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                would_block = 1;
            } else if (sent <= 0) {
                tee_drop_client_locked();
                send_failed = 1;
            }
        } else {
            send_failed = 1;
        }
        pthread_mutex_unlock(&g_lock);

        if (stale_socket) {
            pthread_mutex_unlock(&g2_core_lock);
            usleep(1000);
            continue;
        }

        if (would_block) {
            pthread_mutex_unlock(&g2_core_lock);
            usleep(5000);
            continue;
        }

        if (send_failed) {
            alt111_video_detach(&g2_video);
            pthread_cond_broadcast(&g2_core_cv);
            pthread_mutex_unlock(&g2_core_lock);
            gen2_publish_status();
            usleep(10000);
            continue;
        }

        /*
         * Passive capture mirror: copy only bytes that the primary pre-TS
         * consumer actually accepted. The mirror is nonblocking and may be
         * dropped on any backpressure/partial write; it can never delay or
         * detach the production consumer.
         */
        capture_mirror_send(copy, (size_t)sent, &ticket);

        arc = alt111_video_advance(&g2_video, &ticket, (size_t)sent);
        delivered_after = g2_video.delivered_aus;
        primed_after = g2_video.consumer_primed;
        pthread_mutex_unlock(&g2_core_lock);

        if (arc == ALT111_OK && delivered_after > delivered_before) {
            /*
             * Before a consumer attaches the source heartbeat is enough to let
             * Auto-Direct start the bridge. Once attached, only actual complete
             * AU delivery advances the heartbeat, so DIRECT cannot be declared
             * ready merely on non-IDR source traffic.
             *
             * The first priming AU bypasses the 250 ms throttle: a static map
             * may emit no second frame.
             */
            if (!primed_before && primed_after)
                publish_video_heartbeat_force();
            else
                publish_video_heartbeat();

            if (!primed_before || primed_after != primed_before ||
                (delivered_after & 7u) == 0u)
                gen2_publish_status();
        }
    }
    return NULL;
}

static int read_exact(int fd, void *buf, size_t len)
{
    uint8_t *p = (uint8_t *)buf;
    while (len) {
        ssize_t n = recv(fd, p, len, 0);
        if (n == 0) return 0;
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        p += n; len -= (size_t)n;
    }
    return 1;
}

static void tee_drop_client_locked(void)
{
    if (g_tee_client >= 0) close(g_tee_client);
    g_tee_client = -1;
    g_tee_consumer_generation = 0;
}

static void capture_drop_client_locked(void)
{
    if (g_capture_client >= 0) close(g_capture_client);
    g_capture_client = -1;
    g_capture_wait_idr = 1;
}

static void capture_mirror_send(const uint8_t *p, size_t n,
                                const struct alt111_output_ticket *ticket)
{
    ssize_t w;
    int drop = 0, publish = 0;

    if (!p || !n || !ticket) return;

    pthread_mutex_lock(&g_capture_lock);
    if (g_capture_client < 0) {
        pthread_mutex_unlock(&g_capture_lock);
        return;
    }

    if (g_capture_wait_idr) {
        if (!ticket->idr || ticket->offset != 0) {
            pthread_mutex_unlock(&g_capture_lock);
            return;
        }
        g_capture_wait_idr = 0;
        publish = 1;
    }

    do {
        w = send(g_capture_client, p, n, MSG_NOSIGNAL);
    } while (w < 0 && errno == EINTR);

    if (w != (ssize_t)n) {
        ++g_capture_drops;
        capture_drop_client_locked();
        drop = 1;
        publish = 1;
    } else {
        g_capture_bytes += (uint64_t)n;
        ++g_capture_chunks;
        if ((g_capture_chunks & 63u) == 0u) publish = 1;
    }
    pthread_mutex_unlock(&g_capture_lock);

    if (drop)
        logf_u2("GEN2 capture mirror dropped: nonblocking write rc=%ld expected=%zu; production unaffected",
                (long)w, n);
    if (publish) capture_publish_status(drop ? "dropped" : "recording");
}

static void *tee_accept_thread(void *arg)
{
    (void)arg;
    for (;;) {
        int c = accept(g_tee_listen, NULL, NULL);
        if (c < 0) { if (errno == EINTR) continue; sleep(1); continue; }
        {
            int flags = fcntl(c, F_GETFL, 0);
            if (flags >= 0) (void)fcntl(c, F_SETFL, flags | O_NONBLOCK);
        }
        pthread_mutex_lock(&g_lock);
        tee_drop_client_locked();
        g_tee_client = c;
        pthread_mutex_unlock(&g_lock);
        logf_u2("gen2 renderer connected nonblocking on 127.0.0.1:%d; waiting for config+complete IDR", g_tee_port);
        gen2_consumer_attach();
    }
    return NULL;
}

static int start_tee_server(void)
{
    int actual = 0;
    if (g_tee_started) return 0;
    g_tee_listen = bind_listener_ipv4(g_tee_port, &actual, 1, 0);
    if (g_tee_listen < 0) { logf_u2("cannot bind renderer tee port %d: %s", g_tee_port, strerror(errno)); return -1; }
    g_tee_port = actual;
    if (pthread_create(&g_tee_thread, NULL, tee_accept_thread, NULL) != 0) { close(g_tee_listen); g_tee_listen=-1; return -1; }
    pthread_detach(g_tee_thread);
    g_tee_started = 1;
    logf_u2("renderer tee listening on 127.0.0.1:%d", g_tee_port);
    return 0;
}

static void *capture_accept_thread(void *arg)
{
    (void)arg;
    for (;;) {
        int c = accept(g_capture_listen, NULL, NULL);
        uint8_t cfg[ALT111_CODEC_CAP];
        size_t cfg_n = 0, off = 0;

        if (c < 0) {
            if (errno == EINTR) continue;
            sleep(1);
            continue;
        }

        pthread_mutex_lock(&g2_core_lock);
        if (g2_video.config_valid && g2_video.config_length <= sizeof(cfg)) {
            cfg_n = g2_video.config_length;
            memcpy(cfg, g2_video.config, cfg_n);
        }
        pthread_mutex_unlock(&g2_core_lock);

        /*
         * Seed the standalone .h264 capture with current SPS/PPS when available.
         * This happens only in the diagnostic accept thread, never on the media
         * worker. A fresh forceKeyFrame is then requested and forwarding waits
         * for that IDR before recording pictures.
         */
        while (off < cfg_n) {
            ssize_t w = send(c, cfg + off, cfg_n - off, MSG_NOSIGNAL);
            if (w > 0) {
                off += (size_t)w;
                continue;
            }
            if (w < 0 && errno == EINTR) continue;
            break;
        }
        if (off != cfg_n) {
            close(c);
            logf_u2("GEN2 capture mirror seed failed bytes=%zu/%zu", off, cfg_n);
            continue;
        }

        {
            int flags = fcntl(c, F_GETFL, 0);
            if (flags >= 0) (void)fcntl(c, F_SETFL, flags | O_NONBLOCK);
        }

        pthread_mutex_lock(&g_capture_lock);
        capture_drop_client_locked();
        g_capture_client = c;
        g_capture_wait_idr = 1;
        g_capture_bytes = (uint64_t)cfg_n;
        g_capture_chunks = cfg_n ? 1u : 0u;
        g_capture_drops = 0;
        pthread_mutex_unlock(&g_capture_lock);

        capture_publish_status("waiting_idr");
        logf_u2("GEN2 capture mirror connected on 127.0.0.1:%d seed=%zu; requesting fresh IDR",
                g_capture_port, cfg_n);
        gen2_keyframe_intent();
    }
    return NULL;
}

static int start_capture_server(void)
{
    int actual = 0;
    if (g_capture_started) return 0;
    g_capture_listen = bind_listener_ipv4(g_capture_port, &actual, 1, 0);
    if (g_capture_listen < 0) {
        logf_u2("cannot bind diagnostic H264 capture port %d: %s",
                g_capture_port, strerror(errno));
        capture_publish_status("unavailable");
        return -1;
    }
    g_capture_port = actual;
    if (pthread_create(&g_capture_thread, NULL, capture_accept_thread, NULL) != 0) {
        close(g_capture_listen);
        g_capture_listen = -1;
        capture_publish_status("unavailable");
        return -1;
    }
    pthread_detach(g_capture_thread);
    g_capture_started = 1;
    capture_publish_status("listening");
    logf_u2("diagnostic H264 capture mirror listening on 127.0.0.1:%d", g_capture_port);
    return 0;
}

/*
 * Stop and join the previous stream-111 worker before its global AES/session
 * state can be reused. The worker owns the accepted client fd; stop only
 * shutdowns that fd to wake read_exact(), then joins the worker. This prevents
 * a late cleanup from an old CarPlay session finalising a newly-created AES
 * context after a fast reconnect.
 */
static void stop_alt_receiver(void)
{
    pthread_t t;
    int do_join = 0;

    pthread_mutex_lock(&g_lock);
    if (g_alt_client >= 0) (void)shutdown(g_alt_client, SHUT_RDWR);
    if (g_alt_listen >= 0) {
        (void)shutdown(g_alt_listen, SHUT_RDWR);
        close(g_alt_listen);
        g_alt_listen = -1;
    }
    if (g_alt_thread_started && !pthread_equal(pthread_self(), g_alt_thread)) {
        t = g_alt_thread;
        g_alt_thread_started = 0;
        do_join = 1;
    }
    pthread_mutex_unlock(&g_lock);

    if (do_join) (void)pthread_join(t, NULL);

    pthread_mutex_lock(&g_lock);
    if (!do_join && g_alt_client >= 0) {
        /* Constructor/failure fallback: no worker owns this descriptor. */
        close(g_alt_client);
        g_alt_client = -1;
    }
    if (g_alt_aes_valid) {
        g_aes_ctr_final(&g_alt_aes);
        g_alt_aes_valid = 0;
    }
    pthread_mutex_unlock(&g_lock);
    gen2_video_end_current();
}

static void *alt_receiver_thread(void *arg)
{
    int listen_fd = (int)(intptr_t)arg;
    int c;
    logf_u2("waiting for iPhone AltScreen connection on 0.0.0.0:%d", g_alt_port);
#ifdef ALT111_TARGET_MHI2Q
    /*
     * On MHI2Q the first question after SETUP is whether the iPhone can reach
     * this port (unit firewall / interface binding). Wait in 1 s slices so a
     * silent listener is logged and stop_alt_receiver() is observed within 1 s.
     */
    {
        unsigned waited = 0;
        for (;;) {
            fd_set rd; struct timeval tv; int n, current;
            pthread_mutex_lock(&g_lock); current = g_alt_listen; pthread_mutex_unlock(&g_lock);
            if (current != listen_fd) {
                logf_u2("MHI2Q stream111: listener closed before accept");
                return NULL;
            }
            /* QNX FD_SET indexes fds_bits with no range check (FD_SETSIZE is 256). */
            if (listen_fd < 0 || listen_fd >= FD_SETSIZE) break;
            FD_ZERO(&rd); FD_SET(listen_fd, &rd);
            tv.tv_sec = 1; tv.tv_usec = 0;
            n = select(listen_fd + 1, &rd, NULL, NULL, &tv);
            if (n > 0) {
                pthread_mutex_lock(&g_lock); current = g_alt_listen; pthread_mutex_unlock(&g_lock);
                if (current != listen_fd) {
                    logf_u2("MHI2Q stream111: listener closed before accept");
                    return NULL;
                }
                break;
            }
            if (n < 0 && errno != EINTR) {
                logf_u2("MHI2Q stream111: select failed: %s", strerror(errno));
                return NULL;
            }
            if (n == 0 && (++waited == 10u || waited % 60u == 0u))
                logf_u2("MHI2Q stream111: no iPhone connection on port %d after %us. "
                        "SETUP was answered, so the port is almost certainly not in the PF "
                        "allowlist for carplay0 (pfctl -sr): 5000:5001 5010 6000:6001 6030 "
                        "6100 6200 7000:7001 7100 (5000 5001 6030 are stock). Set a permitted "
                        "port with 'echo 6200 > /mnt/app/mibr-carplay111.port' and reconnect.",
                        g_alt_port, waited);
        }
    }
#endif
    c = accept(listen_fd, NULL, NULL);
    if (c < 0) { logf_u2("AltScreen accept failed: %s", strerror(errno)); return NULL; }
    pthread_mutex_lock(&g_lock); g_alt_client = c; pthread_mutex_unlock(&g_lock);
    logf_u2("AltScreen stream 111 connected");
    publish_state("connected");
    for (;;) {
        AirPlayScreenHeaderCompat h;
        uint8_t *body = NULL;
        int rr = read_exact(c, &h, sizeof(h));
        if (rr <= 0) break;
        if (h.bodySize > MAX_SCREEN_BODY) { logf_u2("reject bodySize=%u", h.bodySize); break; }
        if (h.bodySize) {
            body = malloc(h.bodySize);
            if (!body) break;
            rr = read_exact(c, body, h.bodySize);
            if (rr <= 0) { free(body); break; }
        }
        switch (h.opcode) {
            case K_SCREEN_VIDEO_CONFIG:
                if (body && h.bodySize) {
                    float source_w = 0.0f, source_h = 0.0f;
                    memcpy(&source_w, h.params + 8, sizeof(source_w));
                    memcpy(&source_h, h.params + 12, sizeof(source_h));
                    logf_u2("VideoConfig header source=%.1fx%.1f flags=0x%02x body=%u",
                            (double)source_w, (double)source_h,
                            (unsigned)h.smallParam[1], (unsigned)h.bodySize);
                    {
                        int vrc = gen2_video_config(body, h.bodySize);
                        if (vrc == ALT111_OK || vrc == ALT111_RESTART_CONSUMER) {
                            if (!g_video_config_seen) {
                                g_video_config_seen = 1;
                                publish_state("video_config");
                                logf_u2("gen2: stream111 valid transactional video config received");
                            }
                        } else if (vrc == ALT111_INVALID) {
                            g_video_config_seen = 0;
                            g_streaming = 0;
                            clear_video_heartbeat();
                            publish_state("video_config_invalid");
                            logf_u2("gen2: invalid VideoConfig revoked Stream111 readiness");
                        }
                    }
                }
                break;
            case K_SCREEN_VIDEO_FRAME:
                if (body && h.bodySize) {
                    if (!g_alt_aes_valid || g_aes_ctr_update(&g_alt_aes, body, h.bodySize, body) != K_NO_ERR) {
                        logf_u2("AES-CTR decrypt failed"); free(body); goto done;
                    }
                    if (!g_video_config_seen) {
                        logf_u2("video frame ignored before valid avcC config body=%u", (unsigned)h.bodySize);
                        break;
                    }
                    {
                        int accepted = 0, consumer_attached = 0;
                        int vrc = gen2_video_submit_au(body, h.bodySize, &accepted, &consumer_attached);
                        if (accepted) {
                            if (!g_streaming) {
                                g_streaming = 1;
                                publish_state("streaming");
                                logf_u2("gen2: first complete Screen VIDEO_FRAME accepted as AU");
                            }
                            /* Source activity only creates the initial arm
                             * heartbeat. It never advances that heartbeat while
                             * no local consumer exists; otherwise Auto-Direct
                             * could mistake pre-connect source traffic for
                             * post-connect decoder-ready output. */
                            if (!consumer_attached) publish_video_heartbeat_source_arm();
                        }
                        if (vrc == ALT111_INVALID) {
                            g_streaming = 0;
                            clear_video_heartbeat();
                            publish_state("video_invalid");
                            logf_u2("gen2: rejected VIDEO_FRAME body=%u as invalid complete AU; source readiness revoked",
                                    (unsigned)h.bodySize);
                        }
                    }
                }
                break;
            case K_SCREEN_KEEPALIVE:
            case K_SCREEN_KEEPALIVE_BODY:
            case K_SCREEN_IGNORE:
            case K_SCREEN_FORCE_KEYFRAME:
                break;
            default:
                logf_u2("unknown screen opcode=%u", h.opcode);
                break;
        }
        free(body);
    }
done:
    logf_u2("AltScreen stream 111 disconnected");
    clear_video_observer();
    publish_state("disconnected");
    pthread_mutex_lock(&g_lock);
    if (g_alt_client == c) g_alt_client = -1;
    close(c);
    if (g_alt_listen == listen_fd) {
        close(g_alt_listen);
        g_alt_listen = -1;
    }
    if (g_alt_aes_valid) { g_aes_ctr_final(&g_alt_aes); g_alt_aes_valid=0; }
    pthread_mutex_unlock(&g_lock);
    gen2_video_end_current();
    return NULL;
}

static int start_alt_receiver(uint64_t connection_id)
{
    uint8_t key[16], iv[16];
    int actual = 0;
    int allow_ephemeral = env_i("ALTSCREEN111_ALLOW_EPHEMERAL", ALT111_DEFAULT_ALLOW_EPHEMERAL);

    /* Complete previous worker teardown before reusing global session state. */
    stop_alt_receiver();
    clear_video_observer();

    /* Start the core generation before taking g_lock: gen2_video_begin_current()
     * deliberately closes/reset the local consumer and therefore takes g_lock. */
    gen2_video_begin_current();

    pthread_mutex_lock(&g_lock);
    if (!g_master_valid) {
        pthread_mutex_unlock(&g_lock);
        gen2_video_end_current();
        logf_u2("no captured session AES key; cannot start stream111");
        return -1;
    }

    g_derive_screen(g_master_key, 16, connection_id, key, iv);
    memset(&g_alt_aes, 0, sizeof(g_alt_aes));
    if (g_aes_ctr_init(&g_alt_aes, key, iv) != K_NO_ERR) {
        memset(key,0,sizeof(key)); memset(iv,0,sizeof(iv));
        pthread_mutex_unlock(&g_lock);
        gen2_video_end_current();
        return -1;
    }
    memset(key,0,sizeof(key)); memset(iv,0,sizeof(iv));
    g_alt_aes_valid = 1;

    /* iPhone-facing listener: reachable from CarPlay link, not loopback. */
    g_alt_port = current_alt_port();
    g_alt_listen = bind_listener_stream111(g_alt_port, &actual, allow_ephemeral);
#ifdef ALT111_TARGET_MHI2Q
    {
        size_t i;
        int tried = g_alt_port;
        for (i = 0; g_alt_listen < 0 &&
                    i < sizeof(g_alt_port_fallbacks) / sizeof(g_alt_port_fallbacks[0]); ++i) {
            int p = g_alt_port_fallbacks[i];
            if (p == g_alt_port) continue;
            logf_u2("stream111 port %d unavailable (%s); trying PF-permitted %d",
                    tried, strerror(errno), p);
            tried = p;
            g_alt_listen = bind_listener_stream111(p, &actual, 0);
        }
    }
#endif
    if (g_alt_listen < 0) {
        g_aes_ctr_final(&g_alt_aes);
        g_alt_aes_valid=0;
        pthread_mutex_unlock(&g_lock);
        gen2_video_end_current();
        logf_u2("cannot bind iPhone stream111 listener on port %d: %s"
#ifdef ALT111_TARGET_MHI2Q
                " (no ephemeral fallback on MHI2Q: PF only permits 5000:5001 5010 6000:6001"
                " 6030 6100 6200 7000:7001 7100 on carplay0, and 5000 5001 6030 are stock;"
                " every non-stock permitted port failed to bind)"
#endif
                , g_alt_port, strerror(errno));
        return -1;
    }
    g_alt_port = actual;
    if (pthread_create(&g_alt_thread, NULL, alt_receiver_thread, (void *)(intptr_t)g_alt_listen) != 0) {
        close(g_alt_listen);
        g_alt_listen=-1;
        g_aes_ctr_final(&g_alt_aes);
        g_alt_aes_valid=0;
        pthread_mutex_unlock(&g_lock);
        gen2_video_end_current();
        return -1;
    }
    g_alt_thread_started = 1;
    pthread_mutex_unlock(&g_lock);
    publish_state("listening");
    logf_u2("stream111 receiver ready: conn=%llu dataPort=%d transport=%s",
            (unsigned long long)connection_id,g_alt_port,
            g_alt_listener_ipv6?"IPv6-dualstack":"IPv4");
    return g_alt_port;
}

static void set_str(CFMutableDictionaryRef d, const char *key, const char *val)
{
    CFStringRef k=s_cf(key), v=s_cf(val); p_CFDictionarySetValue(d,k,v); p_CFRelease(v); p_CFRelease(k);
}

static void set_i64(CFMutableDictionaryRef d, const char *key, int64_t val)
{
    CFStringRef k=s_cf(key); p_CFDictionarySetInt64(d,k,val); p_CFRelease(k);
}

static void set_false(CFMutableDictionaryRef d, const char *key)
{
    CFStringRef k;
    if(!d || !key || !p_cfl_boolean_false) return;
    k=s_cf(key);
    if(k){p_CFDictionarySetValue(d,k,p_cfl_boolean_false);p_CFRelease(k);}
}

static void remove_key(CFMutableDictionaryRef d, const char *name)
{
    CFStringRef k;
    if(!d || !name) return;
    k=s_cf(name);
    if(k){p_CFDictionaryRemoveValue(d,k);p_CFRelease(k);}
}

/*
 * iOS 27.2 builds navigator cluster UI candidates and intersects them with the
 * receiver-advertised altScreenSuggestUIURLs list.  Stock MU1440 has no such
 * key because it has no auxiliary cluster display.  Our synthetic type-111
 * display must therefore advertise the roles it intentionally supports.
 *
 * This is capability metadata only: it does not issue showUI/stopUI, rebuild
 * Stream111, change codec state, or synthesize a provider suggestion.
 */
static void add_alt_suggest_ui_urls(CFMutableDictionaryRef alt)
{
    CFMutableArrayRef urls=NULL;
    CFStringRef key=NULL,base=NULL,map=NULL,card=NULL;
    if(!alt)return;

    urls=p_CFArrayCreateMutable(NULL,0,p_array_callbacks);
    if(!urls)return;

    base=s_cf(ALT_URL_DEFAULT);
    map=s_cf(ALT_URL_MAP);
    card=s_cf(ALT_URL_INSTRUCTIONCARD);
    if(!base||!map||!card)goto done;

    p_CFArrayAppendValue(urls,base);
    p_CFArrayAppendValue(urls,map);
    p_CFArrayAppendValue(urls,card);

    key=s_cf("altScreenSuggestUIURLs");
    if(!key)goto done;
    p_CFDictionarySetValue(alt,key,urls);

    logf_u2("GEN2 advertised altScreenSuggestUIURLs[0]=%s [1]=%s [2]=%s",
            ALT_URL_DEFAULT,ALT_URL_MAP,ALT_URL_INSTRUCTIONCARD);

done:
    if(key)p_CFRelease(key);
    if(card)p_CFRelease(card);
    if(map)p_CFRelease(map);
    if(base)p_CFRelease(base);
    if(urls)p_CFRelease(urls);
}

/*
 * Keep the recovered reference ViewArea/SafeArea structure, but do not import
 * the Audi-specific 420x330 safe window into the 1010x376 Skoda VC baseline.
 *
 * Vehicle-PoC policy: initially expose the complete secondary-display canvas
 * as both ViewArea and SafeArea. This lets iOS/the navigation app decide what
 * cluster UI it can render without us prematurely constraining overlays to a
 * narrow center strip. A smaller/tube-specific SafeArea can be added later
 * once the full-width and classic VC layouts have been measured on-car.
 */
static void add_reference_viewarea(CFMutableDictionaryRef alt)
{
    CFMutableDictionaryRef view=NULL,safe=NULL;
    CFMutableArrayRef areas=NULL;
    CFStringRef k=NULL;
    int safe_w,safe_h,safe_x,safe_y;

    if(!alt || g_width<=0 || g_height<=0) return;

    /*
     * Full-canvas remains the fail-safe default.  Vehicle-test overrides are
     * persistent plain-text integers so the iOS composition envelope can be
     * tuned without recompiling again.  Clamp every value to the advertised
     * 1010x376 display canvas; malformed/out-of-range files fall back safely.
     */
    load_safearea_config(&safe_x,&safe_y,&safe_w,&safe_h);

    view=dict_new();
    safe=dict_new();
    areas=p_CFArrayCreateMutable(NULL,0,p_array_callbacks);
    if(!view||!safe||!areas) goto done;

    set_i64(view,"widthPixels",g_width);
    set_i64(view,"heightPixels",g_height);
    set_i64(view,"originXPixels",0);
    set_i64(view,"originYPixels",0);

    set_i64(safe,"widthPixels",safe_w);
    set_i64(safe,"heightPixels",safe_h);
    set_i64(safe,"originXPixels",safe_x);
    set_i64(safe,"originYPixels",safe_y);

    /*
     * These are ViewArea policy booleans, not numeric zero values. MIBSI
     * serializes both as real CFBoolean false objects.
     */
    set_false(view,"drawUIOutsideSafeArea");
    set_false(view,"viewAreaTransitionControl");

    k=s_cf("safeArea");
    p_CFDictionarySetValue(view,k,safe);
    p_CFRelease(k); k=NULL;

    p_CFArrayAppendValue(areas,view);
    k=s_cf("viewAreas");
    p_CFDictionarySetValue(alt,k,areas);
    p_CFRelease(k); k=NULL;
    set_i64(alt,"initialViewArea",0);
    logf_u2("GEN2 ViewArea full=%dx%d safe=%dx%d@%d,%d",
            g_width,g_height,safe_w,safe_h,safe_x,safe_y);

done:
    if(k)p_CFRelease(k);
    if(areas)p_CFRelease(areas);
    if(safe)p_CFRelease(safe);
    if(view)p_CFRelease(view);
}

static void log_stream_types(const char *tag, CFDictionaryRef request)
{
    CFArrayRef a=get_streams(request);
    CFIndex i,n;
    char b[256];
    int used=0;
    if(!a){logf_u2("%s streams=<none>",tag);return;}
    n=p_CFArrayGetCount(a);
    used=snprintf(b,sizeof(b),"%s streams=%ld types=",tag,(long)n);
    for(i=0;i<n && i<16 && used>0 && used<(int)sizeof(b)-16;++i)
        used+=snprintf(b+used,sizeof(b)-(size_t)used,"%s%d",(i?",":""),stream_type((CFDictionaryRef)p_CFArrayGetValueAtIndex(a,i)));
    logf_u2("%s",b);
}

static const char *flight_command_cstr(CFStringRef command, char *buf, size_t cap)
{
    const char *p;
    if(!buf||cap==0)return "<invalid-buffer>";
    buf[0]='\0';
    if(!command)return "<null>";
    p=p_CFStringGetCStringPtr(command,CF_UTF8);
    if(p&&*p)return p;
    if(p_CFStringGetCString(command,buf,(CFIndex)cap,CF_UTF8) && buf[0])return buf;
    snprintf(buf,cap,"<cfstr:%p>",(void *)command);
    return buf;
}

static int flight_dict_i64(CFDictionaryRef d, const char *key, int64_t *out)
{
    CFStringRef k;
    OSStatus e=0;
    int64_t v;
    if(!d||!key||!out)return 0;
    k=s_cf(key);
    if(!k)return 0;
    v=p_CFDictionaryGetInt64(d,k,&e);
    p_CFRelease(k);
    if(e)return 0;
    *out=v;
    return 1;
}

#define FLIGHT_MAX_DEPTH 4
#define FLIGHT_MAX_CONTAINER_ITEMS 48
#define FLIGHT_MAX_STRING 180

static int flight_sensitive_key(const char *key)
{
    char b[96];
    size_t i,n;
    static const char *needles[]={"token","signature","password","credential","secret","nonce","auth"};
    static const char *identifiers[]={
        "deviceid","macaddress","sessionuuid","sessioncorrelationuuid","name"
    };
    if(!key)return 0;
    n=strlen(key);
    if(n>=sizeof(b))n=sizeof(b)-1;
    for(i=0;i<n;++i){
        unsigned char c=(unsigned char)key[i];
        b[i]=(char)((c>='A'&&c<='Z')?(c-'A'+'a'):c);
    }
    b[n]='\0';
    for(i=0;i<sizeof(needles)/sizeof(needles[0]);++i)
        if(strstr(b,needles[i]))return 1;
    /*
     * Flight Recorder logs are routinely copied out of the vehicle and may
     * later be used in public issue/research evidence.  Keep protocol shape
     * but never persist phone/user correlators such as the device name, MAC/
     * device identifier, or per-session UUIDs.
     */
    for(i=0;i<sizeof(identifiers)/sizeof(identifiers[0]);++i)
        if(strcmp(b,identifiers[i])==0)return 1;
    return 0;
}

static const char *flight_cfstring(CFStringRef s, char *buf, size_t cap)
{
    const char *p;
    if(!buf||cap==0)return "<invalid-buffer>";
    buf[0]='\0';
    if(!s)return "<null>";
    p=p_CFStringGetCStringPtr(s,CF_UTF8);
    if(p)return p;
    if(p_CFStringGetCString(s,buf,(CFIndex)cap,CF_UTF8))return buf;
    snprintf(buf,cap,"<cfstr:%p>",(void *)s);
    return buf;
}

static void flight_dump_value(const char *command, const char *path, CFTypeRef obj, int depth);

static void flight_dump_dict(const char *command, const char *path, CFDictionaryRef d, int depth)
{
    CFIndex count,i;
    const void **keys=NULL,**vals=NULL;
    if(!d)return;
    count=p_CFDictionaryGetCount(d);
    logf_u2("GEN2 FLIGHT DUMP command=%s path=%s type=dict count=%ld depth=%d",
            command?command:"<null>",path?path:"params",(long)count,depth);
    if(depth>=FLIGHT_MAX_DEPTH){
        logf_u2("GEN2 FLIGHT DUMP command=%s path=%s stop=max-depth limit=%d",
                command?command:"<null>",path?path:"params",FLIGHT_MAX_DEPTH);
        return;
    }
    if(count<0 || count>FLIGHT_MAX_CONTAINER_ITEMS){
        logf_u2("GEN2 FLIGHT DUMP command=%s path=%s enumeration=skipped count=%ld limit=%d",
                command?command:"<null>",path?path:"params",(long)count,FLIGHT_MAX_CONTAINER_ITEMS);
        return;
    }
    if(count==0)return;
    keys=(const void **)calloc((size_t)count,sizeof(*keys));
    vals=(const void **)calloc((size_t)count,sizeof(*vals));
    if(!keys||!vals){
        logf_u2("GEN2 FLIGHT DUMP command=%s path=%s enumeration=oom count=%ld",
                command?command:"<null>",path?path:"params",(long)count);
        free(keys); free(vals); return;
    }
    p_CFDictionaryGetKeysAndValues(d,keys,vals);
    for(i=0;i<count;++i){
        char kbuf[96],pbuf[256];
        const char *k="<non-string-key>";
        CFTypeRef ko=(CFTypeRef)keys[i];
        if(ko && p_CFGetTypeID(ko)==p_CFStringGetTypeID())
            k=flight_cfstring((CFStringRef)ko,kbuf,sizeof(kbuf));
        snprintf(pbuf,sizeof(pbuf),"%s.%s",path?path:"params",k);
        if(flight_sensitive_key(k)){
            logf_u2("GEN2 FLIGHT DUMP command=%s path=%s type=redacted value=REDACTED",
                    command?command:"<null>",pbuf);
            continue;
        }
        flight_dump_value(command,pbuf,(CFTypeRef)vals[i],depth+1);
    }
    free(keys); free(vals);
}

static void flight_dump_array(const char *command, const char *path, CFArrayRef a, int depth)
{
    CFIndex count,i;
    if(!a)return;
    count=p_CFArrayGetCount(a);
    logf_u2("GEN2 FLIGHT DUMP command=%s path=%s type=array count=%ld depth=%d",
            command?command:"<null>",path?path:"params",(long)count,depth);
    if(depth>=FLIGHT_MAX_DEPTH){
        logf_u2("GEN2 FLIGHT DUMP command=%s path=%s stop=max-depth limit=%d",
                command?command:"<null>",path?path:"params",FLIGHT_MAX_DEPTH);
        return;
    }
    if(count<0 || count>FLIGHT_MAX_CONTAINER_ITEMS){
        logf_u2("GEN2 FLIGHT DUMP command=%s path=%s enumeration=skipped count=%ld limit=%d",
                command?command:"<null>",path?path:"params",(long)count,FLIGHT_MAX_CONTAINER_ITEMS);
        return;
    }
    for(i=0;i<count;++i){
        char pbuf[256];
        snprintf(pbuf,sizeof(pbuf),"%s[%ld]",path?path:"params",(long)i);
        flight_dump_value(command,pbuf,p_CFArrayGetValueAtIndex(a,i),depth+1);
    }
}

static void flight_dump_value(const char *command, const char *path, CFTypeRef obj, int depth)
{
    CFTypeID t;
    if(!obj){
        logf_u2("GEN2 FLIGHT DUMP command=%s path=%s type=null",
                command?command:"<null>",path?path:"params");
        return;
    }
    t=p_CFGetTypeID(obj);
    if(t==p_CFDictionaryGetTypeID()){
        flight_dump_dict(command,path,(CFDictionaryRef)obj,depth);
    }else if(t==p_CFArrayGetTypeID()){
        flight_dump_array(command,path,(CFArrayRef)obj,depth);
    }else if(t==p_CFStringGetTypeID()){
        char b[FLIGHT_MAX_STRING+1];
        const char *v=flight_cfstring((CFStringRef)obj,b,sizeof(b));
        char safe[FLIGHT_MAX_STRING+1];
        size_t i,n=strlen(v);
        if(n>FLIGHT_MAX_STRING)n=FLIGHT_MAX_STRING;
        for(i=0;i<n;++i){
            unsigned char c=(unsigned char)v[i];
            safe[i]=(c>=32 && c<127)?(char)c:'?';
        }
        safe[n]='\0';
        logf_u2("GEN2 FLIGHT DUMP command=%s path=%s type=string value=%s%s",
                command?command:"<null>",path?path:"params",safe,strlen(v)>n?"<truncated>":"");
    }else if(t==p_CFNumberGetTypeID()){
        OSStatus e=0;
        int64_t v=p_CFGetInt64(obj,&e);
        if(!e){
            logf_u2("GEN2 FLIGHT DUMP command=%s path=%s type=int value=%lld%s",
                    command?command:"<null>",path?path:"params",(long long)v,
                    v==111?" MATCH_STREAM111":"");
        }else{
            logf_u2("GEN2 FLIGHT DUMP command=%s path=%s type=number int64_error=%d",
                    command?command:"<null>",path?path:"params",(int)e);
        }
    }else if(t==p_CFBooleanGetTypeID()){
        logf_u2("GEN2 FLIGHT DUMP command=%s path=%s type=bool value=%d",
                command?command:"<null>",path?path:"params",(int)p_CFBooleanGetValue(obj));
    }else if(t==p_CFDataGetTypeID()){
        CFIndex n=p_CFDataGetLength((CFDataRef)obj);
        (void)p_CFDataGetBytePtr((CFDataRef)obj);
        logf_u2("GEN2 FLIGHT DUMP command=%s path=%s type=data len=%ld payload=not-logged",
                command?command:"<null>",path?path:"params",(long)n);
    }else{
        logf_u2("GEN2 FLIGHT DUMP command=%s path=%s type=unknown object=%p typeid=%lu",
                command?command:"<null>",path?path:"params",(void *)obj,(unsigned long)t);
    }
}

static void flight_log_stream_descriptors(const char *command, CFDictionaryRef params)
{
    CFArrayRef a=get_streams(params);
    CFIndex i,n;
    if(!a){
        logf_u2("GEN2 FLIGHT command=%s streams=<none>",command?command:"<null>");
        return;
    }
    n=p_CFArrayGetCount(a);
    logf_u2("GEN2 FLIGHT command=%s streams=%ld",command?command:"<null>",(long)n);
    for(i=0;i<n && i<16;++i){
        CFDictionaryRef sd=(CFDictionaryRef)p_CFArrayGetValueAtIndex(a,i);
        int64_t type=0,cid=0,sid=0,port=0,display=0,screen=0;
        int ht=flight_dict_i64(sd,"type",&type);
        int hc=flight_dict_i64(sd,"streamConnectionID",&cid);
        int hs=flight_dict_i64(sd,"streamID",&sid);
        int hp=flight_dict_i64(sd,"dataPort",&port);
        int hd=flight_dict_i64(sd,"displayID",&display);
        int hscr=flight_dict_i64(sd,"screenID",&screen);
        logf_u2("GEN2 FLIGHT stream[%ld] type=%s%lld streamConnectionID=%s%llu streamID=%s%lld dataPort=%s%lld displayID=%s%lld screenID=%s%lld",
                (long)i,
                ht?"":"<absent>",(long long)(ht?type:0),
                hc?"":"<absent>",(unsigned long long)(hc?(uint64_t)cid:0),
                hs?"":"<absent>",(long long)(hs?sid:0),
                hp?"":"<absent>",(long long)(hp?port:0),
                hd?"":"<absent>",(long long)(hd?display:0),
                hscr?"":"<absent>",(long long)(hscr?screen:0));
    }
}

static void flight_log_selected_scalars(const char *command, CFDictionaryRef params)
{
    static const char *keys[]={
        "type","streamType","streamID","streamConnectionID","dataPort",
        "displayID","screenID","appStateID","state","mode","resourceMode",
        "viewArea","viewAreaIndex","visibility","reason","priority"
    };
    size_t i;
    if(!params)return;
    for(i=0;i<sizeof(keys)/sizeof(keys[0]);++i){
        int64_t v=0;
        if(flight_dict_i64(params,keys[i],&v))
            logf_u2("GEN2 FLIGHT command=%s scalar %s=%lld",
                    command?command:"<null>",keys[i],(long long)v);
    }
}

static void flight_record_platform_control(AirPlayReceiverSessionRef session, uint32_t flags,
                                           CFStringRef command, CFTypeRef qualifier,
                                           CFDictionaryRef params, CFDictionaryRef *outParams)
{
    char cbuf[96];
    const char *c=flight_command_cstr(command,cbuf,sizeof(cbuf));
    logf_u2("GEN2 FLIGHT PlatformControl enter session=%p flags=%u command=%s commandRef=%p qualifier=%p params=%p outParams=%p",
            session,(unsigned)flags,c,(void *)command,qualifier,params,outParams);
    if(qualifier)
        flight_dump_value(c,"qualifier",qualifier,0);
    if(params){
        flight_log_selected_scalars(c,params);
        if(get_streams(params))
            flight_log_stream_descriptors(c,params);
        flight_dump_value(c,"params",(CFTypeRef)params,0);
    }
}

static void flight_record_session_control(AirPlayReceiverSessionRef session, uint32_t flags,
                                          CFStringRef command, CFTypeRef qualifier,
                                          CFDictionaryRef params, CFDictionaryRef *outParams)
{
    char cbuf[96];
    const char *c=flight_command_cstr(command,cbuf,sizeof(cbuf));
    logf_u2("GEN2 FLIGHT SessionControl enter session=%p flags=%u command=%s commandRef=%p qualifier=%p params=%p outParams=%p",
            session,(unsigned)flags,c,(void *)command,qualifier,params,outParams);
    if(qualifier)
        flight_dump_value(c,"sessionControl.qualifier",qualifier,0);
    if(params){
        flight_log_selected_scalars(c,params);
        if(get_streams(params))
            flight_log_stream_descriptors(c,params);
        flight_dump_value(c,"sessionControl.params",(CFTypeRef)params,0);
    }
}

static void log_stock_url_capability(CFDictionaryRef stock_display, const char *name)
{
    CFStringRef key;
    CFTypeRef value;
    CFTypeID tid;
    if(!stock_display||!name)return;
    key=s_cf(name);
    if(!key)return;
    value=(CFTypeRef)p_CFDictionaryGetValue(stock_display,key);
    p_CFRelease(key);
    if(!value){
        logf_u2("GEN2 stock display URL capability %s=<absent>",name);
        return;
    }
    tid=p_CFGetTypeID(value);
    if(tid==p_CFArrayGetTypeID()){
        CFIndex i,n=p_CFArrayGetCount((CFArrayRef)value);
        logf_u2("GEN2 stock display URL capability %s=array count=%ld",name,(long)n);
        for(i=0;i<n && i<8;++i){
            CFTypeRef item=(CFTypeRef)p_CFArrayGetValueAtIndex((CFArrayRef)value,i);
            if(item && p_CFGetTypeID(item)==p_CFStringGetTypeID()){
                char b[192];
                const char *u=flight_command_cstr((CFStringRef)item,b,sizeof(b));
                logf_u2("GEN2 stock display URL capability %s[%ld]=%s",name,(long)i,u);
            }else{
                logf_u2("GEN2 stock display URL capability %s[%ld]=<type:%lu>",
                        name,(long)i,(unsigned long)(item?p_CFGetTypeID(item):0));
            }
        }
    }else{
        logf_u2("GEN2 stock display URL capability %s=<type:%lu>",
                name,(unsigned long)tid);
    }
}

CFDictionaryRef AirPlayCopyServerInfo(AirPlayReceiverSessionRef session, CFArrayRef properties, uint8_t *mac, OSStatus *outErr)
{
    CFDictionaryRef base,stock_display;
    CFMutableDictionaryRef info=NULL,alt=NULL;
    CFStringRef kdisplays=NULL;
    CFArrayRef old=NULL;
    CFMutableArrayRef displays=NULL;
    int advertised_fps;

    gen2_runtime_ready();
    advertised_fps=current_advertised_fps();
    if(!g_real_serverinfo)g_real_serverinfo=(fn_serverinfo_t)sym_next("AirPlayCopyServerInfo");
    if(!g_real_serverinfo){
        logf_u2("AirPlayCopyServerInfo stock delegate unavailable");
        if(outErr)*outErr=-1;
        return NULL;
    }
    base=g_real_serverinfo(session,properties,mac,outErr);
    if(!g_enabled||!base)return base;
    log_stock_enabled_features(base);
    if(!advertise_enabled()){
        logf_u2("GEN2 /info advertisement disabled (ALTSCREEN111_ADVERTISE=0 or %s); returning stock",
                g_advertise_off_marker);
        return base;
    }

    info=dict_clone(base);
    if(!info)return base;

    /* Reference order: root capability first, display transformation second. */
    apply_airplay_bit26_ab(info);
    set_reference_enabled_features(info);

    kdisplays=s_cf("displays");
    old=(CFArrayRef)p_CFDictionaryGetValue(base,kdisplays);
    if(!old || p_CFArrayGetCount(old)<=0){
        logf_u2("IRC-parity /info: root features added but stock displays missing");
        p_CFRelease(kdisplays);
        p_CFRelease(base);
        return info;
    }

    stock_display=(CFDictionaryRef)p_CFArrayGetValueAtIndex(old,0);
    /*
     * iOS 27.2 intersects provider suggestions with receiver-advertised
     * cluster URL capabilities.  Observe the exact stock MU1440 baseline
     * before mutating the clone; do not synthesize capability arrays here.
     */
    log_stock_url_capability(stock_display,"altScreenSuggestUIURLs");
    log_stock_url_capability(stock_display,"altScreenURLs");
    log_stock_url_capability(stock_display,"uiContextURLs");
    {
        OSStatus fe=0, ie=0;
        CFStringRef fk=s_cf("features"), ik=s_cf("primaryInputDevice");
        int64_t fv=p_CFDictionaryGetInt64(stock_display,fk,&fe);
        int64_t iv=p_CFDictionaryGetInt64(stock_display,ik,&ie);
        logf_u2("GEN2 stock display capability baseline: features=%s%lld primaryInputDevice=%s%lld; alt clone removes primaryInputDevice",
                fe?"<absent>":"",(long long)(fe?0:fv),
                ie?"<absent>":"",(long long)(ie?0:iv));
        p_CFRelease(ik); p_CFRelease(fk);
    }
    displays=p_CFArrayCreateMutableCopy(NULL,0,old);
    alt=dict_clone(stock_display);
    if(displays&&alt){
        char active_url[384];
        active_alt_url_copy(active_url,sizeof(active_url));
        /* Exact reference removal set before AltScreen-specific overrides. */
        remove_key(alt,"primaryInputDevice");
        remove_key(alt,"edid");
        remove_key(alt,"platformLayer");
        remove_key(alt,"avcc");
        remove_key(alt,"xOffset");
        remove_key(alt,"yOffset");
        remove_key(alt,"windowWidth");
        remove_key(alt,"windowHeight");

        /*
         * Coherent GEN2 no-HID profile. Do not inherit main-display touch
         * capabilities or cadence. The validated profile and MIBSI parity
         * evidence both identify this secondary display explicitly as type 111.
         */
        set_i64(alt,"type",(int64_t)g2_profile.type);
        set_i64(alt,"maxFPS",(int64_t)advertised_fps);
        set_i64(alt,"features",(int64_t)g2_profile.features);
        set_i64(alt,"widthPixels",(int64_t)g2_profile.width);
        set_i64(alt,"heightPixels",(int64_t)g2_profile.height);
        set_i64(alt,"widthPhysical",(int64_t)g2_profile.width_mm);
        set_i64(alt,"heightPhysical",(int64_t)g2_profile.height_mm);
        set_str(alt,"uuid",g_alt_uuid);
        set_str(alt,"initialURL",active_url);
        add_alt_suggest_ui_urls(alt);
        if(g_viewareas)add_reference_viewarea(alt);

        p_CFArrayAppendValue(displays,alt);
        p_CFDictionarySetValue(info,kdisplays,displays);
        logf_u2("GEN2 /info ready: root=altScreen%s type=%u maxFPS=%u features=%u input=none geometry=%ux%u physical=%ux%u uuid=%s url=%s",
                g_viewareas?"+viewAreas":"",
                g2_profile.type,(unsigned)advertised_fps,g2_profile.features,
                g2_profile.width,g2_profile.height,g2_profile.width_mm,g2_profile.height_mm,
                g_alt_uuid,active_url);
    }else{
        logf_u2("IRC-parity /info: display clone failed");
    }

    if(alt)p_CFRelease(alt);
    if(displays)p_CFRelease(displays);
    p_CFRelease(kdisplays);
    p_CFRelease(base);
    return info;
}

static CFMutableDictionaryRef command_showui(void)
{
    char active_url[384];
    CFMutableDictionaryRef req=dict_new(), params=dict_new();
    if(!req||!params){if(req)p_CFRelease(req);if(params)p_CFRelease(params);return NULL;}
    active_alt_url_copy(active_url,sizeof(active_url));
    set_str(req,"type","showUI"); set_str(params,"uuid",g_alt_uuid); set_str(params,"url",active_url);
    { CFStringRef k=s_cf("params"); p_CFDictionarySetValue(req,k,params); p_CFRelease(k); }
    p_CFRelease(params); return req;
}

static CFMutableDictionaryRef command_force_keyframe(void)
{
    CFMutableDictionaryRef req=dict_new(), params=dict_new();
    if(!req||!params){if(req)p_CFRelease(req);if(params)p_CFRelease(params);return NULL;}
    set_str(req,"type","forceKeyFrame"); set_str(params,"uuid",g_alt_uuid);
    { CFStringRef k=s_cf("params"); p_CFDictionarySetValue(req,k,params); p_CFRelease(k); }
    p_CFRelease(params); return req;
}

static CFMutableDictionaryRef command_stopui(void)
{
    CFMutableDictionaryRef req=dict_new(), params=dict_new();
    if(!req||!params){if(req)p_CFRelease(req);if(params)p_CFRelease(params);return NULL;}
    set_str(req,"type","stopUI"); set_str(params,"uuid",g_alt_uuid);
    { CFStringRef k=s_cf("params"); p_CFDictionarySetValue(req,k,params); p_CFRelease(k); }
    p_CFRelease(params); return req;
}

static CFMutableDictionaryRef command_update_view(unsigned view)
{
    CFMutableDictionaryRef req=dict_new(), params=dict_new();
    if(!req||!params){if(req)p_CFRelease(req);if(params)p_CFRelease(params);return NULL;}
    set_str(req,"type","updateViewArea");
    set_str(params,"uuid",g_alt_uuid);
    set_i64(params,"viewAreaIndex",(int64_t)view);
    set_i64(params,"animationDurationMillis",0);
    { CFStringRef k=s_cf("params"); p_CFDictionarySetValue(req,k,params); p_CFRelease(k); }
    p_CFRelease(params); return req;
}

/* Semantic cluster map zoom (harman-f/mhi2_altscreen_carplay research, iOS 26.7.1
 * command index): {uuid, zoomDirection}, 0 = in, 1 = out. Not vehicle-proven yet;
 * every dispatch and completion status is logged. */
static CFMutableDictionaryRef command_map_zoom(int zoom_out)
{
    CFMutableDictionaryRef req=dict_new(), params=dict_new();
    if(!req||!params){if(req)p_CFRelease(req);if(params)p_CFRelease(params);return NULL;}
    set_str(req,"type","changeMapZoomLevel");
    set_str(params,"uuid",g_alt_uuid);
    set_i64(params,"zoomDirection",zoom_out?1:0);
    { CFStringRef k=s_cf("params"); p_CFDictionarySetValue(req,k,params); p_CFRelease(k); }
    p_CFRelease(params); return req;
}

/*
 * Exported for libcarplay_hook (same dio_manager process): its bus handler for
 * CMD_ALT_ZOOM resolves this with dlsym(RTLD_DEFAULT). Only queues the step; the
 * AirPlay command is sent from gen2_control_worker, never from the bus thread.
 * Returns 0 when queued, -1 when no stream-111 session is up.
 */
int altscreen111_map_zoom(int mapscale_steps)
{
    int queued = -1;
    if(!mapscale_steps) return 0;
    pthread_mutex_lock(&g2_core_lock);
    if(g2_command_ready){
        g2_zoom_pending += mapscale_steps;
        if(g2_zoom_pending > 8) g2_zoom_pending = 8;
        if(g2_zoom_pending < -8) g2_zoom_pending = -8;
        queued = 0;
    }
    pthread_cond_broadcast(&g2_core_cv);
    pthread_mutex_unlock(&g2_core_lock);
    logf_u2("gen2 map zoom request steps=%d %s",
            mapscale_steps, queued==0 ? "queued" : "dropped (no stream111 session)");
    return queued;
}

static void gen2_dispatch_diag_command(const char *label, CFMutableDictionaryRef req);

/* One queued step per worker pass, so a fast roller spin cannot burst the phone. */
static void gen2_process_map_zoom(void)
{
    static uint64_t last_ms;
    int step = 0, zoom_out;
    uint64_t now = monotonic_ms();
    if(last_ms && now - last_ms < 150u) return;
    pthread_mutex_lock(&g2_core_lock);
    if(g2_zoom_pending > 0){ step = 1; --g2_zoom_pending; }
    else if(g2_zoom_pending < 0){ step = -1; ++g2_zoom_pending; }
    pthread_mutex_unlock(&g2_core_lock);
    if(!step) return;
    last_ms = now;
    zoom_out = step > 0;
    if(access(g2_zoom_inverted_marker,F_OK)==0) zoom_out = !zoom_out;
    logf_u2("gen2 changeMapZoomLevel mapscale_step=%d zoomDirection=%d (%s)",
            step, zoom_out, zoom_out ? "out" : "in");
    gen2_dispatch_diag_command("changeMapZoomLevel", command_map_zoom(zoom_out));
}

static CFMutableDictionaryRef gen2_command_dictionary(const struct alt111_command *cmd)
{
    if (!cmd) return NULL;
    switch (cmd->type) {
    case ALT111_CMD_SHOW: return command_showui();
    case ALT111_CMD_STOP: return command_stopui();
    case ALT111_CMD_VIEW: return command_update_view(cmd->view);
    case ALT111_CMD_KEYFRAME: return command_force_keyframe();
    default: return NULL;
    }
}

static void gen2_publish_status(void)
{
    char b[3072];
    int fd, n;
    struct alt111_control cs;
    struct alt111_video vs;
    struct alt111_resync rs;
    mibr_mode_state_t ms;
    unsigned mode_valid;
    uint64_t mode_sequence;
    unsigned command_ready;
    uint64_t last_dispatched, last_completed;
    int last_completion_status;
    unsigned d2_enabled, d2_pending_sources;
    unsigned nav_query_enabled;
    char nav_url[384];
    uint64_t d2_pending_due_ms, d2_last_request_ms, d2_last_idr_ms;
    uint64_t d2_event_triggers, d2_watchdog_triggers, d2_coalesced;
    pthread_mutex_lock(&g2_core_lock);
    cs = g2_control;
    vs = g2_video;
    rs = g2_resync;
    ms = g2_last_mode_state;
    mode_valid = g2_last_mode_valid;
    mode_sequence = g2_mode_sequence;
    command_ready = g2_command_ready;
    last_dispatched = g2_last_dispatched_request;
    last_completed = g2_last_completed_request;
    last_completion_status = g2_last_completion_status;
    d2_enabled = gen2_d2_enabled();
    d2_pending_sources = g2_d2_pending_sources;
    d2_pending_due_ms = g2_d2_pending_due_ms;
    d2_last_request_ms = g2_d2_last_request_ms;
    d2_last_idr_ms = g2_d2_last_idr_ms;
    d2_event_triggers = g2_d2_event_triggers;
    d2_watchdog_triggers = g2_d2_watchdog_triggers;
    d2_coalesced = g2_d2_coalesced;
    pthread_mutex_unlock(&g2_core_lock);
    nav_query_enabled = access(g_nav_query_enable_marker,F_OK)==0 ? 1u : 0u;
    active_alt_url_copy(nav_url,sizeof(nav_url));
    n = snprintf(b,sizeof(b),
        "gen2=1\ncontrol_session=%llu\ncommand_ready=%u\nprojection_desired=%u\nshown_ack=%u\nreacquiring=%u\n"
        "last_dispatched_request=%llu\nlast_completed_request=%llu\nlast_completion_status=%d\n"
        "nav_query_enabled=%u\nnav_url=%s\n"
        "stream_gen=%llu\ncodec_gen=%llu\nconsumer_gen=%llu\nconfig_valid=%u\n"
        "source_aus=%llu\nsource_idrs=%llu\nconsumer_primed=%u\ndelivered_aus=%llu\n"
        "dropped_aus=%llu\nqueue_count=%u\nqueue_bytes=%zu\n"
        "resync_enabled=%u\nresync_state=%u\nresync_reason=%u\nresync_cancel_reason=%u\n"
        "resync_epoch=%llu\nresync_stream_at_arm=%llu\nresync_codec_at_arm=%llu\n"
        "resync_consumer_at_arm=%llu\nresync_au_at_arm=%llu\nresync_idr_at_arm=%llu\n"
        "resync_requests=%llu\nresync_retries=%llu\nresync_completions=%llu\nresync_cancels=%llu\n"
        "resync_last_request_ms=%llu\nresync_next_request_ms=%llu\nresync_completed_ms=%llu\n"
        "resync_retry_ms=%u\n"
        "d2_enabled=%u\nd2_pending_sources=%u\nd2_pending_due_ms=%llu\n"
        "d2_last_request_ms=%llu\nd2_last_idr_ms=%llu\n"
        "d2_event_triggers=%llu\nd2_watchdog_triggers=%llu\nd2_coalesced=%llu\n"
        "mode_valid=%u\nmode_sequence=%llu\nmode_screen=%d\nmode_main_audio=%d\n"
        "mode_speech=%d\nmode_speech_detail=%d\nmode_phone=%d\nmode_turns=%d\n",
        (unsigned long long)cs.session,command_ready,cs.desired,cs.shown_ack,cs.reacquiring,
        (unsigned long long)last_dispatched,(unsigned long long)last_completed,last_completion_status,
        nav_query_enabled,nav_url,
        (unsigned long long)vs.stream,(unsigned long long)vs.codec,
        (unsigned long long)vs.consumer,vs.config_valid,
        (unsigned long long)vs.source_aus,(unsigned long long)vs.source_idrs,
        vs.consumer_primed,(unsigned long long)vs.delivered_aus,
        (unsigned long long)vs.dropped_aus,vs.count,vs.queued_bytes,
        rs.enabled,rs.state,rs.reason,rs.cancel_reason,
        (unsigned long long)rs.epoch,(unsigned long long)rs.stream_at_arm,
        (unsigned long long)rs.codec_at_arm,(unsigned long long)rs.consumer_at_arm,
        (unsigned long long)rs.au_at_arm,(unsigned long long)rs.idr_at_arm,
        (unsigned long long)rs.requests,(unsigned long long)rs.retries,
        (unsigned long long)rs.completions,(unsigned long long)rs.cancels,
        (unsigned long long)rs.last_request_ms,(unsigned long long)rs.next_request_ms,
        (unsigned long long)rs.completed_ms,rs.retry_ms,
        d2_enabled,d2_pending_sources,(unsigned long long)d2_pending_due_ms,
        (unsigned long long)d2_last_request_ms,(unsigned long long)d2_last_idr_ms,
        (unsigned long long)d2_event_triggers,(unsigned long long)d2_watchdog_triggers,
        (unsigned long long)d2_coalesced,
        mode_valid,(unsigned long long)mode_sequence,
        ms.screen,ms.main_audio,ms.speech,ms.speech_detail,ms.phone,ms.turns);
    if(n<=0)return;
    if((size_t)n>=sizeof(b))n=(int)sizeof(b)-1;
    pthread_mutex_lock(&g2_status_lock);
    fd=open(g2_status_path,O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(fd>=0){(void)write(fd,b,(size_t)n);close(fd);}
    pthread_mutex_unlock(&g2_status_lock);
}

static void gen2_set_command_ready(unsigned ready)
{
    pthread_mutex_lock(&g2_core_lock);
    g2_command_ready = ready ? 1u : 0u;
    if(!ready) g2_zoom_pending = 0;
    pthread_cond_broadcast(&g2_core_cv);
    pthread_mutex_unlock(&g2_core_lock);
    gen2_publish_status();
}

static void gen2_control_projection_on(void)
{
    int repeated = 0, rrc = ALT111_OK;
    pthread_mutex_lock(&g2_core_lock);
    if (!g2_control_session) {
        g2_control_session = alt111_control_begin(&g2_control);
    } else {
        repeated = 1;
    }
    (void)alt111_control_intent(&g2_control,1,0);
    if (repeated)
        rrc = alt111_control_reacquire(&g2_control,g2_control_session);
    pthread_cond_broadcast(&g2_core_cv);
    pthread_mutex_unlock(&g2_core_lock);
    if (repeated)
        logf_u2("gen2 repeated stream111 SETUP -> ownership reacquire rc=%d",rrc);
    gen2_publish_status();
}

static void gen2_control_release(void)
{
    uint64_t s;
    int stop_handed_off = 0;
    pthread_mutex_lock(&g2_core_lock);
    s=g2_control_session;
    alt111_resync_cancel(&g2_resync, ALT111_RESYNC_CANCEL_PROJECTION);
    if(s)(void)alt111_control_intent(&g2_control,0,0);
    pthread_cond_broadcast(&g2_core_cv);
    pthread_mutex_unlock(&g2_core_lock);

    /*
     * Do not confuse SendCommand() queueing with the controller response.
     * During teardown we cannot wait indefinitely for the HTTP completion,
     * but we do wait until either:
     *   - stopUI has completed successfully (may_be_visible == 0), or
     *   - the STOP request has at least been handed to AirPlay's HTTP client.
     * A later callback is generation-checked by the core and may go stale
     * after alt111_control_end(), which is intentional.
     */
    if(s){
        unsigned waitn;
        for(waitn=0; waitn<100; ++waitn){
            pthread_mutex_lock(&g2_core_lock);
            if(!g2_control.may_be_visible){
                stop_handed_off = 1;
            }else if(g2_control.pending.type == ALT111_CMD_STOP &&
                     g2_last_dispatched_request == g2_control.pending.request){
                stop_handed_off = 1;
            }
            pthread_mutex_unlock(&g2_core_lock);
            if(stop_handed_off)break;
            usleep(5000);
        }
        if(!stop_handed_off)
            logf_u2("gen2 stopUI not handed off within bounded teardown window session=%llu",
                    (unsigned long long)s);
    }

    pthread_mutex_lock(&g2_core_lock);
    if(s)(void)alt111_control_end(&g2_control,s);
    g2_control_session=0;
    pthread_mutex_unlock(&g2_core_lock);
    gen2_publish_status();
}

struct gen2_command_context {
    uint64_t session;
    uint64_t request;
    unsigned type;
};

static void gen2_command_completion(OSStatus status, CFDictionaryRef response, void *opaque)
{
    struct gen2_command_context *ctx = (struct gen2_command_context *)opaque;
    int crc = ALT111_STALE;
    (void)response;
    if(!ctx)return;

    pthread_mutex_lock(&g2_core_lock);
    g2_last_completed_request = ctx->request;
    g2_last_completion_status = (int)status;
    crc = alt111_control_complete(&g2_control,ctx->session,ctx->request,
                                  status==K_NO_ERR,monotonic_ms());
    pthread_cond_broadcast(&g2_core_cv);
    pthread_mutex_unlock(&g2_core_lock);

    logf_u2("gen2 UI completion type=%u request=%llu status=%d core_rc=%d",
            ctx->type,(unsigned long long)ctx->request,(int)status,crc);
    free(ctx);
    gen2_publish_status();
}

struct gen2_diag_context {
    const char *label;
};

static void gen2_diag_completion(OSStatus status, CFDictionaryRef response, void *opaque)
{
    struct gen2_diag_context *ctx=(struct gen2_diag_context *)opaque;
    (void)response;
    logf_u2("gen2 DIAG completion command=%s status=%d",
            (ctx&&ctx->label)?ctx->label:"<unknown>",(int)status);
    if(ctx)free(ctx);
}

static void gen2_dispatch_diag_command(const char *label, CFMutableDictionaryRef req)
{
    AirPlayReceiverSessionRef s=NULL;
    struct gen2_diag_context *ctx=NULL;
    OSStatus e=-1;

    if(!req||!label){
        if(req)p_CFRelease(req);
        return;
    }

    s=retain_active_session();
    ctx=(struct gen2_diag_context *)calloc(1,sizeof(*ctx));
    if(ctx)ctx->label=label;

    if(s&&ctx){
        e=g_sendcmd(s,req,gen2_diag_completion,ctx);
        if(e==K_NO_ERR){
            logf_u2("gen2 DIAG dispatched command=%s",label);
            ctx=NULL;
        }else{
            logf_u2("gen2 DIAG dispatch failed command=%s os=%d",label,(int)e);
        }
    }else{
        logf_u2("gen2 DIAG dispatch unavailable command=%s session=%s ctx=%s",
                label,s?"yes":"no",ctx?"yes":"no");
    }

    if(ctx)free(ctx);
    if(s)p_CFRelease(s);
    p_CFRelease(req);
}

static void gen2_resync_snapshot_locked(struct alt111_resync_snapshot *snap)
{
    memset(snap,0,sizeof(*snap));
    snap->stream = g2_video.stream;
    snap->codec = g2_video.codec;
    snap->consumer = g2_video.consumer;
    snap->source_aus = g2_video.source_aus;
    snap->source_idrs = g2_video.source_idrs;
    snap->projection_desired = g2_control.desired;
    snap->config_valid = g2_video.config_valid;
    snap->consumer_primed = g2_video.consumer_primed;
}

static void gen2_resync_poll(void)
{
    struct alt111_resync_snapshot snap;
    enum alt111_resync_action action;
    uint64_t now = monotonic_ms();
    uint64_t epoch = 0, idr_at_arm = 0, source_idrs = 0;
    uint64_t requests = 0, retries = 0, completions = 0, cancels = 0;
    unsigned manual_enabled = access(g2_resync_enable_marker,F_OK)==0 ? 1u : 0u;
    unsigned d2_enabled = gen2_d2_enabled();
    unsigned enabled = (manual_enabled || d2_enabled) ? 1u : 0u;
    unsigned arm = access(g2_resync_arm_marker,F_OK)==0 ? 1u : 0u;
    unsigned before_enabled, before_state, before_cancel;
    unsigned auto_reason = ALT111_RESYNC_REASON_NONE;
    unsigned auto_sources = 0;
    int arm_rc = ALT111_WAIT;
    int auto_arm_rc = ALT111_WAIT;
    int keyframe_rc = ALT111_WAIT;
    int changed = 0;

    if(arm) (void)unlink(g2_resync_arm_marker);

    pthread_mutex_lock(&g2_core_lock);
    before_enabled = g2_resync.enabled;
    before_state = g2_resync.state;
    before_cancel = g2_resync.cancel_reason;
    alt111_resync_set_enabled(&g2_resync,enabled);
    gen2_resync_snapshot_locked(&snap);

    if(d2_enabled) {
        if(!g2_d2_was_enabled) {
            g2_d2_was_enabled = 1;
            g2_d2_last_seen_idrs = snap.source_idrs;
            g2_d2_last_idr_ms = now;
            g2_d2_pending_sources = 0;
            g2_d2_pending_due_ms = 0;
            changed = 1;
        } else if(snap.source_idrs != g2_d2_last_seen_idrs) {
            g2_d2_last_seen_idrs = snap.source_idrs;
            g2_d2_last_idr_ms = now;
            changed = 1;
        }
    } else if(g2_d2_was_enabled) {
        g2_d2_was_enabled = 0;
        g2_d2_pending_sources = 0;
        g2_d2_pending_due_ms = 0;
        changed = 1;
    }

    if(arm)
        arm_rc = alt111_resync_arm(&g2_resync,now,ALT111_RESYNC_REASON_MANUAL,&snap);

    if(d2_enabled && !arm && g2_resync.state == ALT111_RESYNC_SYNCED) {
        if(g2_d2_pending_sources && g2_d2_pending_due_ms &&
           now >= g2_d2_pending_due_ms) {
            auto_sources = g2_d2_pending_sources;
            auto_reason = (auto_sources & G2_D2_SOURCE_SUGGEST_UI) ?
                ALT111_RESYNC_REASON_SUGGEST_UI : ALT111_RESYNC_REASON_TURNS;
            auto_arm_rc = alt111_resync_arm(&g2_resync,now,auto_reason,&snap);
            if(auto_arm_rc == ALT111_OK) {
                g2_d2_pending_sources = 0;
                g2_d2_pending_due_ms = 0;
            }
        } else if(!g2_d2_pending_sources && g2_d2_last_idr_ms &&
                  now >= g2_d2_last_idr_ms + G2_D2_WATCHDOG_MS &&
                  (!g2_d2_last_request_ms ||
                   now >= g2_d2_last_request_ms + G2_D2_MIN_GAP_MS)) {
            auto_reason = ALT111_RESYNC_REASON_WATCHDOG;
            auto_arm_rc = alt111_resync_arm(&g2_resync,now,auto_reason,&snap);
            if(auto_arm_rc == ALT111_OK)
                ++g2_d2_watchdog_triggers;
        }
    }

    action = alt111_resync_tick(&g2_resync,now,&snap);
    /*
     * Candidate D recovery must share the normal GEN2 command transaction.
     * Do not send forceKeyFrame through the out-of-band diagnostic dispatcher:
     * that path has independent completion state and can overlap show/stop/view.
     *
     * alt111_control_keyframe() records demand only.  The same control worker
     * calls alt111_control_next() immediately after this poll and therefore
     * serializes the real AirPlay command behind any in-flight UI transaction.
     */
    if(action==ALT111_RESYNC_REQUEST_KEYFRAME && g2_control_session){
        /*
         * Coalesce retries while an earlier keyframe demand is still pending
         * or awaiting controller completion.  Otherwise a 1 s resync tick can
         * increment keyframe_wanted again before the first serialized request
         * has completed and leave a redundant forceKeyFrame queued even after
         * the fresh source IDR has already completed the resync epoch.
         */
        if(g2_control.keyframe_done != g2_control.keyframe_wanted)
            keyframe_rc=ALT111_BUSY;
        else
            keyframe_rc=alt111_control_keyframe(&g2_control,g2_control_session);
        if(keyframe_rc==ALT111_OK && g2_resync.reason!=ALT111_RESYNC_REASON_MANUAL)
            g2_d2_last_request_ms=now;
        pthread_cond_broadcast(&g2_core_cv);
    }
    epoch = g2_resync.epoch;
    idr_at_arm = g2_resync.idr_at_arm;
    source_idrs = snap.source_idrs;
    requests = g2_resync.requests;
    retries = g2_resync.retries;
    completions = g2_resync.completions;
    cancels = g2_resync.cancels;
    if(before_enabled!=g2_resync.enabled || before_state!=g2_resync.state ||
       before_cancel!=g2_resync.cancel_reason || arm ||
       auto_arm_rc==ALT111_OK || action!=ALT111_RESYNC_NONE)
        changed=1;
    pthread_mutex_unlock(&g2_core_lock);

    if(arm){
        if(arm_rc==ALT111_OK)
            logf_u2("GEN2 RESYNC ARM epoch=%llu reason=manual idr_at_arm=%llu source_idrs=%llu",
                    (unsigned long long)epoch,(unsigned long long)idr_at_arm,
                    (unsigned long long)source_idrs);
        else
            logf_u2("GEN2 RESYNC ARM rejected rc=%d enabled=%u projection=%u config=%u primed=%u stream=%llu codec=%llu consumer=%llu",
                    arm_rc,enabled,snap.projection_desired,snap.config_valid,snap.consumer_primed,
                    (unsigned long long)snap.stream,(unsigned long long)snap.codec,
                    (unsigned long long)snap.consumer);
    }

    if(auto_arm_rc==ALT111_OK){
        logf_u2("GEN2 D2 ARM epoch=%llu reason=%u sources=0x%x idr_at_arm=%llu source_idrs=%llu delay_ms=%u watchdog_ms=%u",
                (unsigned long long)epoch,auto_reason,auto_sources,
                (unsigned long long)idr_at_arm,(unsigned long long)source_idrs,
                G2_D2_EVENT_DELAY_MS,G2_D2_WATCHDOG_MS);
    }else if(auto_reason!=ALT111_RESYNC_REASON_NONE && auto_arm_rc!=ALT111_WAIT){
        logf_u2("GEN2 D2 ARM rejected rc=%d reason=%u sources=0x%x projection=%u config=%u primed=%u",
                auto_arm_rc,auto_reason,auto_sources,
                snap.projection_desired,snap.config_valid,snap.consumer_primed);
    }

    if(action==ALT111_RESYNC_REQUEST_KEYFRAME){
        logf_u2("GEN2 RESYNC REQUEST epoch=%llu request_count=%llu retry_count=%llu idr_at_arm=%llu source_idrs=%llu queue_rc=%d",
                (unsigned long long)epoch,(unsigned long long)requests,
                (unsigned long long)retries,(unsigned long long)idr_at_arm,
                (unsigned long long)source_idrs,keyframe_rc);
        if(keyframe_rc!=ALT111_OK)
            logf_u2("GEN2 RESYNC REQUEST not queued through serialized controller rc=%d session=%llu projection=%u",
                    keyframe_rc,(unsigned long long)g2_control_session,snap.projection_desired);
    }else if(action==ALT111_RESYNC_COMPLETED){
        logf_u2("GEN2 RESYNC COMPLETE epoch=%llu completions=%llu idr_at_arm=%llu source_idrs=%llu",
                (unsigned long long)epoch,(unsigned long long)completions,
                (unsigned long long)idr_at_arm,(unsigned long long)source_idrs);
    }else if(action==ALT111_RESYNC_CANCELLED || (changed && cancels)){
        logf_u2("GEN2 RESYNC CANCEL epoch=%llu cancels=%llu",
                (unsigned long long)epoch,(unsigned long long)cancels);
    }

    if(changed) gen2_publish_status();
}

static void gen2_process_diag_markers(void)
{
    if(access(g2_diag_keyframe_marker,F_OK)==0){
        unlink(g2_diag_keyframe_marker);
        logf_u2("gen2 DIAG marker keyframe-only");
        gen2_dispatch_diag_command("forceKeyFrame",command_force_keyframe());
    }
    if(access(g2_diag_show_marker,F_OK)==0){
        char active_url[384];
        unlink(g2_diag_show_marker);
        active_alt_url_copy(active_url,sizeof(active_url));
        logf_u2("gen2 DIAG marker show-only url=%s",active_url);
        gen2_dispatch_diag_command("showUI",command_showui());
    }
    if(access(g2_diag_stop_marker,F_OK)==0){
        unlink(g2_diag_stop_marker);
        logf_u2("gen2 DIAG marker stop-only");
        gen2_dispatch_diag_command("stopUI",command_stopui());
    }
    if(access(g2_diag_zoom_in_marker,F_OK)==0){
        unlink(g2_diag_zoom_in_marker);
        logf_u2("gen2 DIAG marker zoom-in");
        gen2_dispatch_diag_command("changeMapZoomLevel",command_map_zoom(0));
    }
    if(access(g2_diag_zoom_out_marker,F_OK)==0){
        unlink(g2_diag_zoom_out_marker);
        logf_u2("gen2 DIAG marker zoom-out");
        gen2_dispatch_diag_command("changeMapZoomLevel",command_map_zoom(1));
    }
}

static void *gen2_control_worker(void *arg)
{
    (void)arg;
    for (;;) {
        struct alt111_command cmd;
        struct gen2_command_context *ctx=NULL;
        int nrc, queued=0;
        CFMutableDictionaryRef req=NULL;
        AirPlayReceiverSessionRef s=NULL;
        OSStatus e=-1;

        pthread_mutex_lock(&g2_core_lock);
        if(!g2_command_ready){
            pthread_mutex_unlock(&g2_core_lock);
            usleep(10000);
            continue;
        }
        pthread_mutex_unlock(&g2_core_lock);

        gen2_resync_poll();
        gen2_process_diag_markers();
        gen2_process_map_zoom();

        if(access(g2_reacquire_marker,F_OK)==0){
            unlink(g2_reacquire_marker);
            pthread_mutex_lock(&g2_core_lock);
            if(g2_control_session){
                int r=alt111_control_reacquire(&g2_control,g2_control_session);
                logf_u2("gen2 explicit reacquire requested rc=%d",r);
            }
            pthread_mutex_unlock(&g2_core_lock);
        }

        pthread_mutex_lock(&g2_core_lock);
        nrc=alt111_control_next(&g2_control,monotonic_ms(),&cmd);
        pthread_mutex_unlock(&g2_core_lock);
        if(nrc!=ALT111_OK){usleep(10000);continue;}

        req=gen2_command_dictionary(&cmd);
        s=retain_active_session();
        ctx=(struct gen2_command_context *)calloc(1,sizeof(*ctx));
        if(ctx){
            ctx->session=cmd.session;
            ctx->request=cmd.request;
            ctx->type=(unsigned)cmd.type;
        }

        if(req&&s&&ctx){
            e=g_sendcmd(s,req,gen2_command_completion,ctx);
            if(e==K_NO_ERR){
                queued=1;
                pthread_mutex_lock(&g2_core_lock);
                g2_last_dispatched_request=cmd.request;
                pthread_cond_broadcast(&g2_core_cv);
                pthread_mutex_unlock(&g2_core_lock);
                ctx=NULL; /* completion callback owns it */
            }
        }
        if(req)p_CFRelease(req);
        if(s)p_CFRelease(s);

        if(!queued){
            if(ctx)free(ctx);
            pthread_mutex_lock(&g2_core_lock);
            (void)alt111_control_complete(&g2_control,cmd.session,cmd.request,0,monotonic_ms());
            pthread_cond_broadcast(&g2_core_cv);
            pthread_mutex_unlock(&g2_core_lock);
            logf_u2("gen2 UI command dispatch failed type=%u request=%llu os=%d",
                    (unsigned)cmd.type,(unsigned long long)cmd.request,(int)e);
        }else{
            logf_u2("gen2 UI command dispatched type=%u request=%llu awaiting completion",
                    (unsigned)cmd.type,(unsigned long long)cmd.request);
        }
        gen2_publish_status();
    }
    return NULL;
}

static int gen2_start_workers(void)
{
    if(g2_workers_started)return 0;
    if(pthread_create(&g2_control_thread,NULL,gen2_control_worker,NULL)!=0)return -1;
    pthread_detach(g2_control_thread);
    if(pthread_create(&g2_output_thread,NULL,gen2_output_worker,NULL)!=0)return -1;
    pthread_detach(g2_output_thread);
    g2_workers_started=1;
    return 0;
}

/* ARM32 absolute jump trampoline: first 8 target bytes must be position-independent prologue. */
static int install_arm_hook_expected(void *target, void *replacement, void **trampoline, const char *name,
                                     uint32_t expected0, uint32_t expected1, int allow_page_clone)
{
#if defined(__arm__)
    uint32_t *src=(uint32_t *)target;
    uint32_t *tr;
    long ps=sysconf(_SC_PAGESIZE);
    uintptr_t page;
    int saved_errno;

    if(ps<=0){
        logf_u2("%s hook sysconf(_SC_PAGESIZE) failed ps=%ld errno=%d %s",
                name,ps,errno,strerror(errno));
        return -1;
    }
    page=((uintptr_t)target)&~((uintptr_t)ps-1u);

    if(src[0]!=expected0||src[1]!=expected1){
        logf_u2("%s prologue mismatch %08x %08x",name,src[0],src[1]);
        return -1;
    }

    /*
     * Keep W^X: never request writable+executable memory at the same time.
     * Older QNX targets may reject RWX even though an RW -> RX transition is
     * permitted.
     */
    errno=0;
    tr=mmap(NULL,16,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(tr==MAP_FAILED){
        saved_errno=errno;
        logf_u2("%s trampoline mmap RW failed errno=%d %s",
                name,saved_errno,strerror(saved_errno));
        return -1;
    }

    tr[0]=src[0];
    tr[1]=src[1];
    tr[2]=0xe51ff004u;
    tr[3]=(uint32_t)((uintptr_t)target+8u);
    __builtin___clear_cache((char *)tr,(char *)tr+16);

    errno=0;
    if(mprotect((void *)tr,16,PROT_READ|PROT_EXEC)!=0){
        saved_errno=errno;
        logf_u2("%s trampoline mprotect RX failed errno=%d %s",
                name,saved_errno,strerror(saved_errno));
        munmap(tr,16);
        return -1;
    }

    errno=0;
    if(mprotect((void *)page,(size_t)ps,PROT_READ|PROT_WRITE)!=0){
        uint8_t *shadow;
        void *fixed;
        saved_errno=errno;
        if(!allow_page_clone){
            logf_u2("%s target mprotect RW denied page=%p size=%ld errno=%d %s; refusing MAP_FIXED (other threads may be executing this page)",
                    name,(void *)page,ps,saved_errno,strerror(saved_errno));
            munmap(tr,16);
            return -1;
        }
        logf_u2("%s target mprotect RW denied page=%p size=%ld errno=%d %s; trying anonymous MAP_FIXED clone",
                name,(void *)page,ps,saved_errno,strerror(saved_errno));

        /*
         * QNX may refuse write permission on a file-backed executable text
         * mapping. Preserve the already-relocated live page byte-for-byte,
         * replace only that page by private anonymous RW memory at the same
         * virtual address, then restore RX after the 8-byte hook patch.
         *
         * The target functions live in libairplay; this installer executes
         * from the preload library, so replacing the target page does not
         * replace the currently executing code.
         */
        shadow=(uint8_t *)malloc((size_t)ps);
        if(!shadow){
            logf_u2("%s target page shadow allocation failed size=%ld",name,ps);
            munmap(tr,16);
            return -1;
        }
        memcpy(shadow,(const void *)page,(size_t)ps);

        errno=0;
        fixed=mmap((void *)page,(size_t)ps,PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANON|MAP_FIXED,-1,0);
        if(fixed==MAP_FAILED || fixed!=(void *)page){
            saved_errno=errno;
            logf_u2("%s anonymous MAP_FIXED clone failed wanted=%p got=%p size=%ld errno=%d %s",
                    name,(void *)page,fixed,ps,saved_errno,strerror(saved_errno));
            free(shadow);
            munmap(tr,16);
            return -1;
        }
        memcpy(fixed,shadow,(size_t)ps);
        free(shadow);
        src=(uint32_t *)target;
        logf_u2("%s target page cloned private RW at %p size=%ld",
                name,(void *)page,ps);
    }

    src[0]=0xe51ff004u;
    src[1]=(uint32_t)(uintptr_t)replacement;
    __builtin___clear_cache((char *)src,(char *)src+8);

    errno=0;
    if(mprotect((void *)page,(size_t)ps,PROT_READ|PROT_EXEC)!=0){
        saved_errno=errno;
        logf_u2("%s target restore RX failed page=%p size=%ld errno=%d %s",
                name,(void *)page,ps,saved_errno,strerror(saved_errno));
        /* The hook is already installed. Keep the trampoline authoritative. */
    }

    *trampoline=tr;
    logf_u2("installed ARM hook %s target=%p replacement=%p trampoline=%p page=%p pagesize=%ld",
            name,target,replacement,tr,(void *)page,ps);
    return 0;
#else
    (void)target;(void)replacement;(void)trampoline;(void)name;
    (void)expected0;(void)expected1;(void)allow_page_clone;
    return 0; /* host syntax-test only */
#endif
}

#ifndef ALT111_TARGET_MHI2Q
/* MU1440 only: MHI2Q installs per-symbol with allow_page_clone=0. */
static int install_arm_hook(void *target, void *replacement, void **trampoline, const char *name)
{
    return install_arm_hook_expected(target,replacement,trampoline,name,0xe92d4ff0u,0xed2d8b02u,1);
}
#endif

/*
 * Resolve the stock delegates. Idempotent and never fatal: the MU1440
 * constructor calls it before hooking, the MHI2Q lazy start calls it, and every
 * call_stock_* retries it when its pointer is still NULL, so an interposed call
 * always reaches stock.
 */
static void gen2_resolve_stock_targets(void)
{
    if(!g_real_setup)g_real_setup=(fn_setup_t)sym_next("AirPlayReceiverSessionSetup");
    if(!g_real_start)g_real_start=(fn_start_t)sym_next("AirPlayReceiverSessionStart");
    if(!g_real_teardown)g_real_teardown=(fn_teardown_t)sym_next("AirPlayReceiverSessionTearDown");
    if(!g_real_platform_control)
        g_real_platform_control=(fn_platform_control_t)sym_next("AirPlayReceiverSessionPlatformControl");
    if(!g_real_session_control)
        g_real_session_control=(fn_session_control_t)sym_next("AirPlayReceiverSessionControl");
    if(!g_make_mode_state)
        g_make_mode_state=(fn_make_mode_state_t)sym_next("AirPlayReceiverSessionMakeModeStateFromDictionary");
    if(!g_real_serverinfo)g_real_serverinfo=(fn_serverinfo_t)sym_next("AirPlayCopyServerInfo");
}

static OSStatus call_stock_setup(AirPlayReceiverSessionRef s, CFDictionaryRef request, CFDictionaryRef *outResponse)
{
    fn_setup_t fn = g_setup_trampoline ? g_setup_trampoline : g_real_setup;
    if(!fn){ gen2_resolve_stock_targets(); fn=g_real_setup; }
    if(!fn){
        logf_u2("FATAL no stock SessionSetup delegate");
        return -1;
    }
    return fn(s,request,outResponse);
}

static OSStatus call_stock_start(AirPlayReceiverSessionRef s, void *info)
{
    fn_start_t fn = g_start_trampoline ? g_start_trampoline : g_real_start;
    if(!fn){ gen2_resolve_stock_targets(); fn=g_real_start; }
    if(!fn){
        logf_u2("FATAL no stock SessionStart delegate");
        return -1;
    }
    return fn(s,info);
}

static void call_stock_teardown(AirPlayReceiverSessionRef s, CFDictionaryRef request, OSStatus reason, Boolean *outDone)
{
    fn_teardown_t fn = g_teardown_trampoline ? g_teardown_trampoline : g_real_teardown;
    if(!fn){ gen2_resolve_stock_targets(); fn=g_real_teardown; }
    if(!fn){
        logf_u2("FATAL no stock SessionTearDown delegate");
        if(outDone)*outDone=1;
        return;
    }
    fn(s,request,reason,outDone);
}


static OSStatus call_stock_session_control(AirPlayReceiverSessionRef session, uint32_t flags,
                                           CFStringRef command, CFTypeRef qualifier,
                                           CFDictionaryRef params, CFDictionaryRef *outParams)
{
    fn_session_control_t fn=g_session_control_trampoline?
        g_session_control_trampoline:g_real_session_control;
    if(!fn){ gen2_resolve_stock_targets(); fn=g_real_session_control; }
    if(!fn){
        logf_u2("FATAL no stock SessionControl delegate");
        return -1;
    }
    return fn(session,flags,command,qualifier,params,outParams);
}

static OSStatus mibr_session_control(AirPlayReceiverSessionRef session, uint32_t flags,
                                     CFStringRef command, CFTypeRef qualifier,
                                     CFDictionaryRef params, CFDictionaryRef *outParams)
{
    OSStatus e;
    char cbuf[96];
    const char *c;
    int private111_active=0;

    gen2_runtime_ready();
    flight_record_session_control(session,flags,command,qualifier,params,outParams);
    e=call_stock_session_control(session,flags,command,qualifier,params,outParams);
    c=flight_command_cstr(command,cbuf,sizeof(cbuf));

    pthread_mutex_lock(&g2_core_lock);
    private111_active = g2_control_session ? 1 : 0;
    pthread_mutex_unlock(&g2_core_lock);

    logf_u2("GEN2 FLIGHT SessionControl exit session=%p command=%s commandRef=%p rc=%d out=%p private111=%d",
            session,c,(void *)command,(int)e,
            (outParams&&*outParams)?(void *)*outParams:NULL,private111_active);
    if(outParams&&*outParams)
        flight_dump_value(c,"sessionControl.outParams",(CFTypeRef)*outParams,0);

    /*
     * Observation-only lifecycle hook.  Exact stock RE proves inbound
     * modesChanged is handled here before PlatformControl fallback.  Do not
     * alter return values or arm Candidate D from this event yet.
     */
    if(c && strcmp(c,"modesChanged")==0){
        mibr_mode_state_t parsed;
        mibr_mode_state_t previous;
        OSStatus prc=-1;
        unsigned previous_valid=0;
        unsigned delta=0;
        uint64_t seq=0, source_idrs=0, source_aus=0;
        unsigned projection=0;

        memset(&parsed,0,sizeof(parsed));
        memset(&previous,0,sizeof(previous));
        if(g_make_mode_state && params)
            prc=g_make_mode_state(session,params,&parsed);

        pthread_mutex_lock(&g2_core_lock);
        previous_valid=g2_last_mode_valid;
        previous=g2_last_mode_state;
        if(prc==K_NO_ERR){
            if(previous_valid){
                if(previous.screen!=parsed.screen) delta|=1u<<0;
                if(previous.main_audio!=parsed.main_audio) delta|=1u<<1;
                if(previous.speech!=parsed.speech) delta|=1u<<2;
                if(previous.speech_detail!=parsed.speech_detail) delta|=1u<<3;
                if(previous.phone!=parsed.phone) delta|=1u<<4;
                if(previous.turns!=parsed.turns) delta|=1u<<5;
            }else{
                delta=0x3fu;
            }
            g2_last_mode_state=parsed;
            g2_last_mode_valid=1;
            ++g2_mode_sequence;
        }
        seq=g2_mode_sequence;
        projection=g2_control.desired;
        source_idrs=g2_video.source_idrs;
        source_aus=g2_video.source_aus;
        pthread_mutex_unlock(&g2_core_lock);

        logf_u2("GEN2 FLIGHT MODES_CHANGED parsed rc=%d seq=%llu delta=0x%02x screen=%d mainAudio=%d speech=%d speechDetail=%d phone=%d turns=%d projection=%u source_aus=%llu source_idrs=%llu private111=%d policy=OBSERVE_ONLY",
                (int)prc,(unsigned long long)seq,delta,
                parsed.screen,parsed.main_audio,parsed.speech,parsed.speech_detail,
                parsed.phone,parsed.turns,projection,
                (unsigned long long)source_aus,(unsigned long long)source_idrs,
                private111_active);
        if(prc==K_NO_ERR && previous_valid && (delta & (1u<<5)))
            gen2_d2_schedule(G2_D2_SOURCE_TURNS,"turns-edge");
        gen2_publish_status();
    }

    return e;
}

OSStatus AirPlayReceiverSessionControl(AirPlayReceiverSessionRef session, uint32_t flags,
                                       CFStringRef command, CFTypeRef qualifier,
                                       CFDictionaryRef params, CFDictionaryRef *outParams)
{
    return mibr_session_control(session,flags,command,qualifier,params,outParams);
}

static OSStatus call_stock_platform_control(AirPlayReceiverSessionRef session, uint32_t flags,
                                            CFStringRef command, CFTypeRef qualifier,
                                            CFDictionaryRef params, CFDictionaryRef *outParams)
{
    fn_platform_control_t fn=g_platform_control_trampoline?
        g_platform_control_trampoline:g_real_platform_control;
    if(!fn){ gen2_resolve_stock_targets(); fn=g_real_platform_control; }
    if(!fn){
        logf_u2("FATAL no stock PlatformControl delegate");
        return -1;
    }
    return fn(session,flags,command,qualifier,params,outParams);
}

static OSStatus mibr_platform_control(AirPlayReceiverSessionRef session, uint32_t flags,
                                      CFStringRef command, CFTypeRef qualifier,
                                      CFDictionaryRef params, CFDictionaryRef *outParams)
{
    OSStatus e;
    char cbuf[96];
    const char *c;
    int private111_active=0;

    gen2_runtime_ready();
    flight_record_platform_control(session,flags,command,qualifier,params,outParams);
    e=call_stock_platform_control(session,flags,command,qualifier,params,outParams);
    c=flight_command_cstr(command,cbuf,sizeof(cbuf));

    logf_u2("GEN2 FLIGHT PlatformControl exit session=%p command=%s commandRef=%p rc=%d out=%p",
            session,c,(void *)command,(int)e,
            (outParams&&*outParams)?(void *)*outParams:NULL);
    if(outParams&&*outParams)
        flight_dump_value(c,"outParams",(CFTypeRef)*outParams,0);

    /*
     * Public vehicle-proven MHI2Q AltScreen parity:
     *
     * The Yuedi/LIVI-derived receiver lets stock observe suggestUI, records
     * stock_rc, but acknowledges the command to the phone with success and
     * deliberately does NOT translate the suggested URL list into showUI.
     *
     * MU1440 stock returns kNotHandledErr (-6714) because stock has no
     * auxiliary cluster presenter.  Once our private type-111 control session
     * is active, propagating that stock-only error back to iOS is therefore
     * the wrong ownership contract.  Keep every other PlatformControl command
     * stock-authoritative.
     */
    if(g_enabled && c && strcmp(c,"suggestUI")==0){
        pthread_mutex_lock(&g2_core_lock);
        private111_active = g2_control_session ? 1 : 0;
        pthread_mutex_unlock(&g2_core_lock);
        if(private111_active){
            gen2_d2_schedule(G2_D2_SOURCE_SUGGEST_UI,"suggestUI");
            logf_u2("GEN2 PARITY SUGGEST_UI_ACCEPTED session=%p stock_rc=%d return_rc=0 policy=LIVI_NOOP urls_not_shown=1",
                    session,(int)e);
            return K_NO_ERR;
        }
    }

    return e;
}

OSStatus AirPlayReceiverSessionPlatformControl(AirPlayReceiverSessionRef session, uint32_t flags,
                                               CFStringRef command, CFTypeRef qualifier,
                                               CFDictionaryRef params, CFDictionaryRef *outParams)
{
    return mibr_platform_control(session,flags,command,qualifier,params,outParams);
}

static OSStatus mibr_session_setup(AirPlayReceiverSessionRef s, CFDictionaryRef request, CFDictionaryRef *outResponse)
{
    CFDictionaryRef altDesc=NULL;
    CFDictionaryRef stockResp=NULL;
    int other=0,hasAlt=0;
    OSStatus e;
    int port=-1;
    uint64_t cid=0;

    gen2_runtime_ready();
    log_stream_types("SETUP entry",request);

    /*
     * Recovered IRC ordering: pass the original request to stock first.
     * Only after a successful stock response advertise root capabilities and
     * inspect the original request for stream 111.
     */
    e=call_stock_setup(s,request,&stockResp);
    logf_u2("SETUP stock result=%d response=%s",(int)e,stockResp?"yes":"no");
#ifdef ALT111_TARGET_MHI2Q
    /*
     * 210.81 has no type-111 branch. If it fails a request carrying stream 111,
     * give stock the request it understands and append 111 ourselves as usual.
     */
    if(e!=K_NO_ERR && g_enabled && contains_stream111(request,NULL,&other)){
        CFMutableDictionaryRef stripped=clone_without_111(request);
        logf_u2("MHI2Q stock SETUP rejected a stream111 request rc=%d; retrying without 111 otherStreams=%d",
                (int)e,other);
        if(stockResp){p_CFRelease(stockResp);stockResp=NULL;}
        if(stripped){
            e=call_stock_setup(s,stripped,&stockResp);
            p_CFRelease(stripped);
            logf_u2("MHI2Q stock SETUP retry result=%d response=%s",(int)e,stockResp?"yes":"no");
        }
        other=0;
    }
#endif
    if(e==K_NO_ERR && g_enabled) set_active_session(s);
    if(e!=K_NO_ERR){
        if(outResponse)*outResponse=stockResp;
        else if(stockResp)p_CFRelease(stockResp);
        return e;
    }

    /*
     * A partially installed inline-hook set must be indistinguishable from
     * stock once the constructor disables GEN2. In particular, do not mutate
     * enabledFeatures when SessionSetup was hooked successfully but a later
     * SessionStart/TearDown hook failed.
     */
    if(!g_enabled){
        if(outResponse)*outResponse=stockResp;
        else if(stockResp)p_CFRelease(stockResp);
        return e;
    }

    if(stockResp && advertise_enabled())
        set_reference_enabled_features((CFMutableDictionaryRef)stockResp);

    hasAlt=g_enabled && contains_stream111(request,&altDesc,&other);
    if(!hasAlt){
        if(outResponse)*outResponse=stockResp;
        else if(stockResp)p_CFRelease(stockResp);
        return e;
    }

    cid=stream_connection_id(altDesc);
    logf_u2("GEN2 SETUP contains stream111 cid=%llu otherStreams=%d",
            (unsigned long long)cid,other);
    publish_state("setup");

    if(!cid){
        logf_u2("stream111 SETUP fail-soft: missing streamConnectionID");
        publish_state("setup_failed");
    }else{
        port=start_alt_receiver(cid);
        if(port>0 && stockResp){
            append_alt_setup_response((CFMutableDictionaryRef)stockResp,altDesc,port);
            gen2_control_projection_on();
#ifdef ALT111_TARGET_MHI2Q
            /* SessionStart is not a proven PLT seam on 210.81; a stream-111 SETUP
             * only arrives on a running session, so arm the control worker here. */
            gen2_set_command_ready(1);
#endif
            logf_u2("GEN2 stream111 SETUP accepted cid=%llu dataPort=%d response=cloned-request+streamID111; UI acquisition armed",
                    (unsigned long long)cid,port);
        }else{
            if(port>0) stop_alt_receiver();
            clear_video_observer();
            logf_u2("stream111 SETUP fail-soft: receiver/key setup failed cid=%llu",
                    (unsigned long long)cid);
            publish_state("setup_failed");
        }
    }

    if(outResponse)*outResponse=stockResp;
    else if(stockResp)p_CFRelease(stockResp);
    return e;
}

static OSStatus mibr_session_start(AirPlayReceiverSessionRef s, void *info)
{
    OSStatus e;
    gen2_runtime_ready();
    e=call_stock_start(s,info);
    if(e==K_NO_ERR && g_enabled) {
        set_active_session(s);
        gen2_set_command_ready(1);
    }
    /* Gen-2 ownership commands are serialized by gen2_control_worker. */
    return e;
}

static void clear_master_key(void)
{
    pthread_mutex_lock(&g_lock);
    memset(g_master_key, 0, sizeof(g_master_key));
    g_master_valid = 0;
    pthread_mutex_unlock(&g_lock);
    logf_u2("cleared captured CarPlay master AES key");
}

static void mibr_session_teardown(AirPlayReceiverSessionRef s, CFDictionaryRef request, OSStatus reason, Boolean *outDone)
{
    int other=0;
    int has_alt;

    gen2_runtime_ready();
    if(!g_enabled){
        call_stock_teardown(s,request,reason,outDone);
        return;
    }

    has_alt = request && contains_stream111(request,NULL,&other);
    int has_main = request && contains_stream_type(request,110);
    /*
     * Lifecycle-decoupling probe:
     *
     * Stream 111 is a private secondary display and must not be destroyed just
     * because stock tears down screen 110.  Only an explicit 111 teardown or a
     * true whole-session teardown (NULL request) owns private111 cleanup.
     *
     * Keep the retained AirPlay session reference for the probe so showUI /
     * forceKeyFrame can still be exercised after a 110-only lifecycle event.
     * If the underlying AirPlay session is actually dead, SendCommand will
     * fail and the diagnostic log will tell us; do not hide that by rebuilding
     * 111 or by adding timing workarounds here.
     */
    int full_session = (request == NULL);

    logf_u2("GEN2 TEARDOWN entry request=%s reason=%d alt=%d main110=%d otherStreams=%d private111_decoupled=1",
            request?"dict":"NULL",(int)reason,has_alt,has_main,other);

    if(has_alt){
        logf_u2("GEN2 TEARDOWN explicit stream111 otherStreams=%d main110=%d",other,has_main);
        gen2_control_release();
        if(full_session) gen2_set_command_ready(0);
        stop_alt_receiver();
        clear_video_observer();
        publish_state("idle");
        if(other==0){if(outDone)*outDone=0;return;}
        {
            CFMutableDictionaryRef f=clone_without_111(request);
            call_stock_teardown(s,f?f:request,reason,outDone);
            if(f)p_CFRelease(f);
        }
        if(full_session) {
            set_active_session(NULL);
            clear_master_key();
        }
        return;
    }

    if(full_session) {
        logf_u2("GEN2 TEARDOWN whole-session -> private111 cleanup");
        gen2_control_release();
        gen2_set_command_ready(0);
        stop_alt_receiver();
        clear_video_observer();
        publish_state("idle");
    } else if(has_main) {
        logf_u2("GEN2 TEARDOWN stock110-only -> preserving private111 transport/control state");
    }

    call_stock_teardown(s,request,reason,outDone);

    if(full_session) {
        set_active_session(NULL);
        clear_master_key();
    }
}

OSStatus AirPlayReceiverSessionSetup(AirPlayReceiverSessionRef s, CFDictionaryRef request, CFDictionaryRef *outResponse)
{
    return mibr_session_setup(s,request,outResponse);
}

OSStatus AirPlayReceiverSessionStart(AirPlayReceiverSessionRef s, void *info)
{
    return mibr_session_start(s,info);
}

void AirPlayReceiverSessionTearDown(AirPlayReceiverSessionRef s, CFDictionaryRef request, OSStatus reason, Boolean *outDone)
{
    mibr_session_teardown(s,request,reason,outDone);
}

/* Capture the master session key at the exact stock SetSecurityInfo caller. */
__attribute__((noinline))
OSStatus AES_CBCFrame_Init(void *ctx, const uint8_t key[16], const uint8_t iv[16], Boolean encrypt)
{
    void *ra=__builtin_return_address(0);
    OSStatus e;
    gen2_runtime_ready();
    if(!g_real_aes_cbc_init)g_real_aes_cbc_init=(fn_aes_cbc_init_t)sym_next("AES_CBCFrame_Init");
    e=g_real_aes_cbc_init(ctx,key,iv,encrypt);
    if(g_enabled && e==K_NO_ERR && !encrypt && g_security_fn &&
       (uintptr_t)ra>=g_security_fn && (uintptr_t)ra<g_security_fn+0x100u){
        pthread_mutex_lock(&g_lock); memcpy(g_master_key,key,16); g_master_valid=1; pthread_mutex_unlock(&g_lock);
        logf_u2("captured stock CarPlay master AES key at SetSecurityInfo caller");
    }
    return e;
}

static int gen2_configure_profile(void)
{
    alt111_profile_mu1440(&g2_profile,0);
    if(g_width<=0 || g_height<=0 || g_width_mm<=0 || g_height_mm<=0 ||
       g_fps<=0 || g_fps>60){
        logf_u2("gen2 invalid runtime geometry/fps %dx%d physical=%dx%d fps=%d",
                g_width,g_height,g_width_mm,g_height_mm,g_fps);
        return -1;
    }
    if(strlen(g_alt_uuid)!=36u){
        logf_u2("gen2 invalid secondary display UUID length=%zu",strlen(g_alt_uuid));
        return -1;
    }
    memcpy(g2_profile.uuid,g_alt_uuid,37u);
    g2_profile.width=(uint32_t)g_width;
    g2_profile.height=(uint32_t)g_height;
    g2_profile.width_mm=(uint32_t)g_width_mm;
    g2_profile.height_mm=(uint32_t)g_height_mm;
    g2_profile.max_fps=(uint32_t)g_fps;
    g2_profile.views[0].area.width=g2_profile.width;
    g2_profile.views[0].area.height=g2_profile.height;
    g2_profile.views[0].safe=g2_profile.views[0].area;
    if(alt111_profile_validate(&g2_profile)!=ALT111_OK){
        logf_u2("gen2 runtime profile validation failed");
        return -1;
    }
    if(alt111_control_init(&g2_control,g2_profile.view_count)!=ALT111_OK){
        logf_u2("gen2 control init failed");
        return -1;
    }
    alt111_video_init(&g2_video);
    alt111_resync_init(&g2_resync);
    return 0;
}

#ifdef ALT111_TARGET_MHI2Q
/* 1 if the page can be made writable in place. Never MAP_FIXED: this runs
 * after AirPlay threads exist, and replacing their text page crashes them. */
static int text_page_mprotect_ok(void *target)
{
#if defined(__arm__)
    long ps=sysconf(_SC_PAGESIZE);
    uintptr_t page;
    if(!target || ps<=0) return 0;
    page=((uintptr_t)target)&~((uintptr_t)ps-1u);
    if(mprotect((void *)page,(size_t)ps,PROT_READ|PROT_WRITE)!=0) return 0;
    if(mprotect((void *)page,(size_t)ps,PROT_READ|PROT_EXEC)!=0){
        logf_u2("MHI2Q inline hooks: restore RX failed page=%p errno=%d %s",
                (void *)page,errno,strerror(errno));
        return 0;
    }
    return 1;
#else
    (void)target;
    return 1;
#endif
}

/*
 * Optional inline-hook install for MHI2Q (ALTSCREEN111_INLINE_HOOKS=1). Default
 * is PLT interposition. Prologue expectations are the MU1438 offline values.
 * A mismatch or a text page that needs MAP_FIXED leaves that symbol on PLT.
 */
static void gen2_install_inline_hooks(void)
{
    void *setup=sym_next("AirPlayReceiverSessionSetup");
    void *start=sym_next("AirPlayReceiverSessionStart");
    void *td=sym_next("AirPlayReceiverSessionTearDown");
    void *platform=sym_next("AirPlayReceiverSessionPlatformControl");
    void *control=sym_next("AirPlayReceiverSessionControl");
    if(!setup||!start||!td||!platform||!control){
        logf_u2("MHI2Q inline hooks: missing target(s); staying on PLT interposition only");
        return;
    }
    if(!text_page_mprotect_ok(setup)||!text_page_mprotect_ok(start)||!text_page_mprotect_ok(td)
       ||!text_page_mprotect_ok(platform)||!text_page_mprotect_ok(control)){
        logf_u2("MHI2Q inline hooks: text is not writable without MAP_FIXED; staying on PLT");
        return;
    }
    if(install_arm_hook_expected(setup,(void*)mibr_session_setup,
                                 (void**)&g_setup_trampoline,"SessionSetup(MHI2Q)",
                                 0xe92d4ff0u,0xed2d8b02u,0)!=0 ||
       install_arm_hook_expected(start,(void*)mibr_session_start,
                                 (void**)&g_start_trampoline,"SessionStart(MHI2Q)",
                                 0xe92d4ff0u,0xed2d8b02u,0)!=0 ||
       install_arm_hook_expected(td,(void*)mibr_session_teardown,
                                 (void**)&g_teardown_trampoline,"SessionTearDown(MHI2Q)",
                                 0xe92d4ff0u,0xed2d8b02u,0)!=0 ||
       install_arm_hook_expected(platform,(void*)mibr_platform_control,
                                 (void**)&g_platform_control_trampoline,
                                 "PlatformControl(MHI2Q)",0xe92d4ff0u,0xed2d8b04u,0)!=0 ||
       install_arm_hook_expected(control,(void*)mibr_session_control,
                                 (void**)&g_session_control_trampoline,
                                 "SessionControl(MHI2Q)",0xe92d4ff0u,0xe1a06002u,0)!=0){
        logf_u2("MHI2Q inline hooks: install incomplete; unpatched symbols stay on PLT");
        return;
    }
    logf_u2("MHI2Q inline hooks installed");
}
#endif

/*
 * Deferred runtime start (threads, dlsym, servers). Runs once from the first
 * interposed AirPlay call, never from the ELF constructor, so no pthread or
 * loader work happens while the QNX loader lock is held (mib2q-carplay-rgi
 * K1004). MHI2Q only; MU1440 finishes init in the constructor.
 */
#ifdef ALT111_TARGET_MHI2Q
static void gen2_runtime_start(void)
{
    if(!g_enabled) return;
    if(init_api()!=0){
        g_enabled=0; publish_state("error");
        logf_u2("GEN2 lazy init: API resolution failed"); return;
    }
    gen2_resolve_stock_targets();
    if(gen2_start_workers()!=0){
        g_enabled=0; publish_state("error");
        logf_u2("GEN2 lazy init: worker start failed"); return;
    }
    if(start_tee_server()!=0){
        g_enabled=0; publish_state("error");
        logf_u2("GEN2 lazy init: local H264 tee bind failed; disabled fail-closed"); return;
    }
    if(start_capture_server()!=0)
        logf_u2("GEN2 lazy init: diagnostic capture mirror unavailable; production path unchanged");
#ifdef ALT111_TARGET_MHI2Q
    if(g_mhi2q_want_inline) gen2_install_inline_hooks();
#endif
    g_runtime_ok=1;
    publish_state("ready");
    {
        char active_url[384];
        active_alt_url_copy(active_url,sizeof(active_url));
        logf_u2("GEN2 lazy runtime ready target=%s 111=%dx%d@%d physical=%dx%d altPort=%d tee=%d capture=%d URL=%s uuid=%s viewAreas=%d",
                ALT111_TARGET_NAME,g_width,g_height,g_fps,g_width_mm,g_height_mm,
                g_alt_port,g_tee_port,g_capture_port,active_url,g_alt_uuid,g_viewareas);
    }
}
#endif /* ALT111_TARGET_MHI2Q */

static void gen2_runtime_ready(void)
{
#ifdef ALT111_TARGET_MHI2Q
    (void)pthread_once(&g_runtime_once,gen2_runtime_start);
#else
    (void)g_runtime_once; (void)g_runtime_ok;
#endif
}

__attribute__((constructor))
static void altscreen111_init(void)
{
    char active_url[384];
#ifndef ALT111_TARGET_MHI2Q
    void *setup,*start,*td,*platform,*control;
#endif
    g_enabled=env_i("ALTSCREEN111_ENABLED",1);
    g_alt_port=env_i("ALTSCREEN111_PORT",ALT111_DEFAULT_PORT);
    g_tee_port=env_i("ALTSCREEN111_TEE_PORT",19820);
    g_capture_port=env_i("ALTSCREEN111_CAPTURE_PORT",19821);
    g_width=env_i("ALTSCREEN111_WIDTH",ALT111_DEFAULT_WIDTH);
    g_height=env_i("ALTSCREEN111_HEIGHT",ALT111_DEFAULT_HEIGHT);
    /* Physical geometry defaults are per-target (ALT111_DEFAULT_* block). */
    g_width_mm=env_i("ALTSCREEN111_WIDTH_MM",ALT111_DEFAULT_WIDTH_MM);
    g_height_mm=env_i("ALTSCREEN111_HEIGHT_MM",
                      g_width>0 ? (g_width_mm*g_height)/g_width
                                : (ALT111_DEFAULT_WIDTH_MM*ALT111_DEFAULT_HEIGHT)/ALT111_DEFAULT_WIDTH);
    g_fps=env_i("ALTSCREEN111_FPS",30);
    /*
     * Reference IRC does not prove startup showUI/forceKeyFrame. Ignore the
     * legacy AUTO_SHOW=1 package default unless parity-specific opt-in is set.
     */
    g_auto_show=0; /* Gen-2 control worker owns UI acquisition. */
    if(access(g_autoshow_disable_marker,F_OK)==0)g_auto_show=0;
    g_advertise=env_i("ALTSCREEN111_ADVERTISE",1);
    g_viewareas=env_i("ALTSCREEN111_PARITY_VIEWAREAS",1);
    if(access(g_viewareas_marker,F_OK)==0)g_viewareas=1;
    /* Do not inherit Run117/118 legacy UUID/URL env values in the parity build. */
    env_s("ALTSCREEN111_PARITY_UUID",g_alt_uuid,sizeof(g_alt_uuid),ALT_UUID_DEFAULT);
    env_s("ALTSCREEN111_PARITY_URL",g_alt_url,sizeof(g_alt_url),ALT_URL_DEFAULT);
    clear_video_observer();
    publish_state(g_enabled ? "initializing" : "disabled");
    if(!g_enabled)return;

#ifdef ALT111_TARGET_MHI2Q
    /*
     * No dlsym and no SIGPIPE change in .init. dio_manager snapshots SIGPIPE
     * on its first Cinemo call; SIG_IGN here would freeze that snapshot.
     * init_api() runs from gen2_runtime_start and can retry.
     */
    if(gen2_configure_profile()!=0){
        g_enabled=0;
        publish_state("error");
        return;
    }
    g_mhi2q_want_inline=env_i("ALTSCREEN111_INLINE_HOOKS",0);
    active_alt_url_copy(active_url,sizeof(active_url));
    logf_u2("GEN2 MHI2Q constructor complete (deferred runtime): 111=%dx%d@%d physical=%dx%d altPort=%d tee=%d capture=%d URL=%s uuid=%s viewAreas=%d inlineHooks=%d bit26Mode=%d",
            g_width,g_height,g_fps,g_width_mm,g_height_mm,g_alt_port,g_tee_port,g_capture_port,
            active_url,g_alt_uuid,g_viewareas,g_mhi2q_want_inline,airplay_bit26_mode());
    return;
#else
    signal(SIGPIPE,SIG_IGN);
    if(init_api()!=0){
        g_enabled=0;
        publish_state("error");
        logf_u2("GEN2 disabled: API initialization failed");
        return;
    }
    if(gen2_configure_profile()!=0){
        g_enabled=0;
        publish_state("error");
        return;
    }
    if(gen2_start_workers()!=0){
        g_enabled=0;
        logf_u2("gen2 worker start failed; disabled fail-closed");
        publish_state("error");
        return;
    }
    if(start_tee_server()!=0){
        g_enabled=0;
        publish_state("error");
        logf_u2("cannot start local H264 tee; AltScreen disabled fail-closed");
        return;
    }
    if(start_capture_server()!=0)
        logf_u2("GEN2 diagnostic H264 capture mirror unavailable; production path unchanged");
    setup=sym_next("AirPlayReceiverSessionSetup");
    start=sym_next("AirPlayReceiverSessionStart");
    td=sym_next("AirPlayReceiverSessionTearDown");
    platform=sym_next("AirPlayReceiverSessionPlatformControl");
    control=sym_next("AirPlayReceiverSessionControl");
    g_make_mode_state=(fn_make_mode_state_t)sym_next("AirPlayReceiverSessionMakeModeStateFromDictionary");
    g_real_setup=(fn_setup_t)setup;
    g_real_start=(fn_start_t)start;
    g_real_teardown=(fn_teardown_t)td;
    g_real_platform_control=(fn_platform_control_t)platform;
    g_real_session_control=(fn_session_control_t)control;
    if(!setup||!start||!td||!platform||!control){
        logf_u2("missing direct-hook target(s) setup=%p start=%p teardown=%p platform=%p control=%p",
                setup,start,td,platform,control);
        g_enabled=0;
        publish_state("error");
        return;
    }
    logf_u2("hook destinations setup=%p start=%p teardown=%p platform=%p control=%p modeParser=%p",
            (void *)mibr_session_setup,(void *)mibr_session_start,(void *)mibr_session_teardown,
            (void *)mibr_platform_control,(void *)mibr_session_control,(void *)g_make_mode_state);
    if(install_arm_hook(setup,(void *)mibr_session_setup,(void **)&g_setup_trampoline,"SessionSetup")!=0 ||
       install_arm_hook(start,(void *)mibr_session_start,(void **)&g_start_trampoline,"SessionStart")!=0 ||
       install_arm_hook(td,(void *)mibr_session_teardown,(void **)&g_teardown_trampoline,"SessionTearDown")!=0 ||
       install_arm_hook_expected(platform,(void *)mibr_platform_control,
                                 (void **)&g_platform_control_trampoline,"PlatformControl",
                                 0xe92d4ff0u,0xed2d8b04u,1)!=0 ||
       install_arm_hook_expected(control,(void *)mibr_session_control,
                                 (void **)&g_session_control_trampoline,"SessionControl",
                                 0xe92d4ff0u,0xe1a06002u,1)!=0){
        g_enabled=0;
        publish_state("error");
        logf_u2("direct hook install failed; disabled fail-closed");
        return;
    }
    publish_state("ready");
    active_alt_url_copy(active_url,sizeof(active_url));
    logf_u2("GEN2 candidate active: 111=%dx%d@%d physical=%dx%d altPort=%d tee=%d capture=%d URL=%s uuid=%s viewAreas=%d autoShow=%d bit26Mode=%d",
            g_width,g_height,g_fps,g_width_mm,g_height_mm,g_alt_port,g_tee_port,g_capture_port,active_url,g_alt_uuid,
            g_viewareas,g_auto_show,airplay_bit26_mode());
    logf_u2("GEN2 Candidate-D manual=%s D2_keyframes=%s eventDelayMs=%u watchdogMs=%u minGapMs=%u sessionMarker=%s persistentMarker=%s",
            access(g2_resync_enable_marker,F_OK)==0 ? "enabled" : "disabled",
            gen2_d2_enabled() ? "enabled" : "disabled",
            G2_D2_EVENT_DELAY_MS,G2_D2_WATCHDOG_MS,G2_D2_MIN_GAP_MS,
            g2_d2_enable_marker,g2_d2_persist_marker);
#endif
}
