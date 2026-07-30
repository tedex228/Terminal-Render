#pragma once

struct Config {
    int target_width = 0;
    int target_height = 0;
    bool fullscreen = false;
    bool select_window = true;
    int target_fps = 144;
    char block_chars[3] = {' ', ' ', ' '};
};
