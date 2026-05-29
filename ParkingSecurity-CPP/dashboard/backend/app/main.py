import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

from app.core.init_db import init_db
from app.routers import analytics, audit, auth, employees, events, schedule, users, ws

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(name)s: %(message)s")


@asynccontextmanager
async def lifespan(app: FastAPI):
    await init_db()
    logging.getLogger(__name__).info("Database initialized, admin seeded")
    yield


app = FastAPI(
    title="Parking Security System",
    version="1.0.0",
    lifespan=lifespan,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Static files for snapshots
import os
SNAPSHOTS_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), "snapshots")
if os.path.isabs("/app/snapshots") and os.path.exists("/app"):
    SNAPSHOTS_DIR = "/app/snapshots"
os.makedirs(SNAPSHOTS_DIR, exist_ok=True)
try:
    app.mount("/snapshots", StaticFiles(directory=SNAPSHOTS_DIR), name="snapshots")
except Exception:
    pass

# Static files for student photos
STUDENT_PHOTOS_DIR = "/app/student_photos"
if not os.path.exists(STUDENT_PHOTOS_DIR):
    STUDENT_PHOTOS_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), "images")
if os.path.exists(STUDENT_PHOTOS_DIR):
    try:
        app.mount("/student_photos", StaticFiles(directory=STUDENT_PHOTOS_DIR), name="student_photos")
    except Exception:
        pass

# Routers
app.include_router(auth.router)
app.include_router(employees.router)
app.include_router(events.router)
app.include_router(analytics.router)
app.include_router(users.router)
app.include_router(audit.router)
app.include_router(schedule.router)
app.include_router(ws.router)


@app.get("/api/health")
async def health():
    return {"status": "ok", "service": "parking-security-backend"}


from fastapi import Depends, Header
from pydantic import BaseModel
from app.core.deps import verify_camera_api_key
from app.services.websocket_manager import ws_manager


class FramePayload(BaseModel):
    camera_id: str
    frame: str  # base64


@app.post("/api/camera/frame")
async def camera_frame(body: FramePayload, _: bool = Depends(verify_camera_api_key)):
    await ws_manager.broadcast({
        "type": "frame",
        "camera_id": body.camera_id,
        "frame": body.frame,
    })
    return {"status": "ok"}
