#pragma once
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using U16 = std::uint16_t;
using U32 = std::uint32_t;

using Bytes = std::vector<std::uint8_t>;
using ByteMatrix = std::vector<Bytes>;

static U16 readU16(const Bytes& bytes, std::size_t offset) {
    return bytes[offset] | (bytes[offset + 1] << 8);
}

static U32 readU32(const Bytes& bytes, std::size_t offset) {
    return bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16) | (bytes[offset + 3] << 24);
}

struct  ShapeFile {
    U16 width = 0;
    U16 height = 0;
    U16 frameCnt = 0;
    std::vector<ByteMatrix> pixels; // pixels[帧编号][y][x]，0 表示透明。

    explicit ShapeFile(const std::string& path) {
        std::ifstream fp(path, std::ios::binary);
        if (!fp) throw std::runtime_error("无法加载文件: " + path);
        const auto bytes = Bytes(std::istreambuf_iterator(fp), std::istreambuf_iterator<char>());

        width = readU16(bytes, 2);
        height = readU16(bytes, 4);
        frameCnt = readU16(bytes, 6);

        for (int k = 0; k < frameCnt; ++k) {
            const auto entry = 8 + k * 24;
            const auto frameX = readU16(bytes, entry);
            const auto frameY = readU16(bytes, entry + 2);
            const auto frameWidth = readU16(bytes, entry + 4);
            const auto frameHeight = readU16(bytes, entry + 6);
            const auto compression = bytes[entry + 8];
            auto offset = readU32(bytes, entry + 20);

            ByteMatrix image(height, Bytes(width, 0));
            for (int i = 0; i < frameHeight; ++i) {
                if (compression != 3) {
                    std::copy_n(bytes.data() + offset, frameWidth, image[frameY + i].begin() + frameX);
                    offset += frameWidth;
                }
                else { //解码RLE
                    auto length = readU16(bytes, offset), cur = frameX;
                    for (int j = 2; j < length; ++j) {
                        auto v = bytes[offset + j];
                        if (v != 0) image[frameY + i][cur++] = v;
                        else cur += bytes[offset + (++j)];
                    }
                    offset += length;
                }
            }
            pixels.push_back(std::move(image));
        }
    }
};
