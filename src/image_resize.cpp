// Image resize using stb libraries.
// Decodes image, resizes if needed, re-encodes as JPEG or PNG.

#include "image_resize.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "../third_party/stb/stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "../third_party/stb/stb_image_resize2.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb/stb_image_write.h"

namespace boxsh {

// Base64 encoding (duplicated here to keep image_resize self-contained).
static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string b64_encode(const unsigned char *src, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(src[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(src[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(src[i + 2]);
        out.push_back(b64_table[(n >> 18) & 0x3F]);
        out.push_back(b64_table[(n >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? b64_table[(n >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? b64_table[n & 0x3F] : '=');
    }
    return out;
}

// stb_image_write callback: accumulate bytes into a std::string.
static void write_callback(void *ctx, void *data, int size) {
    auto *out = static_cast<std::string *>(ctx);
    out->append(static_cast<const char *>(data), static_cast<size_t>(size));
}

// Encode pixels as JPEG, return base64.
static std::string encode_jpeg(const unsigned char *pixels,
                               int w, int h, int channels, int quality) {
    std::string buf;
    stbi_write_jpg_to_func(write_callback, &buf, w, h, channels, pixels, quality);
    return b64_encode(reinterpret_cast<const unsigned char *>(buf.data()), buf.size());
}

// Encode pixels as PNG, return base64.
static std::string encode_png(const unsigned char *pixels,
                              int w, int h, int channels) {
    std::string buf;
    stbi_write_png_to_func(write_callback, &buf, w, h, channels, pixels, w * channels);
    return b64_encode(reinterpret_cast<const unsigned char *>(buf.data()), buf.size());
}

// Pick the smallest encoding from PNG and JPEG candidates.
struct Candidate {
    std::string data;
    std::string mime;
};

static Candidate best_encoding(const unsigned char *pixels,
                               int w, int h, int channels,
                               int jpeg_quality) {
    Candidate png_c = {encode_png(pixels, w, h, channels), "image/png"};
    Candidate jpg_c = {encode_jpeg(pixels, w, h, channels, jpeg_quality), "image/jpeg"};
    return (png_c.data.size() <= jpg_c.data.size()) ? std::move(png_c) : std::move(jpg_c);
}

ResizedImage resize_image(const std::string &raw, const std::string &mime,
                          int max_width, int max_height, size_t max_bytes) {
    int w, h, channels;
    unsigned char *pixels = stbi_load_from_memory(
        reinterpret_cast<const unsigned char *>(raw.data()),
        static_cast<int>(raw.size()), &w, &h, &channels, 0);
    if (!pixels)
        return {};

    // Clamp channels to 4 (stb sometimes reports >4 for HDR).
    if (channels > 4) channels = 4;

    // Check if dimensions are already within limits — only then compute
    // the original base64 (avoids wasting CPU on large images that will
    // be resized anyway).
    if (w <= max_width && h <= max_height) {
        std::string orig_b64 = b64_encode(
            reinterpret_cast<const unsigned char *>(raw.data()), raw.size());
        if (orig_b64.size() <= max_bytes) {
            stbi_image_free(pixels);
            return {std::move(orig_b64), mime, w, h, w, h, false};
        }
    }

    int orig_w = w, orig_h = h;

    // Calculate initial target dimensions.
    int tw = w, th = h;
    if (tw > max_width) {
        th = static_cast<int>(std::round(static_cast<double>(th) * max_width / tw));
        tw = max_width;
    }
    if (th > max_height) {
        tw = static_cast<int>(std::round(static_cast<double>(tw) * max_height / th));
        th = max_height;
    }

    // Try encoding at target dimensions with decreasing JPEG quality.
    static const int jpeg_qualities[] = {80, 60, 40, 20};

    for (int attempt = 0; attempt < 8 && tw > 0 && th > 0; ++attempt) {
        // Resize pixels.
        std::vector<unsigned char> resized(tw * th * channels);
        stbir_resize_uint8_linear(
            pixels, w, h, w * channels,
            resized.data(), tw, th, tw * channels,
            static_cast<stbir_pixel_layout>(channels));

        // Try all JPEG qualities.
        for (int q : jpeg_qualities) {
            auto c = best_encoding(resized.data(), tw, th, channels, q);
            if (c.data.size() <= max_bytes) {
                stbi_image_free(pixels);
                return {std::move(c.data), std::move(c.mime),
                        tw, th, orig_w, orig_h, true};
            }
        }

        // Reduce dimensions by 50%.
        tw /= 2;
        th /= 2;
    }

    // All attempts failed.
    stbi_image_free(pixels);
    return {};
}

}  // namespace boxsh
