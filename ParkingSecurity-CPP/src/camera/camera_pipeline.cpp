// Real DeepStream-backed camera pipeline.
//
// Architecture (Milestone 2 — source + decode + muxer + detector + appsink):
//
//   filesrc/v4l2src/rtspsrc → [demux/parse] → nvv4l2decoder → nvvideoconvert →
//       NV12 → nvstreammux → nvinfer (SCRFD, optional) → nvvideoconvert →
//       RGBA → appsink → frame callback
//
// When the detector is enabled (CameraConfig::enable_detector = true) we:
//   1. Insert `nvinfer config-file-path=<cfg>` right after nvstreammux.
//   2. Attach a pad probe on nvinfer's src pad. The probe walks the attached
//      NvDsBatchMeta → NvDsFrameMeta.obj_meta_list and emits a Detection for
//      each NvDsObjectMeta populated by libnvds_parsebbox_scrfd.so.
//
// Milestone 3 will add:    nvtracker → persistent track IDs (before nvvideoconvert)
// Milestone 4 will add:    second nvinfer (ArcFace) → embeddings

#include "parking/camera/camera_pipeline.hpp"

#include "parking/camera/mjpeg_server.hpp"
#include "parking/core/logger.hpp"
#include "parking/inference/embedding_index.hpp"
#include "parking/tracking/face_tracker.hpp"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#ifdef PARKING_USE_DEEPSTREAM
#include <gstnvdsinfer.h>
#include <gstnvdsmeta.h>
#include <nvdsinfer.h>
#include <nvdsmeta.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace parking::camera {

namespace {

/// Build a pipeline description string from the camera config.
std::string build_pipeline_string(const CameraConfig& cfg) {
    std::ostringstream p;

    std::string source = cfg.source_uri;
    bool is_rtsp = source.rfind("rtsp://", 0) == 0;
    bool is_v4l2 = source.rfind("v4l2://", 0) == 0;
    bool is_file = source.rfind("file://", 0) == 0;

    if (!is_rtsp && !is_v4l2 && !is_file) {
        if (source.rfind("/dev/video", 0) == 0) {
            source = "v4l2://" + source;
            is_v4l2 = true;
        } else {
            source = "file://" + source;
            is_file = true;
        }
    }

    // ── Source + decode ──────────────────────────────────────────
    if (is_v4l2) {
        const std::string device = source.substr(std::string{"v4l2://"}.size());
        if (cfg.mjpeg_input) {
            p << "v4l2src device=" << device << " io-mode=2 ! "
              << "image/jpeg,width=" << cfg.width
              << ",height=" << cfg.height
              << ",framerate=" << cfg.target_fps << "/1 ! "
              << "jpegparse ! nvv4l2decoder mjpeg=1 ! ";
        } else {
            p << "v4l2src device=" << device << " ! "
              << "video/x-raw,width=" << cfg.width
              << ",height=" << cfg.height << " ! "
              << "videoconvert ! ";
        }
    } else if (is_rtsp) {
        p << "rtspsrc location=" << source << " latency=100 ! "
          << "rtph264depay ! h264parse ! nvv4l2decoder ! ";
    } else {  // file
        const std::string path =
            is_file ? source.substr(std::string{"file://"}.size()) : source;
        p << "filesrc location=" << path << " ! "
          << "qtdemux ! h264parse ! nvv4l2decoder ! ";
    }

    // ── Normalize decoder output to NV12 on GPU ──────────────────
    // Different nvv4l2decoder backends may emit Y42B / I420 / NV12. nvinfer
    // only negotiates NV12 via nvstreammux, so force it here to keep caps
    // negotiation stable whether or not the detector is in the pipeline.
    p << "nvvideoconvert ! video/x-raw(memory:NVMM),format=NV12 ! ";

    // ── Batch muxer ──────────────────────────────────────────────
    // batched-push-timeout=40000 (40ms) prevents the known DS 7.1 nvinfer
    // cudaErrorIllegalAddress on Jetson that otherwise appears every few
    // minutes when the muxer occasionally waits too long for a batch.
    p << "m.sink_0 "
      << "nvstreammux name=m batch-size=1 width=" << cfg.width
      << " height=" << cfg.height
      << " live-source=" << (is_rtsp || is_v4l2 ? 1 : 0) << ' '
      << "batched-push-timeout=40000 "
      << "buffer-pool-size=4 "
      << "! ";

    // ── Primary detector (optional) ──────────────────────────────
    if (cfg.enable_detector && !cfg.detector_config_path.empty()) {
        p << "nvinfer name=detector config-file-path=" << cfg.detector_config_path
          << " ! ";
    }

    // ── Tracker ──────────────────────────────────────────────────
    // We intentionally do NOT insert DeepStream's nvtracker here. Instead the
    // pad probe runs a tiny C++ IoU tracker (parking::tracking::FaceTracker)
    // on the detector's NvDsObjectMeta output. This avoids the nvtracker
    // config/YAML complexity and keeps all per-frame logic in one place.

    // ── Secondary recognizer (optional, needs detector) ──────────
    if (cfg.enable_recognizer && cfg.enable_detector &&
        !cfg.recognizer_config_path.empty()) {
        p << "nvinfer name=recognizer "
          << "config-file-path=" << cfg.recognizer_config_path << " ! ";
    }

    // ── Optional visual-streaming branch ─────────────────────────
    // Adds nvdsosd (draws bboxes from NvDsObjectMeta) + tee, so one branch
    // feeds the main `sink` appsink and a parallel branch encodes JPEG for
    // the MJPEG HTTP server.
    if (cfg.enable_stream) {
        p << "nvvideoconvert ! "
          << "video/x-raw(memory:NVMM),format=RGBA ! "
          << "nvdsosd process-mode=0 ! "
          << "tee name=osd_t "
          // main branch → appsink
          << "osd_t. ! queue max-size-buffers=2 leaky=downstream ! "
          << "nvvideoconvert ! video/x-raw,format=RGBA ! "
          << "appsink name=sink emit-signals=true sync=false max-buffers=2 drop=true "
          // stream branch → JPEG appsink
          << "osd_t. ! queue max-size-buffers=2 leaky=downstream ! "
          << "nvvideoconvert ! video/x-raw,format=I420 ! "
          << "jpegenc quality=" << cfg.stream_jpeg_quality << " ! "
          << "appsink name=jpeg emit-signals=true sync=false max-buffers=2 drop=true";
    } else {
        // ── Convert on GPU → drop to CPU RGBA → appsink ──────────
        p << "nvvideoconvert ! "
          << "video/x-raw,format=RGBA ! "
          << "appsink name=sink emit-signals=true sync=false max-buffers=2 drop=true";
    }

    return p.str();
}

}  // namespace

// ────────────────────────────────────────────────────────────────
// Impl
// ────────────────────────────────────────────────────────────────

class CameraPipeline::Impl {
public:
    explicit Impl(CameraConfig config) : config_(std::move(config)) {}
    ~Impl() { stop(); }

    bool start();
    void stop();

    void set_detection_callback(DetectionCallback cb) {
        std::lock_guard<std::mutex> l(mu_);
        on_detection_ = std::move(cb);
    }
    void set_recognition_callback(RecognitionCallback cb) {
        std::lock_guard<std::mutex> l(mu_);
        on_recognition_ = std::move(cb);
    }
    void set_frame_callback(FrameCallback cb) {
        std::lock_guard<std::mutex> l(mu_);
        on_frame_ = std::move(cb);
    }
    void set_error_callback(ErrorCallback cb) {
        std::lock_guard<std::mutex> l(mu_);
        on_error_ = std::move(cb);
    }
    void set_embedding_index(const inference::EmbeddingIndex* idx, float thr) {
        std::lock_guard<std::mutex> l(mu_);
        embedding_index_       = idx;
        recognition_threshold_ = thr;
    }

    Stats stats() const {
        std::lock_guard<std::mutex> l(mu_);
        return stats_;
    }

    std::vector<uint8_t> latest_frame_jpeg() const {
        std::lock_guard<std::mutex> lock(latest_frame_mu_);
        return latest_frame_jpeg_;
    }

private:
    static GstFlowReturn on_new_sample_static(GstElement* appsink, gpointer user_data);
    GstFlowReturn        handle_sample(GstElement* appsink);

    static GstFlowReturn on_new_jpeg_static(GstElement* appsink, gpointer user_data);
    GstFlowReturn        handle_jpeg_sample(GstElement* appsink);

    static gboolean bus_callback_static(GstBus*, GstMessage* msg, gpointer user_data);
    gboolean        handle_bus_message(GstMessage* msg);

#ifdef PARKING_USE_DEEPSTREAM
    static GstPadProbeReturn detector_src_probe_static(GstPad* pad,
                                                        GstPadProbeInfo* info,
                                                        gpointer user_data);
    GstPadProbeReturn        handle_detector_probe(GstBuffer* buffer);
#endif

    void main_loop_thread();

    CameraConfig          config_;
    GstElement*           pipeline_  = nullptr;
    GMainLoop*            main_loop_ = nullptr;
    std::thread           loop_thread_;
    std::atomic<bool>     running_{false};

    mutable std::mutex  mu_;
    DetectionCallback   on_detection_;
    RecognitionCallback on_recognition_;
    FrameCallback       on_frame_;
    ErrorCallback       on_error_;
    const inference::EmbeddingIndex* embedding_index_{nullptr};
    float                            recognition_threshold_{0.5f};
    Stats               stats_{};

    std::chrono::steady_clock::time_point last_frame_time_{};
    double                                smoothed_fps_{0.0};

    // Lazily-allocated IoU tracker — only constructed when enable_tracker=true.
    std::unique_ptr<tracking::FaceTracker> face_tracker_;

    // Optional MJPEG HTTP server (enable_stream=true). The jpeg appsink
    // callback pushes encoded frames here; the server fans out to clients.
    std::unique_ptr<MjpegServer> mjpeg_server_;

    // Last JPEG pushed by the jpeg appsink. EventPublisher reads this when
    // it needs a face/frame thumbnail for a newly-persisted event.
    mutable std::mutex     latest_frame_mu_;
    std::vector<uint8_t>   latest_frame_jpeg_;
};

bool CameraPipeline::Impl::start() {
    if (running_.exchange(true)) {
        PLOG_WARN(camera, "start() called while already running");
        return true;
    }

    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }

    if (config_.enable_tracker && !face_tracker_) {
        tracking::FaceTracker::Config tcfg{};
        tcfg.iou_threshold = 0.3f;
        tcfg.max_age       = 30;   // ~1s at 30 FPS
        tcfg.min_hits      = 2;    // confirm after 2 consecutive matches
        face_tracker_ = std::make_unique<tracking::FaceTracker>(tcfg);
    }

    if (config_.enable_stream && !mjpeg_server_) {
        mjpeg_server_ = std::make_unique<MjpegServer>();
        if (!mjpeg_server_->start(config_.stream_port)) {
            PLOG_ERROR(camera, "MJPEG server failed to bind port {}",
                       config_.stream_port);
            mjpeg_server_.reset();
        }
    }

    const std::string pipeline_str = build_pipeline_string(config_);
    PLOG_INFO(camera, "Pipeline: {}", pipeline_str);

    GError* err = nullptr;
    pipeline_ = gst_parse_launch(pipeline_str.c_str(), &err);
    if (!pipeline_ || err != nullptr) {
        std::string msg = err ? err->message : "unknown";
        if (err) g_error_free(err);
        PLOG_ERROR(camera, "gst_parse_launch failed: {}", msg);
        running_.store(false);
        return false;
    }

    // Wire appsink callback
    GstElement* appsink = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
    if (!appsink) {
        PLOG_ERROR(camera, "appsink 'sink' not found in pipeline");
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        running_.store(false);
        return false;
    }
    g_signal_connect(appsink, "new-sample",
                     G_CALLBACK(&Impl::on_new_sample_static), this);
    gst_object_unref(appsink);

    // Wire jpeg appsink (present only when enable_stream=true).
    if (config_.enable_stream) {
        GstElement* jpeg = gst_bin_get_by_name(GST_BIN(pipeline_), "jpeg");
        if (!jpeg) {
            PLOG_ERROR(camera, "jpeg appsink not found (enable_stream=true)");
        } else {
            g_signal_connect(jpeg, "new-sample",
                             G_CALLBACK(&Impl::on_new_jpeg_static), this);
            gst_object_unref(jpeg);
            PLOG_INFO(camera, "Attached JPEG appsink → MJPEG server");
        }
    }

#ifdef PARKING_USE_DEEPSTREAM
    // Attach the pad probe to whichever is the LAST inference stage in the
    // pipeline — that's the recognizer when it's enabled (embedding ready
    // post-inference), else the detector.
    if (config_.enable_detector) {
        const char* probe_elem_name =
            config_.enable_recognizer ? "recognizer" : "detector";
        GstElement* elem = gst_bin_get_by_name(GST_BIN(pipeline_), probe_elem_name);
        if (!elem) {
            PLOG_ERROR(camera, "Probe element '{}' not found", probe_elem_name);
        } else {
            GstPad* src_pad = gst_element_get_static_pad(elem, "src");
            if (src_pad) {
                gst_pad_add_probe(
                    src_pad, GST_PAD_PROBE_TYPE_BUFFER,
                    &Impl::detector_src_probe_static, this, nullptr);
                gst_object_unref(src_pad);
                PLOG_INFO(camera, "Attached face pad probe on {} src pad",
                          probe_elem_name);
            } else {
                PLOG_ERROR(camera, "Could not get {} src pad", probe_elem_name);
            }
            gst_object_unref(elem);
        }
    }
#endif

    // Wire bus watcher
    GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline_));
    gst_bus_add_watch(bus, &Impl::bus_callback_static, this);
    gst_object_unref(bus);

    main_loop_ = g_main_loop_new(nullptr, FALSE);

    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        PLOG_ERROR(camera, "Failed to set pipeline to PLAYING");
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        g_main_loop_unref(main_loop_);
        main_loop_ = nullptr;
        running_.store(false);
        return false;
    }

    loop_thread_ = std::thread(&Impl::main_loop_thread, this);
    PLOG_INFO(camera, "Pipeline started");
    return true;
}

void CameraPipeline::Impl::stop() {
    if (!running_.exchange(false)) return;

    PLOG_INFO(camera, "Stopping pipeline");

    if (main_loop_ && g_main_loop_is_running(main_loop_)) {
        g_main_loop_quit(main_loop_);
    }

    if (loop_thread_.joinable()) {
        loop_thread_.join();
    }

    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }

    if (main_loop_) {
        g_main_loop_unref(main_loop_);
        main_loop_ = nullptr;
    }

    if (mjpeg_server_) {
        mjpeg_server_->stop();
        mjpeg_server_.reset();
    }
}

void CameraPipeline::Impl::main_loop_thread() {
    g_main_loop_run(main_loop_);
}

GstFlowReturn CameraPipeline::Impl::on_new_sample_static(GstElement* appsink, gpointer ud) {
    return static_cast<Impl*>(ud)->handle_sample(appsink);
}

GstFlowReturn CameraPipeline::Impl::on_new_jpeg_static(GstElement* appsink, gpointer ud) {
    return static_cast<Impl*>(ud)->handle_jpeg_sample(appsink);
}

GstFlowReturn CameraPipeline::Impl::handle_jpeg_sample(GstElement* appsink) {
    GstSample* sample = nullptr;
    g_signal_emit_by_name(appsink, "pull-sample", &sample);
    if (!sample) return GST_FLOW_ERROR;

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    if (buffer) {
        GstMapInfo map;
        if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            if (mjpeg_server_) mjpeg_server_->publish(map.data, map.size);
            {
                std::lock_guard<std::mutex> lock(latest_frame_mu_);
                latest_frame_jpeg_.assign(map.data, map.data + map.size);
            }
            gst_buffer_unmap(buffer, &map);
        }
    }
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

GstFlowReturn CameraPipeline::Impl::handle_sample(GstElement* appsink) {
    GstSample* sample = nullptr;
    g_signal_emit_by_name(appsink, "pull-sample", &sample);
    if (!sample) return GST_FLOW_ERROR;

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstCaps*   caps   = gst_sample_get_caps(sample);
    if (buffer && caps) {
        GstVideoInfo info;
        gst_video_info_from_caps(&info, caps);

        const auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> l(mu_);
            stats_.frames_captured++;
            stats_.frames_processed++;
            if (last_frame_time_.time_since_epoch().count() > 0) {
                auto dt = std::chrono::duration<double>(now - last_frame_time_).count();
                if (dt > 0.0) {
                    double instant_fps = 1.0 / dt;
                    smoothed_fps_ = (smoothed_fps_ == 0.0)
                                        ? instant_fps
                                        : 0.9 * smoothed_fps_ + 0.1 * instant_fps;
                    stats_.current_fps = smoothed_fps_;
                }
            }
            last_frame_time_ = now;
        }

        FrameCallback cb;
        {
            std::lock_guard<std::mutex> l(mu_);
            cb = on_frame_;
        }
        if (cb) {
            Frame f{};
            f.sequence    = stats_.frames_captured;
            f.captured_at = std::chrono::time_point_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now());
            f.width         = info.width;
            f.height        = info.height;
            f.buffer_handle = buffer;
            cb(f);
        }
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

gboolean CameraPipeline::Impl::bus_callback_static(GstBus*, GstMessage* msg, gpointer ud) {
    return static_cast<Impl*>(ud)->handle_bus_message(msg);
}

gboolean CameraPipeline::Impl::handle_bus_message(GstMessage* msg) {
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            PLOG_INFO(camera, "End of stream");
            if (main_loop_) g_main_loop_quit(main_loop_);
            break;

        case GST_MESSAGE_ERROR: {
            GError* err = nullptr;
            gchar*  dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            std::string emsg = err ? err->message : "unknown";
            std::string dmsg = dbg ? dbg : "";
            PLOG_ERROR(camera, "Pipeline error: {} ({})", emsg, dmsg);
            if (err) g_error_free(err);
            g_free(dbg);

            ErrorCallback cb;
            {
                std::lock_guard<std::mutex> l(mu_);
                cb = on_error_;
            }
            if (cb) cb(emsg);

            if (main_loop_) g_main_loop_quit(main_loop_);
            break;
        }

        case GST_MESSAGE_WARNING: {
            GError* warn = nullptr;
            gst_message_parse_warning(msg, &warn, nullptr);
            if (warn) {
                PLOG_WARN(camera, "Pipeline warning: {}", warn->message);
                g_error_free(warn);
            }
            break;
        }

        default:
            break;
    }
    return TRUE;
}

// ────────────────────────────────────────────────────────────────
// SCRFD detector probe
// ────────────────────────────────────────────────────────────────

#ifdef PARKING_USE_DEEPSTREAM

GstPadProbeReturn CameraPipeline::Impl::detector_src_probe_static(
    GstPad* /*pad*/, GstPadProbeInfo* info, gpointer user_data) {
    auto* buffer = static_cast<GstBuffer*>(info->data);
    if (!buffer) return GST_PAD_PROBE_OK;
    return static_cast<Impl*>(user_data)->handle_detector_probe(buffer);
}

GstPadProbeReturn CameraPipeline::Impl::handle_detector_probe(GstBuffer* buffer) {
    NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buffer);
    if (!batch_meta) return GST_PAD_PROBE_OK;

    DetectionCallback   on_det;
    RecognitionCallback on_rec_local;
    {
        std::lock_guard<std::mutex> l(mu_);
        on_det       = on_detection_;
        on_rec_local = on_recognition_;
    }
    // Continue if EITHER callback is set OR an embedding index is attached
    // (we still want to run recognition and mutate NvDsObjectMeta.text_params
    // for the OSD overlay even when nobody subscribed to the detection hook).
    const bool any_sink =
        static_cast<bool>(on_det) ||
        static_cast<bool>(on_rec_local) ||
        (embedding_index_ != nullptr);
    if (!any_sink) return GST_PAD_PROBE_OK;

    // Snapshot recognition state once for this batch.
    const inference::EmbeddingIndex* idx;
    float                            thr;
    {
        std::lock_guard<std::mutex> l(mu_);
        idx = embedding_index_;
        thr = recognition_threshold_;
    }

    for (NvDsMetaList* l_frame = batch_meta->frame_meta_list;
         l_frame != nullptr;
         l_frame = l_frame->next) {
        auto* frame_meta = static_cast<NvDsFrameMeta*>(l_frame->data);
        if (!frame_meta) continue;

        const int img_w = frame_meta->source_frame_width > 0
                              ? static_cast<int>(frame_meta->source_frame_width)
                              : config_.width;
        const int img_h = frame_meta->source_frame_height > 0
                              ? static_cast<int>(frame_meta->source_frame_height)
                              : config_.height;

        // Gather detections and keep a pointer back to each NvDsObjectMeta
        // so we can later overwrite its text_params (for nvdsosd overlay)
        // with the recognised name + similarity.
        struct Row { Detection d; NvDsObjectMeta* obj; };
        std::vector<Row> rows;
        rows.reserve(16);
        for (NvDsMetaList* l_obj = frame_meta->obj_meta_list;
             l_obj != nullptr;
             l_obj = l_obj->next) {
            auto* obj = static_cast<NvDsObjectMeta*>(l_obj->data);
            if (!obj) continue;

            const auto& r = obj->rect_params;
            Detection d{};
            d.bbox.x1 = static_cast<int32_t>(std::lround(std::clamp(
                r.left, 0.0f, static_cast<float>(img_w - 1))));
            d.bbox.y1 = static_cast<int32_t>(std::lround(std::clamp(
                r.top,  0.0f, static_cast<float>(img_h - 1))));
            d.bbox.x2 = static_cast<int32_t>(std::lround(std::clamp(
                r.left + r.width, 0.0f, static_cast<float>(img_w - 1))));
            d.bbox.y2 = static_cast<int32_t>(std::lround(std::clamp(
                r.top  + r.height, 0.0f, static_cast<float>(img_h - 1))));
            d.detection_score = obj->confidence;
            d.captured_at     = std::chrono::time_point_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now());

            // ArcFace secondary GIE attaches a 512-d embedding as user meta.
            for (NvDsMetaList* l_user = obj->obj_user_meta_list;
                 l_user != nullptr;
                 l_user = l_user->next) {
                auto* user_meta = static_cast<NvDsUserMeta*>(l_user->data);
                if (!user_meta ||
                    user_meta->base_meta.meta_type != NVDSINFER_TENSOR_OUTPUT_META) {
                    continue;
                }
                auto* tensor_meta =
                    static_cast<NvDsInferTensorMeta*>(user_meta->user_meta_data);
                if (!tensor_meta || tensor_meta->num_output_layers == 0) continue;
                if (tensor_meta->unique_id != 2) continue;

                const NvDsInferLayerInfo& layer = tensor_meta->output_layers_info[0];
                if (layer.dataType != FLOAT) continue;
                const float* src =
                    static_cast<const float*>(tensor_meta->out_buf_ptrs_host[0]);
                if (!src) continue;

                std::size_t avail = 1;
                for (unsigned int i = 0; i < layer.inferDims.numDims; ++i) {
                    avail *= layer.inferDims.d[i];
                }
                const std::size_t copy_n = std::min(d.embedding.size(), avail);
                for (std::size_t i = 0; i < copy_n; ++i) d.embedding[i] = src[i];
                break;
            }
            // Reject bboxes that touch the image border — those are partial
            // faces that produce garbage embeddings and noisy events.
            constexpr int kEdgeMargin = 30;
            const bool touches_edge =
                d.bbox.x1 <= kEdgeMargin ||
                d.bbox.y1 <= kEdgeMargin ||
                d.bbox.x2 >= img_w - kEdgeMargin ||
                d.bbox.y2 >= img_h - kEdgeMargin;
            if (touches_edge) continue;

            rows.push_back({d, obj});
        }

        // IoU tracker → persistent track_id.
        if (face_tracker_) {
            std::vector<tracking::FaceTracker::Input> inputs;
            inputs.reserve(rows.size());
            for (const auto& r : rows) inputs.push_back({r.d.bbox, r.d.detection_score});
            const auto tracks = face_tracker_->update(inputs);
            for (auto& r : rows) {
                for (const auto& t : tracks) {
                    if (t.age == 0 &&
                        t.bbox.x1 == r.d.bbox.x1 && t.bbox.y1 == r.d.bbox.y1 &&
                        t.bbox.x2 == r.d.bbox.x2 && t.bbox.y2 == r.d.bbox.y2) {
                        r.d.track_id = t.track_id;
                        break;
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> l(mu_);
            stats_.detections += rows.size();
        }

        // For each detection: fire callbacks (if any) + draw name in the OSD.
        for (auto& r : rows) {
            // Detection callback (only if subscribed).
            if (on_det) on_det(r.d);

            // Recognition: need an index AND a non-zero embedding.
            const bool has_embedding =
                std::any_of(r.d.embedding.begin(), r.d.embedding.end(),
                            [](float v) { return v != 0.0f; });

            RecognitionResult rec{};
            rec.detection = r.d;

            if (idx && has_embedding) {
                // Always query top-1 so we get the real similarity even when
                // no enrolled person clears the threshold. An unknown face is
                // often 0.2-0.4 cosine-similar to the nearest person; writing
                // 0.0 (as before) throws away that signal.
                const auto top = idx->query_top_k(r.d.embedding, 1);
                if (!top.empty() && top[0].score >= thr) {
                    rec.event_type       = EventType::Authorized;
                    rec.person_id        = top[0].person_id;
                    rec.display_name     = top[0].display_name;
                    rec.person_type      = top[0].person_type;
                    rec.similarity_score = top[0].score;
                } else {
                    rec.event_type       = EventType::Unknown;
                    rec.similarity_score = top.empty() ? 0.0f : top[0].score;
                }

                if (on_rec_local) on_rec_local(rec);
            } else {
                // No embedding (tiny face) OR no index: still mark as Unknown
                // so the OSD reflects reality.
                rec.event_type       = EventType::Unknown;
                rec.similarity_score = 0.0f;
            }

            // Rewrite the OSD label so nvdsosd renders the recognized name
            // (or "Unknown") next to the bbox. nvdsosd owns display_text and
            // frees it with g_free(), so we must use g_strdup.
            char label[96];
            if (rec.event_type != EventType::Unknown && rec.display_name) {
                std::snprintf(label, sizeof(label), "%s  %.2f",
                              rec.display_name->c_str(), rec.similarity_score);
                // Green border for known people.
                r.obj->rect_params.border_color = NvOSD_ColorParams{0.0f, 1.0f, 0.0f, 1.0f};
            } else {
                std::snprintf(label, sizeof(label), "Unknown");
                // Red border for unknowns.
                r.obj->rect_params.border_color = NvOSD_ColorParams{1.0f, 0.2f, 0.2f, 1.0f};
            }
            if (r.obj->text_params.display_text) {
                g_free(r.obj->text_params.display_text);
            }
            r.obj->text_params.display_text = g_strdup(label);
        }
    }

    return GST_PAD_PROBE_OK;
}

#endif  // PARKING_USE_DEEPSTREAM

// ────────────────────────────────────────────────────────────────
// Public interface
// ────────────────────────────────────────────────────────────────

CameraPipeline::CameraPipeline(CameraConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

CameraPipeline::~CameraPipeline() = default;

void CameraPipeline::set_detection_callback(DetectionCallback cb) noexcept {
    impl_->set_detection_callback(std::move(cb));
}
void CameraPipeline::set_recognition_callback(RecognitionCallback cb) noexcept {
    impl_->set_recognition_callback(std::move(cb));
}
void CameraPipeline::set_frame_callback(FrameCallback cb) noexcept {
    impl_->set_frame_callback(std::move(cb));
}
void CameraPipeline::set_error_callback(ErrorCallback cb) noexcept {
    impl_->set_error_callback(std::move(cb));
}
void CameraPipeline::set_embedding_index(const inference::EmbeddingIndex* idx,
                                          float threshold) noexcept {
    impl_->set_embedding_index(idx, threshold);
}

bool CameraPipeline::start() {
    bool ok = impl_->start();
    running_.store(ok);
    return ok;
}

void CameraPipeline::stop() {
    impl_->stop();
    running_.store(false);
}

std::vector<uint8_t> CameraPipeline::capture_snapshot(int /*quality*/) {
    // Alias for latest_frame_jpeg(). The stream branch already JPEG-encodes
    // every frame via nvdsosd + jpegenc so we just return the cached copy.
    return latest_frame_jpeg();
}

std::vector<uint8_t> CameraPipeline::latest_frame_jpeg() const {
    return impl_->latest_frame_jpeg();
}

CameraPipeline::Stats CameraPipeline::stats() const { return impl_->stats(); }

}  // namespace parking::camera
