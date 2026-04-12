#ifndef BOXSH_IMAGE_RESIZE_H
#define BOXSH_IMAGE_RESIZE_H

#include <string>

namespace boxsh {

struct ResizedImage {
    std::string data;       // base64-encoded image data
    std::string mime_type;  // output MIME type (may differ from input if re-encoded)
    int width;
    int height;
    int original_width;
    int original_height;
    bool was_resized;
};

// Resize an image to fit within max dimensions and base64 size limit.
// Returns empty data on failure (unsupported format, decode error, etc.).
//
// Strategy (following pi's approach):
//   1. If already within limits → return base64 of original
//   2. Resize to maxWidth×maxHeight
//   3. Try both PNG and JPEG, pick smaller
//   4. If still over maxBytes, reduce JPEG quality
//   5. If still over, reduce dimensions progressively
//
// raw: raw file bytes (not base64)
// mime: detected MIME type of the input
ResizedImage resize_image(const std::string &raw, const std::string &mime,
                          int max_width = 2000, int max_height = 2000,
                          size_t max_bytes = 4718592 /* 4.5 MB */);

}  // namespace boxsh

#endif  // BOXSH_IMAGE_RESIZE_H
