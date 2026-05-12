/* dump_fs.c — payload that lists /system_ex/app/ and dumps key files */
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

static void dump_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { printf("  [cannot open %s]\n", path); return; }
    printf("  === %s ===\n", path);
    unsigned char buf[512];
    size_t n;
    size_t total = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && total < 2048) {
        /* print as hex + ascii */
        for (size_t i = 0; i < n; i += 16) {
            printf("  %04zx  ", total + i);
            for (size_t j = i; j < i+16 && j < n; j++)
                printf("%02x ", buf[j]);
            printf("  ");
            for (size_t j = i; j < i+16 && j < n; j++)
                printf("%c", (buf[j]>=32&&buf[j]<127)?buf[j]:'.');
            printf("\n");
        }
        total += n;
    }
    fclose(f);
}

static void list_dir(const char *path, int depth) {
    DIR *d = opendir(path);
    if (!d) { printf("%*s[cannot open %s]\n", depth*2, "", path); return; }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name,".") || !strcmp(e->d_name,"..")) continue;
        char sub[512];
        snprintf(sub, sizeof(sub), "%s/%s", path, e->d_name);
        printf("%*s%s\n", depth*2, "", e->d_name);
        if (depth < 3) list_dir(sub, depth+1);
    }
    closedir(d);
}

int main(void) {
    printf("\n=== /system_ex/app/ ===\n");
    list_dir("/system_ex/app", 0);

    printf("\n=== /user/app/ ===\n");
    list_dir("/user/app", 0);

    /* dump key files from the homebrew loader */
    printf("\n=== file dumps ===\n");
    dump_file("/user/app/FAKE00000/sce_sys/param.json");
    dump_file("/system_ex/app/FAKE00000/sce_sys/param.json");

    /* try to find any .pkg files left in temp */
    printf("\n=== /user/data/tmp/ ===\n");
    list_dir("/user/data/tmp", 0);

    return 0;
}
