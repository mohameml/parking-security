// DeepStream custom bbox parser for SCRFD (InsightFace buffalo_l/det_10g).
//
// nvinfer calls this function after each inference. It receives the raw
// output tensor buffers (from trtexec-built engine) and must fill the
// `objectList` with NvDsInferParseObjectInfo records. nvinfer then wraps
// each entry in an NvDsObjectMeta and appends it to the frame meta — which
// the pipeline's pad probe can iterate.
//
// Build as a shared library; reference from the nvinfer config via:
//   parse-bbox-func-name=NvDsInferParseCustomSCRFD
//   custom-lib-path=<path>/libnvds_parsebbox_scrfd.so

#include "nvdsinfer_custom_impl.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace {

constexpr int kNumStrides                 = 3;
constexpr std::array<int, 3> kStrides     = {8, 16, 32};
constexpr int kNumAnchorsPerCell          = 2;
// Assumes 640×640 network input (SCRFD 10G default).
constexpr std::array<int, 3> kNumAnchors  = {12800, 3200, 800};
constexpr std::array<int, 3> kFeatWH      = {80, 40, 20};

struct Cand {
    float x1, y1, x2, y2, score;
};

float iou(const Cand& a, const Cand& b) {
    const float ix1 = std::max(a.x1, b.x1);
    const float iy1 = std::max(a.y1, b.y1);
    const float ix2 = std::min(a.x2, b.x2);
    const float iy2 = std::min(a.y2, b.y2);
    const float iw  = std::max(0.0f, ix2 - ix1);
    const float ih  = std::max(0.0f, iy2 - iy1);
    const float inter = iw * ih;
    const float area_a = std::max(0.0f, a.x2 - a.x1) * std::max(0.0f, a.y2 - a.y1);
    const float area_b = std::max(0.0f, b.x2 - b.x1) * std::max(0.0f, b.y2 - b.y1);
    const float un    = area_a + area_b - inter;
    return un > 0.0f ? inter / un : 0.0f;
}

/// Match the nine SCRFD output layers to their (stride, kind) role.
/// Returns pointers indexed as [stride_idx * 3 + kind] where
/// kind 0=score(1ch), 1=bbox(4ch), 2=kps(10ch). Uses channel count (d[0])
/// and order-of-appearance within each kind, matching the ONNX export layout.
bool route_layers(const std::vector<NvDsInferLayerInfo>& layers,
                  std::array<const float*, 9>&           out) {
    out.fill(nullptr);
    int score_i = 0, bbox_i = 0, kps_i = 0;
    for (const auto& layer : layers) {
        if (layer.dataType != FLOAT) continue;
        if (layer.inferDims.numDims < 1) continue;
        // Channel count is the LAST dim (e.g., [N, 1], [N, 4], [N, 10] or
        // [B, N, C] after we added explicit batch dim).
        const unsigned int ch =
            layer.inferDims.d[layer.inferDims.numDims - 1];
        int stride_idx = -1, kind = -1;
        if (ch == 1 && score_i < 3) { stride_idx = score_i++; kind = 0; }
        else if (ch == 4 && bbox_i < 3) { stride_idx = bbox_i++; kind = 1; }
        else if (ch == 10 && kps_i < 3) { stride_idx = kps_i++; kind = 2; }
        else continue;
        out[stride_idx * 3 + kind] = static_cast<const float*>(layer.buffer);
    }
    for (const float* p : out) {
        if (p == nullptr) return false;
    }
    return true;
}

}  // namespace

extern "C" bool NvDsInferParseCustomSCRFD(
    std::vector<NvDsInferLayerInfo> const&      outputLayersInfo,
    NvDsInferNetworkInfo const&                 networkInfo,
    NvDsInferParseDetectionParams const&        detectionParams,
    std::vector<NvDsInferParseObjectInfo>&      objectList);

extern "C" bool NvDsInferParseCustomSCRFD(
    std::vector<NvDsInferLayerInfo> const&      outputLayersInfo,
    NvDsInferNetworkInfo const&                 networkInfo,
    NvDsInferParseDetectionParams const&        detectionParams,
    std::vector<NvDsInferParseObjectInfo>&      objectList) {
    std::array<const float*, 9> layers{};
    if (!route_layers(outputLayersInfo, layers)) {
        return false;
    }

    const float score_thresh = detectionParams.perClassPreclusterThreshold.empty()
                                   ? 0.5f
                                   : detectionParams.perClassPreclusterThreshold[0];

    // Network input size — SCRFD is square, use width. Unused here but kept
    // for clarity; nvinfer rescales bboxes into source-image coords itself.
    (void)networkInfo;
    (void)score_thresh;

    std::vector<Cand> cands;
    cands.reserve(256);

    for (int s = 0; s < kNumStrides; ++s) {
        const int     stride    = kStrides[s];
        const int     feat_w    = kFeatWH[s];
        const int     num_anch  = kNumAnchors[s];
        const float*  scores    = layers[s * 3 + 0];
        const float*  bboxes    = layers[s * 3 + 1];

        for (int i = 0; i < num_anch; ++i) {
            // ONNX has sigmoid fused onto the score output and we rebuild the
            // engine from a variant with an explicit batch dim, so the TRT
            // engine emits post-sigmoid scores in [0, 1]. Threshold directly.
            // 0.7 here is a quality gate: lower scores are typically blurry
            // profiles, motion blur, or partial-crop false positives.
            const float score = scores[i];
            if (score < 0.7f) continue;

            const int point_idx = i / kNumAnchorsPerCell;
            const int cy        = point_idx / feat_w;
            const int cx        = point_idx % feat_w;
            const float ax = static_cast<float>(cx) * stride;
            const float ay = static_cast<float>(cy) * stride;

            const float* b = bboxes + i * 4;
            const float  l = b[0] * stride;
            const float  t = b[1] * stride;
            const float  r = b[2] * stride;
            const float  btm = b[3] * stride;

            cands.push_back({ax - l, ay - t, ax + r, ay + btm, score});
        }
    }

    if (cands.empty()) return true;

    std::sort(cands.begin(), cands.end(),
              [](const Cand& a, const Cand& b) { return a.score > b.score; });

    const float nms_iou = 0.4f;
    const int   max_out = 50;
    std::vector<bool> suppressed(cands.size(), false);

    // We emit bboxes in NETWORK COORDS (640×640). nvinfer auto-rescales them
    // into source-image space before wrapping in NvDsObjectMeta, so the pad
    // probe reads rect_params in the original frame resolution.

    for (std::size_t i = 0; i < cands.size(); ++i) {
        if (suppressed[i]) continue;
        if (static_cast<int>(objectList.size()) >= max_out) break;
        const Cand& c = cands[i];

        NvDsInferParseObjectInfo obj{};
        obj.classId         = 0;
        obj.detectionConfidence = c.score;
        obj.left            = std::max(0.0f, c.x1);
        obj.top             = std::max(0.0f, c.y1);
        obj.width           = std::max(0.0f, c.x2 - c.x1);
        obj.height          = std::max(0.0f, c.y2 - c.y1);
        objectList.push_back(obj);

        for (std::size_t j = i + 1; j < cands.size(); ++j) {
            if (!suppressed[j] && iou(c, cands[j]) > nms_iou) {
                suppressed[j] = true;
            }
        }
    }

    return true;
}

// Required by nvinfer when loading a custom .so.
CHECK_CUSTOM_PARSE_FUNC_PROTOTYPE(NvDsInferParseCustomSCRFD)
