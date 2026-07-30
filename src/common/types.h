#pragma once

#include <cstdint>
#include <vector>

struct Frame {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> data;

    Frame() = default;

    Frame(int w, int h) : width(w), height(h), data(w * h, 0) {}

    uint32_t& at(int x, int y) { return data[y * width + x]; }
    const uint32_t& at(int x, int y) const { return data[y * width + x]; }
};
