/*
 * install_net.c — small PS5 payload that fetches files from an HTTP server
 * on the host machine and installs Moonlight as a homescreen app.
 *
 * Compile for PS5, send via port 9021.
 * Host must be running:  python3 -m http.server 8888  in the build dir.
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

/* ── tunables ────────────────────────────────────────────────────────────── */
#define HOST_IP   "YOUR_SERVER_IP"
#define HOST_PORT 8888
#define TITLE_ID  "MLPS00001"

/* ── SceAppInstUtil ──────────────────────────────────────────────────────── */
int sceAppInstUtilInitialize(void);
int sceAppInstUtilAppInstallTitleDir(const char *titleId,
                                      const char *mountPath,
                                      void *reserved);

/* ── helpers ─────────────────────────────────────────────────────────────── */
static void make_dir(const char *path) {
    if (mkdir(path, 0755) == 0)
        printf("mkdir: %s\n", path);
    else if (errno != EEXIST)
        printf("mkdir FAIL %s errno=%d\n", path, errno);
}

/* HTTP/1.0 GET → write to path; returns bytes written or -1 */
static long http_fetch(const char *url_path, const char *dest) {
    printf("fetch http://%s:%d%s -> %s\n", HOST_IP, HOST_PORT, url_path, dest);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { printf("socket err %d\n", sock); return -1; }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(HOST_PORT);
    inet_pton(AF_INET, HOST_IP, &addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        printf("connect err %d\n", errno);
        close(sock);
        return -1;
    }

    char req[512];
    snprintf(req, sizeof(req),
        "GET %s HTTP/1.0\r\nHost: %s:%d\r\nConnection: close\r\n\r\n",
        url_path, HOST_IP, HOST_PORT);
    send(sock, req, strlen(req), 0);

    /* skip HTTP response headers */
    char buf[4096];
    int header_done = 0;
    char hdr_buf[8192];
    int hdr_len = 0;
    while (!header_done) {
        int n = recv(sock, hdr_buf + hdr_len, 1, 0);
        if (n <= 0) break;
        hdr_len++;
        if (hdr_len >= 4 &&
            hdr_buf[hdr_len-4]=='\r' && hdr_buf[hdr_len-3]=='\n' &&
            hdr_buf[hdr_len-2]=='\r' && hdr_buf[hdr_len-1]=='\n')
            header_done = 1;
    }

    FILE *f = fopen(dest, "wb");
    if (!f) { printf("fopen FAIL %s errno=%d\n", dest, errno); close(sock); return -1; }

    long total = 0;
    int n;
    while ((n = recv(sock, buf, sizeof(buf), 0)) > 0) {
        fwrite(buf, 1, n, f);
        total += n;
    }
    fclose(f);
    close(sock);
    printf("  -> %ld bytes\n", total);
    return total;
}

static const char PARAM_JSON[] =
    "{\"applicationCategoryType\":0,"
    "\"localizedParameters\":{\"defaultLanguage\":\"en-US\","
    "\"en-US\":{\"titleName\":\"Moonlight PS5\"}},"
    "\"titleId\":\"" TITLE_ID "\"}";

static const char PARAM_JSON_SYS[] =
    "{\"applicationCategoryType\":33554432,"
    "\"localizedParameters\":{\"defaultLanguage\":\"en-US\","
    "\"en-US\":{\"titleName\":\"Moonlight PS5\"}},"
    "\"titleId\":\"" TITLE_ID "\"}";

static void write_str(const char *path, const char *s) {
    FILE *f = fopen(path, "w");
    if (!f) { printf("write_str FAIL %s errno=%d\n", path, errno); return; }
    fputs(s, f);
    fclose(f);
    printf("wrote: %s\n", path);
}

int main(void) {
    printf("\n=== Moonlight PS5 Network Installer ===\n");
    printf("Host: %s:%d\n\n", HOST_IP, HOST_PORT);

    /* Create directories */
    make_dir("/system_ex/app/" TITLE_ID);
    make_dir("/system_ex/app/" TITLE_ID "/sce_sys");
    make_dir("/user/app/" TITLE_ID);
    make_dir("/user/app/" TITLE_ID "/sce_sys");

    /* Fetch eboot.bin from host */
    long n = http_fetch("/eboot.bin", "/system_ex/app/" TITLE_ID "/eboot.bin");
    if (n <= 0) {
        printf("ERROR: failed to fetch eboot.bin\n");
        return 1;
    }

    /* Optionally fetch icon */
    http_fetch("/icon0.png", "/user/app/" TITLE_ID "/sce_sys/icon0.png");

    /* Write param.json */
    write_str("/user/app/" TITLE_ID "/sce_sys/param.json", PARAM_JSON);
    write_str("/system_ex/app/" TITLE_ID "/sce_sys/param.json", PARAM_JSON_SYS);

    /* Register with system */
    printf("\nCalling sceAppInstUtilInitialize...\n");
    int err = sceAppInstUtilInitialize();
    if (err) { printf("sceAppInstUtilInitialize: 0x%x\n", err); return 1; }

    printf("Calling sceAppInstUtilAppInstallTitleDir...\n");
    err = sceAppInstUtilAppInstallTitleDir(TITLE_ID, "/user/app/", NULL);
    if (err) { printf("sceAppInstUtilAppInstallTitleDir: 0x%x\n", err); return 1; }

    printf("\n=== Installed! Look for 'Moonlight PS5' on homescreen ===\n");
    return 0;
}
