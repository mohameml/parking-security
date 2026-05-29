FROM python:3.11-slim

# libgl1 + libglib2.0-0 are required by opencv-python-headless' native deps;
# build-essential is needed because insightface pulls a couple of sdist wheels
# (cython_bbox etc.) that compile at install time on ARM/aarch64.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        libgl1 \
        libglib2.0-0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt

COPY face_engine.py app.py ./

# InsightFace caches its model pack here; mount a host volume on this path so
# subsequent container restarts don't re-download the ~300 MB buffalo_l pack.
ENV INSIGHTFACE_HOME=/root/.insightface

EXPOSE 8080

CMD ["uvicorn", "app:app", "--host", "0.0.0.0", "--port", "8080"]
