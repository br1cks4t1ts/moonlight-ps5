/* dump_pkg.c — read FAKE00000 app.pkg and hunt for pkg files */
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

static void hexdump(const char *path, size_t maxbytes) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { printf("[cannot open %s errno=%d]\n", path, fd); return; }
    printf("=== %s ===\n", path);
    unsigned char buf[maxbytes > 4096 ? 4096 : maxbytes];
    ssize_t n = read(fd, buf, sizeof(buf));
    close(fd);
    if (n <= 0) { printf("[read failed n=%zd]\n", n); return; }
    for (ssize_t i = 0; i < n; i += 16) {
        printf("%04zx  ", i);
        for (ssize_t j = i; j < i+16 && j < n; j++) printf("%02x ", buf[j]);
        printf(" ");
        for (ssize_t j = i; j < i+16 && j < n; j++)
            printf("%c", buf[j]>=32&&buf[j]<127 ? buf[j] : '.');
        printf("\n");
    }
}

static void list_r(const char *path, int depth) {
    if (depth > 4) return;
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name,".") || !strcmp(e->d_name,"..")) continue;
        char sub[512]; snprintf(sub,sizeof(sub),"%s/%s",path,e->d_name);
        struct stat st; stat(sub, &st);
        printf("%*s%s  (%lld bytes)\n", depth*2,"", e->d_name, (long long)st.st_size);
        if (S_ISDIR(st.st_mode)) list_r(sub, depth+1);
    }
    closedir(d);
}

int main(void) {
    /* Try every plausible FAKE00000 location */
    const char *paths[] = {
        "/user/app/FAKE00000",
        "/system_ex/app/FAKE00000",
        "/mnt/sandbox/FAKE00000_00",
        "/system_data/priv/appmeta/FAKE00000",
        "/user/appmeta/FAKE00000",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        DIR *d = opendir(paths[i]);
        if (d) {
            printf("\nFOUND: %s\n", paths[i]);
            closedir(d);
            list_r(paths[i], 1);
        }
    }

    /* dump the app.pkg from user/app/FAKE00000 */
    printf("\n");
    hexdump("/user/app/FAKE00000/app.pkg", 512);

    /* dump app.json */
    hexdump("/user/app/FAKE00000/app.json", 1024);

    /* Check XMLS00001 */
    printf("\n=== XMLS00001 ===\n");
    list_r("/system_ex/app/XMLS00001", 1);

    /* any .pkg files in tmp */
    printf("\n=== /user/data/tmp/ ===\n");
    list_r("/user/data/tmp", 0);

    return 0;
}
