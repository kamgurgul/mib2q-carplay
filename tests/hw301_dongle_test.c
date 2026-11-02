#define _GNU_SOURCE
#include <pthread.h>
#include <sys/socket.h>
static int thread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
static int thread_detach(pthread_t);
static int bind_test(int, const struct sockaddr *, socklen_t);
#define pthread_create thread_create
#define pthread_detach thread_detach
#define bind bind_test
#define CLUSTER_STEP 3
#define STEP_DECRYPT 1
#define CLUSTER_DIAG 0
#define HW301_HOST_TEST 1
#include "../dongle/hw301/work/src/cluster_dual.c"
#include "hw301_cf_stub.h"
#undef pthread_create
#undef pthread_detach
#undef bind

static struct rx_arg *receiver;
static int create_error, bind_error, bind_attempts;
static CFRef fail_copy_src;
static int thread_create(pthread_t *t,const pthread_attr_t *a,void *(*fn)(void *),void *arg) {
    *t=pthread_self();(void)a;assert(fn==rx_thread && !receiver);
    if(create_error)return create_error;
    receiver=arg;return 0;
}
static int thread_detach(pthread_t t){(void)t;return 0;}
static int bind_test(int fd,const struct sockaddr *sa,socklen_t len){
    ++bind_attempts;
    if(bind_error){errno=EADDRINUSE;return -1;}
    return bind(fd,sa,len);
}
static CFRef copy_test(CFRef a,long n,CFRef source){
    return source==fail_copy_src ? NULL : t_copy(a,n,source);
}
static void clear_receiver(void){
    if(receiver){close(receiver->listen_fd);t_release(receiver->session);free(receiver);receiver=NULL;}
}
static void no_bind(void){}
static void bind_cf(void) {
    Dl_info di;
    cf_string=t_string;cf_release=t_release;cf_retain=t_retain;
    cf_get_type=t_type;cf_dict_type=t_dict_type;cf_array_type=t_array_type;
    cf_string_type=t_string_type;cf_number_type=t_number_type;
    cf_dict_new=t_dict_new;cf_dict_get=t_get;cf_dict_set=t_set;
    cf_dict_set_i64=t_set_int;r_get_i64=t_get_int;cf_dict_remove=t_remove;
    cf_dict_copy=copy_test;cf_array_copy=t_copy;cf_array_new=t_array_new;
    cf_array_append=t_append;cf_array_get=t_at;cf_array_count=t_count;
    cf_cstr_get=t_string_get;cf_cstr_ptr=t_string_ptr;
    k_type=s_cf("type");k_displays=s_cf("displays");k_features=s_cf("enabledFeatures");
    k_streams=s_cf("streams");k_conn=s_cf("streamConnectionID");
    pthread_once(&g_once,no_bind);g_ok=1;
    assert(dladdr((void *)bind_cf,&di));g_cpaa_base=di.dli_fbase;
    g_createplist_lo=0;g_createplist_hi=UINTPTR_MAX;
}
static CFRef dict(void){return t_dict_new(0,0,0,0);}
static CFRef array(void){return t_array_new(0,0,0);}
static void rect(CFRef d,int x,int y,int w,int h){
    set_i64(d,"originXPixels",x);set_i64(d,"originYPixels",y);
    set_i64(d,"widthPixels",w);set_i64(d,"heightPixels",h);
}
static CFRef profile(void) {
    CFRef root=dict(),d=dict(),main=dict(),displays=array(),areas=array();
    int i;
    set_i64(main,"type",110);set_i64(main,"widthPixels",1024);t_append(displays,main);
    set_i64(d,"type",111);set_i64(d,"widthPixels",1280);set_i64(d,"heightPixels",720);
    set_i64(d,"widthPhysical",250);set_i64(d,"heightPhysical",140);set_i64(d,"maxFPS",60);
    set_str(d,"uuid","12345678-2222-4000-8000-000000000099");
    set_str(d,"initialURL","maps:/car/instrumentcluster/map?showETA=yes");
    set_str(d,"futureCapability","preserve me");
    for(i=0;i<5;++i){
        CFRef area=dict(),safe=dict();rect(area,0,0,1280,720);rect(safe,20+i,30,900,600);
        set_value(area,"safeArea",safe);t_append(areas,area);t_release(safe);t_release(area);
    }
    set_value(d,"viewAreas",areas);set_i64(d,"initialViewArea",4);
    t_append(displays,d);set_value(root,"displays",displays);
    t_release(d);t_release(main);t_release(displays);t_release(areas);return root;
}
static CFRef input,output;
static int invalidate_calls;
static void invalidate_client(void *c){(void)c;++invalidate_calls;}
static CFRef plist_read(const void *p,size_t n,OSStatus *err){(void)p;(void)n;*err=0;return t_retain(input);}
static void *plist_write(CFRef d,size_t *n,OSStatus *err){output=t_retain(d);*n=1;*err=0;return (void *)1;}
static CFRef decode(CFRef d){OSStatus e=-1;CFRef r;input=d;r=CFBinaryPlistV0CreateWithData(NULL,0,&e);assert(e==0);return r;}
static CFRef encode(CFRef d){OSStatus e=-1;size_t n=0;assert(CFBinaryPlistV0Create(d,&n,&e)==(void *)1 && !e && n==1);return output;}
static CFRef stream_request(int type,uint64_t cid){
    CFRef root=dict(),streams=array(),sd=dict();set_i64(sd,"type",type);
    if(cid)set_i64(sd,"streamConnectionID",(int64_t)cid);
    t_append(streams,sd);set_value(root,"streams",streams);t_release(streams);t_release(sd);return root;
}
static void test_profile(void){
    CFRef root=profile(),base,phone,d=t_at(get_value(root,"displays"),1);
    int64_t n;
    assert(!augment_info(root)); /* already contains 111 */
    base=decode(root);assert(car_available() && t_count(get_value(base,"displays"))==1);
    assert(t_count(get_value(root,"displays"))==2); /* decode never mutates shared input */
    phone=augment_info(base);assert(t_count(get_value(phone,"displays"))==2);
    assert(ascii_eq(get_value(t_at(get_value(phone,"displays"),1),"futureCapability"),"preserve me"));
    assert(get_int(t_at(get_value(phone,"displays"),1),"widthPixels",0,2000,&n) && n==1280);
    assert(car_view_count()==5);
    set_i64(d,"widthPixels",0);capture_car_info(root);assert(!car_available());
    set_i64(d,"widthPixels",1280);
    set_i64(get_value(t_at(get_value(d,"viewAreas"),0),"safeArea"),"widthPixels",1280);
    capture_car_info(root);assert(!car_available());
    {CFRef k=s_cf("viewAreas");t_remove(d,k);t_release(k);}
    capture_car_info(root);assert(car_available() && car_view_count()==0); /* optional */
    capture_car_info(base);assert(!car_available());
    t_release(root);t_release(base);t_release(phone);
}
static uint8_t car_master[16];
static void *car_pair,*car_client=(void *)0x1234;
static void derive(const void *p,size_t n,uint64_t cid,uint8_t *key,uint8_t *iv){
    size_t i;assert(n==16);
    for(i=0;i<16;++i){key[i]=((const uint8_t *)p)[i]^(uint8_t)(cid>>(i%8*8));iv[i]=key[i]^0xa5;}
}
static int peer_error, peer_family=AF_INET6;
static int peer(void *client,void *out,size_t n,void *len){
    struct sockaddr_in6 *sa=out;(void)len;assert(client==car_client && n>=sizeof *sa);
    if(peer_error)return peer_error;
    sa->sin6_family=peer_family;sa->sin6_scope_id=7;sa->sin6_port=htons(5000);sa->sin6_addr=in6addr_loopback;return 0;
}
static void test_native_setup(void){
    CFRef root,req,out,sd,resp,filtered,identity=dict();uint64_t cid;
    uint8_t key[16],iv[16];int64_t n;
    set_str(identity,"model","iPhone9,1");set_str(identity,"osBuildVersion","18D70");out=encode(identity);
    assert(ascii_eq(get_value(out,"model"),"HW301") && ascii_eq(get_value(out,"osBuildVersion"),"18D70") &&
           !car_available());t_release(out);
    g_createplist_hi=1;out=encode(identity);assert(ascii_eq(get_value(out,"model"),"iPhone9,1"));
    g_createplist_hi=UINTPTR_MAX;t_release(out);t_release(identity);
    root=profile();filtered=decode(root);t_release(filtered);
    req=stream_request(110,123);out=encode(req);sd=find_stream(out,111);
    assert(sd && (cid=dict_cid(sd)) && cid!=123 && !find_stream(req,111));
    assert(ascii_eq(get_value(sd,"uuid"),"12345678-2222-4000-8000-000000000099"));
    assert(get_int(sd,"maxFPS",0,100,&n) && n==60 && !g_hu_key_valid && !g_car_port);
    resp=stream_request(111,cid+1);set_i64(find_stream(resp,111),"dataPort",6200);
    {CFRef main=dict();set_i64(main,"type",110);set_i64(main,"dataPort",6030);t_append(streams_of(resp),main);t_release(main);}
    filtered=decode(resp);assert(!find_stream(filtered,111) && find_stream(filtered,110) && !g_car_port);t_release(filtered);
    set_i64(find_stream(resp,111),"streamConnectionID",(int64_t)cid);
    filtered=decode(resp);assert(g_car_port==6200 && !find_stream(filtered,111));
    assert(((struct sockaddr_in6 *)&g_car_addr)->sin6_scope_id==7);
    memset(key,0xff,16);memset(iv,0xff,16);capture_car_key(key,iv);assert(!g_hu_key_valid);
    derive(car_master,16,123,key,iv);capture_car_key(key,iv);assert(g_hu_key_valid);
    assert(memcmp(key,g_hu_key,16) && memcmp(iv,g_hu_iv,16));
    derive(car_master,16,cid,key,iv);assert(!memcmp(key,g_hu_key,16) && !memcmp(iv,g_hu_iv,16));
    t_release(out);out=encode(req);assert(!find_stream(out,111) && g_car_cid==cid && g_hu_key_valid);
    assert(!memcmp(key,g_hu_key,16));t_release(out);
    {CFRef stop=stream_request(110,0);out=encode(stop);
     assert(!find_stream(out,111) && g_car_cid==cid && g_car_port==6200 && g_hu_key_valid);
     t_release(out);t_release(stop);}
    derive(car_master,16,555,key,iv);capture_car_key(key,iv);assert(g_hu_key_valid && g_car_cid==cid);
    /* A failed accepted socket can be replaced only by a fresh native SETUP. */
    g_relay.dead=1;g_relay.for_gen=g_hu_gen;
    out=encode(req);assert(dict_cid(find_stream(out,111))!=cid && !g_hu_key_valid && !g_car_port);
    t_release(filtered);t_release(resp);t_release(out);t_release(req);
    /* Stock _TeardownProxyClient invalidates before clearing gAirPlayClient. */
    HTTPClientInvalidate((void *)0x5678);assert(g_car_cid && invalidate_calls==1);
    HTTPClientInvalidate(car_client);assert(!g_car_cid && !car_available() && invalidate_calls==2);
    capture_car_info(root);
    car_pair=(void *)1;req=stream_request(110,321);out=encode(req);assert(!find_stream(out,111));
    car_pair=0;t_release(out);t_release(req);t_release(root);
}
static void test_car_setup_recovery(void){
    CFRef root=profile(),req=stream_request(110,442);
    int fault;
    capture_car_info(root);
    for(fault=0;fault<6;++fault){
        CFRef out,resp,filtered,sd;
        uint64_t old_cid,new_cid;
        uint8_t key[16],iv[16];
        car_reset(0);
        out=encode(req);old_cid=dict_cid(find_stream(out,111));assert(old_cid && g_car_pending);
        derive(car_master,16,442,key,iv);capture_car_key(key,iv);assert(g_hu_key_valid);
        resp=stream_request(111,old_cid);sd=find_stream(resp,111);
        if(fault==1)set_i64(sd,"dataPort",0);
        else if(fault==2)set_i64(sd,"dataPort",65536);
        else if(fault==3)set_str(sd,"dataPort","6200");
        else if(fault>=4)set_i64(sd,"dataPort",6200);
        peer_error=fault==4 ? -55 : 0;
        peer_family=fault==5 ? AF_UNSPEC : AF_INET6;
        filtered=decode(resp);
        assert(!find_stream(filtered,111) && !g_car_pending && !g_car_cid && !g_car_main_cid);
        assert(!g_car_port && !g_car_addr_len && !g_hu_key_valid && car_needs_setup());
        t_release(filtered);t_release(out);
        peer_error=0;peer_family=AF_INET6;
        out=encode(req);new_cid=dict_cid(find_stream(out,111));assert(new_cid && new_cid!=old_cid);
        assert(g_car_pending && !g_hu_key_valid && !car_needs_setup());
        /* An old failed response cannot retire the replacement setup. */
        filtered=decode(resp);assert(g_car_pending && g_car_cid==new_cid);t_release(filtered);
        set_i64(sd,"streamConnectionID",(int64_t)new_cid);set_i64(sd,"dataPort",6200);
        filtered=decode(resp);capture_car_key(key,iv);
        assert(!g_car_pending && g_car_port==6200 && g_hu_key_valid && !car_needs_setup());
        t_release(filtered);t_release(out);t_release(resp);
    }
    car_reset(1);t_release(root);t_release(req);
}
static int setup_calls,teardown_calls,setup_error,pair_error;
static CFRef last_stock_response;
static int stock_setup(void *session,CFRef req,CFRef *response){
    (void)session;assert(!find_stream(req,111));++setup_calls;
    last_stock_response=NULL;
    if(response){
        *response=t_copy(0,0,req);set_str(*response,"stockResult","preserved");
        last_stock_response=*response;
    }
    return setup_error;
}
static void stock_teardown(void *session,CFRef req,int reason,void *done){
    (void)session;(void)reason;(void)done;assert(!find_stream(req,111));++teardown_calls;
}
static int pair_derive(void *p,const void *salt,size_t sl,const void *info,size_t il,size_t n,uint8_t *key){
    assert(p==(void *)1 && sl>15 && !memcmp(salt,"DataStream-Salt",15));
    assert(il==strlen("DataStream-Output-Encryption-Key") && !memcmp(info,"DataStream-Output-Encryption-Key",il));
    assert(n==32);
    if(pair_error)return pair_error;
    memset(key,0x44,n);return 0;
}
static int verify_error;
static void cc_init(void *c,const uint8_t *k,const uint8_t *n){(void)c;(void)k;(void)n;}
static void cc_aad(void *c,const void *p,size_t n){(void)c;(void)p;assert(n==128);}
static size_t cc_dec(void *c,const void *s,size_t n,void *d){(void)c;(void)s;(void)d;return n;}
static size_t cc_verify(void *c,void *d,const uint8_t *tag,int *err){(void)c;(void)d;(void)tag;*err=verify_error;return 0;}
static void test_phone_lifecycle(void){
    CFRef session=t_new(1),req=stream_request(111,77),resp=NULL,root=profile();
    int fds[2];uint8_t done=9;unsigned gen;
    /* test_cf has enough aligned storage for the v129 pairing-session pointer. */
    {void *pair=(void *)1;memcpy((char *)session+0x3ec,&pair,sizeof pair);}
    capture_car_info(root);
    { int rc=AirPlayReceiverSessionSetup(session,req,&resp); if(rc) fprintf(stderr,"setup rc=%d errno=%d car=%d receiver=%p\n",rc,errno,car_available(),(void*)receiver); assert(!rc && !setup_calls && find_stream(resp,111)); }
    assert(receiver && receiver->cid==77 && receiver->key[0]==0x44);clear_receiver();t_release(resp);
    {CFRef main=dict();set_i64(main,"type",110);t_append(streams_of(req),main);t_release(main);}
    assert(!AirPlayReceiverSessionSetup(session,req,&resp) && setup_calls==1);clear_receiver();t_release(resp);
    assert(!socketpair(AF_UNIX,SOCK_STREAM,0,fds));g_relay.fd=fds[0];g_hu_key_valid=1;g_car_port=6200;
    gen=g_hu_gen;AirPlayReceiverSessionTearDown(session,NULL,0,&done);
    assert(teardown_calls==1 && g_hu_key_valid && gen==g_hu_gen && g_relay.fd==fds[0]);
    t_release(req);req=stream_request(111,77);AirPlayReceiverSessionTearDown(session,req,0,&done);
    assert(done==0 && teardown_calls==1 && g_relay.fd==fds[0]);
    car_reset(1);assert(!g_hu_key_valid && g_relay.fd==-1 && !car_available());close(fds[1]);
    memset((char *)session+0x3ec,0,sizeof(void *));t_release(session);t_release(req);t_release(root);
}
static void test_phone_setup_failures(void){
    CFRef session=t_new(1),root=profile();
    int mixed,fault;
    void *pair=(void *)1;
    memcpy((char *)session+0x3ec,&pair,sizeof pair);
    for(mixed=0;mixed<2;++mixed){
        for(fault=0;fault<6;++fault){
            CFRef req=stream_request(111,77),resp=NULL,sd=find_stream(req,111);
            int calls=setup_calls,binds=bind_attempts;
            OSStatus rc;
            if(mixed){
                CFRef main=dict();set_i64(main,"type",110);set_i64(main,"streamConnectionID",88);
                t_append(streams_of(req),main);t_release(main);
            }
            capture_car_info(root);
            if(fault==0)car_reset(1); /* descriptor lost after phone /info */
            else if(fault==1)set_i64(sd,"streamConnectionID",0);
            else if(fault==2)pair_error=-77;
            else if(fault==3)bind_error=1;
            else if(fault==4)create_error=EAGAIN;
            else fail_copy_src=sd; /* response allocation fails after worker starts */
            rc=AirPlayReceiverSessionSetup(session,req,&resp);
            assert(find_stream(req,111)); /* caller's request is untouched */
            if(mixed){
                assert(rc==0 && setup_calls==calls+1 && resp==last_stock_response);
                assert(find_stream(resp,110) && !find_stream(resp,111));
                assert(ascii_eq(get_value(resp,"stockResult"),"preserved"));
            }else assert(rc!=0 && setup_calls==calls && !find_stream(resp,111));
            if(fault==3)assert(bind_attempts==binds+2); /* preferred and ephemeral */
            if(fault==5)assert(receiver && receiver->gen!=g_rx_gen); /* worker retired */
            else assert(!receiver);
            fail_copy_src=NULL;pair_error=bind_error=create_error=0;
            clear_receiver();assert(((test_cf *)session)->refs==1);
            if(resp)t_release(resp);
            t_release(req);
        }
    }
    {
        CFRef req=stream_request(110,88),resp=NULL,alt=dict();
        int calls=setup_calls;
        car_reset(1);
        assert(!AirPlayReceiverSessionSetup(session,req,&resp) && setup_calls==calls+1);
        assert(resp==last_stock_response && find_stream(resp,110));t_release(resp);
        set_i64(alt,"type",111);set_i64(alt,"streamConnectionID",77);t_append(streams_of(req),alt);
        capture_car_info(root);setup_error=-4321;
        assert(AirPlayReceiverSessionSetup(session,req,&resp)==setup_error && !receiver);
        assert(resp==last_stock_response && !find_stream(resp,111));
        setup_error=0;t_release(resp);
        /* Invalid caller output is only cluster-fatal on a mixed request. */
        assert(!AirPlayReceiverSessionSetup(session,req,NULL));
        /* OOM while filtering must still fail closed, never expose 111 to stock. */
        calls=setup_calls;fail_copy_src=req;
        assert(AirPlayReceiverSessionSetup(session,req,&resp)!=0 && setup_calls==calls);
        fail_copy_src=NULL;t_release(req);t_release(alt);
    }
    car_reset(1);memset((char *)session+0x3ec,0,sizeof pair);t_release(session);t_release(root);
}
static int sends;
static void *pending;
static void (*pending_done)(OSStatus,CFRef,void *);
static CFRef sent;
static int send_command(void *session,CFRef req,void *cb,void *ctx){
    (void)session;assert(!pending);pending=ctx;pending_done=cb;sent=t_retain(req);++sends;return 0;
}
static void finish_command(int e){void *ctx=pending;pending=NULL;pending_done(e,NULL,ctx);t_release(sent);sent=NULL;}
static CFRef command(const char *type){
    CFRef req=dict(),params=dict();set_str(req,"type",type);set_car_uuid(params);
    if(!strcmp(type,"showUI"))set_str(params,"url","maps:/dynamic");
    set_str(params,"futureParam","kept");set_value(req,"params",params);t_release(params);return req;
}
static void test_controls(void){
    CFRef root=profile(),ui,view;unsigned old;capture_car_info(root);
    ui=command("showUI");view=command("updateViewArea");capture_car_command(ui);capture_car_command(view);
    {CFRef empty=command("showUI");unsigned version=g_controls[0].version;
     set_str(get_value(empty,"params"),"url","");capture_car_command(empty);
     assert(g_controls[0].version==version);t_release(empty);}
    ++g_rx_gen;service_car_commands((void *)1,g_rx_gen);assert(sends==1 && pending);
    assert(ascii_eq(get_value(get_value(sent,"params"),"futureParam"),"kept"));
    service_car_commands((void *)1,g_rx_gen);assert(sends==1);finish_command(-1);
    service_car_commands((void *)1,g_rx_gen);assert(sends==1); /* 1s limit; view also waits */
    g_controls[0].next_try=0;service_car_commands((void *)1,g_rx_gen);assert(sends==2);finish_command(0);
    service_car_commands((void *)1,g_rx_gen);assert(sends==3);finish_command(0);
    service_car_commands((void *)1,g_rx_gen);assert(sends==3);
    old=g_rx_gen++;g_controls[0].next_try=g_controls[1].next_try=0;
    service_car_commands((void *)1,old);assert(sends==3);
    service_car_commands((void *)1,g_rx_gen);assert(sends==4);finish_command(0);
    service_car_commands((void *)1,g_rx_gen);assert(sends==5);finish_command(0);
    capture_car_command(ui);g_controls[0].next_try=0;service_car_commands((void *)1,g_rx_gen);assert(sends==6);
    finish_command(-1);service_car_commands((void *)1,g_rx_gen);assert(sends==6);
    g_controls[0].next_try=0;service_car_commands((void *)1,g_rx_gen);assert(sends==7);finish_command(0);
    g_controls[1].next_try=0;service_car_commands((void *)1,g_rx_gen);assert(sends==8);finish_command(0);
    capture_car_command(ui);g_controls[0].next_try=0;service_car_commands((void *)1,g_rx_gen);assert(sends==9);
    car_reset(0);finish_command(0);assert(!g_controls[0].request); /* late completion after car reset */
    t_release(root);t_release(ui);t_release(view);
}
static int final_calls,update_calls;
static void ctr_final(void *c){(void)c;++final_calls;}
static int ctr_fail(void *c,const void *s,size_t n,void *d){(void)c;(void)s;(void)n;(void)d;++update_calls;return -1;}
static void test_crypto_failure(void){
    int fds[2],attached=1;ScreenHeader h={0};uint8_t body[16]={0},out;
    assert(!socketpair(AF_UNIX,SOCK_STREAM,0,fds));g_relay.fd=fds[0];g_relay.enc=1;
    g_relay.for_gen=g_hu_gen;g_hu_key_valid=1;g_car_port=6200;h.bodySize=sizeof body;
    r_ctr_final=ctr_final;r_ctr_update=ctr_fail;relay_frame(g_rx_gen,&attached,&h,body,NULL,NULL,0);
    assert(update_calls==1 && final_calls==1 && g_relay.fd==-1 && read(fds[1],&out,1)==0);close(fds[1]);
}
static void test_phone_crypto(void){
    uint8_t key[32]={0},nonce[8]={0},body[32]={0};uint32_t plain=99;ScreenHeader h={0};
    assert(!chacha_open(key,nonce,&h,body,15,&plain) && nonce[0]==0);
    verify_error=-1;assert(!chacha_open(key,nonce,&h,body,32,&plain) && nonce[0]==0);
    verify_error=0;assert(chacha_open(key,nonce,&h,body,16,&plain) && plain==0 && nonce[0]==1);
    nonce[0]=255;assert(chacha_open(key,nonce,&h,body,32,&plain) && plain==16 && nonce[0]==0 && nonce[1]==1);
}
/* ---- route guidance bridge ---- */
static size_t msg(uint8_t *m,unsigned id,const uint8_t *params,size_t n){
    m[0]=m[1]=0x40;put_be16(m+2,(unsigned)(6+n));put_be16(m+4,id);memcpy(m+6,params,n);return 6+n;
}
static size_t param(uint8_t *p,unsigned id,const uint16_t *v,size_t count){
    size_t i;put_be16(p,(unsigned)(4+2*count));put_be16(p+2,id);
    for(i=0;i<count;++i)put_be16(p+4+2*i,v[i]);
    return 4+2*count;
}
static const uint16_t car_comp_body[]={6,0,0x0010,6,2,128}; /* {id 0x10}, {max road name 128} */
static size_t car_comp(uint8_t *p){return param(p,ID_RGD_COMPONENT,car_comp_body,6);}
static size_t phone_identify(uint8_t *m,int with_lists,int with_rgd){
    static const uint16_t sent[]={0x5000,0x5203},recv[]={0x5001},host[]={6,0,1};
    uint8_t p[256];size_t n=0;
    n+=param(p+n,0,NULL,0);
    if(with_lists){n+=param(p+n,ID_SENT_BY_ACC,sent,2);n+=param(p+n,ID_RECV_BY_ACC,recv,1);}
    n+=param(p+n,0x10,host,3);
    if(with_rgd)n+=car_comp(p+n);
    return msg(m,IAP_ID_INFO,p,n);
}
static void test_rgd_identify(void){
    uint8_t m[512],out[1024],comp[64];size_t n,cn=car_comp(comp),o,pl;const uint8_t *p;
    n=phone_identify(m,1,0);o=rgd_patch_identify(m,n,comp,cn,out,sizeof out);
    assert(o==n+cn+2*4 && iap_msg_id(out,(uint32_t)o)==IAP_ID_INFO); /* 0x5203 was already listed */
    p=iap_param(out,o,ID_SENT_BY_ACC,&pl);assert(p && pl==4+2*3 && list_has(p,pl,RGD_START) && list_has(p,pl,RGD_STOP));
    p=iap_param(out,o,ID_RECV_BY_ACC,&pl);
    assert(p && pl==4+2*4 && list_has(p,pl,RGD_UPDATE) && list_has(p,pl,RGD_MANEUVER) && list_has(p,pl,RGD_LANE));
    p=iap_param(out,o,ID_RGD_COMPONENT,&pl);assert(p && pl==cn && !memcmp(p,comp,cn));
    p=iap_param(out,o,0x10,&pl);assert(p && pl==10);
    assert(!rgd_patch_identify(m,n,comp,0,out,sizeof out));           /* no car component */
    assert(!rgd_patch_identify(m,n,comp,cn,out,n+cn));                 /* no room */
    n=phone_identify(m,0,0);assert(!rgd_patch_identify(m,n,comp,cn,out,sizeof out));
    n=phone_identify(m,1,1);assert(!rgd_patch_identify(m,n,comp,cn,out,sizeof out));
    n=phone_identify(m,1,0);put_be16(m+6,200);assert(!rgd_patch_identify(m,n,comp,cn,out,sizeof out));
}
static uint8_t sent_msg[2048];static size_t sent_len;static int sent_calls;static uint8_t sent_session;
static void *phone_link_var;static uint8_t phone_ctrl_var=7;static int fake_link;
static uint8_t car_msg[1024];static size_t car_len;static int car_calls,car_stock,phone_stock;
static int queue_send(void *l,const uint8_t *d,uint32_t n,uint8_t s,void *c,void *done){
    (void)l;(void)c;(void)done;assert(n<=sizeof sent_msg);memcpy(sent_msg,d,n);sent_len=n;sent_session=s;++sent_calls;return 1;
}
static void to_car(const uint8_t *d,uint32_t n,int flag){assert(!flag && n<=sizeof car_msg);memcpy(car_msg,d,n);car_len=n;++car_calls;}
static int car_ready_stock(void *l,uint8_t *d,uint32_t n,uint8_t s){(void)l;(void)d;(void)n;(void)s;++car_stock;return 1;}
/* Stock answers the phone's 0x1D00 with its Identify and attaches the link on 0x4E09. */
static int phone_ready_stock(void *l,uint8_t *d,uint32_t n,uint8_t s){
    uint8_t m[512];unsigned id=iap_msg_id(d,n);(void)s;++phone_stock;
    if(id==IAP_ID_START)iAP2LinkQueueSendData(l,m,(uint32_t)phone_identify(m,1,0),1,NULL,NULL);
    if(id==0x4e09)phone_link_var=&fake_link;
    return 1;
}
static void test_rgd_bridge(void){
    static const uint16_t start_body[]={6,0,0x0010};
    uint8_t m[512],p[128],big[600];size_t n,cn;int first;
    pthread_once(&g_rgd_once,no_bind);
    r_queue_send=queue_send;r_to_car=to_car;r_car_ready=car_ready_stock;r_phone_ready=phone_ready_stock;
    r_cp_link=&phone_link_var;r_cp_ctrl=&phone_ctrl_var;g_rgd.off=0;
    /* car Identify, then its start before any phone: kept */
    cn=car_comp(p);n=msg(m,IAP_ID_INFO,p,cn);rgd_car_ready(NULL,m,(uint32_t)n,1);
    assert(g_rgd.comp_len==cn && car_stock==1);
    n=msg(m,RGD_START,p,param(p,0,start_body,3));rgd_car_ready(NULL,m,(uint32_t)n,1);
    assert(!sent_calls && g_rgd.start_len==n && car_stock==2);
    /* phone identifies: the stock Identify leaves with the component */
    n=msg(m,IAP_ID_START,NULL,0);rgd_phone_ready(NULL,m,(uint32_t)n,1);
    assert(sent_calls==1 && iap_param(sent_msg,sent_len,ID_RGD_COMPONENT,&cn) && g_rgd.phone_ok && !t_rgd_phone);
    /* stock attaches the phone link: the start is replayed on the control session */
    n=msg(m,0x4e09,NULL,0);rgd_phone_ready(NULL,m,(uint32_t)n,1);
    assert(sent_calls==2 && iap_msg_id(sent_msg,(uint32_t)sent_len)==RGD_START && sent_session==phone_ctrl_var);
    /* a later start goes straight through; stock still sees every message */
    first=car_stock;n=msg(m,RGD_START,p,param(p,0,start_body,3));rgd_car_ready(NULL,m,(uint32_t)n,1);
    assert(sent_calls==3 && car_stock==first+1);
    /* phone maneuver split over two link packets reaches the car whole */
    memset(big,0xab,sizeof big);n=msg(big,RGD_MANEUVER,big+6,sizeof big-6);
    rgd_phone_ready(NULL,big,300,1);assert(!car_calls);
    rgd_phone_ready(NULL,big+300,(uint32_t)(n-300),2);assert(!car_calls); /* other session ignored */
    rgd_phone_ready(NULL,big+300,(uint32_t)(n-300),1);assert(car_calls==1 && car_len==n && !memcmp(car_msg,big,n));
    n=msg(m,RGD_UPDATE,NULL,0);rgd_phone_ready(NULL,m,(uint32_t)n,1);assert(car_calls==2 && car_len==n);
    n=msg(m,0x5001,NULL,0);rgd_phone_ready(NULL,m,(uint32_t)n,1);assert(car_calls==2); /* stock relays others */
    /* stop clears the replay cache */
    n=msg(m,RGD_STOP,NULL,0);rgd_car_ready(NULL,m,(uint32_t)n,1);assert(sent_calls==4 && !g_rgd.start_len);
    /* a rejection turns the bridge off: the next Identify stays stock */
    n=msg(m,IAP_ID_REJECTED,NULL,0);rgd_phone_ready(NULL,m,(uint32_t)n,1);assert(g_rgd.off && !g_rgd.phone_ok);
    phone_link_var=NULL;n=msg(m,IAP_ID_START,NULL,0);rgd_phone_ready(NULL,m,(uint32_t)n,1);
    assert(sent_calls==5 && !iap_param(sent_msg,sent_len,ID_RGD_COMPONENT,&cn));
    n=msg(m,RGD_START,p,param(p,0,start_body,3));rgd_car_ready(NULL,m,(uint32_t)n,1);assert(sent_calls==5);
    /* outside the phone callback nothing is rewritten */
    n=phone_identify(m,1,0);g_rgd.off=0;iAP2LinkQueueSendData(NULL,m,(uint32_t)n,1,NULL,NULL);
    assert(sent_calls==6 && sent_len==n);
}
int main(void){
    bind_cf();r_client_invalidate=invalidate_client;r_plist_read=plist_read;r_plist_write=plist_write;r_derive=derive;
    r_car_master=car_master;r_car_pair=&car_pair;r_car_client=&car_client;r_peer_address=peer;
    r_setup=stock_setup;r_teardown=stock_teardown;r_pair_derive=pair_derive;
    r_cc_init=cc_init;r_cc_aad=cc_aad;r_cc_dec=cc_dec;r_cc_verify=cc_verify;r_send_cmd=send_command;
    test_profile();test_native_setup();test_car_setup_recovery();test_phone_lifecycle();test_phone_setup_failures();
    test_controls();test_crypto_failure();test_phone_crypto();test_rgd_identify();test_rgd_bridge();
    car_reset(1);t_release(k_type);t_release(k_conn);t_release(k_streams);t_release(k_displays);t_release(k_features);
    puts("OK: native proxy isolation, car metadata/SETUP/keys, controls, reconnect, fail-closed crypto and route guidance bridge");return 0;
}
