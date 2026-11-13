/* Exercise the shipping URL builder and control worker intent, without a phone. */
#define ALT111_TARGET_MHI2Q 1
#include "../altscreen_hook/libaltscreen111_gen2.c"
#include <assert.h>

static void put(const char *path, const char *value) {
    FILE *f = fopen(path, "w"); assert(f); assert(fputs(value, f) >= 0); assert(!fclose(f));
}
int main(int argc, char **argv) {
    char layout[512], hide[512], turn[512], eta[512], wide[512], absent[512], url[384];
    char pausemark[512];
    assert(argc == 2);
#define PATH(name) snprintf(name,sizeof(name),"%s/" #name,argv[1])
    PATH(layout); PATH(hide); PATH(turn); PATH(eta); PATH(wide); PATH(absent);
    g_nav_maneuver_path=layout; g_nav_hide_guidance_path=hide;
    g_nav_show_maneuver_path=turn; g_nav_eta_path=eta; g_nav_wide_all_path=wide;
    g_nav_speed_path=g_nav_compass_path=absent;
    g_url_map_marker=g_url_mode_path=g_nav_query_enable_marker=g_nav_surface_path=g_rgi_url_disable_marker=absent;
    active_alt_url_copy(url,sizeof(url));
    assert(!strcmp(url,"maps:/car/instrumentcluster?showETA=no&showSpeedLimit=no&showCompass=no"));
    put(layout,"left"); active_alt_url_copy(url,sizeof(url)); assert(strstr(url,"maneuverLayout=leftAligned"));
    put(layout,"top"); active_alt_url_copy(url,sizeof(url)); assert(strstr(url,"maneuverLayout=topAligned"));
    put(layout,"rightAligned"); active_alt_url_copy(url,sizeof(url)); assert(strstr(url,"maneuverLayout=rightAligned"));
    unlink(layout);
    put(eta,"yes"); put(hide,"yes");
    alt111_control_init(&g2_control,3); g2_control_session=alt111_control_begin(&g2_control);
    alt111_control_intent(&g2_control,1,0); g2_control.shown_ack=1;
    altscreen111_set_rgi(1);
    assert(!g2_control.shown_ack); /* live route start queues SHOW */
    active_alt_url_copy(url,sizeof(url)); assert(strstr(url,"/map?showETA=no&maneuverLayout=topAligned&"));
    assert(!strstr(url,"maneuverLayout=no")); /* "no" = automatic layout, keeps side insets */
    put(wide,"yes"); active_alt_url_copy(url,sizeof(url));
    assert(strstr(url,"/map?showETA=no") && strstr(url,"showSpeedLimit=yes&showCompass=yes"));
    g2_control.shown_ack=1; altscreen111_set_rgi(0);
    assert(!g2_control.shown_ack);
    active_alt_url_copy(url,sizeof(url)); assert(strstr(url,"instrumentcluster?showETA=yes"));
    unlink(wide); unlink(hide); put(turn,"no");
    g2_control.shown_ack=1; altscreen111_set_rgi(1);
    assert(g2_control.shown_ack); /* independent GEM switches are unchanged on route edges */
    active_alt_url_copy(url,sizeof(url)); assert(strstr(url,"/map?showETA=yes"));
    unlink(turn); unlink(eta);
    /* Hidden cockpit map: information only without the opt-in marker; with it stopUI, and
     * showUI when the map is back, also when the option was switched off meanwhile. A
     * "shown" that meets the teardown's own stop must not queue a showUI. */
    PATH(pausemark); g_pause_hidden_marker=pausemark; unlink(pausemark);
    alt111_control_intent(&g2_control,1,0);
    assert(!altscreen111_set_hidden(1) && g2_control.desired==1);
    assert(!altscreen111_set_hidden(0) && g2_control.desired==1);
    put(pausemark,"");
    altscreen111_set_hidden(1); assert(g2_control.desired==0);
    altscreen111_set_hidden(0); assert(g2_control.desired==1);
    altscreen111_set_hidden(1); assert(g2_control.desired==0);
    unlink(pausemark);
    altscreen111_set_hidden(0); assert(g2_control.desired==1);
    altscreen111_set_hidden(1); assert(g2_control.desired==1);
    alt111_control_intent(&g2_control,0,0);            /* gen2_control_release's stop */
    altscreen111_set_hidden(0); assert(g2_control.desired==0);
    put(pausemark,"");
    altscreen111_set_hidden(1); altscreen111_set_hidden(0); assert(g2_control.desired==0);
    unlink(pausemark);
    puts("AltScreen settings: defaults, card placement, guidance edges, wide-view precedence, "
         "pause while hidden PASS");
    return 0;
}
