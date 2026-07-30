#include "capture/capturer.h"
#ifdef HAS_X11
#include "capture/capturer_x11.h"
#endif
#ifdef HAS_PW
#include "capture/capturer_pw.h"
#endif
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
#include <memory>
#include <sys/ioctl.h>

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
    if (read(STDIN_FILENO, &c, 1) == 1)
        return (c == 'q' || c == 'Q' || c == 0x03);
    return false;
}

static std::unique_ptr<Capturer> createCapturer() {
#ifdef HAS_PW
    bool wayland = getenv("WAYLAND_DISPLAY") != nullptr;
    if (wayland) {
        fprintf(stderr, "Detected Wayland, using PipeWire capture...\n");
        auto cap = std::make_unique<PipewireCapturer>();
        if (cap->init()) return cap;
        fprintf(stderr, "PipeWire failed, falling back...\n");
    }
#endif

#ifdef HAS_X11
    fprintf(stderr, "Using X11 capture...\n");
    auto cap = std::make_unique<X11Capturer>();
    if (cap->init()) return cap;
#endif

    return nullptr;
}

int main(int argc, char** argv) {
    Config cfg;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-r") == 0 && i + 1 < argc)
            sscanf(argv[++i], "%dx%d", &cfg.target_width, &cfg.target_height);
        else if (strcmp(argv[i], "-f") == 0)
            cfg.fullscreen = true;
        else if (strcmp(argv[i], "-h") == 0) {
            printf("Usage: terminal-render [options]\n");
            printf("  -r WxH    Target resolution (default: auto = terminal cols x rows*2)\n");
            printf("  -f        Fullscreen capture\n");
            printf("  -h        Show this help\n");
            return 0;
        }
    }

    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);

    auto capturer = createCapturer();
    if (!capturer) {
        fprintf(stderr, "No capture backend available\n");
        return 1;
    }

    if (!capturer->selectWindow()) return 1;

    // Auto-detect target resolution to fit terminal
    if (cfg.target_width <= 0 || cfg.target_height <= 0) {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
            cfg.target_width = ws.ws_col;
            cfg.target_height = ws.ws_row * 2;
        } else {
            cfg.target_width = 80;
            cfg.target_height = 48;
        }
    }

    fprintf(stderr, "Source: %dx%d\n", capturer->sourceWidth(), capturer->sourceHeight());
    fprintf(stderr, "Target: %dx%d\n", cfg.target_width, cfg.target_height);
    fprintf(stderr, "Press Q or Ctrl+C to quit\n");

    TermRenderer renderer;
    if (!renderer.init()) return 1;
    renderer.clearScreen();

    using clock = std::chrono::steady_clock;
    auto last_time = clock::now();
    int frame_count = 0;
    int display_fps = 0;
    int frame_err = 0;

    while (running) {
        if (isQuitKeyPressed()) break;

        auto frame_start = clock::now();

        Frame src = capturer->capture();
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
