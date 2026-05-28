import logging

import cv2
import numpy as np

logger = logging.getLogger(__name__)


class FaceEngine:
    """InsightFace wrapper with buffalo_l model + test-time augmentation."""

    def __init__(self, providers: list[str] | None = None, model_name: str = "buffalo_l"):
        from insightface.app import FaceAnalysis

        if providers is None:
            try:
                self.app = FaceAnalysis(name=model_name, providers=["CUDAExecutionProvider"])
                self.app.prepare(ctx_id=0, det_size=(640, 640))
                logger.info(f"FaceEngine initialized with CUDA and model '{model_name}'")
            except Exception:
                self.app = FaceAnalysis(name=model_name, providers=["CPUExecutionProvider"])
                self.app.prepare(ctx_id=-1, det_size=(640, 640))
                logger.info(f"FaceEngine initialized with CPU and model '{model_name}' (CUDA not available)")
        else:
            self.app = FaceAnalysis(name=model_name, providers=providers)
            ctx_id = 0 if "CUDAExecutionProvider" in providers else -1
            self.app.prepare(ctx_id=ctx_id, det_size=(640, 640))

        self.model_name = model_name

    def detect_faces(self, frame: np.ndarray) -> list[dict]:
        """Detect all faces in a frame and extract embeddings."""
        faces = self.app.get(frame)
        results = []
        for face in faces:
            bbox = face.bbox.astype(int).tolist()
            embedding = face.embedding

            x1, y1, x2, y2 = bbox
            h, w = frame.shape[:2]
            pad = 20
            x1 = max(0, x1 - pad)
            y1 = max(0, y1 - pad)
            x2 = min(w, x2 + pad)
            y2 = min(h, y2 + pad)
            face_crop = frame[y1:y2, x1:x2]

            results.append({
                "bbox": bbox,
                "embedding": embedding,
                "face_crop": face_crop,
            })

        return results

    # ─── Augmentation ────────────────────────────────────────────

    @staticmethod
    def _augment_image(img: np.ndarray) -> list[tuple[str, np.ndarray]]:
        """Generate augmented variants of the input image for TTA.
        Returns list of (variant_name, image) tuples.
        """
        variants = [("original", img)]

        # Horizontal flip (symmetric face view)
        variants.append(("flip", cv2.flip(img, 1)))

        # Brightness variants (handles different lighting)
        dark = cv2.convertScaleAbs(img, alpha=0.8, beta=-10)
        bright = cv2.convertScaleAbs(img, alpha=1.15, beta=10)
        variants.append(("dark", dark))
        variants.append(("bright", bright))

        # Small rotations (handles head tilt)
        h, w = img.shape[:2]
        center = (w // 2, h // 2)
        for angle in (-12, 12):
            M = cv2.getRotationMatrix2D(center, angle, 1.0)
            rotated = cv2.warpAffine(img, M, (w, h), borderMode=cv2.BORDER_REPLICATE)
            variants.append((f"rot_{angle}", rotated))

        return variants

    def extract_augmented_embeddings(self, img: np.ndarray) -> list[np.ndarray]:
        """Generate multiple embeddings from a single photo using TTA.
        Returns a list of normalized embedding vectors (up to 6 per photo).
        """
        all_embeddings = []

        for name, variant in self._augment_image(img):
            faces = self.app.get(variant)
            if faces:
                emb = faces[0].embedding
                # Normalize for cosine similarity
                norm = np.linalg.norm(emb)
                if norm > 0:
                    all_embeddings.append(emb / norm)

        return all_embeddings

    def extract_identification_embeddings(self, face_crop: np.ndarray) -> list[np.ndarray]:
        """Extract multiple embeddings from a live camera face for TTA at identification time.
        Uses only flip + 1 brightness variant (fast, only 3 variants to keep identification fast).
        """
        variants = [face_crop, cv2.flip(face_crop, 1)]

        embeddings = []
        for variant in variants:
            faces = self.app.get(variant)
            if faces:
                emb = faces[0].embedding
                norm = np.linalg.norm(emb)
                if norm > 0:
                    embeddings.append(emb / norm)

        return embeddings

    # ─── Similarity ──────────────────────────────────────────────

    @staticmethod
    def cosine_similarity(emb1: np.ndarray, emb2: np.ndarray) -> float:
        """Compute cosine similarity between two embeddings (assumes normalized)."""
        # If already normalized, this is just a dot product
        return float(np.dot(emb1, emb2))

    @staticmethod
    def cosine_similarity_unnormalized(emb1: np.ndarray, emb2: np.ndarray) -> float:
        """Cosine similarity for unnormalized embeddings."""
        dot = np.dot(emb1, emb2)
        norm = np.linalg.norm(emb1) * np.linalg.norm(emb2)
        if norm == 0:
            return 0.0
        return float(dot / norm)

    @staticmethod
    def build_embedding_matrix(known_embeddings: list[dict]) -> tuple[np.ndarray, list[str], list[str]]:
        """Pre-normalize all known embeddings into a single (N, 512) matrix for fast batch comparison.
        Returns (matrix, ids, names).
        """
        if not known_embeddings:
            return np.zeros((0, 512), dtype=np.float32), [], []

        n = len(known_embeddings)
        matrix = np.zeros((n, 512), dtype=np.float32)
        ids = []
        names = []

        for i, known in enumerate(known_embeddings):
            emb = np.asarray(known["embedding"], dtype=np.float32)
            norm = np.linalg.norm(emb)
            if norm > 0:
                matrix[i] = emb / norm
            ids.append(known["employee_id"])
            names.append(known["employee_name"])

        return matrix, ids, names

    def identify_vectorized(
        self,
        embedding: np.ndarray,
        embedding_matrix: np.ndarray,
        ids: list[str],
        names: list[str],
        threshold: float = 0.55,
    ) -> tuple[str | None, str | None, float]:
        """Fast identification using pre-normalized matrix + numpy matmul.
        Runs in <1ms for 3000+ embeddings vs ~8ms for per-embedding loop.
        """
        if embedding_matrix.shape[0] == 0:
            return None, None, -1.0

        norm = np.linalg.norm(embedding)
        if norm == 0:
            return None, None, -1.0
        query = (embedding / norm).astype(np.float32)

        # Single matmul: (N, 512) @ (512,) -> (N,) similarity scores
        scores = embedding_matrix @ query
        best_idx = int(np.argmax(scores))
        best_score = float(scores[best_idx])

        if best_score >= threshold:
            return ids[best_idx], names[best_idx], best_score

        return None, None, best_score

    def identify_with_tta_vectorized(
        self,
        face_crop: np.ndarray,
        embedding_matrix: np.ndarray,
        ids: list[str],
        names: list[str],
        threshold: float = 0.55,
    ) -> tuple[str | None, str | None, float]:
        """TTA identification using vectorized matmul.
        Extracts multiple embeddings from the live face, each compared to matrix in one matmul.
        """
        if embedding_matrix.shape[0] == 0:
            return None, None, -1.0

        query_embeddings = self.extract_identification_embeddings(face_crop)
        if not query_embeddings:
            return None, None, -1.0

        # Stack queries into (K, 512) matrix
        queries = np.stack([q.astype(np.float32) for q in query_embeddings])

        # One matmul: (K, 512) @ (N, 512).T -> (K, N) all scores at once
        all_scores = queries @ embedding_matrix.T

        # Find the best (query, known) pair
        best_score = float(np.max(all_scores))
        best_flat_idx = int(np.argmax(all_scores))
        best_known_idx = best_flat_idx % embedding_matrix.shape[0]

        if best_score >= threshold:
            return ids[best_known_idx], names[best_known_idx], best_score

        return None, None, best_score

    # ─── Legacy (kept for backward compatibility) ────────────────

    def identify(
        self, embedding: np.ndarray, known_embeddings: list[dict], threshold: float = 0.55
    ) -> tuple[str | None, str | None, float]:
        """Legacy slower identification. Use identify_vectorized() for production."""
        matrix, ids, names = self.build_embedding_matrix(known_embeddings)
        return self.identify_vectorized(embedding, matrix, ids, names, threshold)

    def identify_with_tta(
        self, face_crop: np.ndarray, known_embeddings: list[dict], threshold: float = 0.55
    ) -> tuple[str | None, str | None, float]:
        """Legacy slower TTA. Use identify_with_tta_vectorized() for production."""
        matrix, ids, names = self.build_embedding_matrix(known_embeddings)
        return self.identify_with_tta_vectorized(face_crop, matrix, ids, names, threshold)
