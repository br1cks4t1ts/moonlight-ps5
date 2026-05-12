/*
 * install_app.c — One-shot payload: installs Moonlight PS5 as a homescreen app.
 * Send this ELF via port 9021. After it completes, "Moonlight PS5" appears on
 * the PS5 homescreen (title ID MLPS00001).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/* ── Embedded eboot.bin (FSELF) via objcopy ─────────────────────────────── */
extern const unsigned char _binary_eboot_bin_start[];
extern const unsigned char _binary_eboot_bin_end[];

/* ── SceAppInstUtil ──────────────────────────────────────────────────────── */
int sceAppInstUtilInitialize(void);
int sceAppInstUtilAppInstallTitleDir(const char *titleId,
                                      const char *mountPath,
                                      void *reserved);

/* ── Helpers ─────────────────────────────────────────────────────────────── */
#define TITLE_ID "MLPS00001"

static const char PARAM_JSON[] =
    "{\n"
    "    \"applicationCategoryType\": 0,\n"
    "    \"localizedParameters\": {\n"
    "        \"defaultLanguage\": \"en-US\",\n"
    "        \"en-US\": {\n"
    "            \"titleName\": \"Moonlight PS5\"\n"
    "        }\n"
    "    },\n"
    "    \"titleId\": \"" TITLE_ID "\"\n"
    "}\n";

static const char PARAM_JSON_SYSTEM[] =
    "{\n"
    "    \"applicationCategoryType\": 33554432,\n"
    "    \"localizedParameters\": {\n"
    "        \"defaultLanguage\": \"en-US\",\n"
    "        \"en-US\": {\n"
    "            \"titleName\": \"Moonlight PS5\"\n"
    "        }\n"
    "    },\n"
    "    \"titleId\": \"" TITLE_ID "\"\n"
    "}\n";

static void make_dir(const char *path) {
    if (mkdir(path, 0755) != 0 && errno != EEXIST)
        fprintf(stderr, "mkdir %s: %d\n", path, errno);
    else
        printf("  dir: %s\n", path);
}

static int write_file(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "fopen %s: %d\n", path, errno); return -1; }
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    if (written != len) { fprintf(stderr, "short write %s\n", path); return -1; }
    printf("  wrote %zu bytes -> %s\n", len, path);
    return 0;
}

int main(void) {
    printf("\n=== Moonlight PS5 Installer ===\n");

    size_t eboot_size = (size_t)(_binary_eboot_bin_end - _binary_eboot_bin_start);
    printf("eboot.bin size: %zu bytes\n", eboot_size);

    /* Create directory tree */
    printf("\nCreating directories...\n");
    make_dir("/system_ex/app/" TITLE_ID);
    make_dir("/system_ex/app/" TITLE_ID "/sce_sys");
    make_dir("/user/app/" TITLE_ID);
    make_dir("/user/app/" TITLE_ID "/sce_sys");

    /* Write files */
    printf("\nWriting files...\n");
    if (write_file("/system_ex/app/" TITLE_ID "/eboot.bin",
                   _binary_eboot_bin_start, eboot_size) != 0)
        return 1;

    write_file("/user/app/" TITLE_ID "/sce_sys/param.json",
               PARAM_JSON, strlen(PARAM_JSON));
    write_file("/system_ex/app/" TITLE_ID "/sce_sys/param.json",
               PARAM_JSON_SYSTEM, strlen(PARAM_JSON_SYSTEM));

    /* Register with the system */
    printf("\nRegistering app with sceAppInstUtil...\n");
    int err;
    if ((err = sceAppInstUtilInitialize()) != 0) {
        fprintf(stderr, "sceAppInstUtilInitialize: 0x%x\n", err);
        return 1;
    }

    if ((err = sceAppInstUtilAppInstallTitleDir(TITLE_ID, "/user/app/", NULL)) != 0) {
        fprintf(stderr, "sceAppInstUtilAppInstallTitleDir: 0x%x\n", err);
        return 1;
    }

    printf("\n=== Moonlight PS5 installed! ===\n");
    printf("Title ID : %s\n", TITLE_ID);
    printf("Look for 'Moonlight PS5' on the homescreen.\n\n");
    return 0;
}
