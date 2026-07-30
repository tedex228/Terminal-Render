#pragma once

struct Config {
    int target_width = 640;
    int target_height = 480;
    bool fullscreen = false;
    bool select_window = true;
    int target_fps = 144;
    char block_chars[3] = {' ', ' ', ' '};
};
