# C++ stack dashboard

Copy of the Python frontend + backend, moved here so the C++ stack has its
own versioned dashboard and the original `parking-security/` repo stays
untouched.

## Layout

- `backend/` — FastAPI app; REST API + WebSocket that the C++ daemon POSTs to.
- `frontend/` — React app; serves `/` and proxies `/api` + `/ws` to backend.
- `docker-compose.yml` — parallel stack, ports 8001 (backend) + 3001 (frontend).

## Running

```bash
cd ~/ParkingSecurity-CPP/dashboard
docker compose up -d --build     # first-time: builds both images
# OR, after a pull:
docker compose pull && docker compose up -d
```

Open the new dashboard at:

    http://<jetson-ip>:3001/

## Shared with the old stack

- **Postgres** — the same container `parking-security-postgres-1` (the new
  backend talks to it via `host.docker.internal:5432`). Both stacks see the
  same `events`, `employees`, `students`, `exam_schedules`, and embeddings.
- **InsightFace models** — at `~/.insightface/models/`; both stacks read
  them (the C++ daemon + this backend).

## Not shared

- Docker containers are different (`parking-cpp-*` vs `parking-security-*`).
- Ports: `8001`/`3001` vs `8000`/`3000`.
- Volumes under `dashboard/snapshots`, `dashboard/images` — separate from
  the old stack's `parking-security/snapshots`, etc.

## Teardown

```bash
docker compose down              # stop containers, keep images
docker compose down --volumes    # stop + drop per-stack volumes too
```
