#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace base {
// Write a 24-bit RGB PNG (stored/uncompressed deflate). pixels are 0xRRGGBB.
bool writePng(const std::string& path, int w, int h, const std::vector<uint32_t>& pixels);
}
