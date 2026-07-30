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
    pw_main_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_stream* stream = nullptr;
    spa_hook stream_listener;
    std::thread* worker = nullptr;

    int src_w = 640;
    int src_h = 480;
    std::mutex mtx;
    Frame latest;
    std::string session_handle;
    bool pw_inited = false;

    bool dbusCallCreateSession();
    bool dbusCallSelectSources();
    bool dbusCallStart(uint32_t& node_id);
    bool pwStream(uint32_t node_id);

    static void onProcess(void* data);
};

bool detectWayland();
