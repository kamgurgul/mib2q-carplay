/* Hold an incoming callback while another thread dispatches outgoing traffic. */
#define ENABLE_LOGGING 0
#include "../hook/framework/hook_framework.c"
#include <assert.h>

const hook_module_def_t * const hook_module_table[] = {NULL};
const size_t hook_module_table_count = 0;
void bus_shutdown(void) { }
static pthread_mutex_t test_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t test_cond = PTHREAD_COND_INITIALIZER;
static int incoming_entered, outgoing_done;
static const uint8_t incoming[] = {0x40,0x40,0,7,0x52,1,0xab};
static const uint8_t outgoing[] = {0x40,0x40,0,7,0x50,0,0xcd};
static bool handler(hook_context_t *ctx, const iap2_frame_t *frame) {
    if (frame->msgid == 0x5201) {
        pthread_mutex_lock(&test_lock);
        incoming_entered=1; pthread_cond_broadcast(&test_cond);
        while (!outgoing_done) pthread_cond_wait(&test_cond,&test_lock);
        pthread_mutex_unlock(&test_lock);
        assert(ctx->direction==MSG_DIR_INCOMING && ctx->msgid==0x5201);
        assert(ctx->raw_buf==incoming && ctx->raw_len==sizeof(incoming));
        assert(ctx->current_module==&g_fw.modules[0] && ctx->_priv==NULL);
    } else {
        assert(ctx->direction==MSG_DIR_OUTGOING && ctx->raw_buf==outgoing);
        assert(ctx->msgid==0x5000 && ctx->raw_len==sizeof(outgoing));
    }
    return false;
}
static void *receive(void *unused) {
    iap2_frame_t f={0}; (void)unused; f.msgid=0x5201;
    dispatch_message(&f,MSG_DIR_INCOMING,incoming,sizeof(incoming)); return NULL;
}
int main(void) {
    pthread_t thread; iap2_frame_t f={0}; alarm(5);
    g_fw.module_count=1; g_fw.modules[0].active=true; g_fw.modules[0].def.on_message=handler;
    assert(!pthread_create(&thread,NULL,receive,NULL));
    pthread_mutex_lock(&test_lock);
    while (!incoming_entered) pthread_cond_wait(&test_cond,&test_lock);
    pthread_mutex_unlock(&test_lock);
    f.msgid=0x5000; dispatch_message(&f,MSG_DIR_OUTGOING,outgoing,sizeof(outgoing));
    pthread_mutex_lock(&test_lock); outgoing_done=1; pthread_cond_broadcast(&test_cond); pthread_mutex_unlock(&test_lock);
    assert(!pthread_join(thread,NULL)); alarm(0);
    puts("iAP2 dispatch: overlapping incoming/outgoing callbacks preserve frame metadata PASS"); return 0;
}
