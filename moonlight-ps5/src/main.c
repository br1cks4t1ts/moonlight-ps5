/*
 * moonlight-ps5 — Moonlight game streaming client for PS5 homebrew
 * Built on moonlight-common-c (https://github.com/moonlight-stream/moonlight-common-c)
 *
 * Original Moonlight developers are credited as contributors to this port.
 * This project is open source under the terms of the GNU GPL v3.
 */

#include "platform.h"
#include "video.h"
#include "audio.h"
#include "input.h"
#include "gamestream.h"
#include "Limelight.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DIAG_HOST "YOUR_DEV_PC_IP"
#define DIAG_PORT 9999

#ifndef HOST_ADDRESS
#define HOST_ADDRESS  "YOUR_GAME_STREAMING_SERVER_IP"
#endif
#ifndef APP_ID
#define APP_ID  134906179   /* Wolf UI */
#endif

#define STREAM_WIDTH   1920
#define STREAM_HEIGHT  1080
#define STREAM_FPS     60
#define STREAM_BITRATE 20000

static volatile int s_running    = 1;
static int          s_pad_handle = -1;

/* ── Connection callbacks ────────────────────────────────────────────────── */
static void cb_stage_starting(int stage)          { printf("moonlight: stage %d starting\n", stage); }
static void cb_stage_complete(int stage)           { printf("moonlight: stage %d complete\n", stage); }
static void cb_stage_failed(int stage, int err)   { printf("moonlight: stage %d failed: 0x%x\n", stage, err); fflush(stdout); }
static void cb_started(void)                       { printf("moonlight: streaming started!\n"); }
static void cb_terminated(int err)                { fprintf(stderr, "moonlight: terminated: 0x%x\n", err); s_running = 0; }
static void cb_rumble(unsigned short c, unsigned short lo, unsigned short hi) { (void)c;(void)lo;(void)hi; }
static void cb_rumble_triggers(uint16_t c, uint16_t l, uint16_t r)           { (void)c;(void)l;(void)r; }
static void cb_status(int s)                       { printf("moonlight: quality: %s\n", s == CONN_STATUS_POOR ? "POOR" : "GOOD"); }
static void cb_log(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
}

/* ── Video callbacks ─────────────────────────────────────────────────────── */
static DECODER_RENDERER_CALLBACKS s_video_cbs = {
    .setup            = video_init,
    .start            = video_start,
    .stop             = video_stop,
    .cleanup          = video_cleanup,
    .submitDecodeUnit = video_submit_decode_unit,
    .capabilities     = CAPABILITY_DIRECT_SUBMIT,
};

/* ── Audio callbacks ─────────────────────────────────────────────────────── */
static AUDIO_RENDERER_CALLBACKS s_audio_cbs = {
    .init                = audio_init,
    .start               = audio_start,
    .stop                = audio_stop,
    .cleanup             = audio_cleanup,
    .decodeAndPlaySample = audio_decode_and_play,
    .capabilities        = CAPABILITY_DIRECT_SUBMIT,
};

/* ── Input thread ────────────────────────────────────────────────────────── */
static void *input_thread(void *arg) {
    (void)arg;
    while (s_running) {
        input_poll_and_send();
        usleep(8333);
    }
    return NULL;
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */
__attribute__((constructor)) static void ps5_init(void) {
    sceUserServiceInitialize(NULL);
    sceSysmoduleLoadModule(SCE_SYSMODULE_PAD);
    sceSysmoduleLoadModule(SCE_SYSMODULE_AUDIO_OUT);
    sceSysmoduleLoadModule(SCE_SYSMODULE_VIDEO_OUT);
}
__attribute__((destructor)) static void ps5_fini(void) {
    sceSysmoduleUnloadModule(SCE_SYSMODULE_VIDEO_OUT);
    sceSysmoduleUnloadModule(SCE_SYSMODULE_AUDIO_OUT);
    sceSysmoduleUnloadModule(SCE_SYSMODULE_PAD);
    sceUserServiceTerminate();
}

/* ── main ────────────────────────────────────────────────────────────────── */
int main(int argc, char **argv) {
    /* Redirect stdout+stderr to diag host so we can see output when running
     * as a FSELF (no etaHEN relay in that case). */
    int diag = socket(AF_INET, SOCK_STREAM, 0);
    if (diag >= 0) {
        struct timeval tv = {3, 0};
        setsockopt(diag, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(diag, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        struct sockaddr_in sa = {0};
        sa.sin_family = AF_INET;
        sa.sin_port   = htons(DIAG_PORT);
        inet_pton(AF_INET, DIAG_HOST, &sa.sin_addr);
        if (connect(diag, (struct sockaddr *)&sa, sizeof(sa)) == 0) {
            dup2(diag, STDOUT_FILENO);
            dup2(diag, STDERR_FILENO);
            close(diag);
            setvbuf(stdout, NULL, _IONBF, 0);
            setvbuf(stderr, NULL, _IONBF, 0);
        } else {
            close(diag);
        }
    }

    const char *host  = (argc > 1) ? argv[1] : HOST_ADDRESS;
    int         appId = (argc > 2) ? atoi(argv[2]) : APP_ID;

    printf("moonlight-ps5 v0.1 — host=%s appId=%d\n", host, appId);

    /* Open pad */
    int userId = 0;
    sceUserServiceGetInitialUser(&userId);
    if (userId <= 0) userId = 1;
    printf("moonlight-ps5: userId=%d\n", userId);

    scePadInit();
    s_pad_handle = scePadOpen(userId, SCE_PAD_PORT_TYPE_STANDARD, 0, NULL);
    if (s_pad_handle < 0)
        fprintf(stderr, "moonlight-ps5: scePadOpen failed: 0x%x (no gamepad input)\n",
                s_pad_handle);

    input_init(s_pad_handle);

    /* Init crypto + generate client cert */
    printf("moonlight-ps5: generating client certificate...\n");
    if (gs_init(host) != 0) {
        fprintf(stderr, "moonlight-ps5: gs_init failed\n");
        return 1;
    }

    /* Load server info */
    GS_SERVER server;
    memset(&server, 0, sizeof(server));
    strncpy(server.address, host, sizeof(server.address) - 1);

    printf("moonlight-ps5: querying server info...\n");
    if (gs_load_server_info(&server) != 0) {
        fprintf(stderr, "moonlight-ps5: cannot reach Sunshine at %s\n", host);
        fprintf(stderr, "               Is Sunshine running? Is the firewall open?\n");
        return 1;
    }

    /* Pair if needed */
    if (!server.paired) {
        /* Generate random 4-digit PIN — same one we use in the crypto */
        srand((unsigned)time(NULL));
        char pin[5];
        snprintf(pin, sizeof(pin), "%04d", rand() % 10000);
        printf("\n");
        printf("╔════════════════════════════════════════════╗\n");
        printf("║  PAIRING — enter this PIN in wolf:         ║\n");
        printf("║                                            ║\n");
        printf("║              PIN: %s                   ║\n", pin);
        printf("║                                            ║\n");
        printf("║  Wolf will log the URL — visit it now.     ║\n");
        printf("╚════════════════════════════════════════════╝\n");
        printf("\n");
        fflush(stdout);
        printf("Sending pairing request to wolf now...\n");

        /* gs_pair sends the PIN immediately; wolf logs the URL and blocks
         * until the user visits it and clicks Accept. */
        if (gs_pair(&server, pin) != 0) {
            fprintf(stderr, "moonlight-ps5: pairing failed\n");
            return 1;
        }
    }

    /* Stream config */
    STREAM_CONFIGURATION cfg;
    LiInitializeStreamConfiguration(&cfg);
    cfg.width              = STREAM_WIDTH;
    cfg.height             = STREAM_HEIGHT;
    cfg.fps                = STREAM_FPS;
    cfg.bitrate            = STREAM_BITRATE;
    cfg.packetSize         = 1392;
    cfg.streamingRemotely  = STREAM_CFG_AUTO;
    cfg.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
    cfg.supportedVideoFormats = VIDEO_FORMAT_H264;
    cfg.encryptionFlags    = ENCFLG_NONE;

    /* Launch app on server */
    printf("moonlight-ps5: launching app %d...\n", appId);
    if (gs_start_app(&server, &cfg, appId) != 0) {
        fprintf(stderr, "moonlight-ps5: gs_start_app failed\n");
        return 1;
    }

    /* Connection callbacks */
    CONNECTION_LISTENER_CALLBACKS connCbs;
    LiInitializeConnectionCallbacks(&connCbs);
    connCbs.stageStarting        = cb_stage_starting;
    connCbs.stageComplete        = cb_stage_complete;
    connCbs.stageFailed          = cb_stage_failed;
    connCbs.connectionStarted    = cb_started;
    connCbs.connectionTerminated = cb_terminated;
    connCbs.logMessage           = cb_log;
    connCbs.rumble               = cb_rumble;
    connCbs.rumbleTriggers       = cb_rumble_triggers;
    connCbs.connectionStatusUpdate = cb_status;

    /* Start stream */
    printf("moonlight-ps5: starting stream...\n");
    int rc = LiStartConnection(&server.serverInfo, &cfg, &connCbs,
                               &s_video_cbs, &s_audio_cbs,
                               NULL, 0, NULL, 0);
    if (rc != 0) {
        fprintf(stderr, "moonlight-ps5: LiStartConnection failed: %d\n", rc);
        gs_quit_app(&server);
        input_cleanup();
        if (s_pad_handle >= 0) scePadClose(s_pad_handle);
        return 1;
    }

    /* Input loop + quit detection (OPTIONS+CREATE) */
    pthread_t input_tid;
    pthread_create(&input_tid, NULL, input_thread, NULL);

    printf("moonlight-ps5: streaming — hold OPTIONS+CREATE to quit\n");
    while (s_running) {
        ScePadData pad;
        memset(&pad, 0, sizeof(pad));
        if (s_pad_handle >= 0)
            scePadReadState(s_pad_handle, &pad);

        if ((pad.buttons & SCE_PAD_BUTTON_OPTIONS) &&
            (pad.buttons & SCE_PAD_BUTTON_CREATE)) {
            printf("moonlight-ps5: quit requested\n");
            s_running = 0;
        }
        usleep(100000);
    }

    LiStopConnection();
    pthread_join(input_tid, NULL);

    gs_quit_app(&server);
    input_cleanup();
    if (s_pad_handle >= 0) scePadClose(s_pad_handle);

    printf("moonlight-ps5: done\n");
    return 0;
}

/* Called automatically by SYS_sprx_load after init_array (ps5_init) runs.
 * This lets the ET_EXEC launcher load us as a module and we run main(). */
__attribute__((visibility("default")))
int module_start(unsigned long args, const void *argp) {
    (void)args; (void)argp;
    char *no_argv[] = {NULL};
    return main(0, no_argv);
}
