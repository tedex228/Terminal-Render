#pragma once

#include "capture/capturer.h"
#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <sys/shm.h>

class X11Capturer : public Capturer {
public:
    X11Capturer() = default;
    ~X11Capturer() override;

    bool init() override;
    bool selectWindow() override;
    Frame capture() override;
    int sourceWidth() const override { return src_w; }
    int sourceHeight() const override { return src_h; }

private:
    Display* display = nullptr;
    Window target_window = 0;
    XShmSegmentInfo shm_info;
    XImage* image = nullptr;
    int src_w = 0;
    int src_h = 0;
    int screen = 0;
};
