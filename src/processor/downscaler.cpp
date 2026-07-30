#include "processor/downscaler.h"

Frame downscaleBox(const Frame& src, int new_w, int new_h) {
    if (new_w <= 0 || new_h <= 0) return {};

    Frame dst(new_w, new_h);
    double x_ratio = (double)src.width / new_w;
    double y_ratio = (double)src.height / new_h;

    for (int dy = 0; dy < new_h; ++dy) {
        int src_y_start = (int)(dy * y_ratio);
        int src_y_end = (int)((dy + 1) * y_ratio);
        if (src_y_end > src.height) src_y_end = src.height;
        if (src_y_end <= src_y_start) src_y_end = src_y_start + 1;

        for (int dx = 0; dx < new_w; ++dx) {
            int src_x_start = (int)(dx * x_ratio);
            int src_x_end = (int)((dx + 1) * x_ratio);
            if (src_x_end > src.width) src_x_end = src.width;
            if (src_x_end <= src_x_start) src_x_end = src_x_start + 1;

            unsigned long r = 0, g = 0, b = 0;
            int count = 0;

            for (int sy = src_y_start; sy < src_y_end; ++sy) {
                for (int sx = src_x_start; sx < src_x_end; ++sx) {
                    uint32_t px = src.at(sx, sy);
                    r += (px >> 16) & 0xFF;
                    g += (px >> 8) & 0xFF;
                    b += px & 0xFF;
                    ++count;
                }
            }

            if (count > 0) {
                r /= count; g /= count; b /= count;
            }

            dst.at(dx, dy) = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    return dst;
}
