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
#include <unistd.h>
#include <fcntl.h>

static volatile bool running = true;

void handleSignal(int) {
    running = false;
}

static bool isQuitKeyPressed() {
    static bool init = false;
    if (!init) {
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        init = true;
    }
    char c;
    if (read(STDIN_FILENO, &c, 1) == 1) {
        return (c == 'q' || c == 'Q' || c == 0x03);
    }
    return false;
}

int main(int argc, char** argv) {
    Config cfg;

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
    fprintf(stderr, "Press Q or Ctrl+C to quit\n");

    TermRenderer renderer;
    if (!renderer.init()) return 1;

    using clock = std::chrono::steady_clock;
    auto last_time = clock::now();
    int frame_count = 0;
    int display_fps = 0;
    int frame_err = 0;

    while (running) {
        if (isQuitKeyPressed()) break;

        auto frame_start = clock::now();

        Frame src = capturer.capture();
        if (src.width == 0 || src.height == 0) {
            ++frame_err;
            if (frame_err > 30) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        frame_err = 0;

        Frame down = downscaleBox(src, cfg.target_width, cfg.target_height);
        if (down.width == 0 || down.height == 0) break;

        int term_w, term_h;
        std::string ansi = frameToAnsi(down, term_w, term_h);

        renderer.render(ansi, term_w, term_h);

        ++frame_count;
        auto now = clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
        if (elapsed >= 1000) {
            display_fps = frame_count * 1000 / elapsed;
            renderer.showFps(display_fps);
            frame_count = 0;
            last_time = now;
        }
    }

    return 0;
}
