"""Fast MJPEG stream server using TurboJPEG + threaded encoding.

TurboJPEG is 3-7x faster than OpenCV's imencode for JPEG compression.
Encoding runs in a dedicated thread so it never blocks the camera/detection loop.
"""
import json
import logging
import threading
import time
from http.server import HTTPServer, BaseHTTPRequestHandler
from socketserver import ThreadingMixIn

import cv2
import numpy as np

logger = logging.getLogger("stream-server")

# ─── TurboJPEG Encoder (3-7x faster than cv2.imencode) ───
try:
    from turbojpeg import TurboJPEG, TJPF_BGR
    _tj = TurboJPEG()
    _USE_TURBO = True
    logger.info("TurboJPEG encoder loaded (3-7x faster)")
except ImportError:
    _USE_TURBO = False
    logger.warning("TurboJPEG not available, falling back to OpenCV")


def encode_jpeg(frame: np.ndarray, quality: int = 82) -> bytes:
    """Encode BGR frame to JPEG bytes using the fastest available encoder."""
    if _USE_TURBO:
        return _tj.encode(frame, quality=quality, pixel_format=TJPF_BGR)
    else:
        _, buf = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, quality])
        return buf.tobytes()


class FrameBuffer:
    """Thread-safe frame buffer with background JPEG encoding.

    The camera loop calls update(frame) with raw BGR frames.
    A background thread encodes them to JPEG asynchronously.
    Stream clients get the latest encoded JPEG instantly.
    """

    def __init__(self):
        self._raw_frame = None
        self._jpeg = None
        self._lock = threading.Lock()
        self._new_frame = threading.Event()
        self._encode_thread = threading.Thread(target=self._encode_loop, daemon=True)
        self._encode_thread.start()

    def update(self, frame: np.ndarray):
        """Called by camera loop -- just stores the raw frame, no encoding here."""
        with self._lock:
            self._raw_frame = frame
        self._new_frame.set()

    def _encode_loop(self):
        """Background thread that encodes frames to JPEG continuously."""
        while True:
            self._new_frame.wait()
            self._new_frame.clear()
            with self._lock:
                frame = self._raw_frame
            if frame is not None:
                jpeg = encode_jpeg(frame, quality=82)
                with self._lock:
                    self._jpeg = jpeg

    def get_frame(self) -> np.ndarray | None:
        with self._lock:
            return self._raw_frame

    def get_jpeg(self) -> bytes | None:
        with self._lock:
            return self._jpeg

    def wait_new(self, timeout: float = 0.05) -> bool:
        return self._new_frame.wait(timeout)


# Global frame buffer
frame_buffer = FrameBuffer()


class StreamHandler(BaseHTTPRequestHandler):
    """HTTP handler for MJPEG stream, snapshots, and embedding extraction."""

    _face_engine = None

    @classmethod
    def set_face_engine(cls, engine):
        cls._face_engine = engine

    def do_GET(self):
        if self.path == "/stream":
            self._serve_stream()
        elif self.path == "/snapshot":
            self._serve_snapshot()
        else:
            self._serve_info()

    def do_POST(self):
        if self.path == "/extract-embedding":
            self._extract_embedding()
        elif self.path == "/extract-embeddings-augmented":
            self._extract_embeddings_augmented()
        else:
            self.send_response(404)
            self.end_headers()

    def _serve_stream(self):
        self.send_response(200)
        self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
        self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        try:
            last_jpeg_id = None
            while True:
                jpeg = frame_buffer.get_jpeg()
                if jpeg and jpeg is not last_jpeg_id:
                    last_jpeg_id = jpeg
                    self.wfile.write(b"--frame\r\n")
                    self.wfile.write(b"Content-Type: image/jpeg\r\n")
                    self.wfile.write(f"Content-Length: {len(jpeg)}\r\n\r\n".encode())
                    self.wfile.write(jpeg)
                    self.wfile.write(b"\r\n")
                else:
                    time.sleep(0.01)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _serve_snapshot(self):
        jpeg = frame_buffer.get_jpeg()
        if jpeg:
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Content-Length", str(len(jpeg)))
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(jpeg)
        else:
            self.send_response(503)
            self.end_headers()

    def _serve_info(self):
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(json.dumps({
            "stream": "/stream",
            "snapshot": "/snapshot",
            "encoder": "turbojpeg" if _USE_TURBO else "opencv",
        }).encode())

    def _extract_embedding(self):
        content_length = int(self.headers.get("Content-Length", 0))
        image_bytes = self.rfile.read(content_length)
        try:
            img_array = cv2.imdecode(np.frombuffer(image_bytes, np.uint8), cv2.IMREAD_COLOR)
            if img_array is None:
                self._json(400, {"error": "Invalid image"})
                return
            if StreamHandler._face_engine is None:
                self._json(503, {"error": "FaceEngine not ready"})
                return
            faces = StreamHandler._face_engine.detect_faces(img_array)
            if not faces:
                self._json(200, {"embedding": None, "error": "No face detected in image"})
                return
            embedding = faces[0]["embedding"].tolist()
            self._json(200, {"embedding": embedding, "faces_found": len(faces)})
        except Exception as e:
            self._json(500, {"error": str(e)})

    def _extract_embeddings_augmented(self):
        """Generate multiple embeddings from one photo using test-time augmentation."""
        content_length = int(self.headers.get("Content-Length", 0))
        image_bytes = self.rfile.read(content_length)
        try:
            img_array = cv2.imdecode(np.frombuffer(image_bytes, np.uint8), cv2.IMREAD_COLOR)
            if img_array is None:
                self._json(400, {"error": "Invalid image"})
                return
            if StreamHandler._face_engine is None:
                self._json(503, {"error": "FaceEngine not ready"})
                return

            embeddings = StreamHandler._face_engine.extract_augmented_embeddings(img_array)
            if not embeddings:
                self._json(200, {"embeddings": [], "error": "No face detected in image"})
                return

            # Return as list of lists
            self._json(200, {
                "embeddings": [emb.tolist() for emb in embeddings],
                "count": len(embeddings),
            })
        except Exception as e:
            self._json(500, {"error": str(e)})

    def _json(self, code, data):
        body = json.dumps(data).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        pass


class ThreadedHTTPServer(ThreadingMixIn, HTTPServer):
    daemon_threads = True


class MJPEGStreamHandler:
    def __init__(self, port: int = 8080):
        self.port = port

    def start(self):
        thread = threading.Thread(target=self._serve, daemon=True)
        thread.start()
        encoder = "TurboJPEG" if _USE_TURBO else "OpenCV"
        logger.info(f"MJPEG stream server started on port {self.port} ({encoder} encoder, threaded)")

    def _serve(self):
        server = ThreadedHTTPServer(("0.0.0.0", self.port), StreamHandler)
        server.serve_forever()
