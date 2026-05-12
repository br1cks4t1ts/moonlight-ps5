/*
 * reinstall.c — wipe MLPS00001 and do a clean install of Moonlight PS5.
 * Send via port 9021. Host must serve files at HOST_IP:8888.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define HOST_IP   "YOUR_SERVER_IP"
#define HOST_PORT 8888
#define TITLE_ID  "MLPS00001"
#define APP_DIR   "/system_ex/app/" TITLE_ID
#define USR_DIR   "/user/app/" TITLE_ID

int sceAppInstUtilInitialize(void);
int sceAppInstUtilAppInstallTitleDir(const char*, const char*, void*);

/* ── helpers ──────────────────────────────────────────────────────────────── */
static void rm_f(const char *p) {
    if (unlink(p) == 0) printf("rm %s\n", p);
}
static void mk_dir(const char *p) {
    if (mkdir(p, 0755) == 0) printf("mkdir %s\n", p);
    else if (errno != EEXIST) printf("mkdir FAIL %s: %d\n", p, errno);
}
static void write_str(const char *p, const char *s) {
    FILE *f = fopen(p, "w");
    if (!f) { printf("write_str FAIL %s: %d\n", p, errno); return; }
    fputs(s, f); fclose(f);
    printf("wrote %s\n", p);
}

static long http_fetch(const char *url_path, const char *dest) {
    printf("fetch /%s -> %s\n", url_path, dest);
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(HOST_PORT);
    inet_pton(AF_INET, HOST_IP, &addr.sin_addr);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        printf("connect err %d\n", errno); close(sock); return -1;
    }
    char req[512];
    snprintf(req, sizeof(req),
        "GET /%s HTTP/1.0\r\nHost: %s:%d\r\nConnection: close\r\n\r\n",
        url_path, HOST_IP, HOST_PORT);
    send(sock, req, strlen(req), 0);
    char c, prev[4] = {0};
    while (recv(sock, &c, 1, 0) == 1) {
        prev[0]=prev[1]; prev[1]=prev[2]; prev[2]=prev[3]; prev[3]=c;
        if (prev[0]=='\r'&&prev[1]=='\n'&&prev[2]=='\r'&&prev[3]=='\n') break;
    }
    FILE *f = fopen(dest, "wb");
    if (!f) { printf("fopen FAIL %s: %d\n", dest, errno); close(sock); return -1; }
    char buf[4096]; long total = 0; int n;
    while ((n = recv(sock, buf, sizeof(buf), 0)) > 0) {
        fwrite(buf, 1, n, f); total += n;
    }
    fclose(f); close(sock);
    printf("  %ld bytes\n", total);
    return total;
}

static const char PARAM_JSON_USR[] =
    "{\"applicationCategoryType\":0,"
    "\"localizedParameters\":{\"defaultLanguage\":\"en-US\","
    "\"en-US\":{\"titleName\":\"Moonlight PS5\"}},"
    "\"titleId\":\"" TITLE_ID "\"}";

static const char PARAM_JSON_SYS[] =
    "{\"applicationCategoryType\":33554432,"
    "\"localizedParameters\":{\"defaultLanguage\":\"en-US\","
    "\"en-US\":{\"titleName\":\"Moonlight PS5\"}},"
    "\"titleId\":\"" TITLE_ID "\"}";

int main(void) {
    printf("\n=== Moonlight PS5 Clean Reinstall ===\n");

    /* Remove old files */
    printf("\n--- Removing old files ---\n");
    rm_f(APP_DIR "/eboot.bin");
    rm_f(APP_DIR "/moonlight.fself");
    rm_f(APP_DIR "/moonlight-ps5.elf");
    rm_f(APP_DIR "/sce_sys/param.json");
    rmdir(APP_DIR "/sce_sys");
    rmdir(APP_DIR);
    rm_f(USR_DIR "/sce_sys/param.json");
    rm_f(USR_DIR "/sce_sys/icon0.png");
    rmdir(USR_DIR "/sce_sys");
    rmdir(USR_DIR);

    /* Create fresh directories */
    printf("\n--- Creating directories ---\n");
    mk_dir(APP_DIR);
    mk_dir(APP_DIR "/sce_sys");
    mk_dir(USR_DIR);
    mk_dir(USR_DIR "/sce_sys");

    /* Download files */
    printf("\n--- Downloading files ---\n");
    long n = http_fetch("launcher_eboot.bin", APP_DIR "/eboot.bin");
    if (n <= 0) { printf("ERROR: launcher fetch failed\n"); return 1; }

    n = http_fetch("moonlight.fself", APP_DIR "/moonlight.fself");
    if (n <= 0) { printf("ERROR: moonlight.fself fetch failed\n"); return 1; }

    http_fetch("icon0.png", USR_DIR "/sce_sys/icon0.png");

    /* Write param.json */
    printf("\n--- Writing param.json ---\n");
    write_str(USR_DIR "/sce_sys/param.json", PARAM_JSON_USR);
    write_str(APP_DIR "/sce_sys/param.json", PARAM_JSON_SYS);

    /* Register with system */
    printf("\n--- Registering app ---\n");
    int err = sceAppInstUtilInitialize();
    if (err) { printf("sceAppInstUtilInitialize: 0x%x\n", err); return 1; }

    err = sceAppInstUtilAppInstallTitleDir(TITLE_ID, "/user/app/", NULL);
    if (err) { printf("sceAppInstUtilAppInstallTitleDir: 0x%x\n", err); return 1; }

    printf("\n=== Done! Moonlight PS5 reinstalled — check homescreen. ===\n");
    return 0;
}
