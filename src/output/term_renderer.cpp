#include "output/term_renderer.h"
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstdio>
#include <cstring>
#include <algorithm>

TermRenderer::TermRenderer() = default;

TermRenderer::~TermRenderer() {
    disableRawMode();
    clearScreen();
    printf("\033[?25h"); // show cursor
    fflush(stdout);
}

bool TermRenderer::init() {
    enableRawMode();
    printf("\033[?25l"); // hide cursor
    fflush(stdout);
    term_rows = getTerminalHeight();
    term_cols = getTerminalWidth();
    return true;
}

void TermRenderer::enableRawMode() {
    if (!isatty(STDOUT_FILENO)) return;

    tcgetattr(STDOUT_FILENO, &orig_term);
    struct termios raw = orig_term;
    cfmakeraw(&raw);
    tcsetattr(STDOUT_FILENO, TCSAFLUSH, &raw);
    raw_mode = true;
}

void TermRenderer::disableRawMode() {
    if (raw_mode) {
        tcsetattr(STDOUT_FILENO, TCSAFLUSH, &orig_term);
        raw_mode = false;
    }
}

void TermRenderer::clearScreen() {
    printf("\033[2J\033[H");
    fflush(stdout);
}

int TermRenderer::getTerminalWidth() {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        return ws.ws_col;
    }
    return 80;
}

int TermRenderer::getTerminalHeight() {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        return ws.ws_row;
    }
    return 24;
}

void TermRenderer::moveTo(int x, int y) {
    printf("\033[%d;%dH", y + 1, x + 1);
}

void TermRenderer::render(const std::string& frame, int term_w, int term_h) {
    (void)term_w;
    (void)term_h;

    // Split into lines
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < frame.size()) {
        size_t nl = frame.find('\n', pos);
        if (nl == std::string::npos) {
            lines.push_back(frame.substr(pos));
            break;
        }
        lines.push_back(frame.substr(pos, nl - pos));
        pos = nl + 1;
    }

    // Diff-based: only render changed lines
    int max_lines = std::max(lines.size(), prev_lines.size());
    for (int i = 0; i < max_lines; ++i) {
        std::string cur = (i < (int)lines.size()) ? lines[i] : "";
        std::string prev = (i < (int)prev_lines.size()) ? prev_lines[i] : "";

        if (cur != prev) {
            moveTo(0, i);
            printf("%s\033[K", cur.c_str());
        }
    }

    // Clear remaining lines if previous frame was taller
    if (lines.size() < prev_lines.size()) {
        for (size_t i = lines.size(); i < prev_lines.size(); ++i) {
            moveTo(0, i);
            printf("\033[K");
        }
    }

    fflush(stdout);
    prev_lines = std::move(lines);
}

void TermRenderer::showFps(int fps) {
    if (fps == prev_fps) return;
    prev_fps = fps;

    int rows = getTerminalHeight();
    moveTo(0, rows - 1);
    printf("\033[1;33mFPS: %d\033[0m\033[K", fps);
    fflush(stdout);
}
