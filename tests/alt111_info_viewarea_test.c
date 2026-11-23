/*
 * Issue #68: the /info display entry states the starting view the way a VIEW press does.
 * Yandex Maps drew its first cluster frame off-centre until two VIEW presses sent
 * updateViewArea (index + adjacentViewAreas); /info gave only initialViewArea. Now /info
 * carries the same adjacentViewAreas list, and a layout Java sends before the session
 * (bus HELLO) becomes initialViewArea. Runs the shipping builders on a stub CF graph.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define ALT111_TARGET_MHI2Q 1
#include "../altscreen_hook/libaltscreen111_gen2.c"
#include "hw301_cf_stub.h"
#include <assert.h>

static CFStringRef s_string(void *a, const char *s, uint32_t enc) { return t_string(a, s, enc); }
static void s_set(CFMutableDictionaryRef d, CFTypeRef k, CFTypeRef v) { t_set(d, (void *)k, (void *)v); }
static void s_set_int(CFMutableDictionaryRef d, CFStringRef k, int64_t n) { t_set_int(d, (void *)k, n); }
static void s_release(CFTypeRef p) { t_release((void *)p); }
static void s_append(CFMutableArrayRef a, CFTypeRef v) { t_append(a, v); }
static CFTypeRef s_number(void *a, int type, const void *value)
{
    test_cf *n = t_new(4);
    (void)a;
    assert(type == 4); /* kCFNumberSInt64Type */
    memcpy(&n->number, value, sizeof(n->number));
    return n;
}

static test_cf *get(void *d, const char *key)
{
    void *k = t_string(NULL, key, 0);
    test_cf *v = t_get(d, k);
    t_release(k);
    return v;
}

/* adjacentViewAreas of a dict (an /info display entry or updateViewArea params) as a
 * bitmask of view indices; -1 when the key is absent. */
static int adjacent_mask(void *d)
{
    test_cf *a = get(d, "adjacentViewAreas");
    long i;
    int mask = 0;
    if (!a) return -1;
    assert(a->type == 2);
    for (i = 0; i < a->count; ++i) {
        assert(a->values[i]->type == 4 && a->values[i]->number >= 0 && a->values[i]->number < 3);
        mask |= 1 << a->values[i]->number;
    }
    return mask;
}

static int64_t number(void *d, const char *key)
{
    test_cf *v = get(d, key);
    assert(v && v->type == 4);
    return v->number;
}

/* The /info display entry for the current g_desired_view. */
static test_cf *info_display(void)
{
    test_cf *alt = t_new(1);
    add_reference_viewarea(alt);
    return alt;
}

/* The updateViewArea params for `view`. */
static test_cf *view_command(unsigned view)
{
    test_cf *req = command_update_view(view), *params;
    assert(req);
    params = t_retain(get(req, "params"));
    t_release(req);
    return params;
}

int main(int argc, char **argv)
{
    char logpath[512];
    test_cf *alt, *params;
    unsigned v;

    assert(argc == 2);
    snprintf(logpath, sizeof(logpath), "%s/altscreen111.log", argv[1]);
    g_log_path = logpath;
    p_CFStringCreateWithCString = s_string;
    p_CFDictionaryCreateMutable = t_dict_new;
    p_CFDictionarySetValue = s_set;
    p_CFDictionarySetInt64 = s_set_int;
    p_CFRelease = s_release;
    p_CFArrayCreateMutable = t_array_new;
    p_CFArrayAppendValue = s_append;
    p_CFNumberCreate = s_number;
    g_width = 1440; g_height = 540;
    g_view_count = 3;

    /* Wide cockpit: initialViewArea 0 and the adjacent views a VIEW press back to wide sends. */
    g_desired_view = 0;
    alt = info_display();
    assert(get(alt, "viewAreas") && get(alt, "viewAreas")->count == 3);
    assert(number(alt, "initialViewArea") == 0);
    assert(adjacent_mask(alt) == ((1 << 1) | (1 << 2)));
    t_release(alt);

    /* Every view: /info states exactly what updateViewArea to that view states. */
    for (v = 0; v < 3; ++v) {
        g_desired_view = (int)v;
        alt = info_display();
        params = view_command(v);
        assert(number(alt, "initialViewArea") == (int64_t)v);
        assert(number(params, "viewAreaIndex") == (int64_t)v);
        assert(adjacent_mask(alt) == (7 & ~(1 << v)));
        assert(adjacent_mask(alt) == adjacent_mask(params));
        t_release(params);
        t_release(alt);
    }

    /* Layout sent at bus HELLO, before any stream-111 session: kept for the next /info. */
    g_desired_view = 0;
    g2_control_session = 0;
    assert(altscreen111_view_area(1) == 0 && g_desired_view == 1);
    alt = info_display();
    assert(number(alt, "initialViewArea") == 1 && adjacent_mask(alt) == ((1 << 0) | (1 << 2)));
    t_release(alt);
    assert(altscreen111_view_area(1) == 0);   /* the repeat at video start changes nothing */
    assert(altscreen111_view_area(3) == -1 && g_desired_view == 1);

    /* One ViewArea (marker): /info as before, no adjacent list. */
    g_view_count = 1;
    g_desired_view = 0;
    alt = info_display();
    assert(number(alt, "initialViewArea") == 0 && adjacent_mask(alt) == -1);
    t_release(alt);
    /* ...and a stale multi-view choice falls back to view 0. */
    g_desired_view = 2;
    alt = info_display();
    assert(number(alt, "initialViewArea") == 0 && adjacent_mask(alt) == -1);
    t_release(alt);

    /* No CFNumberCreate: initialViewArea alone, as before (never a malformed list). */
    g_view_count = 3;
    g_desired_view = 1;
    p_CFNumberCreate = NULL;
    alt = info_display();
    if (!p_CFNumberCreate) assert(number(alt, "initialViewArea") == 1 && adjacent_mask(alt) == -1);
    t_release(alt);

    puts("AltScreen /info ViewAreas: adjacentViewAreas with initialViewArea, same list as "
         "updateViewArea, layout before the session kept, single view unchanged PASS");
    return 0;
}
