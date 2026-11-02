/*
 * Host test for the aa_hook.c load-time gate: receiver profile match with and without the
 * validated gal build, and the unpinned safety marker (armed at start-up, cleared by the first
 * cluster frame or a clean exit, a leftover keeps Android Auto stock).
 * Build with -DAA_UNPINNED_PATH pointing at a temp file.
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
void aa_uiconfig_sink(void *sink) { (void)sink; }
void aa_uiconfig_media(void *media) { (void)media; }
void aa_relayout_tick(void *sink) { (void)sink; }

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

    puts("aa_hook_gate_test: pinned/unpinned receiver match, receiver mismatch, unpinned marker "
         "(arm, leftover refuses, unwritable, first frame, clean exit) PASS");
    return 0;
}
