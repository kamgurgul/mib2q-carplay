/*
 * cluster_probe.c - HW301 shim, DISCOVERY build. Interposes cross-boundary
 * libAirPlay calls, logs first hits, tail-calls the real impl. Each wrapper is a
 * naked ARM trampoline that preserves r0-r3, r4 and the stack (8-byte aligned),
 * so it is correct for ANY signature. Built as libAirPlay.so (wrapper-rename).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

#define N 30
static void *reals[N];
static const char *names[N] = {
    "AirPlayAudioFormatToASBD",
    "AirPlayCreateModesDictionary",
    "AirPlayInfoArrayAddAudioFormat",
    "AirPlayInfoArrayAddAudioLatency",
    "AirPlayInfoArrayAddHIDDevice",
    "AirPlayInfoArrayAddScreenDisplay",
    "AirPlayReceiverServerControl",
    "AirPlayReceiverServerCreate",
    "AirPlayReceiverServerSetDelegate",
    "AirPlayReceiverSessionChangeModes",
    "AirPlayReceiverSessionChangeResourceMode",
    "AirPlayReceiverSessionCopyProperty",
    "AirPlayReceiverSessionForceKeyFrame",
    "AirPlayReceiverSessionGetGetDeviceID",
    "AirPlayReceiverSessionMakeModeStateFromDictionary",
    "AirPlayReceiverSessionRequestSiriAction",
    "AirPlayReceiverSessionRequestUI",
    "AirPlayReceiverSessionSendCommand",
    "AirPlayReceiverSessionSendHIDReport",
    "AirPlayReceiverSessionSendiAPMessage",
    "AirPlayReceiverSessionSetDelegate",
    "CarPlayControlClientConnect",
    "CarPlayControlClientCreateWithServer",
    "CarPlayControlClientDispatchEvent",
    "CarPlayControlClientRestart",
    "CarPlayControlClientStart",
    "CarPlayControlClientStop",
    "CarPlayControllerCopyName",
    "CarPlayControllerGetBluetoothMacAddress",
    "CarPlayControllerGetInterfaceName"
};

static void plog(const char *fmt, ...) {
    char buf[200]; va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    FILE *k = fopen("/dev/kmsg","w"); if (k){ fprintf(k,"cluster_probe: %s\n",buf); fclose(k);} 
    FILE *f = fopen("/mnt/UDISK/logs/app.log","a"); if (f){ fprintf(f,"[cluster_probe] %s\n",buf); fclose(f);} 
}

__attribute__((used)) void *shim_probe_resolve(int id) {
    static int c[N];
    if (id>=0 && id<N) {
        if (c[id]++ < 3) plog("CALL #%d %s (hit %d)", id, names[id], c[id]);
        return reals[id];
    }
    return 0;
}

__attribute__((constructor)) static void probe_init(void) {
    for (int i=0;i<N;i++) reals[i] = dlsym(RTLD_NEXT, names[i]);
    plog("LOADED pid=%d, %d probes armed", (int)getpid(), N);
}

__attribute__((naked, used)) void AirPlayAudioFormatToASBD(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #0\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void AirPlayCreateModesDictionary(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #1\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void AirPlayInfoArrayAddAudioFormat(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #2\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void AirPlayInfoArrayAddAudioLatency(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #3\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void AirPlayInfoArrayAddHIDDevice(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #4\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void AirPlayInfoArrayAddScreenDisplay(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #5\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void AirPlayReceiverServerControl(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #6\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void AirPlayReceiverServerCreate(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #7\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void AirPlayReceiverServerSetDelegate(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #8\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void AirPlayReceiverSessionChangeModes(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #9\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void AirPlayReceiverSessionChangeResourceMode(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #10\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void AirPlayReceiverSessionCopyProperty(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #11\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void AirPlayReceiverSessionForceKeyFrame(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #12\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void AirPlayReceiverSessionGetGetDeviceID(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #13\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void AirPlayReceiverSessionMakeModeStateFromDictionary(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #14\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void AirPlayReceiverSessionRequestSiriAction(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #15\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void AirPlayReceiverSessionRequestUI(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #16\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void AirPlayReceiverSessionSendCommand(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #17\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void AirPlayReceiverSessionSendHIDReport(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #18\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void AirPlayReceiverSessionSendiAPMessage(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #19\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void AirPlayReceiverSessionSetDelegate(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #20\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void CarPlayControlClientConnect(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #21\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void CarPlayControlClientCreateWithServer(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #22\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void CarPlayControlClientDispatchEvent(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #23\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void CarPlayControlClientRestart(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #24\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void CarPlayControlClientStart(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #25\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void CarPlayControlClientStop(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #26\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void CarPlayControllerCopyName(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #27\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void CarPlayControllerGetBluetoothMacAddress(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #28\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
__attribute__((naked, used)) void CarPlayControllerGetInterfaceName(void){
    __asm__ volatile(
        "push {r0-r3, r4, lr}\n\t"
        "mov  r0, #29\n\t"
        "bl   shim_probe_resolve\n\t"
        "mov  ip, r0\n\t"
        "pop  {r0-r3, r4, lr}\n\t"
        "bx   ip\n\t");
}
