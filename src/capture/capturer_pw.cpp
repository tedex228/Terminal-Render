#include "capture/capturer_pw.h"
#define STB_IMAGE_IMPLEMENTATION
#include "common/stb_image.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <spa/param/video/raw-utils.h>
#include <spa/debug/types.h>
#include <gbm.h>
#include <filesystem>

#define DEST "org.freedesktop.portal.Desktop"
#define OPATH "/org/freedesktop/portal/desktop"

static sd_bus_message* mkMsg(sd_bus* bus, const char* iface, const char* method) {
    sd_bus_message* m = nullptr;
    sd_bus_message_new_method_call(bus, &m, DEST, OPATH, iface, method);
    return m;
}

int PipewireCapturer::onResp(sd_bus_message* msg, void* userdata, sd_bus_error* err) {
    (void)err;
    auto* r = (Resp*)userdata;
    uint32_t result;
    sd_bus_message_read(msg, "u", &result);
    r->result = result;
    if (result != 0) { r->done = true; return 0; }

    sd_bus_message_enter_container(msg, 'a', "{sv}");
    while (sd_bus_message_enter_container(msg, 'e', "sv") > 0) {
        const char* key;
        sd_bus_message_read(msg, "s", &key);
        sd_bus_message_enter_container(msg, 'v', nullptr);
        if (strcmp(key, "session_handle") == 0) {
            const char* val;
            sd_bus_message_read(msg, "s", &val);
            r->sh = val;
        } else if (strcmp(key, "pipewire_handle") == 0) {
            int fd;
            sd_bus_message_read(msg, "h", &fd);
            r->fd = fcntl(fd, F_DUPFD, 3);
            close(fd);
        } else {
            const char* st = nullptr;
            sd_bus_message_peek_type(msg, nullptr, &st);
            sd_bus_message_skip(msg, st);
        }
        sd_bus_message_exit_container(msg);
        sd_bus_message_exit_container(msg);
    }
    sd_bus_message_exit_container(msg);
    r->done = true;
    return 0;
}

bool PipewireCapturer::waitResp(const char* match, Resp& out, int t) {
    sd_bus_add_match(bus, nullptr, match, onResp, &out);
    for (int i = 0; i < t * 2 && !out.done; ++i) {
        int r = sd_bus_process(bus, nullptr);
        if (r < 0) break;
        if (r == 0) sd_bus_wait(bus, 500000);
    }
    return out.done && out.result == 0;
}

bool PipewireCapturer::dbusCreate() {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* msg = mkMsg(bus, "org.freedesktop.portal.ScreenCast", "CreateSession");
    if (!msg) return false;

    const char* token = "tr1";
    sd_bus_message_append(msg, "a{sv}", 1);
    sd_bus_message_open_container(msg, 'e', "sv");
    sd_bus_message_append(msg, "s", "session_handle_token");
    sd_bus_message_open_container(msg, 'v', "s");
    sd_bus_message_append(msg, "s", token);
    sd_bus_message_close_container(msg);
    sd_bus_message_close_container(msg);
    sd_bus_message_close_container(msg);

    sd_bus_message* reply = nullptr;
    if (sd_bus_call(bus, msg, 0, &err, &reply) < 0) {
        sd_bus_error_free(&err); sd_bus_message_unref(msg); return false;
    }
    const char* req_path;
    sd_bus_message_read(reply, "o", &req_path);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    sd_bus_message_unref(msg);

    char match[512];
    snprintf(match, sizeof(match),
             "interface='org.freedesktop.portal.Request',member='Response',path='%s'", req_path);
    Resp resp;
    if (!waitResp(match, resp) || resp.sh.empty()) return false;
    session_handle = resp.sh;
    fprintf(stderr, "Session: %s\n", session_handle.c_str());
    return true;
}

bool PipewireCapturer::dbusSelect() {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* msg = mkMsg(bus, "org.freedesktop.portal.ScreenCast", "SelectSources");
    if (!msg) return false;

    sd_bus_message_append(msg, "o", session_handle.c_str());
    sd_bus_message_append(msg, "a{sv}", 1);
    sd_bus_message_open_container(msg, 'e', "sv");
    sd_bus_message_append(msg, "s", "types");

    sd_bus_message_open_container(msg, 'v', "a{u(osa{sv})}");
    sd_bus_message_open_container(msg, 'a', "{u(osa{sv})}");
    sd_bus_message_open_container(msg, 'e', "u(osa{sv})");
    uint32_t st = 1;
    sd_bus_message_append(msg, "u", st);
    sd_bus_message_open_container(msg, 'r', "osa{sv}");
    sd_bus_message_append(msg, "o", "/");
    sd_bus_message_append(msg, "s", "");
    sd_bus_message_append(msg, "a{sv}", 0);
    sd_bus_message_close_container(msg);
    sd_bus_message_close_container(msg);
    sd_bus_message_close_container(msg);
    sd_bus_message_close_container(msg);
    sd_bus_message_close_container(msg);
    sd_bus_message_close_container(msg);

    sd_bus_message* reply = nullptr;
    if (sd_bus_call(bus, msg, 0, &err, &reply) < 0) {
        sd_bus_error_free(&err); sd_bus_message_unref(msg); return false;
    }
    const char* req_path;
    sd_bus_message_read(reply, "o", &req_path);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    sd_bus_message_unref(msg);

    char match[512];
    snprintf(match, sizeof(match),
             "interface='org.freedesktop.portal.Request',member='Response',path='%s'", req_path);
    Resp resp;
    return waitResp(match, resp);
}

bool PipewireCapturer::dbusStart() {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* msg = mkMsg(bus, "org.freedesktop.portal.ScreenCast", "Start");
    if (!msg) return false;

    sd_bus_message_append(msg, "o", session_handle.c_str());
    sd_bus_message_append(msg, "a{sv}", 0);

    sd_bus_message* reply = nullptr;
    if (sd_bus_call(bus, msg, 0, &err, &reply) < 0) {
        sd_bus_error_free(&err); sd_bus_message_unref(msg); return false;
    }
    const char* req_path;
    sd_bus_message_read(reply, "o", &req_path);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    sd_bus_message_unref(msg);

    char match[512];
    snprintf(match, sizeof(match),
             "interface='org.freedesktop.portal.Request',member='Response',path='%s'", req_path);
    Resp resp;
    if (!waitResp(match, resp) || resp.fd < 0) return false;
    return pwConnect(resp.fd);
}

// ─── PipeWire ──────────────────────────────────────────────────

void PipewireCapturer::onProc(void* data) {
    auto* cap = (PipewireCapturer*)data;
    pw_buffer* b = pw_stream_dequeue_buffer(cap->pws);
    if (!b) return;
    cap->onFrame(b->buffer);
    pw_stream_queue_buffer(cap->pws, b);
}

void PipewireCapturer::onFrame(struct spa_buffer* buf) {
    if (buf->n_datas < 1) return;
    struct spa_data* d = &buf->datas[0];
    if (d->chunk->size == 0) return;

    void* pixels = d->data;
    void* map_data = nullptr;
    struct gbm_bo* gbm_bo = nullptr;
    struct gbm_device* gbm_dev = nullptr;
    uint32_t stride = d->chunk->stride;
    uint32_t width = src_w, height = src_h;

    if (stride == 0) stride = d->chunk->size;
    if (width <= 0 || height <= 0) {
        width = stride / 4;
        height = (uint32_t)(d->chunk->size / stride);
    }
    if (width <= 0 || height <= 0) return;

    if (d->type == SPA_DATA_DmaBuf) {
        gbm_dev = gbm_create_device(d->fd);
        if (!gbm_dev) return;
        struct gbm_import_fd_data fd_data;
        fd_data.fd = d->fd;
        fd_data.width = width;
        fd_data.height = height;
        fd_data.stride = stride;
        fd_data.format = GBM_FORMAT_ARGB8888;
        gbm_bo = gbm_bo_import(gbm_dev, GBM_BO_IMPORT_FD, &fd_data, sizeof(fd_data));
        if (!gbm_bo) { gbm_device_destroy(gbm_dev); return; }
        uint32_t ms;
        pixels = gbm_bo_map(gbm_bo, 0, 0, width, height, GBM_BO_TRANSFER_READ, &ms, &map_data);
        if (pixels) stride = ms;
    }

    if (!pixels) return;

    Frame fr(width, height);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t* p = (uint8_t*)pixels + y * stride + x * 4;
            fr.at(x, y) = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
        }
    }

    if (gbm_bo) { gbm_bo_unmap(gbm_bo, map_data); gbm_bo_destroy(gbm_bo); gbm_device_destroy(gbm_dev); }

    std::lock_guard<std::mutex> lk(mtx);
    src_w = width; src_h = height;
    latest = std::move(fr);
}

bool PipewireCapturer::pwConnect(int fd) {
    pw_init(nullptr, nullptr);
    loop = pw_main_loop_new(nullptr);
    if (!loop) { close(fd); return false; }
    pw_ctx = pw_context_new(pw_main_loop_get_loop(loop), nullptr, 0);
    if (!pw_ctx) { close(fd); return false; }
    pw_core_ = pw_context_connect_fd(pw_ctx, fd, nullptr, 0);
    if (!pw_core_) { close(fd); return false; }

    pws = pw_stream_new(pw_core_, "screencast", nullptr);
    if (!pws) return false;

    struct pw_stream_events ev = {};
    ev.version = PW_VERSION_STREAM_EVENTS;
    ev.process = onProc;
    pw_stream_add_listener(pws, &stream_listener, &ev, this);

    struct spa_video_info_raw info;
    memset(&info, 0, sizeof(info));
    info.format = SPA_VIDEO_FORMAT_RGBx;
    info.size = SPA_RECTANGLE(640, 480);
    info.framerate = SPA_FRACTION(0, 1);

    uint8_t params_buf[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(params_buf, sizeof(params_buf));
    const struct spa_pod* params[1];
    params[0] = spa_format_video_raw_build(&b, SPA_PARAM_EnumFormat, &info);

    uint32_t flags = PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS;
    if (pw_stream_connect(pws, PW_DIRECTION_INPUT, PW_ID_ANY, (pw_stream_flags)flags,
                          params, params[0] ? 1 : 0) < 0) {
        fprintf(stderr, "pw_stream_connect failed\n");
        return false;
    }

    streaming = true;
    std::thread([this]() { pw_main_loop_run(loop); }).detach();
    fprintf(stderr, "PipeWire streaming\n");
    return true;
}

// ─── Public API ────────────────────────────────────────────────

bool PipewireCapturer::init() {
    if (sd_bus_open_user(&bus) < 0) { fprintf(stderr, "D-Bus failed\n"); return false; }
    return true;
}

bool PipewireCapturer::selectWindow() {
    fprintf(stderr, "ScreenCast session...\n");
    if (dbusCreate() && dbusSelect()) {
        fprintf(stderr, "ScreenCast ready\n");
        return true;
    }
    fprintf(stderr, "ScreenCast failed, fallback to Screenshot\n");
    session_handle.clear();
    return true;
}

Frame PipewireCapturer::capture() {
    if (!session_handle.empty() && !streaming) {
        fprintf(stderr, "Start stream...\n");
        if (!dbusStart()) {
            fprintf(stderr, "Start failed, fallback\n");
            session_handle.clear();
        }
    }
    if (streaming) {
        std::lock_guard<std::mutex> lk(mtx);
        if (latest.width > 0 && latest.height > 0) return latest;
    }
    return screenshotFallback();
}

// ─── Screenshot fallback ───────────────────────────────────────

static std::string uriToPath(const std::string& uri) {
    if (uri.substr(0, 7) != "file://") return uri;
    std::string r;
    r.reserve(uri.size());
    for (size_t i = 7; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            char hex[3] = {uri[i+1], uri[i+2], 0};
            r += (char)strtol(hex, nullptr, 16);
            i += 2;
        } else if (uri[i] == '+') { r += ' '; }
        else { r += uri[i]; }
    }
    return r;
}

static std::vector<unsigned char> readWhole(const std::string& p) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return {}; }
    rewind(f);
    std::vector<unsigned char> buf(sz);
    if ((long)fread(buf.data(), 1, sz, f) != sz) { fclose(f); return {}; }
    fclose(f);
    return buf;
}

struct ShotCtx { std::string uri; bool done = false; };
static int onShot(sd_bus_message* msg, void* u, sd_bus_error* e) {
    (void)e; auto* c = (ShotCtx*)u;
    uint32_t result;
    sd_bus_message_read(msg, "u", &result);
    if (result != 0) { c->done = true; return 0; }
    sd_bus_message_enter_container(msg, 'a', "{sv}");
    while (sd_bus_message_enter_container(msg, 'e', "sv") > 0) {
        const char* key;
        sd_bus_message_read(msg, "s", &key);
        sd_bus_message_enter_container(msg, 'v', nullptr);
        if (strcmp(key, "uri") == 0) {
            const char* val;
            sd_bus_message_read(msg, "s", &val);
            c->uri = val;
        } else { const char* st; sd_bus_message_peek_type(msg, nullptr, &st); sd_bus_message_skip(msg, st); }
        sd_bus_message_exit_container(msg);
        sd_bus_message_exit_container(msg);
    }
    sd_bus_message_exit_container(msg);
    c->done = true;
    return 0;
}

Frame PipewireCapturer::screenshotFallback() {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* m = mkMsg(bus, "org.freedesktop.portal.Screenshot", "Screenshot");
    if (!m) return {};
    sd_bus_message_append(m, "sa{sv}", "", 1);
    sd_bus_message_open_container(m, 'e', "sv");
    sd_bus_message_append(m, "s", "interactive");
    int f = 0;
    sd_bus_message_append(m, "v", "b", &f);
    sd_bus_message_close_container(m);
    sd_bus_message_close_container(m);
    sd_bus_message* reply = nullptr;
    if (sd_bus_call(bus, m, 0, &err, &reply) < 0) { sd_bus_error_free(&err); sd_bus_message_unref(m); return {}; }
    const char* req_path;
    sd_bus_message_read(reply, "o", &req_path);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    sd_bus_message_unref(m);

    char match[512];
    snprintf(match, sizeof(match),
             "interface='org.freedesktop.portal.Request',member='Response',path='%s'", req_path);
    ShotCtx ctx;
    sd_bus_add_match(bus, nullptr, match, onShot, &ctx);
    for (int i = 0; i < 30 * 2 && !ctx.done; ++i) {
        int r = sd_bus_process(bus, nullptr);
        if (r < 0) break;
        if (r == 0) sd_bus_wait(bus, 500000);
    }
    if (!ctx.done || ctx.uri.empty()) return {};

    std::string path = uriToPath(ctx.uri);
    auto data = readWhole(path);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (data.empty()) return {};

    int w, h, ch;
    unsigned char* px = stbi_load_from_memory(data.data(), (int)data.size(), &w, &h, &ch, 3);
    if (!px) return {};

    Frame fr(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int o = (y * w + x) * 3;
            fr.at(x, y) = ((uint32_t)px[o] << 16) | ((uint32_t)px[o+1] << 8) | px[o+2];
        }
    stbi_image_free(px);

    std::lock_guard<std::mutex> lk(mtx);
    src_w = w; src_h = h;
    latest = std::move(fr);
    return latest;
}

PipewireCapturer::~PipewireCapturer() {
    streaming = false;
    if (pws) { pw_stream_disconnect(pws); pw_stream_destroy(pws); }
    if (pw_core_) pw_core_disconnect(pw_core_);
    if (pw_ctx) pw_context_destroy(pw_ctx);
    if (loop) pw_main_loop_destroy(loop);
    if (bus) sd_bus_unref(bus);
}

bool detectWayland() { return getenv("WAYLAND_DISPLAY") != nullptr; }