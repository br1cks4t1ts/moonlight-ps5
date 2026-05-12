# PS5 Moonlight Streaming — AI Handoff Document

## Goal
Build and deploy a working Moonlight game streaming client as a PS5 homescreen app on a
jailbroken PS5 (firmware 10.00, etaHEN). The streaming server is Wolf/Games-on-Whales
running in Docker at YOUR_WOLF_SERVER_IP.

---

## Hardware / Network
- PS5 fw 10.00, etaHEN jailbreak
- PS5 IP: 172.16.132.27
- Dev machine IP: YOUR_DEV_MACHINE_IP (Linux, Arch, RTX 4070 Ti SUPER)
- PS5 services:
  - FTP: 172.16.132.27:1337
  - klog: 172.16.132.27:9081 (kernel log TCP stream)
  - Package installer: 172.16.132.27:9090
  - Web package installer (DPI v2): 172.16.132.27:12800
- Dev machine runs a simple HTTP server (python3 -m http.server) in /home/mike/ps5-moonlight/
  when needed for pushing files to the PS5

---

## Project Directory: /home/mike/ps5-moonlight/

Key files:
- `make_pkg.py`             — builds the .pkg installer for the PS5
- `moonlight.pkg`           — latest built PKG (ready to test install)
- `launcher.c`              — ABANDONED: ET_EXEC launcher stub (approach failed)
- `build/moonlight-ps5/eboot.bin`        — npdrm_exec FSELF of moonlight-ps5 (main app)
- `build/moonlight-ps5/moonlight-ps5.elf`— the actual moonlight ELF (9 MB)
- `build/moonlight-ps5/launcher_eboot.bin` — old launcher FSELF (not used)
- `moonlight-ps5/src/main.c`            — main app source
- `moonlight-ps5/src/video.c`           — video init (sceVideoOut)
- `moonlight-ps5/src/audio.c`           — audio
- `moonlight-ps5/src/input.c`           — gamepad input
- `moonlight-ps5/src/gamestream.c`      — Moonlight protocol / pairing / streaming

Downloaded reference PKGs (DO NOT install these, for header analysis only):
- `/home/mike/Downloads/PS5_ITEM00001_v1.14.pkg`  — ItemzFlow PS5 homebrew (installs fine)
- `/home/mike/Downloads/PS4_SFIN00000_v0.9.0.pkg` — PS4 Swift homebrew

---

## PS5 Current State (via FTP)

App is installed at TWO locations:
- `/system_ex/app/MLPS00001/` — contains eboot.bin (npdrm_exec FSELF, 4.4 MB),
  moonlight-ps5.elf (9 MB), client_cert.pem, client_key.pem (pairing certs — keep these!)
- `/user/app/MLPS00001/` — contains sce_sys/param.json

`/user/appmeta/MLPS00001/` does NOT exist — the app was installed via sceAppInstUtil
(directory install), not via PKG, so it has no appmeta entry.

Installed homebrew apps visible in appmeta:
- ITEM00001 — ItemzFlow (PS5 package manager, installed on homescreen)
- FAKE00000 — etaHEN homebrew loader
- PPSA01615 — Netflix
- SFIN00000 — PS4 homebrew Swift (installed today via USB, works fine)

---

## Core Problem

`sceVideoOutOpen` returns `0x80290001` (no video session) because the app runs as
`SCE_LNC_APP_TYPE_DAEMON`. Video output only works for `BIG_APP` (game category).

BIG_APP requires `is_pkgstep=1` in the PS5's internal app database. This flag is only set
when an app is installed via PKG (not via directory install / sceAppInstUtil).

Our app was installed via sceAppInstUtil → `is_pkgstep=0` → BIG_APP mount fails with
`initializeMountRoot: 0x80020008`.

---

## What We've Tried

### Approach 1: launcher.c (ABANDONED)
ET_EXEC FSELF stub using syscall 594 (SYS_sprx_load / sceKernelLoadStartModule) to load
moonlight-ps5.elf in-process. Failed: syscall 594 expects SPRX format, not plain PIE ELF.
Result: grey screen, no output.

### Approach 2: PKG installation via DPI v2 (port 12800) — IN PROGRESS
Goal: install as proper PKG so PS5 sets is_pkgstep=1 → BIG_APP works → sceVideoOutOpen works.

PKG format issues encountered and fixed so far in make_pkg.py:
1. `SCE_NP_DRM_CONTENT_ERROR_HEADERVERSION (0x80F00102)` — fixed by adding NP DRM content
   header at offset 0x400 in the PKG (version=1 required)
2. `SCE_NP_DRM_CONTENT_ERROR_HEADERSIGN (0x80F00101)` — CURRENT BLOCKER

The HEADERSIGN error persists even though our PKG at offset 0x400 is byte-for-byte identical
to the working ItemzFlow PKG's content header. The SC entry hashes at 0x100-0x17F in the
PKG main header are all zeros in ours but are non-zero in the working PKG. This is likely
the actual thing being checked.

Also tried: USB install via PS5 native "Install Package Files" menu → same CE-109576-8 error.

---

## make_pkg.py Current Header Values (IMPORTANT)

```python
revision      = 0x0000       # at 0x04
type          = 0x0001       # at 0x06
drm_type      = 0x0000000F   # at 0x70 (fake)
content_type  = 0x0000001A   # at 0x74 (PS5 app)
content_flags = 0x0A000000   # at 0x78
iro_tag       = 0x00000000   # at 0x98
```

Content header at 0x400: first 0x80 bytes copied from ItemzFlow PKG (version=1, includes
the 64-byte signature blob), remaining 0x34c bytes zeroed.

SC entry hashes at 0x100-0x17F: all zeros (NOT filled in — likely causing HEADERSIGN).

---

## Most Promising Next Steps

### Option A: Fill in SC entry hashes
The working PKG has 128 bytes of hash data at 0x100-0x17F. These are HMAC-SHA256 hashes of
the entry table. If we compute and fill these in, the HEADERSIGN check might pass.
Try computing SHA256 of our entry table and writing it at 0x100.

### Option B: Copy full PKG header from ItemzFlow, replace only content_id + entries
Take the first 0x1000 bytes of ItemzFlow's PKG verbatim, replace content_id at 0x40 with
ours, then append our entry table and data. Risky but might get past all header checks.

### Option C: Use ItemzFlow on the PS5 to install
ItemzFlow (ITEM00001) is installed and running on the PS5 homescreen. It has its own PKG
installer. Try uploading moonlight.pkg to ItemzFlow directly — it may use a less strict
installation path than DPI v2.

### Option D: Abandon PKG install, fix sceVideoOutOpen for DAEMON context
Revert param.json to applicationCategoryType=33554432 (DAEMON), app launches but video
fails. The video.c already has ucred elevation logic. Investigate whether it can actually
open video from daemon context, possibly by calling sceVideoOutOpen from a separate thread
that mimics a user session, or by using sceVideoOutOpen with type=0 (system).

---

## app param.json (current — BIG_APP mode, but mount fails)
`/system_ex/app/MLPS00001/sce_sys/param.json` and `/user/app/MLPS00001/sce_sys/param.json`:
```json
{"applicationCategoryType": 0, ...}
```
0 = BIG_APP/game. Was 33554432 (0x2000000) = DAEMON. Change back to 33554432 to get
the app to actually launch (as daemon), then fix video separately.

---

## Diagnostics
- klog (port 9081): `nc 172.16.132.27 9081` — streams PS5 kernel log
- Diag TCP: main.c connects to YOUR_DEV_PC_IP:9999 on startup, redirects stdout/stderr
  there. Run `python3 -c "import socket,sys; s=socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); s.bind(('0.0.0.0',9999)); s.listen(5); c,a=s.accept(); [sys.stdout.write(d.decode(errors='replace')) or sys.stdout.flush() for d in iter(lambda:c.recv(4096),b'')]"` on dev machine to receive output.
- Pairing certs at `/system_ex/app/MLPS00001/client_cert.pem` and `client_key.pem` — DO NOT
  delete, pairing with wolf survived factory reset because these were preserved.

---

## Streaming Config (in main.c)
- Host: YOUR_GAME_STREAMING_SERVER_IP (Wolf server)
- App ID: 134906179 (Wolf UI)
- Resolution: 1920x1080 @ 60fps, 20 Mbps, H.264
- Encryption: ENCFLG_NONE

---

## Build System
CMake-based. To rebuild:
```bash
cd /home/mike/ps5-moonlight
cmake --build build --target moonlight-ps5 2>&1
# output: build/moonlight-ps5/eboot.bin (npdrm_exec FSELF)
#         build/moonlight-ps5/moonlight-ps5.elf
```
Then rebuild PKG:
```bash
python3 make_pkg.py build/moonlight-ps5/eboot.bin moonlight.pkg --title-id MLPS00001 --title-name "Moonlight PS5"
```

---

## What is NOT broken
- Pairing with Wolf: completed, certs on PS5 filesystem, survived factory reset
- TCP diagnostics in main.c: working (verified output reaches dev machine)
- moonlight-common-c integration: builds and links correctly
- Audio/input: untested but code is in place
- The app DOES launch as DAEMON (grey screen) — it runs, just no video
