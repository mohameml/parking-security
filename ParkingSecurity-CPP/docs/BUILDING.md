# Building

## Target Platform

- **Primary**: NVIDIA Jetson Orin Nano Super with JetPack 6.2+ (Ubuntu 22.04, L4T R36.4.x)
- **Development**: Ubuntu 22.04 x86_64 (for code editing; cross-compile or deploy for testing)

Other Jetson modules (Orin Nano non-super, Orin NX, AGX Orin) will also work
with minor CMake tweaks to the CUDA architecture flag (`-DCMAKE_CUDA_ARCHITECTURES=87`).

## Prerequisites

Run the setup script to install everything at once:

```bash
./scripts/setup_jetson.sh
```

That installs:

| Package             | Version | Notes                                          |
|---------------------|---------|------------------------------------------------|
| CMake               | 3.20+   | JetPack ships 3.22.1; newer is fine            |
| GCC / Clang         | 11+     | C++20 required                                 |
| CUDA Toolkit        | 12.6    | Comes with JetPack                             |
| TensorRT            | 10.3+   | Comes with JetPack                             |
| GStreamer           | 1.20    | With plugins-base, -good, -bad                 |
| DeepStream SDK      | 7.x     | Optional but recommended (download from NVIDIA)|
| Qt 6                | 6.4+    | `qt6-base-dev` + `qt6-declarative-dev`         |
| libpqxx             | 7.x     | `libpqxx-dev`                                  |
| libcurl             | 7.74+   | For Telegram/Email alerters                    |

spdlog and nlohmann/json are auto-fetched by CMake if missing.

## Build

```bash
./scripts/build.sh         # Release
./scripts/build.sh Debug   # Debug with sanitizers
```

The script runs:

```bash
cmake -B build/release -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build/release --parallel $(nproc)
```

Output: `build/release/parking-security` and `build/release/tools/*`.

## CMake Options

```
-DPARKING_BUILD_UI=ON        # Build Qt/QML dashboard (default ON)
-DPARKING_BUILD_TESTS=OFF    # Build unit tests (default OFF)
-DPARKING_BUILD_TOOLS=ON     # Build CLI tools (default ON)
-DPARKING_USE_DEEPSTREAM=ON  # Use DeepStream (default ON — fallback to raw GStreamer)
-DPARKING_TARGET_JETSON=ON   # Jetson-specific CPU flags (default ON)
```

Example: build without DeepStream for development on x86:

```bash
cmake -B build -DPARKING_USE_DEEPSTREAM=OFF -DPARKING_TARGET_JETSON=OFF
```

## Convert Models

The TensorRT engine files are generated at build time from the InsightFace
buffalo_l ONNX models. They are **not** checked into version control because
they are tied to the target GPU architecture and TensorRT version.

```bash
# Copy buffalo_l from the Python project first:
scp -r una@192.168.100.39:~/.insightface/models/buffalo_l ~/buffalo_l

# Then:
./scripts/convert_models.sh ~/buffalo_l
# Outputs: models/det_10g.engine, models/w600k_r50.engine, ...
```

For INT8 precision (smallest, fastest, requires a calibration dataset):

```bash
PRECISION=int8 ./scripts/convert_models.sh ~/buffalo_l
```

## Running

```bash
./build/release/parking-security config/parking.ini
```

Or via systemd (copy `scripts/parking-security.service` into `/etc/systemd/system/`
and `systemctl enable --now parking-security`).

## Cross-compile from x86 (optional)

If you prefer to build on a desktop and deploy:

```bash
./scripts/deploy_jetson.sh una@192.168.100.39
```

This rsyncs the source, builds on the target, and installs.

## IDE Setup

The build generates `compile_commands.json` (symlinked to project root). clangd,
VSCode, CLion, and Neovim (with lspconfig) all work out of the box.

Recommended VSCode extensions:
- `llvm-vs-code-extensions.vscode-clangd`
- `twxs.cmake` + `ms-vscode.cmake-tools`

## Troubleshooting

**"TensorRT not found"** — install JetPack 6.x. TensorRT is shipped with it.

**"DeepStream not found"** — download from https://developer.nvidia.com/deepstream-download
and install the `.deb`, or disable with `-DPARKING_USE_DEEPSTREAM=OFF`.

**"Qt 6 not found"** — `sudo apt install qt6-base-dev qt6-declarative-dev`.

**Build is slow** — use `ninja` instead of `make` (default in `build.sh`). Build is ~2 min on Jetson.
