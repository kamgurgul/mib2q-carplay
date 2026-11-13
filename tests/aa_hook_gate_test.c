/*
 * Host test for the aa_hook.c load-time gate: receiver profile match with and without the
 * validated gal build, and the unpinned safety marker (armed at start-up, cleared by the first
 * cluster frame or a clean exit, a leftover keeps Android Auto stock), and the main-screen yield
 * interposers (only the main sink, only with the hook enabled, always forwarded with the callee's
 * return value). Build with -DAA_UNPINNED_PATH pointing at a temp file.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../aa_hook/aa_hook.c"

#undef NDEBUG
#include <assert.h>

/* The other hook units, not under test. */
int aa_tee_start(int port) { (void)port; return 0; }
void aa_tee_codec(const void *data, size_t size) { (void)data; (void)size; }
void aa_tee_frame(const void *data, size_t size) { (void)data; (void)size; }
void aa_tee_reset(const char *reason) { (void)reason; }
unsigned aa_nal_types(const void *data, size_t size) { (void)data; (void)size; return 0; }
void aa_nav_translate(unsigned char channel, const void *shared) { (void)channel; (void)shared; }
void aa_nav_set_channel(unsigned channel) { (void)channel; }
int aa_dpi(void) { return AA_DPI_DEFAULT; }
int aa_fps(void) { return 30; }
void aa_uiconfig_sink(void *sink) { (void)sink; }
void aa_uiconfig_media(void *media) { (void)media; }
void aa_relayout_tick(void *sink) { (void)sink; }
void aa_focus_tick(void *sink, aa_set_focus_fn set_focus) { (void)sink; (void)set_focus; }
void aa_focus_setup_done(void *sink) { (void)sink; }
int aa_tee_keyframe_wanted(void) { return 0; }

static int yields;
static const char *last_yield = "";
int aa_hw_yield(const char *why) { yields++; last_yield = why; return AA_YIELD_NONE; }

static int forwarded;
static int fake_sink_int(void *s, int v) { (void)s; forwarded++; return v + 100; }
static int fake_codec(void *s, void *d, unsigned n) { (void)s; (void)d; forwarded++; return (int)n + 200; }
static int fake_shutdown(void *r) { (void)r; forwarded++; return 7; }

static int acked;
static int fake_ack(void *sink, int session, unsigned n) { (void)sink; (void)session; (void)n; return ++acked; }

/* Point the resolved receiver symbols at the offsets of profile p from a fake load base. */
static void load_receiver(const struct aa_profile *p, uintptr_t base)
{
    real_register = (register_fn)(base + p->reg);
    sink_vtable = (void *)(base + p->sink_vt);
    base_vtable = (void *)(base + p->base_vt);
    real_config = (config_fn)(base + p->config);
    send_config = (sendconfig_fn)(base + p->send_config);
    set_focus = (focus_fn)(base + p->set_focus);
    ack_frames = (ack_fn)(base + p->ack);
}

static int exists(const char *path) { return access(path, F_OK) == 0; }

int main(void)
{
    const struct aa_profile *p5152 = &profiles[1], *got;
    int pinned;
    unsigned char sink[0x50] = {0};

    /* Validated combination: pinned, the exact profile. */
    load_receiver(p5152, 0x40000000u);
    got = match_profile(p5152->gal_controller, p5152->gal_vsink_vt, &pinned);
    assert(got == p5152 && pinned);
    got = match_profile(profiles[2].gal_controller, profiles[2].gal_vsink_vt, &pinned);
    assert(got == &profiles[2] && pinned);
    /* Known receiver, unknown gal build (or gal symbols missing): unpinned, first receiver match. */
    got = match_profile(0x261000u, 0x25a000u, &pinned);
    assert(got == &profiles[0] && !pinned);
    got = match_profile(0, 0, &pinned);
    assert(got && !pinned);
    /* One receiver offset differs: no profile at all, whatever gal is. */
    set_focus = (focus_fn)((uintptr_t)set_focus + 4);
    got = match_profile(p5152->gal_controller, p5152->gal_vsink_vt, &pinned);
    assert(!got && !pinned);
    load_receiver(p5152, 0x40000000u);

    /* Safety marker: armed once; a leftover refuses; done clears it. */
    unlink(AA_UNPINNED_PATH);
    assert(unpinned_begin(AA_UNPINNED_PATH) && exists(AA_UNPINNED_PATH));
    assert(!unpinned_begin(AA_UNPINNED_PATH));            /* previous run died: stay stock */
    unpinned = 1;
    unpinned_done("test");
    assert(!unpinned && !exists(AA_UNPINNED_PATH));
    assert(unpinned_begin(AA_UNPINNED_PATH));              /* next start may try again */
    unpinned = 0;
    unpinned_done("not armed");                            /* a pinned run never removes it */
    assert(exists(AA_UNPINNED_PATH));
    unlink(AA_UNPINNED_PATH);
    assert(!unpinned_begin(AA_UNPINNED_PATH "-missing-dir/x"));  /* cannot arm: stay stock */

    /* The first cluster frame clears the marker; a later frame leaves the state alone. */
    ack_frames = fake_ack;
    assert(unpinned_begin(AA_UNPINNED_PATH));
    unpinned = 1;
    secondary_data(sink, 0, NULL, 0);
    assert(acked == 1 && !unpinned && !exists(AA_UNPINNED_PATH));
    secondary_data(sink, 0, NULL, 0);
    assert(acked == 2 && !exists(AA_UNPINNED_PATH));

    /* Clean exit clears an armed marker (gal ended the session without a cluster frame). */
    assert(unpinned_begin(AA_UNPINNED_PATH));
    unpinned = 1;
    aa_hook_fini();
    assert(!unpinned && !exists(AA_UNPINNED_PATH));

    /* Main-screen yield (issue #48): before every main-sink lifecycle callback and the session
     * shutdown, never for another sink or with the hook disabled; always forwarded. */
    {
        unsigned char main_sink[0x50] = {0}, other_sink[0x50] = {0}, receiver[16] = {0};
        unsigned char codec[4] = {0};
        real_setup = real_start = real_stop = fake_sink_int;
        real_codec = fake_codec;
        real_prepare_shutdown = fake_shutdown;
        primary = main_sink;

        enabled = 0;
        assert(_ZN9VideoSink11handleSetupEi(main_sink, 3) == 103);
        assert(_ZN11GalReceiver15prepareShutdownEv(receiver) == 7);
        assert(yields == 0 && forwarded == 2);

        enabled = 1;
        assert(_ZN9VideoSink11handleSetupEi(main_sink, 3) == 103 && yields == 1 && !strcmp(last_yield, "setup"));
        assert(_ZN9VideoSink17handleCodecConfigEPvj(main_sink, codec, 4) == 204 && yields == 2 &&
               !strcmp(last_yield, "codec"));
        assert(_ZN9VideoSink13playbackStartEi(main_sink, 5) == 105 && yields == 3 && !strcmp(last_yield, "start"));
        assert(_ZN9VideoSink12playbackStopEi(main_sink, 5) == 105 && yields == 4 && !strcmp(last_yield, "stop"));
        assert(_ZN11GalReceiver15prepareShutdownEv(receiver) == 7 && yields == 5 &&
               !strcmp(last_yield, "session-teardown"));
        /* Another stock sink (or none known yet): forwarded, no yield. */
        assert(_ZN9VideoSink11handleSetupEi(other_sink, 3) == 103 && yields == 5);
        assert(_ZN9VideoSink12playbackStopEi(other_sink, 1) == 101 && yields == 5);
        primary = NULL;
        assert(_ZN9VideoSink13playbackStartEi(main_sink, 1) == 101 && yields == 5);
        assert(forwarded == 2 + 8);
        /* Unresolved callee (a receiver without the symbol never calls ours): no crash. */
        real_stop = NULL;
        assert(_ZN9VideoSink12playbackStopEi(main_sink, 1) == 0);
        enabled = 0;
    }

    puts("aa_hook_gate_test: pinned/unpinned receiver match, receiver mismatch, unpinned marker "
         "(arm, leftover refuses, unwritable, first frame, clean exit), main-screen yield "
         "interposers PASS");
    return 0;
}
