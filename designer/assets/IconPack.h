#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Runtime {

struct IconPreview {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> bgra;
};

class IconPack {
public:
    bool Load(const std::filesystem::path& path);
    bool Loaded() const;
    int Count() const;
    const std::string& Name(int index) const;

    const IconPreview* Preview(int index);
    std::string VectorXml(int index) const;

private:
    struct Entry {
        std::string name;
        std::uint32_t localOffset = 0;
        std::uint32_t compressedSize = 0;
        std::uint32_t uncompressedSize = 0;
        std::uint16_t method = 0;
    };

    bool ReadSvg(int index, std::string& svg) const;
    bool Rasterize(const std::string& svg, IconPreview& preview) const;

    std::vector<unsigned char> bytes;
    std::vector<Entry> entries;
    std::vector<IconPreview> cachePreviews;
    std::vector<int> cacheIndex;
};

}  // namespace Runtime
