# Moonlight PS5

A Moonlight game streaming client for PS5 homebrew (firmware 10.00, etaHEN jailbreak).

## Status

**Pre-alpha** - This is a work in progress. The app can launch and connect to a Moonlight server, but video output requires further work to function in the PS5 environment.

## Requirements

- PS5 with etaHEN jailbreak (firmware 10.00)
- A Moonlight-compatible game streaming server (e.g., [Sunshine](https://github.com/LizardByte/Sunshine), Wolf/Games-on-Whales)
- Dev environment with PS5 toolchain

## Getting Dependencies

This repo does not include large dependencies. Clone them into the project root:

```bash
# Core Moonlight protocol (required)
git clone https://github.com/moonlight-stream/moonlight-common-c

# Other dependencies (required)
git clone https://github.com/ARM-software/mbedtls
git clone https://github.com/xiph/opus
git clone https://github.com/FFmpeg/FFmpeg

# Reference code (optional, for learning/porting)
git clone https://github.com/nicoco007/Moonlight-NX reference/Moonlight-Switch
```

## Building

```bash
# Build the ELF
cmake -B build -S moonlight-ps5
cmake --build build

# Create PKG installer
python3 make_pkg.py build/moonlight-ps5/eboot.bin moonlight.pkg --title-id MLPS00001 --title-name "Moonlight PS5"
```

## Configuration

Before running, edit the source files to set your streaming server IP:

- `moonlight-ps5/src/main.c` - Set `HOST_ADDRESS` to your server's IP
- `install_net.c`, `reinstall.c`, `update_app.c` - Set `HOST_IP` for helper tools

Or pass the IP as a command-line argument when launching the app.

## Authentication

The client will generate a new RSA key pair and certificate on first launch if `pkg-content/client_key.pem` and `pkg-content/client_cert.pem` are missing. You'll need to pair with your server using a PIN.

---

## Credits & Acknowledgments

### Core Moonlight Project
- **Moonlight Streaming** - The core game streaming protocol
  - [moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c)
  - Original developers: Moonlight Stream team (many contributors over the years)

### Reference Ports
- **Moonlight-Switch** - This PS5 port is heavily based on the Nintendo Switch port
  - [Moonlight-NX](https://github.com/nicoco007/Moonlight-NX)
  - Maintained by nicoco007 and contributors

### Dependencies
- **FFmpeg** - Video decoding (https://ffmpeg.org)
- **mbedTLS** - TLS/cryptography (https://tls.mbed.org)
- **Opus** - Audio codec (https://opus-codec.org)

### PS5 Development
- **PS5 Payload SDK** - Tools for PS5 homebrew development
- **ps5-pub-tools** - PKG building and signing tools
- **etaHEN** - The jailbreak that makes this possible

### Additional References
- Various PS5 homebrew projects that provided guidance on:
  - SceVideoOut integration
  - ScePad input handling
  - Audio output
  - PKG creation

---

## License

GPLv3 - See LICENSE file for details.

This project combines code from many sources, all used in compliance with their respective licenses.