# Parking Security System — Native C++ Edition

Real-time face recognition access control for parking security, built natively on the NVIDIA Jetson Orin Nano.

**Stack**: C++20 · NVIDIA DeepStream 7 · TensorRT 10 · CUDA 12 · Qt 6 · GStreamer · PostgreSQL

## Why C++ Native

This is the production-grade rewrite of the [Python/React prototype](../Ahmedu). It targets maximum performance by:

- Keeping video frames on GPU memory end-to-end (zero-copy)
- Using TensorRT instead of ONNX Runtime (2-3x faster inference)
- Running ArcFace + SCRFD at FP16 precision on Tensor Cores
- Rendering the UI through Qt's OpenGL shared context with the video pipeline
- Bypassing X11/GNOME via `nvdrmvideosink` for direct framebuffer output

**Targets**:
- 4K @ 60 FPS glass-to-glass latency < 15 ms
- CPU usage < 15% under load
- Concurrent support for 4+ cameras on one Jetson

## Project Status

**Scaffolding phase** — build system, module skeletons, and documentation are in place. Individual modules are being implemented iteratively.

See [docs/ROADMAP.md](docs/ROADMAP.md) for the implementation milestones.

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    parking-security (binary)                 │
│                                                              │
│  ┌─────────────┐   ┌─────────────┐   ┌─────────────────┐   │
│  │  Camera     │   │  Inference  │   │   Tracking      │   │
│  │  (GStreamer)│──►│  (TensorRT) │──►│   (FAISS + KF)  │   │
│  └─────────────┘   └─────────────┘   └────────┬────────┘   │
│                                                │             │
│  ┌─────────────┐   ┌─────────────┐   ┌────────▼────────┐   │
│  │   Alerts    │◄──│  Database   │◄──│   Event Bus     │   │
│  │(TG/Email)   │   │ (PostgreSQL)│   │                 │   │
│  └─────────────┘   └─────────────┘   └────────┬────────┘   │
│                                                │             │
│  ┌──────────────────────────────────────────────▼────────┐ │
│  │              Qt/QML UI (shared GPU context)           │ │
│  └────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the detailed data flow.

## Quick Start

```bash
# On a Jetson Orin Nano with JetPack 6.x:
./scripts/setup_jetson.sh        # Install build dependencies
./scripts/convert_models.sh      # Convert ONNX models to TensorRT engines
./scripts/build.sh               # Build the project (~5 min)
./build/parking-security         # Run
```

See [docs/BUILDING.md](docs/BUILDING.md) for detailed build instructions.

## Directory Layout

| Path                | Purpose                                           |
|---------------------|---------------------------------------------------|
| `include/parking/`  | Public C++ headers (API)                          |
| `src/`              | C++ implementations (per module)                  |
| `ui/`               | Qt6/QML UI code                                   |
| `cmake/`            | Custom `Find*.cmake` modules for deps             |
| `config/`           | Runtime config files (DeepStream, inference)      |
| `models/`           | TensorRT engine files (generated, not checked in) |
| `scripts/`          | Build, conversion, deployment utilities           |
| `tests/`            | Unit tests (GoogleTest)                           |
| `tools/`            | Standalone CLI utilities (enrollment, debug)      |
| `docs/`             | Documentation                                     |

## License

Internal / Proprietary. Do not distribute without permission.
