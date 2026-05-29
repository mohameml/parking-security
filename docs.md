# Parking Security System — Full System Overview

> **Audience:** engineers and operators who need to understand the whole system end-to-end.
> **Scope:** the native C++ edge daemon **and** the Dockerized web dashboard, every layer,
> the complete feature list, known risks, and enhancement opportunities.
>
> For build/deploy mechanics see [BUILDING.md](BUILDING.md), [DEPLOYMENT.md](DEPLOYMENT.md),
> [JETSON_SETUP.md](JETSON_SETUP.md). For the original module-boundary notes see
> [ARCHITECTURE.md](ARCHITECTURE.md).

---

## 1. Executive Summary

Real-time **face-recognition access control** for parking / exam-hall security, running on
an **NVIDIA Jetson Orin Nano**. A person steps in front of a camera; the system detects the
face, recognizes it against an enrolled database, decides whether they're **authorized**, and
records an event with a face snapshot — surfaced live on a web dashboard with alerts.

The system has **two halves**:

1. **Native C++ edge daemon** (`parking-security`) — the real-time vision pipeline. Captures
   the camera, runs face detection + recognition on the GPU (DeepStream + TensorRT), tracks
   faces, classifies events (authorized / wrong-time / wrong-day / unknown), serves a live
   MJPEG stream, and POSTs events to the dashboard backend. Runs as a **systemd service**.
2. **Dockerized web dashboard** — a FastAPI backend + React frontend + a CPU InsightFace
   embedding service + PostgreSQL. Handles enrollment, the operator UI, live event feed,
   analytics, user management, and alerts.

### Actual project status (important)

The older `README.md`/`ROADMAP.md` described this as a "scaffolding phase" with modules "not
started." **That is out of date.** The C++ daemon is implemented and runs in production
(`src/camera/camera_pipeline.cpp` alone is ~790 lines; the daemon loads thousands of
embeddings, runs both TensorRT engines, drives the DeepStream pipeline, serves MJPEG on
:8090, and POSTs live events). The React/FastAPI dashboard is fully functional. The genuinely
incomplete pieces are narrow:

- C++ `telegram_alerter.cpp` / `email_alerter.cpp` are **stubs that return `false`** — but
  alerts work through the **dashboard backend** instead (Telegram/email/Firebase).
- `src/api/` (in-daemon HTTP API) is **empty** — not needed; the dashboard backend is the API.
- `src/camera/gstreamer_helpers.cpp` is a near-empty placeholder (the real pipeline is built
  inline in `camera_pipeline.cpp`).
- The **Qt/QML UI** under `ui/` is a skeleton; the **React dashboard is the production UI**.

---

## 2. High-Level Architecture

```
                          NVIDIA Jetson Orin Nano
┌───────────────────────────────────────────────────────────────────────────┐
│  NATIVE C++ DAEMON  (systemd: parking-security, /opt/parking-security)      │
│                                                                             │
│  USB/CSI/RTSP cam ──► GStreamer ──► nvstreammux ──► nvinfer(SCRFD detect)   │
│   /dev/video0          (nvv4l2decoder)                    │                 │
│                                                           ▼                 │
│                                              nvinfer(ArcFace recognize)     │
│                                                           │ 512-d embedding │
│                                                           ▼                 │
│                                         pad probe: IoU tracker +            │
│                                         EmbeddingIndex cosine match +       │
│                                         ScheduleChecker (students)          │
│                                                           │ RecognitionResult│
│                            ┌──────────────────────────────┤                 │
│                            ▼                               ▼                 │
│                    nvdsosd + jpegenc              EventPublisher            │
│                            │                      (aggregate 1.5s,          │
│                            ▼                       cooldown 10s, dedup)      │
│                    MjpegServer :8090                      │ HTTP POST        │
└────────────────────────────┼─────────────────────────────┼─────────────────┘
                             │ /stream                      │ /api/events
                             │                              │ (X-Api-Key)
        ┌────────────────────┼──────────────────────────────┼──────────────────┐
        │  DOCKER STACK (parking-cpp)                        ▼                  │
        │                                          ┌──────────────────┐         │
        │  nginx :3001 ──/camera/──► daemon:8090   │ FastAPI backend  │ :8001   │
        │     │   ──/api/, /ws──────────────────►  │  - REST + WS     │         │
        │     ▼                                    │  - persist event │         │
        │  React SPA  ◄────── WebSocket /ws ───────│  - broadcast WS  │         │
        │  (browser)         {type:detection}      │  - fire alerts   │         │
        │                                          └───────┬──────────┘         │
        │  Embedding service :8091 (InsightFace) ◄─────────┤ enroll embeddings  │
        │                                                  ▼                    │
        │                                          PostgreSQL :5432             │
        │                                          (parking-security_pgdata)    │
        └───────────────────────────────────────────────────────────────────────┘
```

**Two independent live channels reach the browser:** the **MJPEG video** (daemon → nginx
`/camera/` → `<img>`) and the **event stream** (daemon → backend → WebSocket → React). They
are separate — video can be live while the event WebSocket reconnects, and vice-versa.

---

## 3. Component & Port Map

| Component | Tech | Runs as | Port (host) | Purpose |
|-----------|------|---------|-------------|---------|
| **C++ daemon** | C++20 · DeepStream · TensorRT · GStreamer | systemd `parking-security` | **8090** (MJPEG) | Camera capture, detection, recognition, events |
| **PostgreSQL** | postgres:16-alpine | docker `parking-cpp-postgres` | **5432** | All persistent data (volume `parking-security_pgdata`) |
| **Embedding service** | FastAPI · InsightFace buffalo_l (CPU) | docker `parking-cpp-embedding` | **8091** → 8080 | Extract face embeddings for enrollment (TTA) |
| **Backend** | FastAPI · SQLAlchemy | docker `parking-cpp-backend` | **8001** → 8000 | REST API + WebSocket + alerts |
| **Frontend** | React · Vite · nginx | docker `parking-cpp-frontend` | **3001** → 80 | Operator dashboard (SPA + reverse proxy) |

Verified live state at time of writing: all four containers healthy; daemon `active`;
`GET :8090/stream` → `200`; DB holds ~3,355 events, 3 employees (19 embeddings),
583 students (2,940 embeddings).

---

## 4. Layer-by-Layer Deep Dive

### 4.1 C++ Daemon

Entry point: `src/main.cpp`. Startup order: load `Config` → init `logger` → install
SIGINT/SIGTERM handlers → (optional) DB stack (`ConnectionPool` → `EventRepository` →
`ScheduleChecker` → `EventPublisher`) → load `EmbeddingIndex` from `data/embeddings.json` →
build & start `CameraPipeline` (wired with the index, recognition callback → `EventPublisher`,
and an error callback that exits the process so systemd restarts cleanly). The main loop polls
every 250 ms; if the pipeline dies or frames starve for 60 s, it exits with a non-zero code
(systemd `Restart=on-failure`). **Design choice:** never restart the GStreamer pipeline
in-process — a clean process restart avoids half-released V4L2/CUDA state.

| Layer | Key files | Responsibility | In → Out | Deps |
|-------|-----------|----------------|----------|------|
| **core** | `src/core/{config,logger,event_bus}.cpp`, `include/parking/core/types.hpp` | Config (INI + `PARKING_*` env overrides), spdlog logging, in-proc pub/sub, domain types (`Detection`, `RecognitionResult`, `EventType`) | INI/env → typed config; events → subscribers | spdlog |
| **camera** | `src/camera/camera_pipeline.cpp` (~790 lines), `mjpeg_server.cpp`, `frame_buffer.cpp` | Build & run the GStreamer/DeepStream pipeline; pad-probe that turns metadata into recognition results; serve MJPEG | camera frames → recognition callbacks + JPEG stream | GStreamer, DeepStream, OpenCV |
| **inference** | `src/inference/{scrfd_decoder,nvdsparsebbox_scrfd,embedding_index}.cpp` | SCRFD bbox decode (custom DeepStream parser `libnvds_parsebbox_scrfd.so`); ArcFace 512-d embedding; **in-memory cosine-similarity index** (L2-normalized, shared-mutex, `query`/`query_top_k`/`load_from_json`) | tensors → detections; embedding → person match | TensorRT, nlohmann/json |
| **tracking** | `src/tracking/face_tracker.cpp` | SORT-style **IoU** tracker (no Kalman): `iou_threshold≈0.3`, `max_age≈30`, `min_hits≈2` → persistent `track_id` per face | detections → tracks | — |
| **schedule** | `src/schedule/{schedule_checker,schedule_cache}.cpp` | For students, query `exam_schedules`/`student_exams`; decide Authorized / WrongTime / WrongDay; 10-min TTL cache | person_id → authorization verdict | libpqxx |
| **database** | `src/database/{db_connection,event_repository,event_publisher}.cpp` | libpqxx pool; INSERT/SELECT/clear events; **EventPublisher** aggregates recognitions (1.5 s window), per-person **cooldown** (~10 s), **unknown dedup** by embedding cosine ≥ 0.65, captures frame JPEG, then POSTs to backend or inserts directly | recognition results → durable events | libpqxx, libcurl, OpenCV |
| **alerts** | `src/alerts/{alert_service,telegram_alerter,email_alerter}.cpp` | Dispatcher fans out to alerters asynchronously. **telegram/email are stubs (`return false`)** — alerting is handled by the backend instead | event → (would-be) notification | libcurl (intended) |

**Recognition hot path (per frame):** frame → SCRFD detect → ArcFace embed → pad probe walks
`NvDsBatchMeta`/`NvDsObjectMeta`, reads the 512-d embedding, runs the IoU tracker, queries
`EmbeddingIndex`; if top-1 cosine ≥ `similarity_threshold` → known person, else Unknown; for
students the `ScheduleChecker` refines to WrongTime/WrongDay; the OSD label is rewritten; a
`RecognitionResult` fires to `EventPublisher`.

### 4.2 Model / nvinfer Configs

- `config/face_detection.txt` — SCRFD 10G (`models/det_10g.engine`), **FP16**
  (`network-mode=2`), primary mode (`process-mode=1`), 640×640 letterboxed, RGB,
  custom parser `NvDsInferParseCustomSCRFD` from `lib/libnvds_parsebbox_scrfd.so`.
- `config/face_recognition.txt` — ArcFace buffalo_l (`models/w600k_r50.engine`), **FP16**,
  **secondary mode** (`process-mode=2`, `operate-on-gie-id=1`), 112×112, raw tensor output
  (`output-tensor-meta=1`) → 512-d embedding attached as user meta.
- Engines are **generated at build/deploy time** by `scripts/convert_models.sh` (ONNX →
  TensorRT via `trtexec`); they are **not** checked in and are GPU-arch + TensorRT-version
  specific.

### 4.3 Dashboard Backend (FastAPI) — `dashboard/backend/app/`

REST routes (grouped by router in `app/routers/`):

- **auth** — `POST /api/auth/login`, `POST /api/auth/refresh`. JWT HS256, access 60 min /
  refresh 7 days; **brute-force lockout** (5 fails → 15 min); audit-logged.
- **events** — `POST /api/events` (ingest from daemon, requires `X-Api-Key`; saves snapshot,
  inserts row, **broadcasts over WebSocket**, fires alerts for unknowns), `GET /api/events`
  (filter by type/camera/hours/limit), `DELETE /api/events/clear` (admin).
- **employees** — `GET`/`POST`/`PUT`/`DELETE /api/employees` (photo upload → embedding
  service → augmented embeddings), `GET /api/employees/embeddings` (for the daemon).
- **schedule** — `POST /api/schedule/import` (Excel → exams + assignments),
  `POST /api/schedule/import-photos` (batch student enrollment),
  `GET /api/schedule/check/{student_id}`, `GET /api/schedule/today`,
  `GET /api/schedule/students`, `GET /api/schedule/students/embeddings`,
  `POST /api/schedule/learn-embedding` (progressive learning, capped at 20/person).
- **analytics** — `GET /api/analytics/{summary,daily,hourly,top-employees}`.
- **users** — `GET`/`POST`/`PUT /api/users` (admin only, RBAC: admin|guard).
- **audit** — `GET /api/audit` (admin only).
- **ws** — `WebSocket /ws?token=<jwt>` — auth via token query param; broadcasts
  `{type:"detection", event}` and `{type:"frame", frame}`; `ping`→`pong` keepalive.
- **main** — `GET /api/health`, `POST /api/camera/frame`.

Data model (`app/models.py`): `User`, `Employee` + `FaceEmbedding`, `Student` +
`StudentEmbedding` + `StudentExam`, `ExamSchedule`, `Camera`, `Event`, `AuditLog`. Services:
`websocket_manager.ConnectionManager` (broadcast pool), `alert_service` (Telegram + email +
Firebase, each silently skipped if unconfigured). Core: `security.py` (JWT, bcrypt-12),
`deps.py` (`get_current_user`, `require_admin`, `verify_camera_api_key`).

### 4.4 Embedding Service — `dashboard/embedding-service/`

CPU InsightFace **buffalo_l** (the same model family the daemon runs on GPU, so enrolled
vectors match what the daemon matches against). Endpoints: `POST /extract-embedding`
(first face), `POST /extract-embeddings-augmented` (TTA: flips/rotations/brightness → 6+
embeddings from one photo, more robust enrollment), `GET /health`.

### 4.5 Dashboard Frontend (React + Vite) — `dashboard/frontend/src/`

Pages: **Login**, **Dashboard** (live MJPEG via `/camera/stream` + self-healing watchdog,
real-time event feed, stat cards), **Events** (time/type filters, expandable rows, lightbox),
**Employees/People** (employee CRUD + drag-drop photo enrollment; paginated read-only student
list), **Analytics** (Recharts: daily/hourly/top-employees), **Users** (admin RBAC).
Hooks: `useWebSocket` (JWT-authed WS, auto-reconnect), `useEventFeed` (rolling 100 events),
`useAlerts` (Web-Audio alarms + browser notifications). State: `authStore` (Zustand +
localStorage). `lib/api.js` axios with **auto-refresh on 401**. `nginx.conf` proxies `/api`,
`/ws`, `/camera/` (→ host daemon `:8090`, **unbuffered**), `/snapshots/`, `/student_photos/`.
**PWA** with a Workbox service worker (`registerType: autoUpdate`).

---

## 5. End-to-End Data Flows

**(a) Unknown person →** daemon detects an unrecognized face → aggregates 1.5 s, dedups by
embedding → `POST /api/events` (`event_type=unknown`) → backend saves snapshot, inserts row,
**broadcasts WS** → React plays an urgent beep, shows a browser notification + toast + red
screen flash, adds to the feed → backend also fires Telegram/email/Firebase (if configured).

**(b) Employee enrollment →** admin adds an employee with a photo → backend → embedding
service `/extract-embeddings-augmented` → stores `Employee` + N `FaceEmbedding` rows. The
daemon's matching set is refreshed via `data/embeddings.json` (see §7 caveat).

**(c) Student exam authorization →** daemon recognizes a student → `ScheduleChecker` (in the
daemon) or `GET /api/schedule/check/{id}` decides: has an exam today and within the time slot
→ **authorized**; exam today but outside the window → **wrong_time**; no exam today →
**wrong_day**.

**(d) Live video →** daemon `jpegenc` → `MjpegServer :8090/stream`
(`multipart/x-mixed-replace`) → nginx `/camera/` (same-origin, `proxy_buffering off`) →
browser `<img>` with a 6 s watchdog that reconnects on stall.

---

## 6. Configuration & Deployment

**Daemon config** `config/parking.ini` sections: `[camera]` (source_uri, resolution, fps),
`[models]` (engine/config paths, thresholds), `[recognition]` (`similarity_threshold`,
cooldown, tracker/recognizer toggles, `embedding_index_path`), `[stream]` (MJPEG port 8090,
jpeg_quality), `[database]` (`connection_string`; password injected via env), `[backend]`
(`url`, `api_key` from `PARKING_CAMERA_API_KEY`), `[alerts]`, `[api]` (disabled), `[logging]`.
Any value is overridable by a `PARKING_*` env var (e.g. `PARKING_DB_URL`).

**Daemon deployment:** installed to `/opt/parking-security/{bin,config,models,lib,data}` by
`scripts/install_daemon.sh`; runs as systemd `parking-security.service`
(`Restart=on-failure`, env from `/etc/parking-security/parking-security.env`, LD paths for
CUDA + DeepStream). A `parking-snapshots-cleanup.timer` purges filesystem snapshots > 30 days
daily. Logs: `journalctl -u parking-security` + `/var/log/parking-security/parking.log`
(logrotate).

**Dashboard deployment** `dashboard/docker-compose.yml` (project `parking-cpp`) — now
**self-contained**: it runs its own `postgres` (reusing the external volume
`parking-security_pgdata`), plus `embedding`, `backend`, `frontend`. The frontend has
`extra_hosts: host.docker.internal:host-gateway` so nginx can proxy the host daemon's stream.
Bring up: `cd dashboard && docker compose up -d --build`.

---

## 7. Complete Functionality List

**Vision / edge (daemon):** USB/CSI/RTSP capture · GPU SCRFD detection (FP16) · ArcFace 512-d
embeddings · cosine-similarity recognition · IoU face tracking · per-person event cooldown ·
unknown-face dedup · schedule-based classification (authorized/wrong_time/wrong_day/unknown) ·
on-screen overlay (bbox + name) · live MJPEG stream · resilient auto-restart.

**Dashboard / operator:** JWT login with lockout · live camera feed (self-healing) ·
real-time event feed with type filters · event history with time filters, expandable details,
and image lightbox · clear-events · employee CRUD with drag-drop photo + TTA enrollment ·
student list (search, pagination, next-exams) · Excel exam-schedule import · batch student
photo import · analytics (summary KPIs, daily, hourly, top-employees) · user management with
RBAC (admin/guard) · audit log with IP capture · multi-channel alerts (Telegram, email,
Firebase) · in-app toasts + Web-Audio alarms + browser notifications + mute · progressive
learning (auto-enroll high-confidence captures) · installable PWA with offline support.

---

## 8. Future Problems / Risks

1. **Shipped placeholder secrets.** `docker-compose.yml`, `config/parking-security.env.example`
   and defaults contain `changeme_parking_2025` (DB), `admin123` (seed admin),
   `jetson_cam_key_2025` (camera API key), and a JWT `changeme_super_secret...` key. These
   **must be rotated** before any real deployment — anyone who reads the repo has them.
2. **Camera is a hard single point of failure.** The pipeline binds `/dev/video0`; if the USB
   camera is unplugged the device disappears and the daemon **crash-loops** (observed in
   practice). No fallback source or "degraded" mode.
3. **Single-camera / single-Jetson ceiling.** `nvstreammux batch-size=1`; multi-camera is
   documented as a goal but not wired. Horizontal scale means more Jetsons.
4. **Static embedding snapshot.** The daemon matches against `data/embeddings.json`, an
   exported snapshot. New enrollments in the dashboard do **not** reach the daemon until the
   file is re-exported and the daemon reloads/restarts — easy to forget, causes "why isn't
   the new person recognized?" confusion.
5. **Engine portability.** TensorRT engines are GPU-arch + TensorRT-version specific; a
   JetPack upgrade or different Jetson requires regenerating them. A `batched-push-timeout`
   workaround exists for a DeepStream 7.1 CUDA bug — watch on version bumps.
6. **DB image bloat.** Event face crops are stored as base64 in Postgres (`events.face_image`)
   and full frames on disk. The cleanup timer only prunes the **filesystem** dir, not DB rows,
   so the `events` table grows unbounded (already ~3,355 rows).
7. **WebSocket token staleness.** `useWebSocket` connects with the cached access token and only
   reconnects on close without refreshing it; after the 60-min token expiry it can stick at
   "Reconnecting" until the user re-logs.
8. **Hardening gaps.** The systemd unit currently runs as user `una` (not a dedicated
   `parking` user). The stream proxy depends on `host.docker.internal`. No HTTPS by default.
9. **Thin test coverage.** Only 3 unit tests (`event_bus`, `embedding_index`, `config`); no
   integration tests for the pipeline or the dashboard.

---

## 9. Enhancement Opportunities

1. **ANN index** — replace the O(N) in-memory cosine scan with FAISS-GPU / HNSW for O(log N)
   matching at 10k+ persons (already foreshadowed in `ARCHITECTURE.md`).
2. **Live embedding sync** — have the daemon pull `/api/employees/embeddings` +
   `/api/schedule/students/embeddings` periodically (or on a backend push) instead of the
   static JSON export, so enrollment is instant.
3. **Finish or formally retire the C++ alerters** — either implement libcurl Telegram/email in
   `telegram_alerter.cpp`/`email_alerter.cpp`, or delete them and document that alerting is a
   backend responsibility.
4. **Multi-camera** — `nvstreammux batch-size>1` over a shared TensorRT engine; per-camera
   stats; second `CameraPipeline`.
5. **Secrets management** — env/Docker secrets, a first-run forced admin-password change, and a
   generated JWT key; harden the systemd unit to a dedicated non-root user.
6. **WebSocket token refresh** — refresh the access token before reconnecting (the PWA
   stale-bundle issue was already fixed by moving the SW to `autoUpdate`).
7. **Observability** — implement the optional in-daemon HTTP API (`src/api/`), add a Prometheus
   metrics endpoint and structured health checks.
8. **Move images out of Postgres** — store face crops on the filesystem / object storage and
   keep only references in the DB; extend the cleanup job to prune DB rows.
9. **Add a DB viewer** (e.g. Adminer container) and DB migration tooling.

---

## 10. Known Gaps vs. the Old Docs

The `ROADMAP.md` milestone markers (M1–M9 "not started") and the `README.md` "scaffolding
phase" wording predate the actual implementation and have been corrected. Reality:

- **Implemented & running:** camera pipeline, SCRFD detection, ArcFace recognition, embedding
  index, IoU tracking, event cooldown/dedup, DB persistence, schedule checking, systemd
  service + auto-restart, log rotation, snapshot cleanup, and the full React/FastAPI dashboard.
- **Built differently than the roadmap named:** recognition uses **DeepStream nvinfer** +
  a custom SCRFD parser (`nvdsparsebbox_scrfd.cpp`) rather than hand-rolled
  `tensorrt_engine.cpp`/`face_detector.cpp`; tracking is **IoU-only** (no `kalman_filter.cpp`).
- **Still open:** C++ `telegram_alerter`/`email_alerter` (stubs — alerts via backend), the
  in-daemon HTTP API (`src/api/`, empty), `gstreamer_helpers.cpp` (placeholder), Prometheus
  metrics, integration tests, and the Qt/QML UI (skeleton — React dashboard is the production UI).
