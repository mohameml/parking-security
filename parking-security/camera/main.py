import asyncio
import base64
import json
import logging
import os
import time

import aiohttp
import cv2
import numpy as np

from face_engine import FaceEngine
from stream_server import frame_buffer, MJPEGStreamHandler, StreamHandler

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(name)s: %(message)s")
logger = logging.getLogger("camera-service")

# Configuration
CAMERA_SOURCE = os.getenv("CAMERA_SOURCE", "0")
CAMERA_ID = os.getenv("CAMERA_ID", "cam-entrance-01")
CAMERA_API_KEY = os.getenv("CAMERA_API_KEY", "changeme_camera_api_key_2025")
BACKEND_API_URL = os.getenv("BACKEND_API_URL", "http://backend:8000")
SIMILARITY_THRESHOLD = float(os.getenv("SIMILARITY_THRESHOLD", "0.55"))
FRAME_SKIP = int(os.getenv("FRAME_SKIP", "2"))
EMBEDDING_REFRESH_INTERVAL = int(os.getenv("EMBEDDING_REFRESH_INTERVAL", "60"))
STREAM_PORT = int(os.getenv("STREAM_PORT", "8080"))
DETECTION_COOLDOWN = float(os.getenv("DETECTION_COOLDOWN", "10"))  # seconds between events for same face
MIN_FACE_SIZE = int(os.getenv("MIN_FACE_SIZE", "80"))            # minimum face width/height in pixels
BLUR_THRESHOLD = float(os.getenv("BLUR_THRESHOLD", "30.0"))       # Laplacian variance below this = too blurry
CONFIRM_FRAMES = int(os.getenv("CONFIRM_FRAMES", "3"))            # consecutive unknown frames before alerting


def encode_image_b64(image: np.ndarray, quality: int = 80) -> str:
    _, buffer = cv2.imencode(".jpg", image, [cv2.IMWRITE_JPEG_QUALITY, quality])
    return base64.b64encode(buffer).decode("utf-8")


face_engine_instance = None  # Set after FaceEngine is created


# ─── Drawing Functions ──────────────────────────────────────────────

def draw_rounded_rect(img, pt1, pt2, color, thickness, radius=8):
    """Draw a rectangle with rounded corners."""
    x1, y1 = pt1
    x2, y2 = pt2
    r = radius

    # Draw straight edges
    cv2.line(img, (x1 + r, y1), (x2 - r, y1), color, thickness)
    cv2.line(img, (x1 + r, y2), (x2 - r, y2), color, thickness)
    cv2.line(img, (x1, y1 + r), (x1, y2 - r), color, thickness)
    cv2.line(img, (x2, y1 + r), (x2, y2 - r), color, thickness)

    # Draw corners
    cv2.ellipse(img, (x1 + r, y1 + r), (r, r), 180, 0, 90, color, thickness)
    cv2.ellipse(img, (x2 - r, y1 + r), (r, r), 270, 0, 90, color, thickness)
    cv2.ellipse(img, (x1 + r, y2 - r), (r, r), 90, 0, 90, color, thickness)
    cv2.ellipse(img, (x2 - r, y2 - r), (r, r), 0, 0, 90, color, thickness)


def draw_detections(frame: np.ndarray, detections: list[dict]) -> np.ndarray:
    """Draw bounding boxes with labels on the frame."""
    annotated = frame.copy()
    h, w = annotated.shape[:2]

    for det in detections:
        bbox = det["bbox"]
        x1, y1, x2, y2 = [max(0, v) for v in bbox]
        x2, y2 = min(w, x2), min(h, y2)
        etype = det["event_type"]

        # Colors: green=authorized, orange=wrong_time/wrong_day, red=unknown
        if etype == "authorized" or etype == "known":
            color = (0, 220, 80)
            border_color = (0, 180, 60)
        elif etype in ("wrong_time", "wrong_day"):
            color = (0, 165, 255)  # orange in BGR
            border_color = (0, 130, 220)
        else:
            color = (60, 60, 255)  # red
            border_color = (40, 40, 200)

        # Outer glow effect
        cv2.rectangle(annotated, (x1 - 1, y1 - 1), (x2 + 1, y2 + 1), border_color, 3)
        # Main box with rounded corners
        draw_rounded_rect(annotated, (x1, y1), (x2, y2), color, 2, radius=6)

        # Corner accents (thicker corners for a modern look)
        corner_len = min(20, (x2 - x1) // 4, (y2 - y1) // 4)
        cv2.line(annotated, (x1, y1), (x1 + corner_len, y1), color, 3)
        cv2.line(annotated, (x1, y1), (x1, y1 + corner_len), color, 3)
        cv2.line(annotated, (x2, y1), (x2 - corner_len, y1), color, 3)
        cv2.line(annotated, (x2, y1), (x2, y1 + corner_len), color, 3)
        cv2.line(annotated, (x1, y2), (x1 + corner_len, y2), color, 3)
        cv2.line(annotated, (x1, y2), (x1, y2 - corner_len), color, 3)
        cv2.line(annotated, (x2, y2), (x2 - corner_len, y2), color, 3)
        cv2.line(annotated, (x2, y2), (x2, y2 - corner_len), color, 3)

        # Label
        label = det.get("employee_name") or "UNKNOWN"
        conf = det.get("confidence", 0)
        text = f"{label} {conf*100:.0f}%"
        font = cv2.FONT_HERSHEY_SIMPLEX
        font_scale = 0.55
        (tw, th), baseline = cv2.getTextSize(text, font, font_scale, 1)

        # Label background with alpha blending
        label_y = max(y1 - th - 12, 0)
        overlay = annotated.copy()
        cv2.rectangle(overlay, (x1, label_y), (x1 + tw + 10, label_y + th + 10), color, -1)
        cv2.addWeighted(overlay, 0.85, annotated, 0.15, 0, annotated)
        cv2.putText(annotated, text, (x1 + 5, label_y + th + 4), font, font_scale, (255, 255, 255), 1, cv2.LINE_AA)

        # Status dot
        dot_x = x1 + tw + 16
        dot_y = label_y + th // 2 + 5
        if dot_x < w - 5:
            cv2.circle(annotated, (dot_x, dot_y), 4, (255, 255, 255), -1)

    # HUD overlay - timestamp + camera ID
    overlay = annotated.copy()
    cv2.rectangle(overlay, (0, h - 35), (280, h), (0, 0, 0), -1)
    cv2.rectangle(overlay, (0, 0), (220, 30), (0, 0, 0), -1)
    cv2.addWeighted(overlay, 0.5, annotated, 0.5, 0, annotated)

    ts = time.strftime("%Y-%m-%d %H:%M:%S")
    cv2.putText(annotated, ts, (8, h - 12), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (200, 200, 200), 1, cv2.LINE_AA)
    cv2.putText(annotated, f"CAM: {CAMERA_ID}", (8, 20), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (200, 200, 200), 1, cv2.LINE_AA)

    # Detection counter
    n_faces = len(detections)
    if n_faces > 0:
        count_text = f"{n_faces} face{'s' if n_faces > 1 else ''} detected"
        cv2.putText(annotated, count_text, (w - 200, 20), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 220, 80), 1, cv2.LINE_AA)

    return annotated


def open_camera(source: str) -> cv2.VideoCapture:
    try:
        source_int = int(source)
        cap = cv2.VideoCapture(source_int)
    except ValueError:
        cap = cv2.VideoCapture(source)
        cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)

    if not cap.isOpened():
        raise RuntimeError(f"Cannot open camera source: {source}")

    # Use MJPEG codec for maximum FPS (YUYV is slow)
    fourcc = cv2.VideoWriter_fourcc(*"MJPG")
    cap.set(cv2.CAP_PROP_FOURCC, fourcc)

    # Set 1080p resolution
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1920)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 1080)
    cap.set(cv2.CAP_PROP_FPS, 30)

    actual_w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    actual_h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    actual_fps = cap.get(cv2.CAP_PROP_FPS)
    codec_int = int(cap.get(cv2.CAP_PROP_FOURCC))
    codec = chr(codec_int & 0xFF) + chr((codec_int >> 8) & 0xFF) + chr((codec_int >> 16) & 0xFF) + chr((codec_int >> 24) & 0xFF)
    logger.info(f"Camera opened: {source} at {actual_w}x{actual_h} @ {actual_fps:.0f}FPS [{codec}]")
    return cap


# ─── Camera Service ────────────────────────────────────────────────

class CameraService:
    def __init__(self):
        self.engine = FaceEngine()
        global face_engine_instance
        face_engine_instance = self.engine
        StreamHandler.set_face_engine(self.engine)
        self.known_embeddings: list[dict] = []
        # Pre-normalized matrix for fast batch comparison
        self.embedding_matrix: np.ndarray = np.zeros((0, 512), dtype=np.float32)
        self.embedding_ids: list[str] = []
        self.embedding_names: list[str] = []
        self.last_refresh = 0.0
        self.session: aiohttp.ClientSession | None = None
        self.last_detections: list[dict] = []

        # ── Embedding Similarity Tracker ──
        # Tracks unknown faces by comparing their InsightFace embeddings
        # No extra model needed -- reuses the embeddings we already compute
        # List of {"embedding": np.array, "time": float, "confirm_count": int, "alerted": bool}
        self.recent_unknowns: list[dict] = []
        # {employee_id: last_event_time}
        self.known_cooldowns: dict[str, float] = {}
        self.UNKNOWN_MATCH_THRESHOLD = 0.50  # Same unknown person if similarity > 0.50
        logger.info(
            f"Embedding similarity tracker initialized | "
            f"cooldown={DETECTION_COOLDOWN}s, min_face={MIN_FACE_SIZE}px, "
            f"blur_threshold={BLUR_THRESHOLD}, confirm_frames={CONFIRM_FRAMES}"
        )

    async def get_session(self) -> aiohttp.ClientSession:
        if self.session is None or self.session.closed:
            self.session = aiohttp.ClientSession()
        return self.session

    async def refresh_embeddings(self):
        """Fetch both employee and student embeddings."""
        try:
            session = await self.get_session()
            all_embeddings = []

            # Fetch employee embeddings
            url = f"{BACKEND_API_URL}/api/employees/embeddings"
            headers = {"X-Api-Key": CAMERA_API_KEY}
            async with session.get(url, headers=headers) as resp:
                if resp.status == 200:
                    data = await resp.json()
                    for d in data:
                        d["person_type"] = "employee"
                    all_embeddings.extend(data)

            # Fetch student embeddings
            url2 = f"{BACKEND_API_URL}/api/schedule/students/embeddings"
            async with session.get(url2, headers=headers) as resp:
                if resp.status == 200:
                    data = await resp.json()
                    for d in data:
                        d["person_type"] = "student"
                        d["student_code"] = d.get("student_id", "")
                    all_embeddings.extend(data)

            self.known_embeddings = all_embeddings
            # Pre-normalize into a matrix ONCE for fast batch matmul later
            self.embedding_matrix, self.embedding_ids, self.embedding_names = (
                self.engine.build_embedding_matrix(all_embeddings)
            )
            n_emp = sum(1 for e in all_embeddings if e.get("person_type") == "employee")
            n_stu = sum(1 for e in all_embeddings if e.get("person_type") == "student")
            logger.info(
                f"Refreshed embeddings: {n_emp} employees + {n_stu} students = {len(all_embeddings)} total "
                f"(matrix: {self.embedding_matrix.shape})"
            )
        except Exception as e:
            logger.error(f"Embedding refresh error: {e}")

    async def learn_embedding(self, person_type: str, person_id: str, embedding: np.ndarray):
        """Progressive learning: send a confident runtime embedding to the backend
        so it gets saved and improves recognition over time."""
        try:
            session = await self.get_session()
            url = f"{BACKEND_API_URL}/api/schedule/learn-embedding"
            headers = {"X-Api-Key": CAMERA_API_KEY, "Content-Type": "application/json"}
            payload = {
                "person_type": person_type,
                "person_id": person_id,
                "embedding": embedding.tolist() if hasattr(embedding, "tolist") else list(embedding),
            }
            async with session.post(url, json=payload, headers=headers) as resp:
                if resp.status == 200:
                    data = await resp.json()
                    if data.get("added"):
                        logger.info(f"Progressive learning: added embedding for {person_id} (total: {data.get('total')})")
        except Exception as e:
            logger.debug(f"Learn embedding error (non-critical): {e}")

    async def check_schedule(self, student_code: str) -> dict:
        """Check if a student is authorized right now."""
        try:
            session = await self.get_session()
            url = f"{BACKEND_API_URL}/api/schedule/check/{student_code}"
            async with session.get(url) as resp:
                if resp.status == 200:
                    return await resp.json()
        except Exception as e:
            logger.error(f"Schedule check error: {e}")
        return {"status": "error"}

    async def post_event(self, event_data: dict):
        try:
            session = await self.get_session()
            url = f"{BACKEND_API_URL}/api/events"
            headers = {"X-Api-Key": CAMERA_API_KEY, "Content-Type": "application/json"}
            async with session.post(url, json=event_data, headers=headers) as resp:
                if resp.status not in (200, 201):
                    logger.error(f"Failed to post event: HTTP {resp.status}")
        except Exception as e:
            logger.error(f"Event post error: {e}")

    # ── Quality Filters ──

    @staticmethod
    def _is_face_too_small(bbox: list[int]) -> bool:
        """Reject faces smaller than MIN_FACE_SIZE pixels."""
        x1, y1, x2, y2 = bbox
        w, h = x2 - x1, y2 - y1
        return w < MIN_FACE_SIZE or h < MIN_FACE_SIZE

    @staticmethod
    def _is_face_blurry(face_crop: np.ndarray) -> bool:
        """Reject blurry faces using Laplacian variance."""
        if face_crop is None or face_crop.size == 0:
            return True
        gray = cv2.cvtColor(face_crop, cv2.COLOR_BGR2GRAY)
        variance = cv2.Laplacian(gray, cv2.CV_64F).var()
        return variance < BLUR_THRESHOLD

    # ── Embedding Tracking ──

    def _find_matching_unknown(self, embedding: np.ndarray) -> dict | None:
        """Find a recently seen unknown that matches this embedding."""
        now = time.time()
        # Clean expired entries
        self.recent_unknowns = [u for u in self.recent_unknowns if now - u["time"] < DETECTION_COOLDOWN]

        for unknown in self.recent_unknowns:
            sim = self.engine.cosine_similarity(embedding, unknown["embedding"])
            if sim > self.UNKNOWN_MATCH_THRESHOLD:
                unknown["time"] = now  # Keep alive while visible
                return unknown
        return None

    def _should_send_event(self, event_type: str, person_key: str | None, embedding: np.ndarray) -> bool:
        """Smart cooldown: embedding similarity + confirmation count."""
        now = time.time()

        if event_type != "unknown":
            # Known person (authorized, wrong_time, wrong_day, or employee)
            last = self.known_cooldowns.get(person_key, 0)
            if now - last < DETECTION_COOLDOWN:
                return False
            self.known_cooldowns[person_key] = now
            self.known_cooldowns = {k: v for k, v in self.known_cooldowns.items() if now - v < 300}
            return True

        # Unknown: embedding-based de-duplication + confirmation
        match = self._find_matching_unknown(embedding)

        if match:
            # Same unknown person seen again
            match["confirm_count"] = match.get("confirm_count", 1) + 1

            if match.get("alerted"):
                # Already alerted for this person -- suppress
                return False

            if match["confirm_count"] >= CONFIRM_FRAMES:
                # Confirmed unknown -- fire alert
                match["alerted"] = True
                return True

            # Not enough confirmations yet -- wait
            return False
        else:
            # Brand new unknown person -- start tracking, don't alert yet
            self.recent_unknowns.append({
                "embedding": embedding.copy(),
                "time": now,
                "confirm_count": 1,
                "alerted": False,
            })
            # If CONFIRM_FRAMES is 1, alert immediately
            if CONFIRM_FRAMES <= 1:
                self.recent_unknowns[-1]["alerted"] = True
                return True
            return False

    async def process_frame(self, frame: np.ndarray):
        """Detect faces, filter quality, identify, check schedule, and track."""
        faces = self.engine.detect_faces(frame)

        current_detections = []

        if not faces:
            self.last_detections = []
            return

        for face_data in faces:
            embedding = face_data["embedding"]
            face_crop = face_data["face_crop"]
            bbox = face_data["bbox"]

            # ── Quality Gate 1: Minimum face size ──
            if self._is_face_too_small(bbox):
                continue

            # ── Quality Gate 2: Blur detection ──
            if self._is_face_blurry(face_crop):
                continue

            # ── Fast vectorized identification (matmul, <1ms for 3000 embeddings) ──
            emp_id, emp_name, score = self.engine.identify_vectorized(
                embedding,
                self.embedding_matrix,
                self.embedding_ids,
                self.embedding_names,
                threshold=SIMILARITY_THRESHOLD,
            )

            # ── TTA only for truly borderline cases (narrow range) ──
            # Skip TTA when score is clearly good or clearly unknown
            if (SIMILARITY_THRESHOLD - 0.10) < score < (SIMILARITY_THRESHOLD + 0.10):
                tta_id, tta_name, tta_score = self.engine.identify_with_tta_vectorized(
                    face_crop,
                    self.embedding_matrix,
                    self.embedding_ids,
                    self.embedding_names,
                    threshold=SIMILARITY_THRESHOLD,
                )
                if tta_score > score:
                    emp_id, emp_name, score = tta_id, tta_name, tta_score

            if emp_id:
                # Found a match -- determine person type and check schedule
                matched = next((e for e in self.known_embeddings if e.get("employee_id") == emp_id), {})
                person_type = matched.get("person_type", "employee")
                student_code = matched.get("student_code", "")

                if person_type == "student" and student_code:
                    # Check schedule for students
                    schedule_result = await self.check_schedule(student_code)
                    event_type = schedule_result.get("status", "authorized")
                    schedule_info = schedule_result.get("message", "")

                    # Map status to display
                    if event_type == "authorized":
                        display_name = f"{student_code} (OK)"
                    elif event_type == "wrong_time":
                        display_name = f"{student_code} (WRONG TIME)"
                    elif event_type == "wrong_day":
                        display_name = f"{student_code} (WRONG DAY)"
                    else:
                        display_name = student_code
                        event_type = "authorized"
                else:
                    # Regular employee -- always authorized
                    event_type = "authorized"
                    display_name = emp_name
                    schedule_info = ""
                    student_code = ""
            else:
                event_type = "unknown"
                display_name = None
                schedule_info = ""
                student_code = ""

            # Track for bounding box drawing
            current_detections.append({
                "bbox": bbox,
                "event_type": event_type,
                "employee_name": display_name,
                "confidence": round(score, 4),
            })

            # Progressive learning: if very confident match, save this embedding
            # so future recognition improves with real-world-captured variations
            if emp_id and score > 0.70 and score < 0.95:
                # Throttle to avoid hammering backend (one learn per person per 5 minutes)
                now = time.time()
                last_learn = self._last_learn.get(emp_id, 0) if hasattr(self, '_last_learn') else 0
                if not hasattr(self, '_last_learn'):
                    self._last_learn = {}
                if now - last_learn > 300:  # 5 minutes
                    self._last_learn[emp_id] = now
                    ptype = matched.get("person_type", "employee")
                    asyncio.create_task(self.learn_embedding(ptype, emp_id, embedding))

            # Fire event only if quality + embedding + confirmation all pass
            person_key = emp_id or student_code or None
            if self._should_send_event(event_type, person_key, embedding):
                face_b64 = encode_image_b64(face_crop, quality=80)
                frame_b64 = encode_image_b64(frame, quality=70)

                event = {
                    "camera_id": CAMERA_ID,
                    "event_type": event_type,
                    "employee_id": emp_id,
                    "employee_name": display_name or emp_name,
                    "confidence": round(score, 4),
                    "bbox": json.dumps(bbox),
                    "face_image": face_b64,
                    "frame_image": frame_b64,
                }

                await self.post_event(event)
                logger.info(
                    f"EVENT: {event_type} | "
                    f"{display_name or 'Unknown'} | "
                    f"Score: {score:.3f} | "
                    f"{schedule_info}"
                )

        self.last_detections = current_detections

    async def run(self):
        logger.info("Starting camera service...")

        # Start GStreamer-accelerated MJPEG stream server
        stream = MJPEGStreamHandler(port=STREAM_PORT)
        stream.start()

        # Wait for backend
        for attempt in range(30):
            try:
                session = await self.get_session()
                async with session.get(f"{BACKEND_API_URL}/api/health") as resp:
                    if resp.status == 200:
                        logger.info("Backend is ready")
                        break
            except Exception:
                pass
            logger.info(f"Waiting for backend... (attempt {attempt + 1}/30)")
            await asyncio.sleep(2)

        await self.refresh_embeddings()

        cap = None
        frame_count = 0

        while True:
            try:
                if cap is None or not cap.isOpened():
                    cap = open_camera(CAMERA_SOURCE)

                ret, frame = cap.read()
                if not ret:
                    logger.warning("Failed to read frame, reconnecting...")
                    cap.release()
                    cap = None
                    await asyncio.sleep(2)
                    continue

                frame_count += 1
                now = time.time()

                # Run face detection on selected frames
                if frame_count % (FRAME_SKIP + 1) == 0:
                    await self.process_frame(frame)

                # Update stream with annotated frame (every frame -- GStreamer handles encoding)
                annotated = draw_detections(frame, self.last_detections)
                frame_buffer.update(annotated)

                # Periodic embedding refresh
                if now - self.last_refresh > EMBEDDING_REFRESH_INTERVAL:
                    await self.refresh_embeddings()
                    self.last_refresh = now

            except KeyboardInterrupt:
                break
            except Exception as e:
                logger.error(f"Frame processing error: {e}")
                await asyncio.sleep(1)

        if cap:
            cap.release()
        if self.session and not self.session.closed:
            await self.session.close()
        logger.info("Camera service stopped")


if __name__ == "__main__":
    service = CameraService()
    asyncio.run(service.run())
