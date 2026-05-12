#pragma once
#include "Limelight.h"
#include <stdbool.h>

typedef struct {
    char address[256];
    char appVersion[64];
    char gfeVersion[64];
    char serverCertPem[4096];
    char serverCertHex[8192];
    char rtspSessionUrl[512];
    bool paired;
    int  currentGame;
    unsigned short httpPort;
    unsigned short httpsPort;
    SERVER_INFORMATION serverInfo;
} GS_SERVER;

typedef struct {
    int   id;
    char  name[256];
} GS_APP;

/* Call once at startup — generates or loads client cert/key */
int  gs_init(const char *address);

/* Returns the 4-digit PIN to enter on the Sunshine web UI */
int  gs_pair(GS_SERVER *server, const char *pin);

/* Populate server with current state (version, paired status) */
int  gs_load_server_info(GS_SERVER *server);

/* Print available apps and their IDs */
void gs_print_applist(GS_SERVER *server);

/* Launch appId and fill out LiStartConnection parameters */
int  gs_start_app(GS_SERVER *server, PSTREAM_CONFIGURATION config, int appId);

/* Stop current app */
int  gs_quit_app(GS_SERVER *server);

/* Expose client cert PEM for the LiStartConnection serverInfo */
const char *gs_client_cert_pem(void);
const char *gs_client_key_pem(void);
