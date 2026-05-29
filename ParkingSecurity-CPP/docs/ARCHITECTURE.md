# Architecture

This document describes the runtime architecture, thread model, and data flow
for the native C++ edition of the parking security system.

## Goals

1. **Zero-copy video path**: camera frames stay on GPU memory from capture to display
2. **Deterministic latency**: glass-to-glass < 15 ms at 1080p30
3. **Isolated threads**: no single slow module (DB, alerts, UI) can stall the detection loop
4. **One process**: all modules compiled into a single binary to avoid IPC overhead

## High-Level Diagram

```
                       ┌─────────────────────────┐
                       │   parking-security       │
                       │   (single binary)        │
                       └──────────┬──────────────┘
                                  │
        ┌─────────────────────────┼─────────────────────────────┐
        │                         │                             │
  ┌─────▼─────┐           ┌───────▼──────┐             ┌───────▼─────┐
  │  Camera   │           │  Inference   │             │  UI (Qt)    │
  │  Thread   │──frames──►│  Thread      │──results──►│  Thread     │
  │           │◄─request──│              │             │             │
  └───────────┘           └───────┬──────┘             └─────────────┘
                                  │
                            ┌─────▼──────┐
                            │ EventBus   │ (in-process pub/sub)
                            └─┬───┬───┬──┘
                              │   │   │
                 ┌────────────┘   │   └───────────────┐
                 │                │                   │
           ┌─────▼─────┐    ┌─────▼──────┐    ┌──────▼──────┐
           │  DB Writer│    │   Alerts   │    │  UI Model   │
           │  Thread   │    │  (thread  │    │  (Qt signal)│
           │           │    │   pool)    │    │             │
           └───────────┘    └────────────┘    └─────────────┘
```

## Thread Model

| Thread            | Ownership                | Priority |
|-------------------|--------------------------|----------|
| **Main/UI**       | Qt GUI, QML engine       | Normal   |
| **Camera**        | GStreamer + DeepStream   | Realtime |
| **Inference**     | TensorRT pipeline        | Realtime |
| **DB Writer**     | Event persistence        | Normal   |
| **Alert pool**    | Telegram / Email threads | Low      |
| **HTTP** (optional)| REST + WebSocket         | Normal   |

All threads communicate via:
- **Frame buffer**: single-producer, multi-consumer; latest wins; never blocks
- **EventBus**: subscription-based pub/sub for RecognitionResult
- **Connection pool**: shared DB connection pool, threads check out / return

## Data Flow (Per Frame)

```
1. v4l2 driver  ━━━━ DMA ━━━━►  NvBufSurface (GPU memory)     [0 µs]
2. nvvideoconvert (scale + RGB)  ─── Tensor on GPU             [1 ms]
3. SCRFD TensorRT engine          ─── Bounding boxes           [4 ms]
4. For each face: ArcFace engine  ─── 512-dim embedding        [6 ms / face]
5. EmbeddingIndex.query()         ─── Matched person_id        [0.5 ms]
6. ScheduleChecker.check()        ─── Cached lookup, 10-min TTL [< 0.1 ms]
7. nvdsosd overlay on GPU          ─── Rendered bbox + label   [1 ms]
8. nvdrmvideosink (HDMI)           ─── Direct to framebuffer   [0 ms]
                                    ────────────────────────
                                    Total: ~12 ms glass-to-glass
```

In parallel (non-blocking):
- Frame goes to Qt VideoProvider for dashboard display
- RecognitionResult published to EventBus
- DB writer asynchronously INSERTs the event
- Alert service dispatches to configured channels

## Module Boundaries

Each module has a single `include/parking/<mod>/*.hpp` public header and
a `src/<mod>/*.cpp` implementation. Cross-module calls go through **interfaces**,
not concrete types — this keeps modules swappable.

Example: the `EventBus` has no knowledge of database, alerts, or UI. Each of
those subscribes independently. You could remove the alert service and the
rest still works.

## Performance Budget (1080p @ 30 FPS)

| Component        | Budget  | Measured | Notes                            |
|------------------|---------|----------|----------------------------------|
| Frame capture    | 2 ms    | —        | Direct v4l2, no userspace copy  |
| Preprocessing    | 1 ms    | —        | CUDA kernel on NvBufSurface      |
| Face detection   | 5 ms    | —        | SCRFD FP16, TensorRT 10         |
| Embedding extract| 6 ms    | —        | ArcFace FP16, batch of faces    |
| Index lookup     | 1 ms    | —        | Matmul on pre-normalized matrix |
| Schedule check   | 0.1 ms  | —        | Cached                          |
| Overlay + display| 2 ms    | —        | nvdsosd + nvdrmvideosink        |
| **Total**        | **~17 ms** | —    | Per frame at 30 FPS             |

With a 33 ms per-frame budget (30 FPS), we have 47% headroom for multi-camera
or higher resolutions.

## Scaling

- **Multiple cameras**: add more CameraPipeline instances to the main process.
  TensorRT engines can be shared across pipelines via the same `TrtEngine` (one
  context per thread, shared underlying engine).
- **Larger person database** (10k+): swap `EmbeddingIndex` for FAISS-GPU + HNSW.
  The public API stays identical; only the implementation changes.
- **Horizontal scale**: not needed at Jetson scale. For a facility with
  20+ cameras, use multiple Jetsons sharing one PostgreSQL.

## Implementation Order

1. `core/` + `camera/` + `inference/` — the hot path
2. Wiring in `main.cpp` (skip UI, headless first)
3. `database/` + `schedule/` — enrollment and queries
4. `alerts/` — Telegram, then Email
5. `ui/` — Qt/QML dashboard last

See [docs/ROADMAP.md](ROADMAP.md) for the concrete milestones.
