#include "capture/capturer_x11.h"
#include "processor/downscaler.h"
#include "processor/ascii_conv.h"
#include "output/term_renderer.h"
#include "common/config.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
#include <signal.h>

static volatile bool running = true;

void handleSignal(int) {
    running = false;
}

int main(int argc, char** argv) {
    Config cfg;

    // Parse args
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            sscanf(argv[++i], "%dx%d", &cfg.target_width, &cfg.target_height);
        } else if (strcmp(argv[i], "-f") == 0) {
            cfg.fullscreen = true;
        } else if (strcmp(argv[i], "-h") == 0) {
            printf("Usage: terminal-render [options]\n");
            printf("  -r WxH    Target resolution (default: 640x480)\n");
            printf("  -f        Fullscreen capture\n");
            printf("  -h        Show this help\n");
            return 0;
        }
    }

    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);

    X11Capturer capturer;
    if (!capturer.init()) return 1;

    if (!capturer.selectWindow()) return 1;

    fprintf(stderr, "Window size: %dx%d\n", capturer.sourceWidth(), capturer.sourceHeight());
    fprintf(stderr, "Target resolution: %dx%d\n", cfg.target_width, cfg.target_height);
    fprintf(stderr, "Press Ctrl+C to quit\n");

    // Terminal setup
    TermRenderer renderer;
    if (!renderer.init()) return 1;

    using clock = std::chrono::steady_clock;
    auto last_time = clock::now();
    int frame_count = 0;
    int display_fps = 0;

    while (running) {
        auto frame_start = clock::now();

        Frame src = capturer.capture();
        if (src.width == 0 || src.height == 0) break;

        Frame down = downscaleBox(src, cfg.target_width, cfg.target_height);
        if (down.width == 0 || down.height == 0) break;

        int term_w, term_h;
        std::string ansi = frameToAnsi(down, term_w, term_h);

        renderer.render(ansi, term_w, term_h);

        // FPS counter (update every second)
        ++frame_count;
        auto now = clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
        if (elapsed >= 1000) {
            display_fps = frame_count * 1000 / elapsed;
            renderer.showFps(display_fps);
            frame_count = 0;
            last_time = now;
        }

        // Optional: minimal sleep to avoid busy-wait
        auto frame_end = clock::now();
        auto frame_ms = std::chrono::duration_cast<std::chrono::milliseconds>(frame_end - frame_start).count();
        if (frame_ms < 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    return 0;
}
