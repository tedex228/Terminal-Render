#include "capture/capturer_pw.h"
#define STB_IMAGE_IMPLEMENTATION
#include "common/stb_image.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <filesystem>
#include <vector>

#define DEST "org.freedesktop.portal.Desktop"
#define OPATH "/org/freedesktop/portal/desktop"
#define IFACE_SCREENSHOT "org.freedesktop.portal.Screenshot"
#define IFACE_REQ "org.freedesktop.portal.Request"

static sd_bus_message* msgCall(sd_bus* bus, const char* iface,
                                const char* member) {
    sd_bus_message* m = nullptr;
    int r = sd_bus_message_new_method_call(bus, &m, DEST, OPATH, iface, member);
    if (r < 0) return nullptr;
    return m;
}

// ─── Screenshot portal ──────────────────────────────────────

struct ScreenshotResp {
    std::string uri;
    bool done = false;
};

static int onScreenshotResponse(sd_bus_message* msg, void* userdata,
                                 sd_bus_error* err) {
    (void)err;
    auto* ctx = (ScreenshotResp*)userdata;

    uint32_t result;
    sd_bus_message_read(msg, "u", &result);
    if (result != 0) { ctx->done = true; return 0; }

    sd_bus_message_enter_container(msg, 'a', "{sv}");
    while (sd_bus_message_enter_container(msg, 'e', "sv") > 0) {
        const char* key;
        sd_bus_message_read(msg, "s", &key);
        sd_bus_message_enter_container(msg, 'v', nullptr);

        if (strcmp(key, "uri") == 0) {
            const char* val;
            sd_bus_message_read(msg, "s", &val);
            ctx->uri = val;
        } else {
            const char* sig;
            sd_bus_message_peek_type(msg, nullptr, &sig);
            sd_bus_message_skip(msg, sig);
        }

        sd_bus_message_exit_container(msg);
        sd_bus_message_exit_container(msg);
    }
    sd_bus_message_exit_container(msg);

    ctx->done = true;
    return 0;
}

static bool takeScreenshot(sd_bus* bus, std::string& out_uri,
                           int timeout_sec = 30) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* m = msgCall(bus, IFACE_SCREENSHOT, "Screenshot");
    if (!m) return false;

    // Build options dict: interactive=false
    sd_bus_message_append(m, "sa{sv}", "", 1);
    sd_bus_message_open_container(m, 'e', "sv");
    sd_bus_message_append(m, "s", "interactive");
    int false_val = 0;
    sd_bus_message_append(m, "v", "b", &false_val);
    sd_bus_message_close_container(m);
    sd_bus_message_close_container(m);

    sd_bus_message* reply = nullptr;
    int r = sd_bus_call(bus, m, 0, &err, &reply);
    sd_bus_message_unref(m);
    if (r < 0) { sd_bus_error_free(&err); return false; }

    const char* req_path;
    sd_bus_message_read(reply, "o", &req_path);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);

    char match[512];
    snprintf(match, sizeof(match),
             "interface='%s',member='Response',path='%s'",
             IFACE_REQ, req_path);

    ScreenshotResp ctx;
    sd_bus_add_match(bus, nullptr, match, onScreenshotResponse, &ctx);

    for (int i = 0; i < timeout_sec * 2 && !ctx.done; ++i) {
        r = sd_bus_process(bus, nullptr);
        if (r < 0) break;
        if (r == 0) sd_bus_wait(bus, 500000);
    }

    if (!ctx.done || ctx.uri.empty()) return false;

    out_uri = ctx.uri;
    return true;
}

static std::string uriToPath(const std::string& uri) {
    if (uri.substr(0, 7) != "file://") return uri;
    std::string result;
    result.reserve(uri.size());
    for (size_t i = 7; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            char hex[3] = {uri[i+1], uri[i+2], 0};
            result += (char)strtol(hex, nullptr, 16);
            i += 2;
        } else if (uri[i] == '+') {
            result += ' ';
        } else {
            result += uri[i];
        }
    }
    return result;
}

// ─── Public API ──────────────────────────────────────────────

bool PipewireCapturer::init() {
    if (sd_bus_open_user(&bus) < 0) {
        fprintf(stderr, "D-Bus connection failed\n");
        return false;
    }
    return true;
}

bool PipewireCapturer::selectWindow() {
    fprintf(stderr, "Using Screenshot portal for capture...\n");
    return true; // Lazy: screenshot taken per-frame
}

static std::vector<unsigned char> readWholeFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return {}; }
    rewind(f);
    std::vector<unsigned char> buf(sz);
    size_t n = fread(buf.data(), 1, sz, f);
    fclose(f);
    if ((long)n != sz) return {};
    return buf;
}

Frame PipewireCapturer::capture() {
    std::string uri;
    if (!takeScreenshot(bus, uri)) {
        fprintf(stderr, "Screenshot failed\n");
        return {};
    }

    std::string path = uriToPath(uri);

    // Read file into memory, then delete it immediately
    std::vector<unsigned char> fileData = readWholeFile(path);
    std::error_code ec;
    std::filesystem::remove(path, ec);

    if (fileData.empty()) {
        fprintf(stderr, "cannot read: %s\n", path.c_str());
        return {};
    }

    int w, h, channels;
    unsigned char* pixels = stbi_load_from_memory(
        fileData.data(), (int)fileData.size(), &w, &h, &channels, 3);
    if (!pixels) {
        fprintf(stderr, "stbi_load failed: %s\n", path.c_str());
        return {};
    }

    Frame frame(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int o = (y * w + x) * 3;
            frame.at(x, y) = ((uint32_t)pixels[o] << 16) |
                              ((uint32_t)pixels[o+1] << 8) |
                              pixels[o+2];
        }

    stbi_image_free(pixels);

    std::lock_guard<std::mutex> lk(mtx);
    src_w = w;
    src_h = h;
    latest = std::move(frame);
    return latest;
}

PipewireCapturer::~PipewireCapturer() {
    if (bus) sd_bus_unref(bus);
}

bool detectWayland() {
    return getenv("WAYLAND_DISPLAY") != nullptr;
}
