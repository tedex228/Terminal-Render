#include "processor/ascii_conv.h"
#include <cstdio>

static void appendPixel(std::string& out, uint32_t px) {
    uint8_t r = (px >> 16) & 0xFF;
    uint8_t g = (px >> 8) & 0xFF;
    uint8_t b = px & 0xFF;

    // Reserve space: \033[48;2;R;G;Bm \033[0m = ~28 bytes
    // Using background color space for speed (single char per pixel)
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "\033[48;2;%u;%u;%um \033[0m", r, g, b);
    out.append(buf, n);
}

// Two pixels vertically per terminal cell: top pixel ▄, bottom pixel ▀
static void appendBlock(std::string& out, uint32_t top, uint32_t bottom) {
    uint8_t tr = (top >> 16) & 0xFF;
    uint8_t tg = (top >> 8) & 0xFF;
    uint8_t tb = top & 0xFF;

    uint8_t br = (bottom >> 16) & 0xFF;
    uint8_t bg = (bottom >> 8) & 0xFF;
    uint8_t bb = bottom & 0xFF;

    // If same color -> full block with one color (saves chars)
    if (tr == br && tg == bg && tb == bb) {
        char buf[32];
        int n = snprintf(buf, sizeof(buf), "\033[48;2;%u;%u;%um█\033[0m", tr, tg, tb);
        out.append(buf, n);
    } else {
        // Upper half: top pixel as foreground (▄), bottom as background
        char buf[48];
        int n = snprintf(buf, sizeof(buf),
            "\033[38;2;%u;%u;%um\033[48;2;%u;%u;%um▄\033[0m",
            tr, tg, tb, br, bg, bb);
        out.append(buf, n);
    }
}

std::string frameToAnsi(const Frame& frame, int& out_term_width, int& out_term_height) {
    std::string result;
    result.reserve(frame.width * frame.height * 30);

    // Each row of blocks = 2 pixel rows
    int block_h = (frame.height + 1) / 2;
    out_term_width = frame.width;
    out_term_height = block_h;

    for (int by = 0; by < block_h; ++by) {
        int py_top = by * 2;
        int py_bot = by * 2 + 1;

        for (int x = 0; x < frame.width; ++x) {
            uint32_t top_px = frame.at(x, py_top);
            if (py_bot < frame.height) {
                uint32_t bot_px = frame.at(x, py_bot);
                appendBlock(result, top_px, bot_px);
            } else {
                appendPixel(result, top_px);
            }
        }
        result += "\033[0m\n";
    }

    return result;
}
