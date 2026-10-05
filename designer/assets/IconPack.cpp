#include "designer/assets/IconPack.h"

#include "rendering/image/ImageDecoder.h"
#include "rendering/image/Inflate.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace Runtime {
namespace {

constexpr int CACHE_LIMIT = 192;

std::uint16_t Read16(const unsigned char* data) {
    return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
}

std::uint32_t Read32(const unsigned char* data) {
    return static_cast<std::uint32_t>(data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
}

}  // namespace

bool IconPack::Load(const std::filesystem::path& path) {
    entries.clear();
    bytes.clear();
    cachePreviews.clear();
    cacheIndex.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff length = file.tellg();
    if (length < 22) {
        return false;
    }
    file.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(length));
    file.read(reinterpret_cast<char*>(bytes.data()), length);
    if (!file) {
        bytes.clear();
        return false;
    }
    int directory = -1;
    const int scan = std::max(0, static_cast<int>(bytes.size()) - 66000);
    for (int index = static_cast<int>(bytes.size()) - 22; index >= scan; --index) {
        if (bytes[static_cast<std::size_t>(index)] == 'P' && bytes[static_cast<std::size_t>(index + 1)] == 'K' && bytes[static_cast<std::size_t>(index + 2)] == 5 &&
            bytes[static_cast<std::size_t>(index + 3)] == 6) {
            directory = index;
            break;
        }
    }
    if (directory < 0) {
        bytes.clear();
        return false;
    }
    const unsigned char* end = bytes.data() + directory;
    const int count = Read16(end + 10);
    const std::uint32_t offset = Read32(end + 16);
    if (count < 1 || offset >= bytes.size()) {
        bytes.clear();
        return false;
    }
    std::size_t cursor = offset;
    entries.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        if (cursor + 46 > bytes.size() || std::memcmp(bytes.data() + cursor, "PK\x01\x02", 4) != 0) {
            break;
        }
        const unsigned char* header = bytes.data() + cursor;
        const std::uint16_t method = Read16(header + 10);
        const std::uint32_t compressed = Read32(header + 20);
        const std::uint32_t uncompressed = Read32(header + 24);
        const std::uint16_t nameLength = Read16(header + 28);
        const std::uint16_t extraLength = Read16(header + 30);
        const std::uint16_t commentLength = Read16(header + 32);
        const std::uint32_t localOffset = Read32(header + 42);
        if (cursor + 46 + nameLength > bytes.size()) {
            break;
        }
        const std::string name(reinterpret_cast<const char*>(header + 46), nameLength);
        cursor += static_cast<std::size_t>(46 + nameLength + extraLength + commentLength);
        const bool svg = name.size() > 8 && name.rfind("svg/", 0) == 0 && name.compare(name.size() - 4, 4, ".svg") == 0;
        const bool png = name.size() > 8 && name.rfind("png/", 0) == 0 && name.compare(name.size() - 4, 4, ".png") == 0;
        if (svg || png) {
            Entry entry;
            entry.name = name.substr(4, name.size() - 8);
            entry.localOffset = localOffset;
            entry.compressedSize = compressed;
            entry.uncompressedSize = uncompressed;
            entry.method = method;
            entries.push_back(std::move(entry));
        }
    }
    if (entries.empty()) {
        bytes.clear();
        return false;
    }
    return true;
}

bool IconPack::Loaded() const {
    return !entries.empty();
}

int IconPack::Count() const {
    return static_cast<int>(entries.size());
}

const std::string& IconPack::Name(int index) const {
    static const std::string empty;
    if (index < 0 || index >= static_cast<int>(entries.size())) {
        return empty;
    }
    return entries[static_cast<std::size_t>(index)].name;
}

bool IconPack::ReadSvg(int index, std::string& svg) const {
    if (index < 0 || index >= static_cast<int>(entries.size())) {
        return false;
    }
    const Entry& entry = entries[static_cast<std::size_t>(index)];
    if (entry.localOffset + 30 > bytes.size()) {
        return false;
    }
    const unsigned char* header = bytes.data() + entry.localOffset;
    if (std::memcmp(header, "PK\x03\x04", 4) != 0) {
        return false;
    }
    const std::uint16_t nameLength = Read16(header + 26);
    const std::uint16_t extraLength = Read16(header + 28);
    const std::size_t dataAt = static_cast<std::size_t>(entry.localOffset) + 30u + nameLength + extraLength;
    if (dataAt + entry.compressedSize > bytes.size()) {
        return false;
    }
    if (entry.method == 0) {
        svg.assign(reinterpret_cast<const char*>(bytes.data() + dataAt), entry.compressedSize);
        return !svg.empty();
    }
    if (entry.method != 8) {
        return false;
    }
    std::vector<unsigned char> inflated;
    if (!InflateRaw(bytes.data() + dataAt, static_cast<int>(entry.compressedSize), inflated)) {
        return false;
    }
    svg.assign(reinterpret_cast<const char*>(inflated.data()), inflated.size());
    return !svg.empty();
}

bool IconPack::Rasterize(const std::string& svg, IconPreview& preview) const {
    DecodedImage image;
    const auto* data = reinterpret_cast<const unsigned char*>(svg.data());
    const int size = static_cast<int>(svg.size());
    bool decoded = false;
    if (size >= 8 && std::memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) {
        decoded = DecodeImageBytes(data, size, image);
    } else {
        SvgDecodeOptions options;
        options.maxEdge = 192;
        options.currentRed = 245;
        options.currentGreen = 245;
        options.currentBlue = 247;
        decoded = DecodeSvgBytes(data, size, image, options);
    }
    if (!decoded || image.frames.empty()) {
        return false;
    }
    preview.width = image.frames[0].width;
    preview.height = image.frames[0].height;
    preview.bgra = std::move(image.frames[0].bgra);
    return preview.width > 0 && preview.height > 0 && !preview.bgra.empty();
}

const IconPreview* IconPack::Preview(int index) {
    if (index < 0 || index >= static_cast<int>(entries.size())) {
        return nullptr;
    }
    const auto cached = std::find(cacheIndex.begin(), cacheIndex.end(), index);
    if (cached != cacheIndex.end()) {
        return &cachePreviews[static_cast<std::size_t>(cached - cacheIndex.begin())];
    }
    std::string svg;
    if (!ReadSvg(index, svg)) {
        return nullptr;
    }
    IconPreview preview;
    if (!Rasterize(svg, preview)) {
        return nullptr;
    }
    if (static_cast<int>(cacheIndex.size()) >= CACHE_LIMIT) {
        cacheIndex.erase(cacheIndex.begin());
        cachePreviews.erase(cachePreviews.begin());
    }
    cacheIndex.push_back(index);
    cachePreviews.push_back(std::move(preview));
    return &cachePreviews.back();
}

std::string IconPack::VectorXml(int index) const {
    std::string svg;
    if (!ReadSvg(index, svg)) {
        return {};
    }
    if (svg.size() >= 8 && std::memcmp(svg.data(), "\x89PNG\r\n\x1a\n", 8) == 0) {
        return {};
    }
    return VectorXmlFromSvg(svg, Name(index), 245, 245, 247);
}

bool IconPack::WriteFile(int index, const std::filesystem::path& destination) const {
    std::string payload;
    if (!ReadSvg(index, payload)) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    std::ofstream file(destination, std::ios::binary);
    if (!file) {
        return false;
    }
    file.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    return static_cast<bool>(file);
}

}  // namespace Runtime
