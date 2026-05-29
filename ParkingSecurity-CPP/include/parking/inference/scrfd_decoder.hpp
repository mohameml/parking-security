#pragma once

// SCRFD raw-tensor decoder.
//
// Converts the nine output tensors of InsightFace's SCRFD detector
// (det_10g.onnx / buffalo_l) into a list of face boxes + 5-point
// landmarks in the ORIGINAL image coordinate frame.
//
// SCRFD 10G specifics (from InsightFace's scrfd.py):
//   - Input size 640x640, preprocessing (x - 127.5) / 128 in RGB.
//   - Three FPN strides: 8, 16, 32.
//   - Two anchors per spatial position → total anchors for 640x640:
//         80*80*2 + 40*40*2 + 20*20*2 = 16800.
//   - Score:  [num_anchors, 1]       (logits already sigmoid'd on device)
//   - Bbox:   [num_anchors, 4]       distances (l, t, r, b) from anchor center,
//                                   pre-multiplied network output (× stride on decode)
//   - Kps:    [num_anchors, 10]      5 (x, y) points as distances from anchor center
//
// We assume the feed letterboxes a full 1920x1080 (or similar) input into
// a 640x640 canvas preserving aspect ratio — this matches what nvinfer does
// when maintain-aspect-ratio=1 (default for DeepStream 7.x when no explicit
// setting). If you disable that, the caller must adjust the scale factors.

#include "parking/core/types.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace parking::inference {

/// One face detection with landmarks, in original-image pixel coords.
struct ScrfdDetection {
    BBox  bbox{};
    float score{0.0f};
    std::array<std::pair<float, float>, 5> landmarks{};
};

/// One raw output layer as received from NvDsInferTensorMeta.
/// data points at a host-side float buffer (owned by nvinfer, valid for the
/// lifetime of the GstBuffer) and num_elements is the flattened length.
struct ScrfdTensor {
    const float* data{nullptr};
    std::size_t  num_elements{0};
};

/// Tensor bundle. Exactly 9 tensors are required. The decoder routes them by
/// `num_elements` (unique per stride × channels), so caller doesn't need to
/// worry about the ONNX layer name order.
struct ScrfdTensorBundle {
    std::array<ScrfdTensor, 9> tensors{};
};

struct ScrfdDecodeParams {
    /// SCRFD input blob size (square). buffalo_l det_10g is 640×640.
    int32_t network_input_size{640};

    /// Source image dims (pixels). Needed to map network coords → image coords.
    int32_t image_width{1920};
    int32_t image_height{1080};

    /// Whether nvinfer letterboxed the input (maintain-aspect-ratio=1). If
    /// true, the decoder undoes the letterbox; if false, it assumes a plain
    /// non-uniform resize.
    bool maintain_aspect_ratio{true};

    float score_threshold{0.5f};
    float nms_iou_threshold{0.4f};
    int32_t max_detections{50};
};

/// Decode SCRFD raw tensors into detections.
/// Returns an empty vector if any required tensor is missing/malformed.
std::vector<ScrfdDetection> decode_scrfd(const ScrfdTensorBundle& bundle,
                                         const ScrfdDecodeParams& params);

}  // namespace parking::inference
