# Implementation Roadmap

This is the iterative plan for turning the scaffolding into a production system.
Each milestone is ~1-2 weeks of focused work. Tests and docs grow alongside
each milestone, not as a separate phase.

## Status Legend

- ✓ Done
- 🔶 In progress
- ⬜ Not started

## Milestone 0 — Scaffolding (✓ Complete)

- ✓ Directory structure, CMake build system
- ✓ Core headers (types, config, logger, event bus)
- ✓ Module interfaces (camera, inference, tracking, database, alerts, schedule)
- ✓ Qt UI skeleton
- ✓ Build, convert, deploy scripts
- ✓ Documentation

## Milestone 1 — Hot Path (detection only, headless) ⬜

**Goal**: capture camera → detect faces → print bounding boxes. No DB, no UI.

- ⬜ `src/inference/tensorrt_engine.cpp` — deserialize + infer
- ⬜ `src/inference/face_detector.cpp` — SCRFD pre/post-processing, NMS
- ⬜ `src/camera/camera_pipeline.cpp` — GStreamer `v4l2src → appsink` pipeline
- ⬜ `src/camera/gstreamer_helpers.cpp` — NvBufSurface → CUDA pointer helpers
- ⬜ Wire into `main.cpp` with `--headless --print-detections` flag
- ⬜ Integration test with a sample video

**Exit criteria**: 30 FPS detection at 1080p, bounding boxes printed to stdout.

## Milestone 2 — Recognition + Index ⬜

- ⬜ `src/inference/face_recognizer.cpp` — ArcFace batched inference
- ⬜ `src/inference/embedding_index.cpp` — matmul-based nearest neighbor (already stubbed)
- ⬜ TTA variant generation (CPU-side flip/rotate, GPU-side inference)
- ⬜ `tools/enroll_face.cpp` — working CLI enrollment
- ⬜ Load embeddings from DB on startup

**Exit criteria**: known faces print `[person_id, score]`; unknowns print `unknown`.

## Milestone 3 — Tracking + Cooldown ⬜

- ⬜ `src/tracking/kalman_filter.cpp` — constant-velocity state model
- ⬜ `src/tracking/face_tracker.cpp` — SORT-style association + track lifecycle
- ⬜ Per-track cooldown in main detection loop

**Exit criteria**: one event per person per visit (not per frame).

## Milestone 4 — Database Persistence ⬜

- ⬜ `src/database/db_connection.cpp` — already stubbed, flesh out transactions
- ⬜ `src/database/event_repository.cpp` — INSERT, SELECT, DELETE with prepared statements
- ⬜ `src/database/person_repository.cpp` — load embeddings, add learned, enroll

**Exit criteria**: events persist to PostgreSQL, survive restart.

## Milestone 5 — Schedule Checker ⬜

- ⬜ `src/schedule/schedule_checker.cpp` — DB query + in-memory cache with TTL
- ⬜ Integration with event classification (authorized / wrong_time / wrong_day)

**Exit criteria**: students identified outside their exam time are flagged.

## Milestone 6 — Alert Service ⬜

- ⬜ `src/alerts/alert_service.cpp` — thread pool, already partially stubbed
- ⬜ `src/alerts/telegram_alerter.cpp` — libcurl multipart POST
- ⬜ `src/alerts/email_alerter.cpp` — libcurl SMTP with STARTTLS
- ⬜ Retry + backoff

**Exit criteria**: unknown detection → Telegram message arrives in < 5 s.

## Milestone 7 — Qt UI (Live view + Events) ⬜

- ⬜ `ui/src/app_window.cpp` — Qt bootstrap, signal/slot wiring to EventBus
- ⬜ `ui/src/models/event_model.cpp` — QAbstractListModel with rolling buffer
- ⬜ `ui/src/controllers/video_provider.cpp` — NvBufSurface → QImage
- ⬜ `ui/qml/LiveView.qml` — functional live feed + event list

**Exit criteria**: dashboard shows live 1080p video + real-time events.

## Milestone 8 — Qt UI (People, Analytics, Settings) ⬜

- ⬜ `ui/src/models/people_model.cpp` — paginated list from DB
- ⬜ `ui/qml/PeoplePage.qml` — with search + type filter
- ⬜ `ui/qml/AnalyticsPage.qml` — QtCharts for daily/hourly aggregates
- ⬜ Settings page: threshold, cooldown, alert config

**Exit criteria**: feature parity with the React dashboard's main pages.

## Milestone 9 — Progressive Learning + Multi-camera ⬜

- ⬜ Progressive learning pipeline (auto-enroll from high-confidence matches)
- ⬜ Second CameraPipeline instance in main — verify no contention
- ⬜ Per-camera statistics

## Milestone 10 — Production Readiness ⬜

- ⬜ HTTP API (optional, for integrations) — `src/api/http_server.cpp`
- ⬜ systemd service + auto-restart
- ⬜ Prometheus metrics endpoint
- ⬜ Log rotation
- ⬜ DB migration tooling
- ⬜ Comprehensive integration tests

## Time Estimate

| Milestone | Est. Effort | Cumulative |
|-----------|-------------|------------|
| M1        | 2 weeks     | 2 weeks    |
| M2        | 2 weeks     | 4 weeks    |
| M3        | 1 week      | 5 weeks    |
| M4        | 1 week      | 6 weeks    |
| M5        | 3 days      | 6.5 weeks  |
| M6        | 1 week      | 7.5 weeks  |
| M7        | 2 weeks     | 9.5 weeks  |
| M8        | 2 weeks     | 11.5 weeks |
| M9        | 1 week      | 12.5 weeks |
| M10       | 1 week      | 13.5 weeks |

Realistic total: **~3-4 months full-time for one experienced C++ developer**, or
6-9 months part-time. The Python prototype can stay in production throughout.
