/* update_app.c — replaces eboot.bin with ET_EXEC launcher and
 * downloads moonlight-ps5.elf into the app directory. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>

#define HOST_IP   "YOUR_SERVER_IP"
#define HOST_PORT 8888
#define TITLE_ID  "MLPS00001"

static long http_fetch(const char *url_path, const char *dest) {
    printf("fetch %s -> %s\n", url_path, dest);
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
        "GET %s HTTP/1.0\r\nHost: %s:%d\r\nConnection: close\r\n\r\n",
        url_path, HOST_IP, HOST_PORT);
    send(sock, req, strlen(req), 0);
    /* skip headers */
    char c, prev[4] = {0};
    while (recv(sock, &c, 1, 0) == 1) {
        prev[0]=prev[1]; prev[1]=prev[2]; prev[2]=prev[3]; prev[3]=c;
        if (prev[0]=='\r'&&prev[1]=='\n'&&prev[2]=='\r'&&prev[3]=='\n') break;
    }
    FILE *f = fopen(dest, "wb");
    if (!f) { printf("fopen %s: %d\n", dest, errno); close(sock); return -1; }
    char buf[4096]; long total = 0; int n;
    while ((n = recv(sock, buf, sizeof(buf), 0)) > 0) {
        fwrite(buf, 1, n, f); total += n;
    }
    fclose(f); close(sock);
    printf("  %ld bytes\n", total);
    return total;
}

int main(void) {
    printf("=== Moonlight PS5 App Updater ===\n");

    /* Replace eboot.bin with proper ET_EXEC launcher stub */
    long n = http_fetch("/launcher_eboot.bin",
                        "/system_ex/app/" TITLE_ID "/eboot.bin");
    if (n <= 0) { printf("ERROR: launcher fetch failed\n"); return 1; }

    /* Fetch moonlight-ps5.elf — loaded by launcher and sent to etaHEN port 9021 */
    n = http_fetch("/moonlight-ps5.elf",
                   "/system_ex/app/" TITLE_ID "/moonlight-ps5.elf");
    if (n <= 0) { printf("ERROR: moonlight ELF fetch failed\n"); return 1; }

    printf("\n=== Done! Try launching Moonlight PS5 from homescreen. ===\n");
    return 0;
}
