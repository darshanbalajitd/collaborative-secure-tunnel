# collaborative-secure-tunnel

A secure bidirectional terminal tunnel with PTY support for interactive sessions on Windows and Linux.

## Overview
- Provides an encrypted tunnel over TCP using TLS (mbedTLS) for sending terminal data.
- Supports server (listener) and client (connector) modes.
- Spawns a shell on the server side and bridges I/O across TLS.
- Windows uses ConPTY to run `cmd.exe`; Linux uses a PTY to run `$SHELL` or `/bin/sh`.

## How TLS Works (Brief)
- After a TCP connection is established, a TLS context is configured and a handshake is performed (`src/tls_wrapper.cpp`, `src/session_manager.cpp`).
- TLS version is negotiated automatically (TLS 1.2+; TLS 1.3 if supported by your mbedTLS build).
- Cipher suites are chosen from mbedTLS defaults (typically ECDHE with AES‑GCM or ChaCha20‑Poly1305).
- Certificate fingerprint (SHA‑256) is shown after successful handshake.
- Verification modes:
  - No CA provided: encryption without peer verification.
  - CA provided: optional verification; use `--verify-required` to enforce strict verification.

## Project Structure
- `src/main.cpp`: Parses flags, initializes logging and signal handlers, starts listener or client.
- `src/session_manager.cpp`: Establishes sockets, sets up TLS, runs server/client session.
- `src/tls_wrapper.cpp/.hpp`: TLS context setup, certificates, handshake, read/write helpers.
- `src/io_bridge.cpp`: Frames data and bridges between TLS and console/PTY.
- `src/control_protocol.cpp/.hpp`: Control plane placeholders (e.g., resize messages).
- `src/listener_win.cpp` and `src/listener.cpp`: TCP listener implementations for Windows/Linux.
- `src/pty_handler_win.cpp` and `src/pty_handler.cpp`: PTY handling and shell execution per platform.
- `src/resize_coalescer_*`: Resize event capture and forwarding.
- `CMakeLists.txt`: Build configuration linking `MbedTLS::mbedtls` and `nlohmann_json::nlohmann_json`.

## Installation (Skip steps if already installed)

### Windows
- Visual Studio 2022 (Desktop development with C++) or Build Tools.
- CMake (3.12+).
- Git.
- OpenSSL (optional; used for local certificate generation).
- vcpkg:
  - Clone: `git clone https://github.com/microsoft/vcpkg "%USERPROFILE%\vcpkg"`
  - Bootstrap: `%USERPROFILE%\vcpkg\bootstrap-vcpkg.bat`
  - Install libraries: `%USERPROFILE%\vcpkg\vcpkg.exe install mbedtls nlohmann-json`

### Linux/WSL
- Install toolchain: `sudo apt-get update && sudo apt-get install -y build-essential cmake git openssl pkg-config`
- vcpkg:
  - Clone: `git clone https://github.com/microsoft/vcpkg "$HOME/vcpkg"`
  - Bootstrap: `$HOME/vcpkg/bootstrap-vcpkg.sh`
  - Install libraries: `$HOME/vcpkg/vcpkg install mbedtls nlohmann-json`

## Building

### Windows
- Configure:
  - `cmake -S . -B build -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE="%USERPROFILE%\vcpkg\scripts\buildsystems\vcpkg.cmake"`
- Build:
  - `cmake --build build --config Release`

### Linux/WSL
- Configure:
  - `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake" -DCMAKE_BUILD_TYPE=Release`
- Build:
  - `cmake --build build --config Release -- -j$(nproc)`

## Certificates and Authentication
- Provide `--cert` and `--key` for the server (and optionally client) plus `--cacert` for verification in client mode.
- First‑run convenience:
  - Use `--auto-cert` with `--keytype ecdsa|rsa` to generate a self‑signed pair when files are missing.
  - Example: `./secure-tunnel --listen --port 5000 --auto-cert --keytype ecdsa`
- Enforce verification: add `--verify-required` when a CA is provided.
- Show negotiated TLS details: add `--tls-info`.

## Running
- Listener (server):
  - Windows: `build\Release\secure-tunnel.exe --listen --port 5000 --cert cert.pem --key key.pem [--debug] [--tls-info]`
  - Linux: `./build/secure-tunnel --listen --port 5000 --cert cert.pem --key key.pem [--debug] [--tls-info]`
- Connector (client):
  - Windows: `build\Release\secure-tunnel.exe --connect <server_ip> --port 5000 [--cacert cert.pem] [--debug] [--tls-info]`
  - Linux: `./build/secure-tunnel --connect <server_ip> --port 5000 [--cacert cert.pem] [--debug] [--tls-info]`

### First Run Quickstart
- Generate a self‑signed pair on the server if missing and start listening:
  - Windows: `build\Release\secure-tunnel.exe --listen --port 5000 --auto-cert --keytype ecdsa --tls-info`
  - Linux: `./build/secure-tunnel --listen --port 5000 --auto-cert --keytype ecdsa --tls-info`
- Connect from client (optional verification when CA is provided):
  - Windows: `build\Release\secure-tunnel.exe --connect <server_ip> --port 5000 [--cacert cert.pem] --tls-info`
  - Linux: `./build/secure-tunnel --connect <server_ip> --port 5000 [--cacert cert.pem] --tls-info`

### Mirror Flags
- `--mirror-output`: Mirror PTY output to the server’s console.
- `--mirror-input`: Forward server console input to the PTY, enabling local typing while the client is connected.
- `--mirror`: Convenience flag that enables both `--mirror-output` and `--mirror-input`.
- `--mirror-clean`: Clean/sanitize mirrored output for a more readable server console display.

Examples:
- Mirror both directions with cleaned server output:
  - Windows: `build\Release\secure-tunnel.exe --listen --port 5000 --cert cert.pem --key key.pem --mirror --mirror-clean`
  - Linux: `./build/secure-tunnel --listen --port 5000 --cert cert.pem --key key.pem --mirror --mirror-clean`
- Only mirror server input:
  - `... --mirror-input`
- Only mirror server output:
  - `... --mirror-output`

### Verification Modes
- No verification (encrypted channel, peer not verified): omit `--cacert`.
  - Windows: `build\Release\secure-tunnel.exe --connect <server_ip> --port 4444`
  - Linux: `./build/secure-tunnel --connect <server_ip> --port 4444`
- Optional verification (default when `--cacert` is provided): proceeds even if chain fails.
  - Windows: `build\Release\secure-tunnel.exe --connect <server_ip> --port 4444 --cacert cert.pem`
  - Linux: `./build/secure-tunnel --connect <server_ip> --port 4444 --cacert cert.pem`
- Strict verification (peer must validate against CA): add `--verify-required`.
  - Windows: `build\Release\secure-tunnel.exe --connect <server_ip> --port 4444 --cacert cert.pem --verify-required`
  - Linux: `./build/secure-tunnel --connect <server_ip> --port 4444 --cacert cert.pem --verify-required`

### Address Notes
- Replace `<server_ip>` with the actual IP or hostname of the server.
- On the same device, use `localhost` or `127.0.0.1`.
- On the same local network, use the server machine’s LAN IP (e.g., `192.168.x.y`) or its hostname.

## JSON Handling
This project uses the [nlohmann/json](https://github.com/nlohmann/json) library for robust JSON serialization and deserialization.

## Notes
- Prefer ECDSA P‑256 certificates for modern cipher suites; RSA is supported as a fallback.
- TLS configuration uses mbedTLS defaults; you can enforce a minimum version or specific suites by extending `src/tls_wrapper.cpp`.
