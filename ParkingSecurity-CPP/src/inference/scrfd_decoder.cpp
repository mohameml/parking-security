#include "parking/inference/scrfd_decoder.hpp"

#include "parking/core/logger.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace parking::inference {

namespace {

// SCRFD 10G has 3 FPN levels and 2 anchors per position.
constexpr int kNumStrides = 3;
constexpr std::array<int, kNumStrides> kStrides = {8, 16, 32};
constexpr int kNumAnchorsPerCell = 2;

struct StridePlan {
    int stride{0};
    int feat_w{0};
    int feat_h{0};
    int num_points{0};   // feat_w * feat_h
    int num_anchors{0};  // num_points * kNumAnchorsPerCell
};

std::array<StridePlan, kNumStrides> build_plans(int input_size) {
    std::array<StridePlan, kNumStrides> plans{};
    for (int i = 0; i < kNumStrides; ++i) {
        plans[i].stride      = kStrides[i];
        plans[i].feat_w      = input_size / kStrides[i];
        plans[i].feat_h      = input_size / kStrides[i];
        plans[i].num_points  = plans[i].feat_w * plans[i].feat_h;
        plans[i].num_anchors = plans[i].num_points * kNumAnchorsPerCell;
    }
    return plans;
}

struct Candidate {
    float x1, y1, x2, y2;
    float score;
    std::array<std::pair<float, float>, 5> kps;
};

float iou(const Candidate& a, const Candidate& b) {
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

/// Match each of the 9 input tensors to its (stride, kind) role by flat
/// element count. Score tensors have 1 channel, bbox 4, kps 10. Output order:
///   [score_s8, bbox_s8, kps_s8, score_s16, ..., kps_s32]
bool route_tensors(const ScrfdTensorBundle&            bundle,
                   const std::array<StridePlan, 3>&    plans,
                   std::array<const ScrfdTensor*, 9>&  routed) {
    routed.fill(nullptr);
    for (const auto& t : bundle.tensors) {
        if (t.data == nullptr || t.num_elements == 0) continue;
        for (int s = 0; s < 3; ++s) {
            const auto n = static_cast<std::size_t>(plans[s].num_anchors);
            if (t.num_elements == n * 1) {
                routed[s * 3 + 0] = &t;  // score
            } else if (t.num_elements == n * 4) {
                routed[s * 3 + 1] = &t;  // bbox
            } else if (t.num_elements == n * 10) {
                routed[s * 3 + 2] = &t;  // kps
            }
        }
    }
    for (auto* p : routed) {
        if (p == nullptr) return false;
    }
    return true;
}

/// Compute the letterbox transform applied by nvinfer's internal preprocess.
/// Returns (scale, pad_x, pad_y) such that:
///   net_x = img_x * scale + pad_x
///   net_y = img_y * scale + pad_y
/// To go from network coords back to image coords:
///   img_x = (net_x - pad_x) / scale
struct LetterboxXform {
    float scale{1.0f};
    float pad_x{0.0f};
    float pad_y{0.0f};
};

LetterboxXform compute_letterbox(int img_w, int img_h, int net_size, bool maintain_ar) {
    LetterboxXform lb;
    if (!maintain_ar) {
        lb.scale = 1.0f;  // non-uniform — callers use separate x/y scales below.
        return lb;
    }
    const float sw = static_cast<float>(net_size) / static_cast<float>(img_w);
    const float sh = static_cast<float>(net_size) / static_cast<float>(img_h);
    lb.scale = std::min(sw, sh);
    const float resized_w = img_w * lb.scale;
    const float resized_h = img_h * lb.scale;
    lb.pad_x = (static_cast<float>(net_size) - resized_w) * 0.5f;
    lb.pad_y = (static_cast<float>(net_size) - resized_h) * 0.5f;
    return lb;
}

}  // namespace

std::vector<ScrfdDetection> decode_scrfd(const ScrfdTensorBundle& bundle,
                                         const ScrfdDecodeParams& params) {
    const auto plans = build_plans(params.network_input_size);
    std::array<const ScrfdTensor*, 9> routed{};
    if (!route_tensors(bundle, plans, routed)) {
        PLOG_WARN(detector,
                  "SCRFD decode: could not match all 9 output tensors by shape");
        return {};
    }

    const LetterboxXform lb = compute_letterbox(
        params.image_width, params.image_height,
        params.network_input_size, params.maintain_aspect_ratio);

    // Fallback non-uniform scales if letterboxing is disabled.
    const float fallback_sx =
        static_cast<float>(params.image_width)  / static_cast<float>(params.network_input_size);
    const float fallback_sy =
        static_cast<float>(params.image_height) / static_cast<float>(params.network_input_size);

    auto to_image = [&](float nx, float ny) -> std::pair<float, float> {
        if (params.maintain_aspect_ratio) {
            return {(nx - lb.pad_x) / lb.scale, (ny - lb.pad_y) / lb.scale};
        }
        return {nx * fallback_sx, ny * fallback_sy};
    };

    std::vector<Candidate> cands;
    cands.reserve(256);

    for (int s = 0; s < kNumStrides; ++s) {
        const StridePlan& plan = plans[s];
        const float*      scores = routed[s * 3 + 0]->data;
        const float*      bboxes = routed[s * 3 + 1]->data;
        const float*      kps    = routed[s * 3 + 2]->data;

        for (int i = 0; i < plan.num_anchors; ++i) {
            // TRT-built SCRFD engine emits raw logits (sigmoid is not fused by
            // the FP16 builder). Apply sigmoid so the threshold is in [0, 1].
            const float logit = scores[i];
            const float score = 1.0f / (1.0f + std::exp(-logit));
            if (score < params.score_threshold) continue;

            const int point_idx = i / kNumAnchorsPerCell;
            const int cy        = point_idx / plan.feat_w;
            const int cx        = point_idx % plan.feat_w;

            const float anchor_x = static_cast<float>(cx) * plan.stride;
            const float anchor_y = static_cast<float>(cy) * plan.stride;

            const float* b = bboxes + i * 4;
            const float l = b[0] * plan.stride;
            const float t = b[1] * plan.stride;
            const float r = b[2] * plan.stride;
            const float btm = b[3] * plan.stride;

            const float nx1 = anchor_x - l;
            const float ny1 = anchor_y - t;
            const float nx2 = anchor_x + r;
            const float ny2 = anchor_y + btm;

            Candidate c{};
            auto [x1, y1] = to_image(nx1, ny1);
            auto [x2, y2] = to_image(nx2, ny2);
            c.x1    = x1;
            c.y1    = y1;
            c.x2    = x2;
            c.y2    = y2;
            c.score = score;

            const float* k = kps + i * 10;
            for (int j = 0; j < 5; ++j) {
                const float kxn = anchor_x + k[j * 2 + 0] * plan.stride;
                const float kyn = anchor_y + k[j * 2 + 1] * plan.stride;
                auto [kx, ky]  = to_image(kxn, kyn);
                c.kps[j]       = {kx, ky};
            }
            cands.push_back(c);
        }
    }

    if (cands.empty()) return {};

    std::sort(cands.begin(), cands.end(),
              [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

    std::vector<ScrfdDetection> out;
    out.reserve(std::min<std::size_t>(cands.size(), params.max_detections));
    std::vector<bool> suppressed(cands.size(), false);

    for (std::size_t i = 0; i < cands.size(); ++i) {
        if (suppressed[i]) continue;
        const Candidate& c = cands[i];
        ScrfdDetection d{};
        d.bbox.x1 = static_cast<int32_t>(std::lround(
            std::clamp(c.x1, 0.0f, static_cast<float>(params.image_width - 1))));
        d.bbox.y1 = static_cast<int32_t>(std::lround(
            std::clamp(c.y1, 0.0f, static_cast<float>(params.image_height - 1))));
        d.bbox.x2 = static_cast<int32_t>(std::lround(
            std::clamp(c.x2, 0.0f, static_cast<float>(params.image_width - 1))));
        d.bbox.y2 = static_cast<int32_t>(std::lround(
            std::clamp(c.y2, 0.0f, static_cast<float>(params.image_height - 1))));
        d.score   = c.score;
        d.landmarks = c.kps;
        out.push_back(d);

        if (static_cast<int32_t>(out.size()) >= params.max_detections) break;

        for (std::size_t j = i + 1; j < cands.size(); ++j) {
            if (!suppressed[j] && iou(c, cands[j]) > params.nms_iou_threshold) {
                suppressed[j] = true;
            }
        }
    }

    return out;
}

}  // namespace parking::inference
