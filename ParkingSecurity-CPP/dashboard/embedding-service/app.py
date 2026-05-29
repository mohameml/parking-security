"""HTTP embedding service for the dashboard backend.

Exposes the same JSON contracts the old Python camera service did, so the
existing employees.py / students.py routes in dashboard/backend keep working
unchanged — they just need CAMERA_EMBEDDING_URL pointed at this container.

Endpoints:
  POST /extract-embedding          multipart 'file' → {"embedding":[...], "faces_found":N}
  POST /extract-embeddings-augmented multipart 'file' → {"embeddings":[[...]], "count":N}
  GET  /health                     → {"status":"ok"}
"""

import logging

import cv2
import numpy as np
from fastapi import FastAPI, File, Request, UploadFile
from fastapi.responses import JSONResponse

from face_engine import FaceEngine

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("embedding-service")

app = FastAPI(title="Parking-Security Embedding Service")
engine: FaceEngine | None = None


@app.on_event("startup")
def _startup():
    global engine
    log.info("loading buffalo_l model (this can take ~30s on first start)...")
    engine = FaceEngine()
    log.info("model loaded, ready")


def _decode(raw: bytes):
    if not raw:
        return None
    arr = np.frombuffer(raw, dtype=np.uint8)
    return cv2.imdecode(arr, cv2.IMREAD_COLOR)


async def _read_image(request: Request, file: UploadFile | None):
    """Accept both raw-body (Content-Type: application/octet-stream, what the
    dashboard backend sends) and multipart/form-data 'file' (for curl -F and
    other tools)."""
    if file is not None:
        return _decode(await file.read())
    return _decode(await request.body())


@app.post("/extract-embedding")
async def extract_embedding(request: Request, file: UploadFile | None = File(None)):
    img = await _read_image(request, file)
    if img is None:
        return JSONResponse(
            {"embedding": None, "faces_found": 0, "error": "invalid image"},
            status_code=400,
        )
    emb, n = engine.extract_embedding(img)
    if emb is None:
        return {"embedding": None, "faces_found": 0, "error": "no face detected"}
    return {"embedding": emb, "faces_found": n}


@app.post("/extract-embeddings-augmented")
async def extract_augmented(request: Request, file: UploadFile | None = File(None)):
    img = await _read_image(request, file)
    if img is None:
        return JSONResponse(
            {"embeddings": [], "count": 0, "error": "invalid image"},
            status_code=400,
        )
    embs, n = engine.extract_augmented(img)
    if n == 0:
        return {"embeddings": [], "count": 0, "error": "no face detected"}
    return {"embeddings": embs, "count": n}


@app.get("/health")
async def health():
    return {"status": "ok", "model_loaded": engine is not None}
