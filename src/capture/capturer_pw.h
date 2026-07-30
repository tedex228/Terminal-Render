#pragma once

#include "capture/capturer.h"
#include <pipewire/pipewire.h>
#include <pipewire/stream.h>
#include <systemd/sd-bus.h>
#include <mutex>
#include <thread>
#include <string>

class PipewireCapturer : public Capturer {
public:
    PipewireCapturer() = default;
    ~PipewireCapturer() override;

    bool init() override;
    bool selectWindow() override;
    Frame capture() override;
    int sourceWidth() const override { return src_w; }
    int sourceHeight() const override { return src_h; }

private:
    sd_bus* bus = nullptr;
    std::string session_handle;

    pw_main_loop* loop = nullptr;
    pw_context* pw_ctx = nullptr;
    pw_core* pw_core_ = nullptr;
    pw_stream* pws = nullptr;
    spa_hook stream_listener;
    std::thread pw_thread;
    bool streaming = false;

    int src_w = 640, src_h = 480;
    std::mutex mtx;
    Frame latest;

    bool dbusCreate();
    bool dbusSelect();
    bool dbusStart();

    struct Resp { bool done = false; uint32_t result = 0; std::string sh; int fd = -1; };
    static int onResp(sd_bus_message* m, void* u, sd_bus_error* e);
    bool waitResp(const char* match, Resp& out, int t = 15);

    bool pwConnect(int fd);
    static void onProc(void* data);
    void onFrame(struct spa_buffer* b);

    Frame screenshotFallback();
};

bool detectWayland();