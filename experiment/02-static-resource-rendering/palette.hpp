#pragma once
#include "shp.hpp"

struct Palette {
    int size;
    Bytes rgba;

    explicit Palette(const std::string &path) {
        std::ifstream fp(path, std::ios::binary);
        if (!fp) throw std::runtime_error("加载文件失败: " + path);
        auto bytes = Bytes(std::istreambuf_iterator(fp), std::istreambuf_iterator<char>());

        size = bytes.size() / 3;
        rgba.resize(size * 4);
        for (int i = 0; i < size; ++i) {
            for (int channel = 0; channel < 3; ++channel)
                rgba[i * 4 + channel] = bytes[i * 3 + channel] * 255 / 63;
            rgba[i * 4 + 3] = i == 0 ? 0 : 255;
        }
    }
};
