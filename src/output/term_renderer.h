#pragma once

#include <string>
#include <vector>
#include <termios.h>

class TermRenderer {
public:
    TermRenderer();
    ~TermRenderer();

    bool init();
    void render(const std::string& frame, int term_w, int term_h);
    void showFps(int fps);
    void moveTo(int x, int y);

private:
    struct termios orig_term;
    bool raw_mode = false;
    std::vector<std::string> prev_lines;
    int prev_fps = 0;
    int term_rows = 0;
    int term_cols = 0;

    void enableRawMode();
    void disableRawMode();
    void clearScreen();
    int getTerminalWidth();
    int getTerminalHeight();
};
