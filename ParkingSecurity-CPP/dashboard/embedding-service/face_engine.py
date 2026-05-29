"""InsightFace-based embedding extractor for enrollment photos.

Uses the same buffalo_l model pack (SCRFD det_10g + ArcFace w600k_r50) as the
C++ daemon runs on the Jetson GPU — so embeddings produced here match what the
daemon compares enrolled people against on live frames.

Runs on CPU (ctx_id=-1) because enrollment is low-volume and we want this
container portable; the only GPU workload in the system is the live daemon.
"""

import cv2
import numpy as np
from insightface.app import FaceAnalysis


class FaceEngine:
    def __init__(self, model_name: str = "buffalo_l", det_size=(640, 640)):
        self.app = FaceAnalysis(
            name=model_name,
            allowed_modules=["detection", "recognition"],
        )
        # ctx_id=-1 → CPU onnxruntime provider.
        self.app.prepare(ctx_id=-1, det_size=det_size)

    def _largest(self, faces):
        return max(
            faces,
            key=lambda f: (f.bbox[2] - f.bbox[0]) * (f.bbox[3] - f.bbox[1]),
        )

    def extract_embedding(self, image_bgr):
        """Returns (embedding_list_or_None, faces_found)."""
        if image_bgr is None:
            return None, 0
        faces = self.app.get(image_bgr)
        if not faces:
            return None, 0
        best = self._largest(faces)
        emb = np.asarray(best.normed_embedding, dtype=np.float32)
        return emb.tolist(), len(faces)

    def extract_augmented(self, image_bgr):
        """Returns (list_of_embeddings, count).

        Produces the original embedding plus a horizontal-flip averaged
        embedding (re-L2-normalized), matching the old Python camera service's
        TTA behavior so enrolled vectors keep the same statistics.
        """
        if image_bgr is None:
            return [], 0
        emb1, _ = self.extract_embedding(image_bgr)
        if emb1 is None:
            return [], 0

        flipped = cv2.flip(image_bgr, 1)
        emb2, _ = self.extract_embedding(flipped)
        if emb2 is None:
            return [emb1], 1

        a = np.asarray(emb1, dtype=np.float32)
        b = np.asarray(emb2, dtype=np.float32)
        avg = a + b
        n = float(np.linalg.norm(avg))
        if n > 0:
            avg = avg / n
        return [emb1, avg.tolist()], 2
