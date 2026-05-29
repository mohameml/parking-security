# TensorRT Engine Files

This directory holds the **generated** TensorRT engine files used by the
face detection and recognition pipelines. These files are NOT checked into
version control because they are locked to:

- A specific GPU architecture (Jetson Orin Nano = sm_87)
- A specific TensorRT version (changes with JetPack upgrades)
- Build options (FP16, INT8, workspace size, etc.)

## Generating the Engines

```bash
# On the target Jetson, after building the project:
./scripts/convert_models.sh
```

The script reads InsightFace buffalo_l ONNX models from `~/.insightface/models/buffalo_l/`
and produces:

| Engine              | Source                   | Used by             |
|---------------------|--------------------------|---------------------|
| `det_10g.engine`    | `det_10g.onnx`           | SCRFD face detection |
| `w600k_r50.engine`  | `w600k_r50.onnx`         | ArcFace recognition  |
| `1k3d68.engine`     | `1k3d68.onnx` (optional) | 3D landmark detection |
| `2d106det.engine`   | `2d106det.onnx` (optional)| 2D landmark detection |

## Precision

Default: FP16 (2x faster than FP32, negligible accuracy loss on face recognition).

For INT8 (fastest, requires calibration dataset):

```bash
PRECISION=int8 ./scripts/convert_models.sh
```

## Validating

After conversion, benchmark each engine:

```bash
./build/release/tools/benchmark_engine models/det_10g.engine
./build/release/tools/benchmark_engine models/w600k_r50.engine
```

Expected on Jetson Orin Nano Super (MAXN mode, FP16):

| Engine              | Avg latency | FPS  |
|---------------------|-------------|------|
| `det_10g.engine`    | ~4 ms       | 250  |
| `w600k_r50.engine`  | ~3 ms/face  | 300+ |
